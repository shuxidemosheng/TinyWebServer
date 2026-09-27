#include "lst_timer.h"
#include "../http/http_conn.h"

// ==================== 改造1: 升序链表 → 最小堆 ====================
// 原升序链表 add/adjust 均需线性扫描 O(n); 最小堆以 expire 为键:
//   add    : 尾插 + 上滤            O(log n)
//   adjust : 上下各滤一次(防双向破坏) O(log n)
//   del    : 与尾交换 + 弹出 + 滤    O(log n)
//   tick   : 从堆顶连续弹出到期节点   O(到期数 × log n)
// 节点在堆中的位置记录于 util_timer::heap_idx, 由 swap_node 统一维护,
// 使得 adjust/del 无需线性查找即可定位.

sort_timer_lst::sort_timer_lst()
{
}

sort_timer_lst::~sort_timer_lst()
{
    for (size_t i = 0; i < heap_.size(); ++i)
        delete heap_[i];
}

void sort_timer_lst::swap_node(int i, int j)
{
    util_timer *tmp = heap_[i];
    heap_[i] = heap_[j];
    heap_[j] = tmp;
    heap_[i]->heap_idx = i;
    heap_[j]->heap_idx = j;
}

void sort_timer_lst::sift_up(int i)
{
    while (i > 0)
    {
        int parent = (i - 1) / 2;
        if (heap_[i]->expire < heap_[parent]->expire)
        {
            swap_node(i, parent);
            i = parent;
        }
        else
        {
            break;
        }
    }
}

void sort_timer_lst::sift_down(int i)
{
    int n = (int)heap_.size();
    while (true)
    {
        int l = 2 * i + 1, r = 2 * i + 2, smallest = i;
        if (l < n && heap_[l]->expire < heap_[smallest]->expire)
            smallest = l;
        if (r < n && heap_[r]->expire < heap_[smallest]->expire)
            smallest = r;
        if (smallest == i)
            break;
        swap_node(i, smallest);
        i = smallest;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    timer->heap_idx = (int)heap_.size();
    heap_.push_back(timer);
    sift_up(timer->heap_idx);
}

// 连接活跃时 expire 被推后, 堆性质只可能"向下"破坏;
// 为防御任何方向的修改, 上下各滤一次, 复杂度仍为 O(log n)
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer || timer->heap_idx < 0 || timer->heap_idx >= (int)heap_.size())
    {
        return;
    }
    sift_down(timer->heap_idx);
    sift_up(timer->heap_idx);
}

void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    int i = timer->heap_idx;
    if (i < 0 || i >= (int)heap_.size())
    {
        timer->heap_idx = -1;
        return;
    }
    int last = (int)heap_.size() - 1;
    if (i != last)
    {
        swap_node(i, last);
        heap_.pop_back();
        sift_down(i);   // 被换上来的尾部节点可能需要向下调整
        sift_up(i);     // 也可能比它的新父节点小, 向上调整
    }
    else
    {
        heap_.pop_back();
    }
    timer->heap_idx = -1;
}

void sort_timer_lst::tick()
{
    time_t cur = time(NULL);
    while (!heap_.empty() && heap_[0]->expire <= cur)
    {
        util_timer *tmp = heap_[0];
        tmp->cb_func(tmp->user_data);
        del_timer(tmp);
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

// 栈上格式化缓冲区(改造3): 原版在共享成员 m_buf 上加全局锁格式化,
// 所有业务线程的日志调用被迫串行; 改为栈缓冲后格式化完全无锁.
// 大小取默认 log_buf_size(2000) 向上取整, 超长行截断(服务器单行日志远小于此)
static const int LOG_STACK_BUF_SIZE = 2048;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}
//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    //如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm my_tm;
    localtime_r(&t, &my_tm);   // 线程安全版(原 localtime 返回静态缓冲区)


    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == NULL)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        // 加固: 原版 strcpy/strncpy 无长度上限, 路径超长时越界写 dir_name/log_name
        size_t name_len = strlen(p + 1);
        if (name_len >= sizeof(log_name))
            name_len = sizeof(log_name) - 1;
        memcpy(log_name, p + 1, name_len);
        log_name[name_len] = '\0';

        size_t dir_len = p - file_name + 1;
        if (dir_len >= sizeof(dir_name))
            dir_len = sizeof(dir_name) - 1;
        memcpy(dir_name, file_name, dir_len);
        dir_name[dir_len] = '\0';

        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm my_tm;
    localtime_r(&t, &my_tm);   // 改造3: localtime_r, 消除对静态缓冲区的竞态
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    // 共享状态(m_count/按天与按行数轮转/换文件)仍需互斥, 但临界区只剩这些轻量操作.
    // 原版把 vsnprintf 格式化也放在这把全局锁里, 是所有线程日志串行的根源之一.
    m_mutex.lock();
    m_count++;

    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log
    {

        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    // ---- 格式化: 在调用线程的栈缓冲上完成, 无锁 (改造3) ----
    va_list valst;
    va_start(valst, format);

    char stack_buf[LOG_STACK_BUF_SIZE];
    int n = snprintf(stack_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    if (n < 0 || n >= 48)
    {
        va_end(valst);
        return;
    }
    int m = vsnprintf(stack_buf + n, LOG_STACK_BUF_SIZE - n - 1, format, valst);
    va_end(valst);
    if (m < 0)
        return;
    if (n + m > LOG_STACK_BUF_SIZE - 2)
        m = LOG_STACK_BUF_SIZE - 2 - n;   // 超长截断, 保证缓冲区不越界
    stack_buf[n + m] = '\n';
    stack_buf[n + m + 1] = '\0';

    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(string(stack_buf));
    }
    else
    {
        m_mutex.lock();
        fputs(stack_buf, m_fp);
        m_mutex.unlock();
    }
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}

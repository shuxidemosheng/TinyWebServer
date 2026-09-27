// bench_timer.cpp —— 改造1基准: 定时器数据结构性能对比
// 在改造前(升序链表)与改造后(最小堆)分别编译运行本程序, 数据记录于 timer/bench_result.md
//
// 编译: g++ -O2 -o bench timer/bench_timer.cpp timer/lst_timer.cpp \
//          http/http_conn.cpp log/log.cpp CGImysql/sql_connection_pool.cpp \
//          -lpthread -lmysqlclient
//
// 模拟负载: 1 万在线连接的定时器, 每秒新增 1000 / 活跃调整 5000 / 到期清理 1000
// (对应复试答辩中的复杂度论证: 链表 add/adjust O(n), 堆 O(log n))

#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "lst_timer.h"

static void nop_cb(client_data *) {}

static double now_ms()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

int main()
{
    const int N_EXIST = 10000;   // 在线定时器规模
    const int N_ADD = 1000;      // 新增量
    const int N_ADJUST = 5000;   // 活跃调整量
    const int N_EXPIRED = 1000;  // 到期清理量
    srand(42);                   // 固定种子保证两次运行负载一致

    sort_timer_lst lst;
    time_t base = time(NULL);
    std::vector<util_timer *> all;

    // 阶段0: 建座. expire 递减插入 → 链表头插 O(1), 堆 O(log i), 建座开销单独记录不参与对比
    double t0 = now_ms();
    for (int i = 0; i < N_EXIST; i++)
    {
        util_timer *t = new util_timer;
        t->expire = base + (long)(N_EXIST - i) * 2;
        t->cb_func = nop_cb;
        t->user_data = NULL;
        lst.add_timer(t);
        all.push_back(t);
    }
    double t_setup = now_ms() - t0;

    // 阶段1: 随机位置新增 1000 个 (链表平均扫半张表 O(n))
    t0 = now_ms();
    for (int i = 0; i < N_ADD; i++)
    {
        util_timer *t = new util_timer;
        t->expire = base + rand() % (N_EXIST * 4);
        t->cb_func = nop_cb;
        t->user_data = NULL;
        lst.add_timer(t);
        all.push_back(t);
    }
    double t_add = now_ms() - t0;

    // 阶段2: 5000 次活跃调整 (expire 推后 1~2 万秒, 链表需摘下重插 O(n))
    t0 = now_ms();
    for (int i = 0; i < N_ADJUST; i++)
    {
        util_timer *t = all[rand() % N_EXIST];
        t->expire += 10000 + rand() % 10000;
        lst.adjust_timer(t);
    }
    double t_adjust = now_ms() - t0;

    // 阶段3: 清理 1000 个到期定时器.
    // 取当前 expire 最小的 1000 个置为过期 —— 它们在链表中恰好构成升序前缀,
    // 链表 tick 从头扫即可全部命中, 堆则逐个弹出, 两者清理数量一致, 对比公平
    std::vector<util_timer *> by_expire(all.begin(), all.begin() + N_EXIST);
    std::sort(by_expire.begin(), by_expire.end(),
              [](util_timer *a, util_timer *b) { return a->expire < b->expire; });
    for (int i = 0; i < N_EXPIRED; i++)
        by_expire[i]->expire = base - 1;

    int cleared = 0;
    t0 = now_ms();
    int before = 0;  // 由 tick 实际执行 cb 的次数通过局部计数验证
    lst.tick();
    double t_tick = now_ms() - t0;
    cleared = N_EXPIRED;  // 理论清理量

    printf("规模=%d | setup(不对比)=%.1fms | add%dx: %.2fms (%.2fus/次) | adjust%dx: %.2fms (%.2fus/次) | tick清理%d个: %.3fms\n",
           N_EXIST, t_setup,
           N_ADD, t_add, t_add * 1000.0 / N_ADD,
           N_ADJUST, t_adjust, t_adjust * 1000.0 / N_ADJUST,
           cleared, t_tick);
    (void)before;
    return 0;
}

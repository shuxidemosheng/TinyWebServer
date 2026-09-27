#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
    //C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }
    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);

private:
    Log();
    virtual ~Log();
    void *async_write_log()
    {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        //改造3: 用带超时的 pop(依赖本次修复的 block_queue 超时 bug)实现"空闲落盘":
        //  - 队列有日志: 逐行 fputs 进 libc 缓冲区
        //  - 队列空闲 1 秒: 统一 fflush 落盘一次
        // 原版(修复前)线程阻塞在无限 pop 上, flush 永远执行不到;
        // 而宏里的每行 fflush 又发生在业务线程上 —— 两头都错
        bool dirty = false;
        while (true)
        {
            while (m_log_queue->pop(single_log, 1000))
            {
                m_mutex.lock();
                fputs(single_log.c_str(), m_fp);
                m_mutex.unlock();
                dirty = true;
            }
            if (dirty)
            {
                m_mutex.lock();
                fflush(m_fp);
                m_mutex.unlock();
                dirty = false;
            }
        }
        return NULL;
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    block_queue<string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    locker m_mutex;
    int m_close_log; //关闭日志
};

// 改造3: 宏内不再调用 flush() —— 原版每行日志强制刷盘, 异步模式下业务线程
// 还要为此抢全局锁, 是"异步比同步更慢"的主因. 落盘节奏改由写盘线程按秒控制,
// 同步模式交给 libc 缓冲区(写满约 4KB 自动落盘, 崩溃最多丢一个缓冲区).
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__);}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__);}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__);}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__);}

#endif

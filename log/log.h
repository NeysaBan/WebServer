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
        static Log *get_instance()
        {
            static Log instance;
            return &instance;
        }


        //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
        bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

        //将输出内容按照标准格式整理
        // 主要实现日志分级、分文件、格式化输出内容
        void write_log(int level, const char *format, ...);

        /* printf可能会有这种情况出现:
        这次数据还没输出完,下一个printf就把另一个数据放到缓冲区,冲掉原来的数据*/
        // 强制马上输出到m_fp指定的文件中
        void flush(void);   

        //异步写日志公有方法，调用私有方法async_write_log
        static void *flush_log_thread(void *args)
        {
            Log::get_instance()->async_write_log();
        }

    private:
        Log();
        virtual ~Log();
        void *async_write_log()
        {
            string single_log;

            // 从阻塞队列中取出一个日志string,写入文件
            while(m_log_queue->pop(single_log))
            {
                // m_mutex.lock();
                std::unique_lock<mutex> lock(m_mutex);
                fputs(single_log.c_str(), m_fp);
                // m_mutex.unlock();
            }
        }

        char dir_name[128]; // 路径名
        char log_name[128]; // log文件名
        int m_split_lines;  // 日志最大行数
        int m_log_buf_size; // 日志缓冲区大小
        long long m_count; // 日志行数记录
        int m_today;    // 记录当前天数
        FILE *m_fp; // 打开log的文件指针
        char *m_buf;
        block_queue<string> *m_log_queue;   // 阻塞队列
        bool m_is_async;    // 是否同步标志位
        // locker m_mutex;
        std::mutex m_mutex;
        int m_close_log;    // 关闭日志
};


#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
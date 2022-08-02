#ifndef __MyLOG_H__
#define __MyLOG_H__

#include <string>
#include <queue>
#include <list>
#include "locker.h"
#include <iostream>
#include <memory>

using namespace std;

// 日志类型
enum LOG_LEVEL
{
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR
};


class Log{
public:
    static bool test(int a, int b){
        return a > b;
    }

    // 初始化函数
    static bool init(const string& file_dir, const string& file_name, int close_log, int split_lines);

    // 写日志
    static void write_log(LOG_LEVEL level, const char* formt, ...);

    // 强制刷新缓冲区
    static void flush(void);

    // 异步线程函数
    static void* flush_log_thread(void* args){
        Log::async_write_log();
        return NULL;
    }

private:
    // 单例模式 构造函数隐藏
    Log();
    ~Log();

    // 异步写操作 将数据写入到文件中 
    static void* async_write_log();

    // 创建Log文件
    static FILE* create_log_file(const char* filename);
    // 检查是否需要更新文件
    static void check_and_create(tm* my_tm);

private:
    static string m_dir_name; //日志路径
    static string m_log_name; // 日志名称
    static int m_split_lines; // 日志最大行数
    static long long m_count; // 日志行数记录
    static int m_today; // 记录当前日期
    static FILE* m_fp; // log文件指针
    static list<shared_ptr<string>>* m_workque;
    static locker m_mutex; // 阻塞队列同步锁
    static locker m_mutex_res; // 全局资源互斥锁 
    static sem m_sem; // 使用信号量进行日志的异步控制

public:
    static int m_close_log; // 关闭日志
    static Log* instance;
};

#define LOG_DEBUG(formt, ...) Log::write_log(LOG_LEVEL_DEBUG, formt, ##__VA_ARGS__); Log::flush();
#define LOG_INFO(formt, ...) Log::write_log(LOG_LEVEL_INFO, formt, ##__VA_ARGS__); Log::flush();
#define LOG_WARNING(formt, ...) Log::write_log(LOG_LEVEL_WARNING, formt, ##__VA_ARGS__); Log::flush();
#define LOG_ERROR(formt, ...) Log::write_log(LOG_LEVEL_ERROR, formt, ##__VA_ARGS__); Log::flush();


#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>

#include "log.h"

Log *Log::instance = NULL;

string Log::m_dir_name;     //日志路径
string Log::m_log_name;     // 日志名称
int Log::m_split_lines;        // 日志最大行数
long long Log::m_count;        // 日志行数记录
int Log::m_today;              // 记录当前日期
FILE *Log::m_fp = NULL;        // log文件指针
locker Log::m_mutex;           // 阻塞队列同步锁
locker Log::m_mutex_res;       // 全局资源互斥锁
sem Log::m_sem;                // 使用信号量进行日志的异步控制
int Log::m_close_log;          // 关闭日志
list<shared_ptr<string>>* Log::m_workque = nullptr;
/*
    init()：初始化Log参数
*/
bool Log::init(const string& file_dir, const string& file_name, int close_log, int split_lines)
{
    // 01 启动写线程
    pthread_t tid;
    // 只创建一个写线程 避免在写过程中的资源竞争
    if (pthread_create(&tid, NULL, flush_log_thread, NULL) != 0)
    {
        printf("ERROR! Log::init ptherad_cerate failed! \n");
        exit(1);
    }
    
    m_close_log = close_log;       // 是否关闭
    m_split_lines = split_lines;   // 单个文件最大行数
    m_dir_name = file_dir; // 文件路径
    m_log_name = file_name; // 文件标识

    m_workque = new list<shared_ptr<string>>();

    // 创建文件 文件路径 = m_dir_name/m_log_name_time
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    m_today = sys_tm->tm_yday;

    char tail[16] = {0};
    snprintf(tail, 16, "%d_%02d_%02d", sys_tm->tm_year + 1900, sys_tm->tm_mon + 1, sys_tm->tm_mday);
    string log_full_name = "";
    log_full_name = m_dir_name + "/" + m_log_name + "_" + string(tail);
    m_fp = create_log_file(log_full_name.c_str());
    if (m_fp == NULL)
    {
        printf("ERROR! Create file failed! \n");
        exit(1);
        return false;
    }
    return true;
}

// 创建filename名字的log文件并返回指针
FILE *Log::create_log_file(const char *filename)
{
    return fopen(filename, "a+");
}

void Log::check_and_create(tm *my_tm)
{
    // 日志检查 是否拆分日志（情况1：按日期拆分，情况2：按当前文件大小拆分）
    if (my_tm->tm_yday != m_today || m_count % m_split_lines == 0) // everyday log
    {
        //char new_log_name[256] = {0}; // 新文件路径
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        //格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm->tm_year + 1900, my_tm->tm_mon + 1, my_tm->tm_mday);

        string new_log_name = "";
        // 日志过期：创建新文件，文件名 = 文件名_当前日期
        if (m_today != my_tm->tm_mday)
        {
            new_log_name = m_dir_name + "/" + m_log_name + "_" + string(tail);
            m_today = my_tm->tm_mday;
            m_count = 0;
        }
        else
        {
            new_log_name = m_dir_name + "/" + m_log_name + "_" + string(tail) + "_" + to_string(m_count / m_split_lines);
        }
        m_fp = create_log_file(new_log_name.c_str());
    }
}

/*
    生产者调用（负责写数据到队列中）
    参数：
        LOG_LEVEL：DEBUG、INFO、WARNING、ERROR
        format：等待写入的数据格式
        ...：可选参数 类似printf
    输出：
        写入日志格式：
        [日期：时间] [日志等级][输入正文]
*/
void Log::write_log(LOG_LEVEL level, const char *formt, ...)
{
    // 01 获取当前日期和时间
    struct timeval now = {0, 0};
    int ret = gettimeofday(&now, NULL);
    if(ret == -1){
        LOG_WARNING("[Log::write_log gettimeofday() failed!]");
        return;
    }

    time_t t = now.tv_sec;
    struct tm *sys_tm = NULL;
    struct tm my_tm;
    sys_tm = localtime_r(&t, &my_tm);
    if(sys_tm == NULL){
        LOG_WARNING("[Log::write_log localtime() failed!]");
        return;
    }
    // struct tm my_tm = *sys_tm;

    // 02 更新全局资源
    m_mutex_res.lock();
    m_count++;                // 更新行数
    check_and_create(sys_tm); // 检查是否需要更新文件
    m_mutex_res.unlock();

    // 03 格式化输出
    string info_buf = "";

    // 03-01 时间格式化 [年-月-日-时-分-秒-微秒]
    char tail[50] = {0};
    int n = snprintf(tail, 48, "[%d-%02d-%02d %02d:%02d:%02d.%06ld]",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec);
    if(n < 0){
        LOG_WARNING("[Log::write_log snprintf() time failed!]");
        return;
    }
    info_buf += tail;

    // 03-02 补充日志等级 [日志等级]
    switch (level)
    {
    case 0:
        info_buf += "[debug]";
        break;
    case 1:
        info_buf += "[info]";
        break;
    case 2:
        info_buf += "[warn]";
        break;
    case 3:
        info_buf += "[erro]";
        break;
    default:
        info_buf += "[debug]";
        break;
    }

    // 03-03 写入数据
    va_list valst;
    va_start(valst, formt);
    char msg[250] = {0};
    int m = vsnprintf(msg, 248, formt, valst);
    if(m < 0){
        LOG_WARNING("[Log::write_log vsnprintf() time failed!]");
        return;
    }
    msg[m] = '\n';
    msg[m + 1] = '\0';
    info_buf += msg;

    // 04 将格式化后的字符串加入队列（临界资源）
    m_mutex.lock();
    m_workque->push_back(shared_ptr<string>(new string(info_buf)));
    // m_log_queue.push_back(info_buf);
    m_mutex.unlock();

    // 5 释放信号量 通知日志写线程工作
    m_sem.post();
    va_end(valst);
}

void *Log::async_write_log()
{
    // string single_log;
    shared_ptr<string> t;
    while (true)
    {
        // 01 等待信号量
        m_sem.wait();

        // 02 从队列中摘下数据  临界资源保护
        m_mutex.lock();
        if(!m_workque->empty()){
            //cout << *m_workque->front() << endl;
             t = m_workque->front();
            m_workque->pop_front();
        }

        m_mutex.unlock();
        // 03 写入数据
        if(t != nullptr){
            fputs((*t).c_str(), m_fp);
        }
        t.reset();
    }
}

void Log::flush()
{
    fflush(m_fp);
}
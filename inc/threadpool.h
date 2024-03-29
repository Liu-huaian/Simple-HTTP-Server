#ifndef __THREAD_POOL_H_
#define __THREAD_POOL_H_

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "locker.h"

template<typename T>
class threadpool
{
public:
    threadpool(int thread_number = 8, int max_requests = 1000);
    ~threadpool();
    bool append(T* request);
private:
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number; // 线程池中线程数量
    int m_max_requests; // 请求队列中允许的最大请求数
    pthread_t* m_threads; // 线程池数组
    std::list<T*> m_workqueu; // 请求队列
    locker m_queuelocker; // 保护请求队列互斥锁
    sem m_queuestat; // 是否有任务需要处理
    bool m_stop; // 是否结束线程
};

/*
    线程池构造函数：传入线程数量和最大请求数量
*/
template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : m_thread_number(thread_number),
    m_max_requests(max_requests), m_stop(false), m_threads(NULL)
{
    if((thread_number <= 0) || (max_requests <= 0)){
        throw std::exception();
    }
    // 创建线程数组
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
    {
        throw std::exception();
    }
    // 创建线程 并设置位分离状态
    for(int i = 0; i < thread_number; ++i){
        printf("create the %d thread\n", i);
        if(pthread_create(m_threads+i, NULL, worker, this) != 0){
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
    }
}

/*
    析构函数：释放资源
*/
template<class T>
threadpool<T>::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

/*
    向工作队列追加新的请求，同时通过信号量通知对方
*/
template<class T>
bool threadpool<T>::append(T* requset){
    m_queuelocker.lock();
    if(m_workqueu.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueu.push_back(requset);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<class T>
void* threadpool<T>::worker(void* arg)
{
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

template<class T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueu.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueu.front();
        m_workqueu.pop_front();
        m_queuelocker.unlock();
        if(!request){
            continue;
        }
        request->process();
    }
}

#endif

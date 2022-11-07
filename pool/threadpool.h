#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>    // 线程相关头文件
#include <list>
#include <exception>
#include <cstdio>
#include "../lock/locker.h" // locker.h应该用双引号

//线程池类，定义成模板类，为了代码的复用
template<typename T>    // 模板参数T是任务类
class ThreadPool{
    public:
        ThreadPool(int thread_number = 8, int max_requests = 10000);
        ~ThreadPool();
        bool append(T* request, int state);
        bool append(T* request);
    private:
        static void* worker(void * arg);    // 存在问题：需要用到非静态的成员，但是静态成员函数不能访问非静态
        void run();
    private:
        int m_thread_number;    // 线程的数量
        int m_max_requests;// 请求队列中最多允许的，等待处理的请求数量
        pthread_t * m_threads; // 线程池数组,大小为m_thread_number
        std::list< T*> m_workqueue; // 请求队列
        locker m_queuelocker; // 保护请求队列的互斥锁
        sem m_queuestat; // 信号量用来判断是否有任务需要处理

        bool m_stop; // 是否结束线程
};

// 线程池构造函数
template<typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests) : 
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL){
        if((thread_number <= 0) || (m_max_requests <= 0)){
    
            throw std::exception();
        }

        m_threads = new pthread_t[m_thread_number];
        if(!m_threads){
            throw std::exception();
        }

        


        //创建thread_number个线程，并将它们设置为线程脱离
        for(int i = 0 ; i < thread_number; ++i){
            printf("create the %dth thread\n", i);
            
            if(pthread_create(m_threads + i, NULL, worker, this) != 0){ // 这里worker是线程工作的静态成员函数
                delete [] m_threads;
                throw std::exception();
            }

            if(pthread_detach(m_threads[i])){    // 让主线程与子线程分离，子线程结束后，资源自动回收
                delete[] m_threads;
                throw std::exception();
            }
        }
    }

// 线程池析构函数
template<typename T>
ThreadPool<T>::~ThreadPool(){
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool ThreadPool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 增加请求
template<typename T>
bool ThreadPool<T>::append(T * request){
    m_queuelocker.lock();   // 先把队列锁起来再进行操作（只有当前这个线程能操作）
    if(m_workqueue.size() > m_max_requests){    // 请求队列超过最大请求数
        m_queuelocker.unlock();
        return false;
    }

    // request->m_state = state;

    m_workqueue.push_back(request); // 队列中增加了一个request
    m_queuelocker.unlock();
    m_queuestat.post();  // 信号量也要增加
    return true;
}

template<typename T>
void* ThreadPool<T>::worker(void * arg){ // arg:本来静态函数成员中没有this，但是在pthread_create中把this传递给了worker
    ThreadPool * pool = (ThreadPool *) arg;
    pool->run();    // 线程池跑起来
    return pool;
}

//TODO proactor
template<typename T>
void ThreadPool<T>::run(){
    while(!m_stop){
        m_queuestat.wait(); // 如果说信号量有值，那么就不阻塞；反之，阻塞
        m_queuelocker.lock();

        // 如果没数据
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }

        // 如果有数据
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        // 如果对这个数据没有请求
        if(!request){   
            continue;
        }
        // 如果有请求，调用相应的函数
        request->process();
    }
}


#endif
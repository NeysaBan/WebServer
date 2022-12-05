#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>    // 线程相关头文件
#include <thread>
#include <list>
#include <exception>
#include <cstdio>
#include <condition_variable>
#include "../Task&Work/taskqueue.h"
#include "../lock/locker.h" // locker.h应该用双引号

//线程池类，定义成模板类，为了代码的复用
template<typename T>    // 模板参数T是任务类,这里实际上传入的类型是HttpConn
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
        // pthread_t * m_threads; // 线程池数组,大小为m_thread_number
        SafeQueue<T* > m_taskqueue; // 包含互斥锁的任务队列
        std::mutex m_conditional_mutex; // 线程休眠锁互斥变量
        std::condition_variable m_conditional_lock;   // 条件变量,用于让线程休眠或唤醒
        bool m_stop; // 是否结束线程
        class ThreadWorker  // 在线程池内内置类是为了防止环形构造
        {
            private:  
                int m_id;   // 本线程的工作id
                ThreadPool *m_pool; // 所属线程池,主要是为了读所属线程池的任务队列
            
            public:
                ThreadWorker(ThreadPool *pool, const int id): m_pool(pool),m_id(id){}
                void operator()()
                {
                    while(!m_pool->m_stop){
                        // m_queuestat.wait(); // 如果说信号量有值，那么就不阻塞；反之，阻塞
                        
                        std::unique_lock<std::mutex> lock(m_pool->m_conditional_mutex);
                        // 条件变量的wait要和unique_lock联合使用
                        while(m_pool->m_taskqueue.empty())
                            m_pool->m_conditional_lock.wait(lock);  // 如果任务队列中没有任务就一直阻塞在这里

                        T* request;
                        if(!m_pool->m_taskqueue.deQueue(request))
                            continue;   // 说明队列为空

                        // 如果对这个数据没有请求
                        if(!request){   
                            continue;
                        }
                        // 如果有请求，调用相应的函数
                        request->process();
                    }
                }
                
        };
        std::vector<std::thread> m_threads;
};

// 线程池构造函数
template<typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests) : 
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(std::vector<std::thread>(thread_number)){
        if((thread_number <= 0) || (m_max_requests <= 0)){
    
            throw std::exception();
        }

        // m_threads = new pthread_t[m_thread_number];
        // if(!m_threads){
        //     throw std::exception();
        // }

        // //创建thread_number个线程，并将它们设置为线程脱离
        // for(int i = 0 ; i < thread_number; ++i){
        //     printf("create the %dth thread\n", i);
            
        //     if(pthread_create(m_threads + i, NULL, worker, this) != 0){ // 这里worker是线程工作的静态成员函数
        //         delete [] m_threads;
        //         throw std::exception();
        //     }

        //     if(pthread_detach(m_threads[i])){    // 让主线程与子线程分离，子线程结束后，资源自动回收
        //         delete[] m_threads;
        //         throw std::exception();
        //     }
        // }

        for(int i = 0 ; i < thread_number ; i++){
            m_threads.at(i) = std::thread(ThreadWorker(this, i));
        }
    }

// 线程池析构函数
template<typename T>
ThreadPool<T>::~ThreadPool(){
    // delete[] m_threads;
    m_stop = true;
    m_conditional_lock.notify_all();

    for(int i = 0 ; i < m_threads.size() ; i++){
        if(m_threads.at(i).joinable())
            m_threads.at(i).join();
    }
}

template<typename T>
bool ThreadPool<T>::append(T *request, int state)
{
    if(m_taskqueue.size() >= m_max_requests)
        return false;
    request->m_state = state;
    m_taskqueue.enQueue(request);
    // m_queuestat.post();
    m_conditional_lock.notify_one();
    return true;
}

// 增加请求
template<typename T>
bool ThreadPool<T>::append(T * request){
    if(m_taskqueue.size() >= m_max_requests)
        return false;
    m_taskqueue.enQueue(request);
    m_conditional_lock.notify_one();
    return true;
}

template<typename T>
void* ThreadPool<T>::worker(void * arg){ // arg:本来静态函数成员中没有this，但是在pthread_create中把this传递给了worker
    ThreadPool * pool = (ThreadPool *) arg;
    return pool;
}


#endif
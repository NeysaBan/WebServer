#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>    // 互斥锁相关的头文件
#include <exception>    //异常相关头文件
#include <semaphore.h>  // 信号量相关头文件

// 线程同步机制封装类

// 互斥锁类
class locker{
    public:
        // return: 成功——0, 失败——errno
        locker(){
            if(pthread_mutex_init(&m_mutex, NULL) != 0){
                throw std::exception();
            }
        }

        ~locker(){  // 析构函数，销毁操作
            pthread_mutex_destroy(&m_mutex);
        }

        bool lock(){    // 上锁操作
            return pthread_mutex_lock(&m_mutex) == 0;
        }

        bool unlock(){  // 解锁
            return pthread_mutex_unlock(&m_mutex) == 0;
        }

        pthread_mutex_t * get() {
            return &m_mutex;    //获得互斥量
        }
    private:
        pthread_mutex_t m_mutex;
};

// 条件变量类
class cond{
    public:
        cond(){
            if(pthread_cond_init(&m_cond, NULL) != 0){
                throw std::exception();
            }
        }

        ~cond(){
            pthread_cond_destroy(&m_cond);
        }

        // 等待目标条件变量.该函数调用时需要传入 mutex参数(加锁的互斥锁)
        // 函数执行时,先把调用线程放入条件变量的请求队列,然后将互斥锁mutex解锁
            //当函数成功返回为0时,互斥锁会再次被锁上
            // 也就是说函数内部会有一次解锁和加锁操作
        bool wait(pthread_mutex_t * m_mutex){   // 由于这里传进来是指针,所以下面函数使用的m_mutex都不用再加&
            int ret = 0;
            pthread_mutex_lock(m_mutex); //加锁
            ret = pthread_cond_wait(&m_cond, m_mutex);  // 解锁加锁
            pthread_mutex_unlock(m_mutex);  // 解锁
            return  ret == 0;
        }

        bool timedwait(pthread_mutex_t * mutex, struct timespec t){
            return pthread_cond_timedwait(&m_cond, mutex, &t) == 0; // 等待m_cond; 在时间t之前,mutex是锁的状态
        }

        bool signal(pthread_mutex_t * mutex){ // 唤醒一个
            return pthread_cond_signal(&m_cond) == 0;
        }

        bool broadcast(){ // 以广播方式唤醒所有等待目标条件变量的进程
            return pthread_cond_broadcast(&m_cond) == 0;
        }
    private:
        pthread_cond_t m_cond;
};

// 信号量类
class sem{
    public:
        // return: 成功——0, 失败——errno
        sem(){
            if(sem_init(&m_sem, 0, 0) != 0){
                throw std::exception();
            }
        }

        sem(int num){
            if(sem_init(&m_sem, 0, num) != 0){
                throw std::exception();
            }
        }

        ~sem(){
            sem_destroy(&m_sem);
        }

        bool wait(){    // 等待信号量,将信号量减1,信号量为0时,wait()阻塞
            return sem_wait(&m_sem) == 0;
        }

        bool post(){    // 增加信号量,将信号量加1,信号量>0时,唤醒调用post的进程
            return sem_post(&m_sem) == 0;
        }
    private:
        sem_t m_sem;
};

#endif
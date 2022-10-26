#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>    // 互斥锁相关的头文件
#include <exception>    //异常相关头文件
#include <semaphore.h>  // 信号量相关头文件

// 线程同步机制封装类

// 互斥锁类
class locker{
    public:
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

        bool unlock(){
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

        bool wait(pthread_mutex_t * m_mutex){
            return pthread_cond_wait(&m_cond, m_mutex) == 0;
        }

        bool timedwait(pthread_mutex_t * mutex, struct timespec t){
            return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
        }

        bool signal(pthread_mutex_t * mutex){ // 唤醒一个
            return pthread_cond_signal(&m_cond) == 0;
        }

        bool broadcast(){ // 唤醒所有
            return pthread_cond_broadcast(&m_cond) == 0;
        }
    private:
        pthread_cond_t m_cond;
};

// 信号量类
class sem{
    public:
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

        bool wait(){    // 等待信号量
            return sem_wait(&m_sem) == 0;
        }

        bool post(){    // 增加信号量
            return sem_post(&m_sem) == 0;
        }
    private:
        sem_t m_sem;
};

#endif
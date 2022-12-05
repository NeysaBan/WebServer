#ifndef TASKQUEUE_H
#define TASKQUEUE_H

#include <queue>
#include <mutex>

// 实现一个使用互斥锁保护的任务队列

template <typename T>
class SafeQueue{
    private:
        std::queue<T> m_queue;
        std::mutex m_mutex;

    public:
        SafeQueue() {}
        SafeQueue(SafeQueue &&other)    {}
        ~SafeQueue() {}

        bool empty()
        {
            // 此时也要使用锁
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_queue.empty();
        }

        int size()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_queue.size();
        }

        void enQueue(T &t)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_queue.emplace(t);
        }

        bool deQueue(T &t)  // 取出队头元素
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            if(m_queue.empty())
                return false;

            t = std::move(m_queue.front());

            m_queue.pop();

            return true;
        }
};


#endif
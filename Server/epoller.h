#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h> //epoll_ctl()
#include <fcntl.h>  // fcntl()
#include <unistd.h> // close()
#include <assert.h> // close()
#include <vector>
#include <errno.h>

class Epoller{
    public:
        explicit Epoller(int maxEvent = 1024);  // 防止自动隐式转换 https://www.cnblogs.com/wangke-tech/p/11894160.html

        ~Epoller();

        int setnonblocking(int fd); // addfd中会用到

        // 添加文件描述符到epoll中
        void addfd(int fd, bool oneshot);
        // 从epoll中删除文件描述符
        void removefd(int fd);
        //修改文件描述符
        void modfd(int fd, int ev);

        // int wait(int timeoutMs = -1);

        // int GetEventFd(size_t i) const;

        // uint32_t GetEvents(size_t i) const;

    private:

        int epollfd_;

        std::vector<struct epoll_event> events_;
};

#endif
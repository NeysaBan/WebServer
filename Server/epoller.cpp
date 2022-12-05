#include "epoller.h"

Epoller::Epoller(int maxEvent):epollfd_(epoll_create(512)), events_(maxEvent){
    assert(epollfd_ >= 0 && events_.size() > 0);
}

Epoller::~Epoller(){
    close(epollfd_);
}

int Epoller::setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 添加需要监听的文件描述符到epoll中
void Epoller::addfd(int fd, bool oneshot){
    epoll_event ev ;
    ev.data.fd = fd;

    // ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // EPOLLIN:水平触发模式 EPOLLRDHUP:挂起

    ev.events = EPOLLIN |EPOLLRDHUP; // 实际上这样的话，监听描述符添加到epoll中时也变成了边沿触发，应该特殊处理

    if(oneshot){
        // 防止同一个通信被不同的线程处理
        ev.events |= EPOLLONESHOT;    // 位或运算符???
    }
    epoll_ctl(epollfd_, EPOLL_CTL_ADD, fd, &ev);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}
// 从epoll中删除监听的文件描述符
void Epoller::removefd(int fd){
    epoll_ctl(epollfd_, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
// 修改文件描述符,重置socket 上EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发。
void Epoller::modfd(int fd, int cur_event){
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = cur_event | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd_, EPOLL_CTL_MOD, fd, &ev);
}



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include <signal.h>
#include "http_conn.h"
#include <assert.h>

#define MAX_FD 65535    // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000// 最多可监听的事件数量

// 添加信号捕捉
void addsig(int sig, void(handler)(int)){   // sig：处理的信号 void(handler(int))：信号处理函数
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));  // 清空sa中的数据
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);    // 设置临时阻塞
    assert(sigaction(sig, &sa, NULL) != -1);  // 注册信号
}

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool oneshot);
// 从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
//修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

int main(int argc, char* argv[]){   // 在命令行指定端口号

    if(argc <= 1){  // 至少要传递一个端口号
        printf("按照如下格式运行： %s port_number\n", basename(argv[0]));   // argv[0]是程序名
        return 1;
    }

    // 获取端口号
    int port  = atoi(argv[1]);  // 转换成一个整数

    // 在网络通信时，有可能一端断开连接，另一端还在往里面写数据，这时候就会产生一个信号SIGPIPE
    // 所以要对SIGPIE信号进行处理
    // 遇到SIGPIPE默认情况下是终止进程，在这里ignore，即捕捉到信号时忽略它
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化线程池
    threadpool<http_conn> * pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }catch(...){    // 如果捕捉到异常
        return 1;
    }

    // proactor，主线程监听事件，读写数据，把数据封装成任务类，把这个任务对象交给子线程（线程池），线程池取任务、做任务
    // 创建一个数组，用于保存所有客户端信息
    http_conn * users = new http_conn[MAX_FD];
    
    // 创建监听的套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    
    int ret = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // TCP
    // 先设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    // 绑定
    ret = bind(listenfd, (struct  sockaddr*)&address, sizeof(address));
    // 监听
    ret = listen(listenfd, 5);

    // 创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5); // 可以传任何值，除了0

    // 将监听的文件描述符添加到epoll对象中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd; // 静态成员对象

    while(true){
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((num < 0) && (errno != EINTR)){
            printf("epoll failure\n");
            break;
        }

        // 循环遍历事件数组
        for(int i = 0 ; i < num ; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                // 有客户端连接进来
                struct sockaddr_in client_address; // 不用初始化, 因为accept会把数据填充上
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);
                
                if(connfd < 0){
                    printf("errno is : %d\n", errno);
                    continue;
                }

                if(http_conn::m_user_count >= MAX_FD){
                    // 目前连接数满了
                    // 给客户端写一个信息：服务器内部正忙
                    close(connfd);
                    continue;
                }
                // 将新客户端的数据初始化，放到数组中
                users[connfd].init(connfd, client_address);// 将连接的文件描述符作为索引
            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){ // 异常断开事件或错误等事件
                users[sockfd].close_conn();
            }else if(events[i].events & EPOLLIN){  // 读事件,如果数据到了
                if(users[sockfd].read()){
                    // 一次性把所有事件都读完
                    pool->append(users + sockfd); // 添加到线程池
                }else{
                    users[sockfd].close_conn(); // 没读到数据 / 读失败了
                }
            }else if(events[i].events & EPOLLOUT){ 
                if(!users[sockfd].write()) // 一次性写完所有数据
                {
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}
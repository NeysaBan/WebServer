#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <memory>   // unique_ptr

#include "epoller.h"
#include "../timer/lst_timer.h"
#include "../pool/threadpool.h"
#include "../http/httpconn.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer{
    public:

        WebServer();
        ~WebServer();

        void init(int port, int opt_linger, int trigmode, int thread_num,
                    int log_write, int close_log);  

        void thread_pool();
        void trig_mode();
        void log_write();

        void eventListen();
        void eventLoop();

        void timer(int connfd, struct sockaddr_in client_address);
        void adjust_timer(util_timer *timer);
        void deal_timer(util_timer *timer, int sockfd);

        bool dealClinetData();
        bool dealwithSignal(bool& timeout, bool& stop_server);
        void dealwithRead(int sockfd);
        void dealwithWrite(int sockfd);


    public:  
        //基础
        int m_port;
        char *m_root;


        int m_pipefd[2];
        int m_epollfd;
        HttpConn *users;

        // 线程池相关 
        ThreadPool<HttpConn> *m_pool;
        int m_thread_num;
        // int m_maxRequests;

        // epoll_event相关
        epoll_event events[MAX_EVENT_NUMBER];

        int m_listenfd;
        int m_opt_linger;
        int m_TRIGmode;
        int m_LISTENTrigmode;
        int m_CONNTrigmode;

        int m_log_write;
        int m_close_log;

        // 定时器相关
        client_data* users_timer;
        util_func util;
};

#endif
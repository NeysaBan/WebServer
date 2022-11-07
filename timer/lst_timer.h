#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>

#define BUFFER_SIZE 64
class util_timer; // 因为在client_data中要用,所以要前向声明

// 用户数据结构
struct client_data
{
    sockaddr_in address; // 客户端socket地址
    int sockfd; // socket文件描述符
    char buf[BUFFER_SIZE]; // 读缓存
    util_timer* timer; // 定时器
};

// 定时器类
// 每当有数据读写,超时时间就会发生改变
class util_timer{
    public:
        util_timer() : prev(NULL), next(NULL){}

        time_t expire; // 任务超时时间,这里使用绝对时间
        void (*cb_func)(client_data *); // 任务回调函数,回调函数处理的客户数据
        client_data* user_data;
        util_timer* prev; // 指向前一个定时器
        util_timer* next; // 指向后一个定时器
};

// 定时器链表,它是一个升序、双向链表,且带有头节点和尾节点
class sort_timer_lst{
    public:
        sort_timer_lst();
        ~sort_timer_lst();

        void add_timer(util_timer * timer);

        
        void adjust_timer(util_timer* timer);
        void add_timer(util_timer * timer, util_timer * lst_head);

        // 将目标定时器timer从链表中删除
        void del_timer(util_timer * timer);

        /* SIGALARM 信号每次被触发*/
        void tick();

    private:

        util_timer * head;
        util_timer * tail;

        
};

class util_func{
    public:
        util_func(){};
        ~util_func(){};

        void init(int timeslot);

        int setnonblocking(int fd);

        //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
        void addfd(int epollfd, int fd, bool oneshot, int TRIGMode);

        //信号处理函数
        static void sig_handler(int sig);

        //设置信号函数
        void addsig(int sig, void(handler)(int), bool restart = true);

        //定时处理任务，重新定时以不断触发SIGALRM信号
        void timer_handler();

        void show_error(int connfd, const char *info);

    public:
        static int *u_pipefd;
        sort_timer_lst m_timer_lst;
        static int u_epollfd;
        int m_TIMESLOT; 
};

void cb_func(client_data * user_data);

#endif

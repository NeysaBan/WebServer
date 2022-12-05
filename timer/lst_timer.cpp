#include "lst_timer.h"
#include "../HttpConnection/httpconn.h"
#include "../print_time/print_time.h"

PrintTime PTt;

/*----------------------------------------------sort_timer_lst----------------------------------------*/

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }

    if (!head)
    {
        head = tail = timer;
        return;
    }

    /* 如果目标定时器的超时时间小于当前链表中所有定时器的超时时间,则把该定时器插入连表头部,成为链表新的头节点
        否则需要调用重载函数,add_timer(), 把它插入到链表中合适的位置,以保证链表的升序特性*/
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

/* 一个重载的辅助函数,它被公有的add_timer函数和adjust_timer 函数调用
            该函数表示将目标定时器timer添加到节点lst_head之后的部分链表中*/
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;

    /* 遍历list_head节点之后的部分链表,直到找到一个超时时间大雨目标定时器的节点
        并把目标定时器插入到该节点之前*/
    while (tmp)
    {
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    /* 如果遍历完lst_head节点之后的部分链表,仍未找到超时时间大于目标定时器的节点
        则将目标定时器插入链表尾部,并把它设置为链表新的尾节点*/
    if (!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

/* 当某个定时任务发生变化时,调整对应的定时器在链表中的位置.这个函数只考虑超时时间延长的情况,即该定时器需要
            向链表的尾部移动*/
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    util_timer *tmp = timer->next;
    // 如果被调整的目标定时器处在链表的尾部,或该定时器新的超时时间值仍然小于其下一个定时器的超时时间则不用调整
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    // 如果目标定时器时链表的头节点,则将该定时器从链表中取出并且重新插入
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        // 如果目标定时器不是链表的头节点,则将定时器从链表中取出,
        // 然后重新插入其原来所在位置的后面位置(因为只考虑超时情况)
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

// 将目标定时器timer从链表中删除
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    // 下面这个条件成立表示链表中只有一个定时器,即目标定时器
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }

    // 链表中至少两个定时器,且本定时器是头节点
    if (timer == head)
    {
        head = timer->next;
        head->prev = NULL;
        delete timer;
        return;
    }

    // 链表中至少两个定时器,且本定时器是尾节点
    if (timer == tail)
    {
        tail = timer->prev;
        tail->next = NULL;
        delete timer;
        return;
    }

    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
    return;
}

/* SIGALARM 信号每次被触发*/
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    PTt.mvPrintf("[alarm] 间隔时间提醒: Timer Tick\n\n");
    time_t cur = time(NULL); // 获取当前系统时间
    util_timer *tmp = head;
    // 从头节点开始依次处理每个定时器,直到遇到一个尚未到期的定时器
    while (tmp)
    {
        /* 因为每个定时器都使用绝对时间作为超时值,所以可以把定时器的超时值和当前系统时间
            进行比较以判断定时器是否到期*/
        if (cur < tmp->expire) // 链表上的定时器是按照到期时间排列的,快到期的在前面
        {
            break;
        }

        // 说明超时
        // 调用定时器的回调函数,以执行定时任务
        tmp->cb_func(tmp->user_data);
        // 执行完定时器中的定时任务之后,就将它从链表中删除,并重置链表
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

/*-----------------------------------------------util_func-------------------------------------------*/

void util_func::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

int util_func::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void util_func::addfd(int epollfd, int fd, bool oneshot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    // EPOLLIN:水平触发模式 EPOLLRDHUP:挂起
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP; // 实际上这样的话，监听描述符添加到epoll中时也变成了边沿触发，应该特殊处理

    if (oneshot)
    {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT; // 位或运算符???
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

void util_func::sig_handler(int sig)
{ // 信号处理逻辑
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0); // 往pipefd[1]中写, 0表示读
    errno = save_errno;
}

void util_func::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void util_func::timer_handler()
{
    // 定时处理任务,实际上就是调用tick()函数
    m_timer_lst.tick();
    // 因为一次alarm调用只会引起一次SIGALARM信号,所以我们要重新定时,
    alarm(m_TIMESLOT);
}

void util_func::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *util_func::u_pipefd = 0;
int util_func::u_epollfd = 0;

class util_func;
// 定时器回调函数,它删除非活动连接socket上的注册时间,并关闭之
void cb_func(client_data *user_data)
{
    epoll_ctl(util_func::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    HttpConn::m_user_count--;

    
    PTt.mvPrintf("[closed] Client ", user_data->sockfd, " close! Bye! Gooooood day!~\n");
}
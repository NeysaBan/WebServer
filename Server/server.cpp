#include "server.h"

PrintTime PTs;

WebServer::WebServer()
{
    // http_conn对象
    users = new HttpConn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[11] = "/resources";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root); // 路径拼接
    // m_root = "/data/cpp/WebServer/resources";
    PTs.mvPrintf("[info] html stored in ", m_root);
    printf("\n");

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

// TODO 和源代码不同
void WebServer::init(int port, int opt_linger, int trigmode, int thread_num,
                    int log_write, int close_log)
{
    m_port = port;
    m_opt_linger = opt_linger;
    m_TRIGmode = trigmode;
    m_thread_num = thread_num;
    // m_maxRequests = maxRequests;

    m_log_write = log_write;
    m_close_log = close_log;
}

void WebServer::trig_mode()
{
    // LT + LT
    if (0 == m_TRIGmode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGmode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGmode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGmode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new ThreadPool<HttpConn>(m_thread_num);
}

void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if (0 == m_opt_linger)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_opt_linger)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    // 端口复用
    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 6);
    assert(ret >= 0);

    util.init(TIMESLOT);

    // epoll创建内核时间表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    util.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    HttpConn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    util.setnonblocking(m_pipefd[1]);
    util.addfd(m_epollfd, m_pipefd[0], false, 0);

    util.addsig(SIGPIPE, SIG_IGN);
    util.addsig(SIGALRM, util.sig_handler, false);
    util.addsig(SIGTERM, util.sig_handler, false);

    alarm(TIMESLOT);

    util_func::u_pipefd = m_pipefd;
    util_func::u_epollfd = m_epollfd;
}

// 记录用户信息,并把用户的定时器信息初始化掉
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    /*
        struct client_data
        {
            sockaddr_in address; // 客户端socket地址
            int sockfd; // socket文件描述符
            char buf[BUFFER_SIZE]; // 读缓存
            util_timer* timer; // 定时器
        };
    */

    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log);

    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    // 创建定时器
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    util.m_timer_lst.add_timer(timer);
}

// 若有新数据传输,重置该客户端的定时器,调整定时器在链表中的位置
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    util.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
        util.m_timer_lst.del_timer(timer);

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealClinetData()
{
    struct sockaddr_in ClientAddr;
    socklen_t ClientAddr_Len = sizeof(ClientAddr);
    if (0 == m_LISTENTrigmode)
    { // 一次不接收完,后续还会通知
        int connfd = accept(m_listenfd, (struct sockaddr *)&ClientAddr, &ClientAddr_Len);
        PTs.mvPrintf("[connect] 服务器你好, 客户端 ", connfd,", 听你指挥了!\n");
        if (connfd < 0)
        {
            PTs.mvPrintf("[error] errno is : %d\n", errno);
            LOG_ERROR("%s:errno is:", errno , ", accept error");
            return false;
        }
        if (HttpConn::m_user_count >= MAX_FD)
        {
            util.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, ClientAddr);
    }
    else
    { // 一次不接收完,后续不会通知
        while (true)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&ClientAddr, &ClientAddr_Len);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (HttpConn::m_user_count >= MAX_FD)
            {
                util.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, ClientAddr);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithSignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
        return false;
    else if (ret == 0)
        return false;
    else
    {
        for (int i = 0; i < ret; i++)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

// TODO 和源代码不同,只有proactor
void WebServer::dealwithRead(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // reactor
    // 略
    // proactor
    if (users[sockfd].read_once())
    {
        PTs.mvPrintf("[info] 客户端", sockfd, "请稍等, take the task to queue······\n");
        LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
        m_pool->append(users + sockfd);

        if (timer)
            adjust_timer(timer);
    }
    else
    {
        deal_timer(timer, sockfd);
    }
}

// TODO 只有proactor
void WebServer::dealwithWrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // proactor
    if (users[sockfd].write())
    {
        LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
        if (timer)
            adjust_timer(timer);
    }
    else
        deal_timer(timer, sockfd);
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);

        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                PTs.mvPrintf("[connect] 服务端看见: There is a new client~\n");
                bool flag = dealClinetData();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                PTs.mvPrintf("[info] Deal with Signal");
                bool flag = dealwithSignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
                PTs.mvPrintf("Deal Signal success!");
            }
            else if (events[i].events & EPOLLIN)
            {
                PTs.mvPrintf("[info] Deal with Reading");
                dealwithRead(sockfd);
                PTs.mvPrintf("Deal Reading success!\n");
            }
            else if (events[i].events & EPOLLOUT)
            {
                PTs.mvPrintf("[info] Deal with Writing");
                dealwithWrite(sockfd);
                PTs.mvPrintf("Deal Writing success!\n");
            }
        }
        if (timeout)
        {
            util.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}
#include "httpconn.h"

// 所有socket上的事件都被注册到同一个epoll内核事件中,所以设置成静态的
int HttpConn::m_epollfd = -1;
// 所有的客户数
int HttpConn::m_user_count = 0;

// 定义HTTP响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has had bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbbiden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char *doc_root = "/data/cpp/WebServer/resources";

// 设置文件描述符非阻塞
int setnonbloking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 添加需要监听的文件描述符到epoll中
void addfd(int epollfd, int fd, bool oneshot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP; // 实际上这样的话，监听描述符添加到epoll中时也变成了边沿触发，应该特殊处理
    // event.events = EPOLLIN | EPOLLRDHUP; // EPOLLIN:水平触发模式 EPOLLRDHUP:挂起

    if (oneshot)
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT; // 位或运算符???

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonbloking(fd);
}
// 从epoll中删除监听的文件描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
// 修改文件描述符,重置socket 上EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发。
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void HttpConn::close_conn()
{ // 关闭连接
    if (m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; // 关闭一个连接，总客户数量-1
    }
}

// 初始化连接
void HttpConn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode)
{
    m_sockfd = sockfd;
    m_address = addr;

    // 添加到epoll对象中
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++; // 总用户数加1

    // 当浏览器出现连接重置时,可能是网站根目录出错或http响应格式出错,或访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;

    init();
}

void HttpConn::init()
{

    bytes_have_send = 0;
    bytes_to_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE; // 初始化状态为解析请求行
    m_linger = false;                        // 默认不保持连接, Connection: keep-alive保持连接

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;

    m_checked_idx = 0;
    m_start_line = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    m_state = 0;
    timer_flag = 0;
    improv = 0;

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool HttpConn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    // 读取到的字节
    int bytes_read = 0;
    if (0 == m_TRIGMode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
            return false;

        return true;
    }
    else
    {
        // ET模式,一次全部读完(读不完下次也不会再通知了)
        while (true)
        {
            // 从m_read_buf + m_read_idx开始保存数据,大小是READ_BUFFER_SIZE - m_read_idx
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                              READ_BUFFER_SIZE - m_read_idx, 0);
            printf("bytes_read: %d\n", bytes_read);
            if (bytes_read == -1)
            {
                printf("sockfd:%d\n", m_sockfd);
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 非阻塞地读，没有数据时，会有这两个errno
                    break;
                }
                return false; // 出错
            }
            else if (bytes_read == 0)
            {
                // 对方关闭连接
                return false;
            }
            m_read_idx += bytes_read;
        }

        // printf("读取到了数据:%s\n", m_read_buf);
        return true;
    }
}

bool HttpConn::write()
{
    int temp = 0;
    int newadd = 0;
    // int bytes_have_send = 0; // 已发送的字节
    // int bytes_to_send = m_write_idx; // 将要发送的字节， (m_write_idx)写缓冲区中待发送的字节数

    if (bytes_to_send == 0)
    {
        // 将要发送的字节为0，这一次的响应结束
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count); // 分散写,即多块分散内存,连续写,就可以把
        if (temp <= -1)
        {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLL事件，虽然在此期间
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); // https://blog.csdn.net/CODINGCS/article/details/115046256
                return true;
            }
            unmap(); // 发送失败但不是缓冲区问题,取消映射
            return true;
        }
        // 说明成功地把temp个字节写进TCP写缓冲了
        newadd = bytes_have_send - m_write_idx; // 偏移文件iovec的指针
        bytes_to_send -= temp;
        bytes_have_send += temp;

        // 第一个iovec头部信息的数据已经读完,发送第二个iovec数据
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            // 不再发送头部信息
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + newadd;
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();                                         // 全部写完之后要释放掉
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); // 缓冲区可读，所以更改状态为EPOLLIN
            if (m_linger)
            {           // TODO 为什么保持连接的话，要初始化一下？
                init(); // 重新初始化HTTP对象
                return true;
            }
            else
            {
                return false; // TODO 断开连接为什么要return false ?
            }
        }
    }
}

void HttpConn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 往写缓冲中写入待发送的数据
bool HttpConn::add_response(const char *format, ...)
{ // 后面是可变参数
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;           // 用来解析参数
    va_start(arg_list, format); // 对arg_list进行初始化，让arg_list指向可变参数表里面的第一个参数。第一个参数是 ap 本身，第二个参数是在变参表前面紧挨着的一个变量，即“...”之前的那个参数
    // m_write_buf + m_write_idx从哪开始发送数据
    // 大小 WRITE_BUFFER_SIZE - 1 - m_write_idx
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list); // 把可变参数格式化输出到一个字符串数组
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list); // 释放指针，将输入的参数 ap 置为 NULL。通常va_start和va_end是成对出现
    return true;
}

bool HttpConn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool HttpConn::add_headers(int content_length)
{
    add_content_length(content_length);
    add_content_type(); // 内容类型
    add_linger();       // 连接
    add_blank_line();   // 空行

    return true; // TODO 源代码这里没有写返回值
}

bool HttpConn::add_content_length(int content_len)
{
    return add_response("Content-Length :%d\r\n", content_len);
}

bool HttpConn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool HttpConn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool HttpConn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool HttpConn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 主状态机,解析请求
HttpConn::HTTP_CODE HttpConn::process_read() // 解析http请求
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char *text = 0;

    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) ||
           (line_status = parse_line()) == LINE_OK)
    {
        // 解析到了请求体，且是完整的数据 / 解析到了一行完整的数据

        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line :\n       %s\n", text);

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text); // 解析获取的数据
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {                        // 请求内容到请求头结束,才会返回GET_REAQUEST
                return do_request(); // 解析具体的内容
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
            {
                return do_request();
            }
            line_status = LINE_OPEN; // 失败的话需要改状态
            break;
        }
        default:
        {
            return INTERNAL_ERROR;
        }
        }
    }

    return NO_REQUEST;
}

// 根据服务器处理
bool HttpConn::process_write(HTTP_CODE ret)
{
    switch (ret)
    { // 全部是加入状态行和头
    case INTERNAL_ERROR:
        add_status_line(500, error_500_title); // 加入状态码和简单的描述
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
        {
            return false;
        }
        break;
    case BAD_REQUEST:
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
        {
            return false;
        }
        break;
    case NO_RESOURCE:
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    case FORBIDDEN_REQUEST:
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
        {
            return false;
        }
        break;
    case FILE_REQUEST: // 请求到了文件
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    default:
        return false;
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 解析一行，判断依据: \r\n
HttpConn::LINE_STATUS HttpConn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN; // 说明读取不完整,没读上\n
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        // TODO return LINE_BAD就一直解析不出来,原因是当temp不是\r或\n时,循环一次就会返回LINE_BAD,而LINE_BAD是无法进入process_read中的循环的
    }
    return LINE_OPEN;
}

// TODO 和源代码不同但没改的地方
// 解析http请求行，获取请求方法，目标url，http版本
HttpConn::HTTP_CODE HttpConn::parse_request_line(char *text)
{
    // text = "GET / HTTP/1.1"
    m_url = strpbrk(text, " \t"); // 从text第一个字符向后寻找，如果该字符存在于s2中，那么就从这个字符的位置开始返回
    // m_url = " / HTTP/1.1"
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    // text = "GET\0/ HTTP/1.1" = "GET"
    // m_url = "/ HTTP/1.1"

    char *method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }

    // /index.html HTTP?1.1
    // 检索字符串str1中第一个不在字符串str2中出现的字符下标
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    /**
     * http://192.168.110.129:10000/index.html
     */
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr(m_url, '/'); // 找'\'第一次出现的位置
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    m_check_state = CHECK_STATE_HEADER; // 主状态机检查状态变成检查请求头

    return NO_REQUEST;
}
HttpConn::HTTP_CODE HttpConn::parse_headers(char *text) // 解析请求头
{
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0')
    {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则，说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        // 处理Connection头部字段， Connection: keep-alive
        text += 11;
        text += strspn(text, " \t"); // 返回s1中第一个不在字符串s2中出现的字符下标
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true; // 要保持连接
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        printf("oop! Unkonwn header %s\n", text);
    }
    return NO_REQUEST;
}
// 没有真正解析HTTP请求的请求体，只是判断它是否被完整地读入了
HttpConn::HTTP_CODE HttpConn::parse_content(char *text) // 解析请求体
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// TODO 和源文件不同但没改
// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap
// 将其映射到内存地址m_file_address处，并告诉调用获取文件成功
HttpConn::HTTP_CODE HttpConn::do_request()
{
    // "/data/cpp/WebServer/resources"
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // 把m_url中的FILENAME_LEN - len - 1(这个字符可以超过m_url的长度，但不能超过m_real_file的长度)
    // 个字符复制到m_real_file的len位置开始的地方
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1); // 最后一个参数是最长的长度不能超过这些?
    // m_real_file  = "/data/cpp/WebServer/resources/index.html"
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射,把网页数据映射到内存中
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

//由线程池中的工作线程调用，处理http请求的入口函数
void HttpConn::process()
{
    // 解析http请求
    printf("\n~~~~~~~~~~~~~~~~~~~~~~~process~~~~~~~~~~~~~~~~~~~~~~~\n\n");
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    printf("******Process success!******\n");

    // 生成响应
    printf("\n~~~~~~~~~~~~~~~~~~~~~~~Response!~~~~~~~~~~~~~~~~~~~~~~~\n");
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    // 因为使用了ONESHOT，所以必须每次操作完都重新去添加事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
    printf("******Response success!******\n");
}

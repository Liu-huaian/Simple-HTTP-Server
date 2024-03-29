#include "http_conn.h"
#include "log.h"

#define DEBUG 2

const char *ok_200_title = "OK";
const char *err_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";
/* 网站根目录 */
const char *doc_root = "../template/web";
const char *cgi_root = "../template/cgi";

/*
    设置文件描述符fd为非阻塞状态
    返回旧的文件状态
*/
int setNonBlocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option); // 小坑
    return old_option;
}

/*
    将fd注册到epollfd中，选择是否oneShot(触发一次)
    默认：EPOLLIN + EPOLLET + EPOLLRDHUP
*/
void addFd(int epollfd, int fd, bool oneShot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (oneShot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setNonBlocking(fd);
}


int writePipe(int __fd, const void *__buf, size_t n){
    return write(__fd, __buf, n);
}

int readPipe(int __fd, void *__buf, size_t __nbytes){
    return read(__fd, __buf, __nbytes);
}

/*
    将fd从epollfd中移除
*/
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/*
    向epollfd中修改fd，新增属性ev
    event.events = ev | EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;
*/
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLIN | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/*
    是否关闭与客户端的连接套接字
*/
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

/*
    初始化连接：
        sockfd：连接套接字文件描述符
        addr：客户端地址
*/
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    int res = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &res, sizeof(res));
    addFd(m_epollfd, m_sockfd, true);
    m_user_count++;
}

/*
    私有函数，初始化内部变量参数
*/
void http_conn::init()
{
    m_chek_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_check_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
    memset(m_cgi_buf, '\0', WRITE_BUFFER_SIZE);
}   

/*
    检查m_read_buf中合法的一行。
    返回值：
        LINE_OK：合法一行，\r\n均被置为\0，idx指向下一行开始
        LINE_BAD: 非法一行
        LINE_OPEN：目前缓冲区中尚未完全读取一行数据
*/
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_check_idx < m_read_idx; ++m_check_idx)
    {
        temp = m_read_buf[m_check_idx];
        if (temp == '\r')
        {
            if ((m_check_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_check_idx + 1] == '\n')
            {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_check_idx > 1) && (m_read_buf[m_check_idx - 1] == '\r'))
            {
                m_read_buf[m_check_idx - 1] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/*
    从m_sockfd中读取尽可能多的数据到m_read_buf中
    返回值：
        读取成功：true
        读取错误：false
*/
bool http_conn::read()
{
    if (DEBUG==1)
    {
        printf("start read data form socket:\n");
    }
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        if (DEBUG==1)
        {
            printf("读缓冲区不足:\n");
        }
        return false;
    }

    int bytes_read = 0;
    while (true)
    {
        if (DEBUG==1)
        {
            printf("当前缓冲区大小：%d, 已经使用：%d, 剩余：%d:\n", READ_BUFFER_SIZE, m_read_idx, READ_BUFFER_SIZE - m_read_idx);
        }
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            if (DEBUG==1)
            {
                printf("读错误:\n");
            }
            return false;
        }
        else if (bytes_read == 0)
        {
            if (DEBUG==1)
            {
                printf("读0,对方已经关闭连接:\n");
            }
            return false;
        }
        m_read_idx += bytes_read;
    }
    if (DEBUG==1)
    {
        printf("%s\n", m_read_buf);
    }
    return true;
}

/*
    解析HTTP请求行，获得请求方法、目标URL，以及HTTP版本号
    返回值：
        成功：NO_REQUEST
        失败：BAD_REQUEST
*/
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    /*
        格式：
            Method     Url     HTTP_version
            GET /index HTTP/1.1
    */
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char *method = text;
    
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        if(DEBUG==1){
            printf("method = POST\n");
        }
        m_method = POST;
    }
    else
    {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    m_chek_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/*
    传入text，解析text所表示的header代表的信息并记录在变量中
    返回值：
        如果存在content_lenth > 0，则返回NO_REQUEST
            否则返回 GET_REQUEST
        如果是其它请求头，返回NO_REQUES
*/
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0 || m_method == http_conn::POST)
        {
            m_chek_state = CHECK_STATE_CONTETE;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }

    return NO_REQUEST;
}

/*
    解析text传入的post的body数据
*/
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_check_idx))
    {
        text[m_content_length] = '\0';
        // 如果是GET请求则直接忽略
        if (m_method == http_conn::GET){
            if(DEBUG==1){
                printf("!!!!! return GET_REQUEST\n");
            }
            return GET_REQUEST;
        }
        else if (m_method == http_conn::POST)
        {
            if(DEBUG==1){
                printf("!!!!! return POST_REQUEST\n");
            }
            // 记录POST请求
            m_content_data = text;
            return POST_REQUEST;
        }
    }
    return NO_REQUEST;
}

/*
    根据状态机依次进行参数的解析
    根据最终状态指向请求
    返回值：
        解析成功返回NO_REQUEST
        解析失败返回BAD_REQUEST
*/
http_conn::HTTP_CODE http_conn::process_read()
{
    if (DEBUG==1)
    {
        printf("开始解析请求:\n");
    }
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    if (DEBUG==1)
    {
        printf("%d, %d\n", m_chek_state == CHECK_STATE_CONTETE, line_status == LINE_OK);
    }
    while (((m_chek_state == CHECK_STATE_CONTETE) && (line_status == LINE_OK)) ||
           ((line_status = parse_line()) == LINE_OK))
    {
        if (DEBUG==1)
        {
            printf("执行解析:\n");
        }
        text = get_line();
        m_start_line = m_check_idx;
        if (DEBUG==1)
        {
            printf("get 1 http line : %s\n", text);
        }
        switch (m_chek_state)
        {
        // 状态1：检查请求行
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
            {
                LOG_INFO("[%ld BAD_STATE_REQUESTLINE %s]", pthread_self(), m_url);
                return BAD_REQUEST;
            }
            break;
        }
        // 状态2：检查请求头
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
            {
                LOG_INFO("[%ld BAD_STATE_HEADER %s]", pthread_self(), m_url);
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {
                if(DEBUG == 2)
                    printf("GET %s\n", m_url);
                LOG_INFO("[%ld GET %s]", pthread_self(), m_url);
                return do_request();
            }
            break;
        }
        // 状态3：检查最终状态 执行对应的请求
        case CHECK_STATE_CONTETE:
        {
            ret = parse_content(text);
            if(DEBUG==1){
                printf("CHECK_STATE_CONTETE = %d!\n", ret);
            }
            if (ret == GET_REQUEST)
            {
                if(DEBUG==1){
                    printf("STATE = GET!\n");
                }
                if(DEBUG == 2){
                    printf("GET : %s\n", m_url);
                }
                LOG_INFO("[%ld GET %s]", pthread_self(), m_url);
                return do_request();
            }
            else if (ret == POST_REQUEST)
            {
                if(DEBUG==1){
                    printf("STATE = POST!\n");
                }
                if(DEBUG == 2){
                    printf("POST : %s\n", m_url);
                }
                LOG_INFO("[%ld POST %s]", pthread_self(), m_url);
                return do_cgi_request();
            }
            line_status = LINE_OPEN;
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

/*
    查找cgi文件是否存在，并执行
*/
http_conn::HTTP_CODE http_conn::do_cgi_request()
{
    if(DEBUG==1){
        printf("《==== POST请求处理 ====》\n");
    }
    // 确定cgi文件路径
    strcpy(m_real_file, cgi_root);
    int len = strlen(cgi_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (DEBUG==1)
    {
        printf("CGI路径: %s\n", m_real_file);
    }

    // 资源不存在
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    // 禁止读
    if (!(m_file_stat.st_mode & S_IROTH))
    { // S_IROTH 其它读
        return FORBIDDEN_REQUEST;
    }
    // 如果是路径
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    // 读取文件 映射到内存空间
    if (DEBUG==1)
    {
        printf("find cgi successful!, post data = %s\n", m_content_data);
    }
    return execute_cgi();
}

/*
    已确定cgi文件存在，现在执行cgi程序
*/
http_conn::HTTP_CODE http_conn::execute_cgi()
{
    /*
        本函数实现思路如下：
            1、在当前子线程中，fork一个子进程，并使用管道连接
            2、关闭子进程中不必要的连接，并将管道读写文件描述符重定向为stdin和stdout
            3、子进程执行execl，切换为cgi程序开始执行
            4、cgi程序从stdin读取数据，从stdout返回数据
            5、cgi程序执行完毕，应当回收子进程并且关闭管道释放资源
    */

    // 01 pipe与fork
    int cgi_out[2]; // cgi程序读取管道
    int cgi_in[2];  // cgi程序输入管道
    pid_t pid; // 进程号

    // 创建管道
    if(pipe(cgi_in) < 0){
        return INTERNAL_ERROR; // 服务器内部错误
    }
    if(pipe(cgi_out) < 0){
        return INTERNAL_ERROR; // 服务器内部错误
    }
    if(DEBUG==1){
        printf("thread: %ld, call: execute_cgi, msg: pipe create successful!\n", pthread_self());
    }
    if((pid = fork()) == 0){/* 该部分为子进程 */
        close(cgi_out[0]); // pipe为单向通信
        close(cgi_in[1]);

        dup2(cgi_out[1], 1); // dup2 stdin
        dup2(cgi_in[0], 0); // dup2 stdout  
        
        // 通过环境变量设置Content-Length传递
        char content_env[30];
        sprintf(content_env, "CONTENT_LENGTH=%d", m_content_length);
        putenv(content_env);
        execl(m_real_file, m_real_file, NULL);

        if(DEBUG==1){
            printf("thread: %ld, call: execute_cgi, msg: execute cgi failed!\n", pthread_self());
        }
        exit(1); // 执行成功时不应该到这

    }else{/* 该部分为父进程 */
        close(cgi_out[1]);
        close(cgi_in[0]);

        // 向cgi发送content内容，因为pipe是带缓存的，所以可以直接写入
        if(DEBUG==1){
            printf("thread: %ld, call: execute_cgi, msg: main process close pipe!\n", pthread_self());
            printf("write data : %ld :  %s!\n", strlen(m_content_data), m_content_data);
        }
        int ret = writePipe(cgi_in[1], m_content_data, strlen(m_content_data));
        if(DEBUG==1){
            if(ret < 0){
                printf("error : %s\n", strerror(errno));
            }
            //printf("thread: %ld, call: execute_cgi, msg: write data %d bytes!\n", pthread_self(), ret);
        }
        while (readPipe(cgi_out[0], m_cgi_buf + strlen(m_cgi_buf), WRITE_BUFFER_SIZE - strlen(m_cgi_buf) - 1) > 0)
        {
            // 不断读取管道中数据并存入缓存区中
            if(DEBUG==1){
                printf("thread: %ld, call: execute_cgi, msg: recv cgi data : %s", pthread_self(), m_cgi_buf);
            }
        }
        close(cgi_in[1]);
        close(cgi_out[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        if(status > 0){
            return INTERNAL_ERROR;
        }
    }   
    if(DEBUG==1){
        printf("cgi exec successful!\n");
    }
    return CGI_REQUEST;
}

/*
    目前仅支持GET请求，解析URL，判断其是否可获取
    读取静态文件，将其通过内存映射从内核态到用户态，减少IO操作次数
    返回值：
        FILE_REQUEST: 可获取（数据已加载到内存中)
        BAD_REQUEST： 不可获取
*/
http_conn::HTTP_CODE http_conn::do_request()
{
    if(DEBUG==1){
        printf("《==== GET请求处理 ====》\n");
    }
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (DEBUG==1)
    {
        printf("路径: %s\n", m_real_file);
    }

    // 资源不存在 m_real_file
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    // 禁止读
    if (!(m_file_stat.st_mode & S_IROTH))
    { // S_IROTH 其它读
        return FORBIDDEN_REQUEST;
    }
    // 如果是路径
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    // 读取文件 映射到内存空间
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ,
                                  MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/*
    释放内存中的文件映射
*/
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/*
    将内存中的数据写入请求方
*/
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= bytes_have_send)
        {
            unmap();
            if (m_linger)
            {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else
            {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

/*
    将formt格式数据写入响应中
*/
bool http_conn::add_response(const char *formt, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, formt);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, formt, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

/*
    将状态行写入到响应的请求行中
*/
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/*
    将请求头写入到响应中
*/
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
    return true;
}



/*
    写入Content-Length大小
*/
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

/*
    写入Connection状态
*/
bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

/*
    写入空行
*/
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

/*
    追加内容 Content内容
*/
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

/*
    采用状态机进行数据的回复
*/
bool http_conn::process_wirte(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    { // 服务器内部错误，返回500状态码 和 详细信息
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
        {
            return false;
        }
        break;
    }

    case BAD_REQUEST:
    { // 请求错误，返回400状态码
        add_status_line(400, err_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
        {
            return false;
        }
        break;
    }

    case NO_RESOURCE:
    { // 请求资源不存在 返回404状态码
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    }

    case FORBIDDEN_REQUEST:
    { // 权限不允许 返回403状态码
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
        {
            return false;
        }
        break;
    }

    case FILE_REQUEST:
    { // 获取到了文件
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
            {
                return false;
            }
        }
        break;
    }
    
    case CGI_REQUEST:
    {// 获取了cgi
        add_status_line(200, ok_200_title);
        add_headers(strlen(m_cgi_buf));
        add_response(m_cgi_buf);
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_idx;
        m_iv_count = 1;
        return true;
    }

    default:
        return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return false;
}

/*
    处理线程：读 + 写
*/
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_wirte(read_ret);
    if (!read_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

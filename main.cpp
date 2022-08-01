#include "http_conn.h"
#include "threadpool.h"
#include "log.h"

const int thread_num = 8; // 线程池线程数目
const short port = 9000; // 网络端口号
const int MAX_FD = 65536; // 最大文件符数量
const int MAX_EVENT_NUMBER = 10000; // 最大并发事件处理数


// extern int addFd(int epollfd, int fd, bool one_shot);
// extern int removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

/*
    向connfd发送info信息并关闭连接
*/
void show_error(int connfd, const char* info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main()
{
    chdir("./");
    // 创建线程池
    addsig(SIGPIPE, SIG_IGN);

    threadpool<http_conn>* pool = NULL;
    try{
        pool = new threadpool<http_conn>(thread_num);
    }catch(...)
    {
        return 1;
    }

    // 启用日志
    Log::init(".", "log_test", 0, 10000);

    http_conn* users = new http_conn[MAX_FD];
    if(users == NULL){
        LOG_ERROR("malloc %d http_conn memory failed!\n", MAX_FD);
        exit(1);
    }
    int user_count = 0; // 用户计数

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd < 0){
        LOG_ERROR("create socket() failed!\n");
        exit(1);
    }

    // 地址复用
    struct linger tmp = {1, 0};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    ret = bind(listenfd, (sockaddr*)&address, sizeof(address));
    if(ret < 0){
        LOG_ERROR("create bind() failed!\n");
        exit(1);
    }

    ret = listen(listenfd, 5);
    if(ret < 0){
        LOG_ERROR("call listen() failed!\n");
        exit(1);
    }

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != 1);
    addFd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR){
            printf("epoll failed!\n");
            break;
        }

        for(int i = 0; i < number; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (sockaddr*)&client_address, &client_addrlength);
                if(connfd < 0){
                    printf("errno is %d\n", errno);
                    LOG_WARNING(" main.cpp line 112 accept() failed!")
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD){
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, client_address);
            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                users[sockfd].close_conn();
            }else if(events[i].events & EPOLLIN)
            {
                if(users[sockfd].read())
                {
                    pool->append(users + sockfd);
                }else{
                    users[sockfd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT)
            {
                if(!users[sockfd].write())
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
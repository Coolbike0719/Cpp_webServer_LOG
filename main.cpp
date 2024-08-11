#include "requestData.h"
#include "epoll.h"
#include "threadpool.h"
#include "util.h"
#include "log.h"
#include <sys/epoll.h>
#include <queue>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <memory>

using namespace std;

static const int MAXEVENTS = 5000;
static const int LISTENQ = 1024;
const int THREADPOOL_THREAD_NUM = 4; // 线程池核心线程数为4
const int QUEUE_SIZE = 65535; // 线程池最大线程数

const int ASK_STATIC_FILE = 1;
const int ASK_IMAGE_STITCH = 2; // 暂未使用

const string PATH = "/";

const int TIMER_TIME_OUT = 500; // 定时器延时500毫秒


void acceptConnection(int listen_fd, int epoll_fd, const string &path);

extern std::priority_queue<shared_ptr<mytimer>, std::deque<shared_ptr<mytimer>>, timerCmp> myTimerQueue;

// 封装一下创建、绑定、监听，返回配置完的监听描述符listen_fd
int socket_bind_listen(int port)
{
    // 检查port值，取正确区间范围
    if (port < 1024 || port > 65535)
        return -1;

    // 创建socket(IPv4 + TCP)，返回监听描述符
    int listen_fd = 0;
    if((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        return -1;

    // 设置端口复用，消除bind时"Address already in use"错误
    int optval = 1;
    if(setsockopt(listen_fd, SOL_SOCKET,  SO_REUSEADDR, &optval, sizeof(optval)) == -1)
        return -1;

    // 设置服务器IP和Port，和监听描述符绑定
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); //自动分配IP地址
    server_addr.sin_port = htons((unsigned short)port);
    if(bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        return -1;

    // 开始监听，最大等待队列长为LISTENQ
    if(listen(listen_fd, LISTENQ) == -1)
        return -1;

    // 无效监听描述符
    if(listen_fd == -1)
    {
        close(listen_fd);
        return -1;
    }
    return listen_fd;
}

// 检查定时器队列，将已删除或超时的定时器弹出释放
void handle_expired_event()
{
    MutexLockGuard lock; //创建并加锁，开始操作定时器优先队列
    while (!myTimerQueue.empty()) //队列不为空
    {
        shared_ptr<mytimer> ptimer_now = myTimerQueue.top(); 
        if (ptimer_now->isDeleted()) //如果队列顶的定时器已经被删除，则弹出并释放
        {
            myTimerQueue.pop();
            //delete ptimer_now; 当前函数结束后栈上的锁也会自动析构
        }
        else if (ptimer_now->isvalid() == false) //如果队列顶的定时器已经超时，则弹出并释放
        {
            myTimerQueue.pop();
            //delete ptimer_now;
        }
        else //最近的定时器都未超时，直接跳出结束
        {
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    // 命令行参数获取 端口 和 server提供的目录
    if (argc < 3) 
    {
    	printf("./server port path\n");	
    }
    // 获取用户输入的端口 
    int port = atoi(argv[1]);
    // 改变进程工作目录，决定服务器要建在哪个目录下
    int ret = chdir(argv[2]);
    if (ret != 0) {
    	perror("chdir error");	
    	exit(1);
    }
    handle_for_sigpipe(); //忽略SIGPIPE信号，防止任意浏览器断开导致服务器进程退出，在util.cpp中
    if (Epoll::epoll_init(MAXEVENTS, LISTENQ) < 0) //创建epoll句柄（内核事件表）并初始化epoll
    {
        perror("epoll init failed");
        return 1;
    }
    // 创建一个初始线程池
    if (ThreadPool::threadpool_create(THREADPOOL_THREAD_NUM, QUEUE_SIZE) < 0) //创建出错会返回-1
    {
        printf("Threadpool create failed\n");
        return 1;
    }
    // socket()、bind()、listen()
    int listen_fd = socket_bind_listen(port);
    if (listen_fd < 0) 
    {
        perror("socket bind failed");
        return 1;
    }
    // 将lfd设为非阻塞，实现非阻塞I/O，即使已连接队列为空accept也不会阻塞
    if (setSocketNonBlocking(listen_fd) < 0)
    {
        perror("set socket non block failed");
        return 1;
    }
    shared_ptr<requestData> request(new requestData()); //用智能指针接收一个实例化请求对象
    request->setFd(listen_fd);//将该对象绑定lfd
    if (Epoll::epoll_add(listen_fd, request, EPOLLIN | EPOLLET) < 0) //监听事件上树
    {
        perror("epoll add error");
        return 1;
    }
    // 服务器启动日志
    LOG_INFO(LoggerMgr::GetInstance()->getLogger("SERVER")) << "Server started ! port:"<<argv[1]<<" path:"<<argv[2];
    // 主线程开始循环监控
    while (true)
    {
        Epoll::my_epoll_wait(listen_fd, MAXEVENTS, -1); // 封装了epoll_wait，多了打印异常信息
        handle_expired_event(); // 主线程每次还检查下定时器队列
    }
    return 0;
}
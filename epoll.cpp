#include "epoll.h"
#include "threadpool.h"
#include "util.h"
#include "log.h"
#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <queue>
#include <deque>

int TIMER_TIME_OUT = 500;
extern std::priority_queue<std::shared_ptr<mytimer>, std::deque<std::shared_ptr<mytimer>>, timerCmp> myTimerQueue;

epoll_event* Epoll::events; //定义全局动态数组，记录每次轮询的活跃事件
std::unordered_map<int, std::shared_ptr<requestData>> Epoll::fd2req; //哈希映射树上的结点，key=fd，value=请求对象
int Epoll::epoll_fd = 0;
const std::string Epoll::PATH = "/";

// 创建epoll句柄（内核事件表）并初始化
int Epoll::epoll_init(int maxevents, int listen_num)
{
    epoll_fd = epoll_create(listen_num + 1); // 创建一个epoll树
    if(epoll_fd == -1)
        return -1;

    //初始化事件数组，maxevents为最大关注socketfd数量
    events = new epoll_event[maxevents];
    return 0;
}

// 注册新描述符
// 参数：fd 要上树的fd， request 请求对象ev， events要监控的事件
int Epoll::epoll_add(int fd, std::shared_ptr<requestData> request, __uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        perror("epoll_add error");
        return -1;
    }
    fd2req[fd] = request; //记录该请求事件
    return 0;
}

// 修改描述符状态，一般用于重置长连接
int Epoll::epoll_mod(int fd, std::shared_ptr<requestData> request, __uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) < 0)
    {
        perror("epoll_mod error");
        return -1;
    }
    fd2req[fd] = request;
    return 0;
}

// 从epoll中删除描述符
int Epoll::epoll_del(int fd, __uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &event) < 0) //将事件下树
    {
        perror("epoll_del error");
        return -1;
    }
    auto fd_ite = fd2req.find(fd);
    if (fd_ite != fd2req.end()) //在请求事件对象umap里找到了该文件描述符，删掉其对象
        fd2req.erase(fd_ite);
    return 0;
}

// 封装了下epoll_wait()，返回活跃事件数，多了打印异常
// 调用代码 int events_num = my_epoll_wait(epoll_fd, MAXEVENTS, -1);
void Epoll::my_epoll_wait(int listen_fd, int max_events, int timeout)
{
    // printf("fd2req.size()==%d\n", fd2req.size());
    int event_count = epoll_wait(epoll_fd, events, max_events, timeout);
    if (event_count < 0)
        perror("epoll wait error");
    std::vector<std::shared_ptr<requestData>> req_data = getEventsRequest(listen_fd, event_count, PATH); //获取本轮活跃事件数组
    if (req_data.size() > 0)
    {
        for (auto &req: req_data) // 遍历活跃事件
        {
            if (ThreadPool::threadpool_add(req) < 0) // 加入到线程池的任务队列中
            {
                // 线程池满了或者关闭了等原因，抛弃本次监听到的请求。
                break;
            }
        }
    }
}
#include <iostream>
#include <arpa/inet.h>
using namespace std;

// 监听事件回调函数，即有新的连接，要accept返回新的cfd并绑定上树，还添加了定时器，path记录连接要访问的路径
void Epoll::acceptConnection(int listen_fd, int epoll_fd, const std::string path)
{
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_in));
    socklen_t client_addr_len = 0;
    int accept_fd = 0;
    while((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0)
    {
        // cout << client_addr.sin_addr.s_addr << endl;
        // cout << client_addr.sin_port << endl;
        // 新连接请求日志
        LOG_INFO(LoggerMgr::GetInstance()->getLogger("SERVER")) << "New connection from IP:"<<client_addr.sin_addr.s_addr<<" PORT:"<<client_addr.sin_port;
        
        // 将cfd设为非阻塞模式
        int ret = setSocketNonBlocking(accept_fd);
        if (ret < 0)
        {
            perror("Set non block failed!");
            return;
        }
        // 把cfd绑定成一个事件对象，用智能指针接收
        std::shared_ptr<requestData> req_info(new requestData(epoll_fd, accept_fd, path));

        /* 文件描述符可以读，边缘触发(Edge Triggered)模式，还加上了EPOLLONESHOT，每个事件触发一次后内核就会
        将该文件描述符从就绪队列中移除，保证一个socketfd在任一时刻只被一个线程处理，如果在处理完时还要继续
        监控该事件，则要重置或者删除重新上树*/
        __uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
        Epoll::epoll_add(accept_fd, req_info, _epo_event);
        // 给新的连接的请求对象添加一个定时器，
        std::shared_ptr<mytimer> mtimer(new mytimer(req_info, TIMER_TIME_OUT));
        req_info->addTimer(mtimer);
        MutexLockGuard lock; //上锁，要把定时器放进优先级队列里
        myTimerQueue.push(mtimer);
    }
    //if(accept_fd == -1)
     //   perror("accept");
}

// 分发处理函数，遍历活跃事件，装进请求对象加入任务池
std::vector<std::shared_ptr<requestData>> Epoll::getEventsRequest(int listen_fd, int events_num, const std::string path)
{
    std::vector<std::shared_ptr<requestData>> req_data; // 存储本轮发生事件的请求数据对象
    for(int i = 0; i < events_num; ++i)
    {
        // 遍历动态数组events获取活跃事件的fd
        int fd = events[i].data.fd;

        // 活跃事件的描述符为监听描述符
        if(fd == listen_fd)
        {
            //cout << "This is listen_fd" << endl;
            acceptConnection(listen_fd, epoll_fd, path);
        }
        else if (fd < 3) //fd应该至少从3开始，012是标准xx文件
        {
            break;
        }
        else
        {
            // 先排除错误事件
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)
                || (!(events[i].events & EPOLLIN)))
            {
                //printf("error event\n");
                auto fd_ite = fd2req.find(fd);
                if (fd_ite != fd2req.end()) // 如果错误事件的fd被记录了，删去
                    fd2req.erase(fd_ite);
                //printf("fd = %d, here\n", fd);
                continue;
            }

            // 到这是说明是读数据请求，将请求任务加入到线程池中
            std::shared_ptr<requestData> cur_req(fd2req[fd]); // 创建一个智能指针cur_req临时接收下该活跃事件
            //printf("cur_req.use_count=%d\n", cur_req.use_count());
            cur_req->seperateTimer();// 加入线程池tp之前将Timer和request分离，因为任务成功被接管了，不用定时器了
            req_data.push_back(cur_req); //放进本轮活跃事件数组里
            auto fd_ite = fd2req.find(fd); //把fd2req中的清除
            if (fd_ite != fd2req.end())
                fd2req.erase(fd_ite);
        }
    }
    return req_data;
}
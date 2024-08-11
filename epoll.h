#pragma once

#include "requestData.h"
#include <vector>
#include <unordered_map>
#include <sys/epoll.h>
#include <memory>

// 定义一个Epoll类，封装epoll相关函数
class Epoll
{
private:
    static epoll_event *events;
    static std::unordered_map<int, std::shared_ptr<requestData>> fd2req;
    /* fd2req是一个哈希映射，将文件描述符映射到对应的requestData对象的智能指针。
       这样可以在epoll事件发生时快速找到对应的请求数据对象 */
    static int epoll_fd;
    static const std::string PATH;
public:
    static int epoll_init(int maxevents, int listen_num);
    static int epoll_add(int fd, std::shared_ptr<requestData> request, __uint32_t events);
    static int epoll_mod(int fd, std::shared_ptr<requestData> request, __uint32_t events);
    static int epoll_del(int fd, __uint32_t events);
    static void my_epoll_wait(int listen_fd, int max_events, int timeout);
    static void acceptConnection(int listen_fd, int epoll_fd, const std::string path);
    static std::vector<std::shared_ptr<requestData>> getEventsRequest(int listen_fd, int events_num, const std::string path);
};
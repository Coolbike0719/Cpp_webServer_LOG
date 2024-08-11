#include "util.h"
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

// 从fd中读取指定长度n的数据到buff中
ssize_t readn(int fd, void *buff, size_t n)
{
    size_t nleft = n;     // nleft为当前剩余字节数
    ssize_t nread = 0;    // 单次读取的字节数
    ssize_t readSum = 0;  // 当前已经读取的字节数
    char *ptr = (char*)buff;
    while (nleft > 0) // 循环直至读完n个
    {
        if ((nread = read(fd, ptr, nleft)) < 0)
        {
            if (errno == EINTR)
                nread = 0;
            else if (errno == EAGAIN)
            {
                return readSum;
            }
            else
            {
                return -1;
            }  
        }
        else if (nread == 0) // 本次读取字节数=0，说明读完了
            break;
        readSum += nread;
        nleft -= nread;
        ptr += nread;
    }
    return readSum;
}

// 同样非阻塞循环写入
ssize_t writen(int fd, void *buff, size_t n)
{
    size_t nleft = n;
    ssize_t nwritten = 0;
    ssize_t writeSum = 0;
    char *ptr = (char*)buff;
    while (nleft > 0)
    {
        if ((nwritten = write(fd, ptr, nleft)) <= 0)
        {
            if (nwritten < 0)
            {
                if (errno == EINTR || errno == EAGAIN)
                {
                    nwritten = 0;
                    continue;
                }
                else
                    return -1;
            }
        }
        writeSum += nwritten;
        nleft -= nwritten;
        ptr += nwritten;
    }
    return writeSum;
}

// 忽略SIGPIPE信号，防止进程退出
void handle_for_sigpipe()
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if(sigaction(SIGPIPE, &sa, NULL))
        return;
}

/* 将fd设为非阻塞，cfd设成非阻塞可实现ET模式，lfd设为非阻塞可实现非阻塞I/O，进而结合多路复用I/O实现异步I/O。
使用IO多路复用API epoll时，如果lfd阻塞，那么accept()在当前无新连接时就会阻塞，从而整个主线程不能执行下去*/
int setSocketNonBlocking(int fd)
{
    int flag = fcntl(fd, F_GETFL, 0);
    if(flag == -1)
        return -1;

    flag |= O_NONBLOCK;
    if(fcntl(fd, F_SETFL, flag) == -1)
        return -1;
    return 0;
}
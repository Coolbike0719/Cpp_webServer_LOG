#pragma once

#include "requestData.h"
#include "log.h"
#include <pthread.h>
#include <functional>
#include <memory>
#include <vector>


const int THREADPOOL_INVALID = -1;
const int THREADPOOL_LOCK_FAILURE = -2;
const int THREADPOOL_QUEUE_FULL = -3;
const int THREADPOOL_SHUTDOWN = -4;
const int THREADPOOL_THREAD_FAILURE = -5;
const int THREADPOOL_GRACEFUL = 1;

const int MAX_THREADS = 1024;
const int MAX_QUEUE = 65535;

typedef enum 
{
    immediate_shutdown = 1,//立即关闭线程池
    graceful_shutdown  = 2 //等线程池中的任务全部处理完成后，再关闭线程池
} threadpool_shutdown_t;//关闭线程池的方式

// 任务结构体
struct ThreadPoolTask
{
    std::function<void(std::shared_ptr<void>)> fun; //任务的回调函数
    std::shared_ptr<void> args; //回调函数的参数
};

void myHandler(std::shared_ptr<void> req);

//定义线程池类
class ThreadPool
{
private:
    static pthread_mutex_t lock;                //用于内部工作的互斥锁
    static pthread_cond_t notify;               //线程间通知的条件变量
    static std::vector<pthread_t> threads;      //线程数组，存放线程id
    static std::vector<ThreadPoolTask> queue;   //任务队列(数组)，队列中的任务都是未开始运行的
    static int thread_count;                    //线程池里的线程数量
    static int queue_size;                      //任务队列容量
    static int head;                            //任务队列中队首，指向首个任务位置
    static int tail;                            //任务队列中队尾，最后一个任务的下一个位置
    static int count;                           //任务队列里的当前任务数量，即等待运行的任务数
    static int shutdown;                        //表示线程池是否关闭，0表示可用，1或其他值表示处于关闭状态
    static int started;                         //正在运行的线程数
public:
    static int threadpool_create(int _thread_count, int _queue_size);
    static int threadpool_add(std::shared_ptr<void> args, std::function<void(std::shared_ptr<void>)> fun = myHandler);
    static int threadpool_destroy();
    static int threadpool_free();
    static void *threadpool_thread(void *args);
};

#include "threadpool.h"


pthread_mutex_t ThreadPool::lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ThreadPool::notify = PTHREAD_COND_INITIALIZER;
std::vector<pthread_t> ThreadPool::threads;
std::vector<ThreadPoolTask> ThreadPool::queue;
int ThreadPool::thread_count = 0;
int ThreadPool::queue_size = 0;
int ThreadPool::head = 0;
int ThreadPool::tail = 0;
int ThreadPool::count = 0;
int ThreadPool::shutdown = 0;
int ThreadPool::started = 0;

// 创建初始线程池，thread_count 核心线程数，queue_size队列容量，
int ThreadPool::threadpool_create(int _thread_count, int _queue_size)
{
    bool err = false;
    do
    {
        //如果传入的参数不合法，改成合法的
        if(_thread_count <= 0 || _thread_count > MAX_THREADS || _queue_size <= 0 || _queue_size > MAX_QUEUE) 
        {
            _thread_count = 4;
            _queue_size = 1024;
        }
    
        thread_count = 0;           //初始线程数为0
        queue_size = _queue_size;   //任务队列最大容量
        head = tail = count = 0;    //队列游标和当前任务数都初始化成0
        shutdown = started = 0;     //关闭标识和正在运行的线程数也都初始化成0

        threads.resize(_thread_count);
        queue.resize(_queue_size);
    
        /* Start worker threads */
        /* 创建指定数量的线程开始运行 */
        for(int i = 0; i < _thread_count; ++i) 
        {
            if(pthread_create(&threads[i], NULL, threadpool_thread, (void*)(0)) != 0) 
            {
                return -1;
            }
            ++thread_count;
            ++started;
        }
    } while(false);
    
    if (err) 
    {
        return -1;
    }
    return 0;
}

// 任务的回调函数，参数args通常为请求对象，实际是传入该请求对象，然后执行该请求对象的处理函数
void myHandler(std::shared_ptr<void> req)
{
    //用static_pointer_cast把shared_ptr<void>型的req转换成shared_ptr<requestData>型，并用request接收
    std::shared_ptr<requestData> request = std::static_pointer_cast<requestData>(req);
    request->handleRequest();
}

// 往任务池里添加任务。第一个为回调函数的参数，第二个参数为任务的回调函数
int ThreadPool::threadpool_add(std::shared_ptr<void> args, std::function<void(std::shared_ptr<void>)> fun)
{
    int next, err = 0;
    /* 获取线程池的互斥锁，要往里写东西了 */
    if(pthread_mutex_lock(&lock) != 0)
        return THREADPOOL_LOCK_FAILURE;
    do 
    {
        next = (tail + 1) % queue_size; //获取任务队列的队尾游标并+1
        // 任务队列是否已满
        if(count == queue_size) 
        {
            err = THREADPOOL_QUEUE_FULL;
            break;
        }
        // 线程池是否处于关闭状态
        if(shutdown)
        {
            err = THREADPOOL_SHUTDOWN;
            break;
        }
        /* 都没问题则将任务添加到任务队列里*/
        queue[tail].fun = fun;
        queue[tail].args = args;
        tail = next;    //更新任务队列的队尾游标
        ++count;        //任务队列的任务数+1
        
        /* 唤醒一个阻塞在条件变量上的线程，通知它有新的任务可用 */
        if(pthread_cond_signal(&notify) != 0) 
        {
            err = THREADPOOL_LOCK_FAILURE;
            break;
        }
    } while(false);

    // 任务添加完了释放线程池的互斥锁
    if(pthread_mutex_unlock(&lock) != 0)
        err = THREADPOOL_LOCK_FAILURE;
    return err;
}

// 子线程执行的函数
void *ThreadPool::threadpool_thread(void *args)
{
    while (true) //子线程循环接收任务并执行
    {
        ThreadPoolTask task; //创建一个任务
        pthread_mutex_lock(&lock); // 尝试获取互斥锁
        /* 阻塞判断任务池是否有任务，条件变量是否满足，互斥锁是否能获取*/
        while((count == 0) && (!shutdown)) 
        {
            pthread_cond_wait(&notify, &lock);
        }
        // 如果线程池要关闭(立即关闭，或温和关闭且任务池空)，则跳出，关闭子线程
        if((shutdown == immediate_shutdown) ||
           ((shutdown == graceful_shutdown) && (count == 0)))
        {
            break;
        }
        /* 获取任务队列的第一个任务 */
        task.fun = queue[head].fun;
        task.args = queue[head].args;
        queue[head].fun = NULL;     // 函数指针置空
        queue[head].args.reset();   // 智能指针置空
        head = (head + 1) % queue_size; // 任务队列队首指针后移
        --count;    //任务队列的任务数减1
        pthread_mutex_unlock(&lock);
        (task.fun)(task.args); // 执行传入参数args的任务函数fun
    }

    --started;  //线程池的正在运行的进程数减1，该子线程结束循环工作了

    pthread_mutex_unlock(&lock);
    pthread_exit(NULL);
    return(NULL);
}
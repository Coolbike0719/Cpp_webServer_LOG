#include "requestData.h"
#include "util.h"
#include "epoll.h"
#include "log.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/time.h>
#include <unordered_map>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <queue>
#include <cstdlib>
//#include <opencv/cv.h> 已弃用
#include <opencv2/imgproc.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
using namespace cv;


#include <iostream>
using namespace std;

pthread_mutex_t MutexLockGuard::lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MimeType::lock = PTHREAD_MUTEX_INITIALIZER;
std::unordered_map<std::string, std::string> MimeType::mime;

// 接收一个字符串，返回一个文件类型
std::string MimeType::getMime(const std::string &suffix)
{
    if (mime.size() == 0) // 懒汉式双重检查锁
    {
        pthread_mutex_lock(&lock);
        if (mime.size() == 0)
        {
            mime[".html"] = "text/html";
            mime[".avi"] = "video/x-msvideo";
            mime[".bmp"] = "image/bmp";
            mime[".c"] = "text/plain";
            mime[".doc"] = "application/msword";
            mime[".gif"] = "image/gif";
            mime[".gz"] = "application/x-gzip";
            mime[".htm"] = "text/html";
            mime[".ico"] = "application/x-ico";
            mime[".jpg"] = "image/jpeg";
            mime[".png"] = "image/png";
            mime[".txt"] = "text/plain";
            mime[".mp3"] = "audio/mp3";
            mime["default"] = "text/html";
        }
        pthread_mutex_unlock(&lock);
    }
    if (mime.find(suffix) == mime.end()) // 没找到对应的文件类型
        return mime["default"];
    else
        return mime[suffix];
}

// 定义一个装定时器的优先级队列，数据类型为mytimer的智能指针，用deque实现，顺序规则为timerCmp，小顶堆
// 属于临界资源，使用时要加锁
std::priority_queue<shared_ptr<mytimer>, std::deque<shared_ptr<mytimer>>, timerCmp> myTimerQueue;

// 请求对象的构造函数，当有事件请求时会自动调用初始化一个实例对象
requestData::requestData(): 
    now_read_pos(0), 
    state(STATE_PARSE_URI), 
    h_state(h_start), 
    keep_alive(false), 
    againTimes(0)
{
    cout << "requestData()" << endl;
}
requestData::requestData(int _epollfd, int _fd, std::string _path):
    now_read_pos(0), 
    state(STATE_PARSE_URI), 
    h_state(h_start), 
    keep_alive(false), 
    againTimes(0), 
    path(_path), 
    fd(_fd), 
    epollfd(_epollfd)
{
    cout << "requestData()" << endl;
}

// 请求对象的析构函数，异常或超时会导致delete
requestData::~requestData()
{
    cout << "~requestData()" << endl;
    //智能指针接收的对象，自动销毁，关闭fd即可
    close(fd);
}

void requestData::addTimer(shared_ptr<mytimer> mtimer)
{
    timer = mtimer;
}
int requestData::getFd()
{
    return fd;
}
void requestData::setFd(int _fd)
{
    fd = _fd;
}

/*将对象内容清空，一般用于长连接对象，本次通信完成不删除只清空，然后重新上树监控，
  但除非在时限内又发送请求且是长连接，否则清空会变成默认的短连接*/
void requestData::reset()
{
    againTimes = 0;
    content.clear();
    file_name.clear();
    path.clear();
    now_read_pos = 0;
    state = STATE_PARSE_URI;
    h_state = h_start;
    headers.clear();
    keep_alive = false;
    if (timer.lock()) // 若还绑有定时器也清空解绑
    {
        shared_ptr<mytimer> my_timer(timer.lock());
        my_timer->clearReq();
        timer.reset();
    }
}

// 对象要进任务池了，将定时器分离，不再绑定该对象
void requestData::seperateTimer()
{
    if (auto my_timer = timer.lock()) // 如果mytimer还未被释放，则创建一个指向它的shared_ptr<mytimer>
    {
        //shared_ptr<mytimer> my_timer(timer.lock()); // timer是weakptr，要先检查是否存在
        my_timer->clearReq(); // 清除绑定
        timer.reset(); // timer置空
    }
}

// 请求对象的处理函数
void requestData::handleRequest()
{

    char buff[MAX_BUFF];
    bool isError = false;
    while (true)
    {
        int read_num = readn(fd, buff, MAX_BUFF); //fd为记录在请求对象里的cfd，readn是非阻塞循环读完
        if (read_num < 0)
        {
            perror("1");
            isError = true;
            break;
        }
        else if (read_num == 0)
        {
            // 有请求出现但是读不到数据，可能是Request Aborted，或者来自网络的数据没有达到等原因
            perror("read_num == 0");
            if (errno == EAGAIN)
            {
                if (againTimes > AGAIN_MAX_TIMES) //如果该对象请求超过200次则放弃该连接，isError记为true
                    isError = true;
                else
                    ++againTimes; //还没到200，先加一次
            }
            else if (errno != 0) //是其他不可容忍的错误，直接记错跳出
                isError = true;
            break;
        }
        string now_read(buff, buff + read_num); //获取当前读到的字符串，将buff到buff + read_num存到nr中
        content += now_read; //累计到该请求对象的请求内容中

        if (state == STATE_PARSE_URI) //当前状态是解析请求的URI
        {
            int flag = this->parse_URI(); //调用对象的解析URI方法
            if (flag == PARSE_URI_AGAIN) //还要继续解析URI，如一次没读完
            {
                break;
            }
            else if (flag == PARSE_URI_ERROR)
            {
                perror("2");
                isError = true;
                break;
            }
        }
        if (state == STATE_PARSE_HEADERS) //当前状态是解析请求的头部
        {
            int flag = this->parse_Headers(); //调用对象的解析头部方法
            if (flag == PARSE_HEADER_AGAIN) //还要继续解析头部，如一次没读完
            {  
                break;
            }
            else if (flag == PARSE_HEADER_ERROR)
            {
                perror("3");
                isError = true;
                break;
            }
            if (method == METHOD_POST)  // 如果是POST请求还要解析请求体
            {
                state = STATE_RECV_BODY;
            }
            else 
            {
                state = STATE_ANALYSIS; //反之是GET就直接进入分析请求
            }
        }
        if (state == STATE_RECV_BODY)  // POST解析请求体
        {
            int content_length = -1;
            if (headers.find("Content-length") != headers.end()) //在headers剩余内容中找到请求体的长度信息
            {
                content_length = stoi(headers["Content-length"]); //Content-length的值是字符串数字
            }
            else //没找到，请求头有问题，因为post请求肯定要有
            {
                isError = true;
                break;
            }
            if (content.size() < content_length) //当前剩余内容比发来的请求体长度小，说明还没读完
                continue;
            state = STATE_ANALYSIS; //进入分析请求状态
        }
        if (state == STATE_ANALYSIS)
        {
            int flag = this->analysisRequest();
            if (flag < 0)
            {
                isError = true;
                break;
            }
            else if (flag == ANALYSIS_SUCCESS)
            {

                state = STATE_FINISH;
                break;
            }
            else
            {
                isError = true;
                break;
            }
        }
    }

    if (isError) //如果上述过程中被标记出错就直接返回
    {
        return;
    }
    // 如果没被标记为出错，即成功完成任务或有可容忍的错误，加入epoll继续监控
    if (state == STATE_FINISH)
    {
        if (keep_alive) //是长连接就只重置对象，继续保持通信
        {
            //printf("ok\n");
            this->reset(); //清除本次通信的内容
        }
        else //短连接直接返回
        {
            return;
        }
    }
    /* 一定要先加时间信息，否则可能会出现刚加进去，下个in触发来了，然后分离失败后，又加入队列，
    最后超时被删，然后正在线程中进行的任务出错，double free错误。*/
    //cout << "shared_from_this().use_count() ==" << shared_from_this().use_count() << endl;
    shared_ptr<mytimer> mtimer(new mytimer(shared_from_this(), 500));
    this->addTimer(mtimer); //更新该对象的定时器
    {
        MutexLockGuard lock; //在一个函数体里创建锁对象，这样结束自动销毁
        myTimerQueue.push(mtimer); //再放进队列里
    }
    // 重置对象上树
    __uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
    int ret = Epoll::epoll_mod(fd, shared_from_this(), _epo_event);
    //cout << "shared_from_this().use_count() ==" << shared_from_this().use_count() << endl;
    if (ret < 0)
    {
        // 返回错误处理
        //delete this;
        return;
    }
}

// 解析请求的URI(请求行)
int requestData::parse_URI()
{
    string &str = content; //获取请求对象的请求内容
    // 读到完整的请求行再开始解析请求
    int pos = str.find('\r', now_read_pos); //从当前位置开始找到\r，刚开始当前位置为0
    if (pos < 0)
    {
        return PARSE_URI_AGAIN;
    }
    
    string request_line = str.substr(0, pos); // 把请求行取出来做解析
    if (str.size() > pos + 1)
        str = str.substr(pos + 1); // str去掉请求行，节省空间
    else 
        str.clear();
    // 获取Method
    pos = request_line.find("GET");
    if (pos < 0)  // 不是GET请求
    {
        pos = request_line.find("POST");
        if (pos < 0) //也不是POST请求，说明URI错误
        {
            return PARSE_URI_ERROR;
        }
        else
        {
            method = METHOD_POST;
        }
    }
    else
    {
        method = METHOD_GET;
    }
    //printf("method = %d\n", method);
    // filename
    pos = request_line.find("/", pos);
    if (pos < 0)  // 没找到/，URI错误
    {
        return PARSE_URI_ERROR;
    }
    else
    {
        int _pos = request_line.find(' ', pos); //从当前位置开始找空格，是URI格式文件名的结尾
        if (_pos < 0)
            return PARSE_URI_ERROR;
        else
        {
            if (_pos - pos > 1)
            {
                file_name = request_line.substr(pos + 1, _pos - pos - 1); //获取这之间的文件名
                int __pos = file_name.find('?'); //如果文件名中有?说明还有查询参数
                if (__pos >= 0)
                {
                    file_name = file_name.substr(0, __pos);
                }
            }
                
            else  //_pos - pos=0,没输入请求的文件名，返回一个预设的页面
                file_name = "index.html";
        }
        pos = _pos;  // 更新当前位置
        // 解析请求日志
        LOG_INFO(LoggerMgr::GetInstance()->getLogger("SERVER")) << "Processing request: "<<file_name;
    }
    //cout << "file_name: " << file_name << endl;
    // 检查 HTTP 版本号
    pos = request_line.find("/", pos);
    if (pos < 0)
    {
        return PARSE_URI_ERROR;
    }
    else
    {
        if (request_line.size() - pos <= 3) //版本号至少3个字节
        {
            return PARSE_URI_ERROR;
        }
        else
        {
            string ver = request_line.substr(pos + 1, 3);
            if (ver == "1.0")
                HTTPversion = HTTP_10;
            else if (ver == "1.1")
                HTTPversion = HTTP_11;
            else
                return PARSE_URI_ERROR;
        }
    }
    state = STATE_PARSE_HEADERS; //URI解析完成，进入头部解析状态
    return PARSE_URI_SUCCESS;
}

// 解析请求的头部
int requestData::parse_Headers()
{
    string &str = content;
    //key_start 键开始, key_end 键结束, value_start 值开始, value_end 值结束
    int key_start = -1, key_end = -1, value_start = -1, value_end = -1;
    int now_read_line_begin = 0; //当前解析位置
    bool notFinish = true; //是否还未解析完
    for (int i = 0; i < str.size() && notFinish; ++i)
    {
        switch(h_state)
        {
            case h_start: //开始解析行首的键
            {
                if (str[i] == '\n' || str[i] == '\r') //这行开始就没了，说明结束了
                    break;
                h_state = h_key; //转移到键
                key_start = i;
                now_read_line_begin = i;
                break;
            }
            case h_key:  // 开始解析键
            {
                if (str[i] == ':')
                {
                    key_end = i;
                    if (key_end - key_start <= 0)
                        return PARSE_HEADER_ERROR;
                    h_state = h_colon;
                }
                else if (str[i] == '\n' || str[i] == '\r')
                    return PARSE_HEADER_ERROR;
                break;  
            }
            case h_colon:  // 开始解析冒号
            {
                if (str[i] == ' ')
                {
                    h_state = h_spaces_after_colon;
                }
                else
                    return PARSE_HEADER_ERROR;
                break;  
            }
            case h_spaces_after_colon:  // 开始解析冒号后的空格
            {
                h_state = h_value;
                value_start = i;
                break;  
            }
            case h_value:  // 开始解析值
            {
                if (str[i] == '\r') //遇到回车符了
                {
                    h_state = h_CR;
                    value_end = i;
                    if (value_end - value_start <= 0)
                        return PARSE_HEADER_ERROR;
                }
                else if (i - value_start > 255)
                    return PARSE_HEADER_ERROR;
                break;  
            }
            case h_CR:  // 开始解析这一行的回车符
            {
                if (str[i] == '\n') //遇到换行符了，说明该行读完，将键值对写入headers
                {
                    h_state = h_LF;
                    string key(str.begin() + key_start, str.begin() + key_end);
                    string value(str.begin() + value_start, str.begin() + value_end);
                    headers[key] = value;
                    now_read_line_begin = i;
                }
                else
                    return PARSE_HEADER_ERROR;
                break;  
            }
            case h_LF:  // 开始解析这一行的换行符
            {
                if (str[i] == '\r') //如果又遇到回车符了，说明下一行可能是空行
                {
                    h_state = h_end_CR; //进入头部结尾的空行判断
                }
                else //当前字符是正常字符，说明是行首重新读
                {
                    key_start = i;
                    h_state = h_key;
                }
                break;
            }
            case h_end_CR:  // 开始解析下一行是不是头部结尾的空行
            {
              if (str[i] == '\n')  // 换行也对了
                {
                    h_state = h_end_LF;
                }
                else
                    return PARSE_HEADER_ERROR;
                break;
            }
            case h_end_LF:  // 头部解析完成状态
            {
                notFinish = false; //头部解析完了
                key_start = i;
                now_read_line_begin = i;
                break;
            }
        }
    }
    if (h_state == h_end_LF) //如果for循环解析完了当前状态是h_end_LF，说明成功
    {
        str = str.substr(now_read_line_begin); //把请求头去掉
        return PARSE_HEADER_SUCCESS;
    }
    str = str.substr(now_read_line_begin); //不是也去掉，说明这次头部没读完
    return PARSE_HEADER_AGAIN;
}

// 分析和处理请求
int requestData::analysisRequest()
{
    if (method == METHOD_POST) // 处理POST请求
    {
        //get content
        char header[MAX_BUFF];
        sprintf(header, "HTTP/1.1 %d %s\r\n", 200, "OK"); //写响应消息的状态行
        if(headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        { //如果有Connection信息且信息是长连接，设置对象为长连接状态并额外写入相关信息
            keep_alive = true;
            sprintf(header, "%sConnection: keep-alive\r\n", header);
            sprintf(header, "%sKeep-Alive: timeout=%d\r\n", header, EPOLL_WAIT_TIME);
        }
        //cout << "content=" << content << endl;
        // test char*
        char *send_content = "I have receiced this.";

        sprintf(header, "%sContent-length: %zu\r\n", header, strlen(send_content));
        sprintf(header, "%s\r\n", header); //响应消息除了消息正文都写完了
        size_t send_len = (size_t)writen(fd, header, strlen(header)); //非阻塞循环写入cfd
        if(send_len != strlen(header))
        {
            perror("Send header failed");
            return ANALYSIS_ERROR;
        }
        
        send_len = (size_t)writen(fd, send_content, strlen(send_content)); //把"I have receiced this."也发过去
        if(send_len != strlen(send_content))
        {
            perror("Send content failed");
            return ANALYSIS_ERROR;
        }
        cout << "content size ==" << content.size() << endl;    //回复对方自己收到的POST请求体大小
        vector<char> data(content.begin(), content.end());
        // 用OpenCV库的imdecode函数将收到的内容解码为位图，并用imwrite函数保存到文件 "receive.bmp" 中
        Mat test = imdecode(data, IMREAD_ANYDEPTH|IMREAD_ANYCOLOR);
        imwrite("receive.bmp", test);
        return ANALYSIS_SUCCESS;
    }
    else if (method == METHOD_GET) // 处理GET请求
    {
        char header[MAX_BUFF];
        sprintf(header, "HTTP/1.1 %d %s\r\n", 200, "OK");   //写响应消息的状态行
        if(headers.find("Connection") != headers.end() && headers["Connection"] == "keep-alive")
        { //如果有Connection信息且信息是长连接，设置对象为长连接状态额外写入相关信息
            keep_alive = true;
            sprintf(header, "%sConnection: keep-alive\r\n", header);
            sprintf(header, "%sKeep-Alive: timeout=%d\r\n", header, EPOLL_WAIT_TIME);
        }
        int dot_pos = file_name.find('.');
        const char* filetype;   //用getMine获取文件类型
        if (dot_pos < 0) 
            filetype = MimeType::getMime("default").c_str();
        else
            filetype = MimeType::getMime(file_name.substr(dot_pos)).c_str();
        struct stat sbuf;
        if (stat(file_name.c_str(), &sbuf) < 0) //获取文件状态sbuf，如果返回值<0则文件未找到
        {
            handleError(fd, 404, "Not Found!");
            return ANALYSIS_ERROR;
        }
        // 返回文件类型
        sprintf(header, "%sContent-type: %s\r\n", header, filetype);
        // 返回文件大小
        sprintf(header, "%sContent-length: %ld\r\n", header, sbuf.st_size);

        sprintf(header, "%s\r\n", header); // 响应消息除了消息正文都写完了
        size_t send_len = (size_t)writen(fd, header, strlen(header)); // 非阻塞循环写入cfd
        if(send_len != strlen(header))
        {
            perror("Send header failed");
            return ANALYSIS_ERROR;
        } // 非消息正文发送完毕

        // 打开文件开始发送消息正文
        int src_fd = open(file_name.c_str(), O_RDONLY, 0);
        // 用mmap将文件映射到内存中。这样做可以将文件内容映射到一块内存区域，避免了频繁的磁盘I/O操作
        char *src_addr = static_cast<char*>(mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0));
        close(src_fd); // 内存映射完毕，不需要文件了
    
        // 发送文件并校验完整性
        send_len = writen(fd, src_addr, sbuf.st_size);
        if(send_len != sbuf.st_size) //发送的字节数不等于该文件的大小
        {
            perror("Send file failed");
            return ANALYSIS_ERROR;
        }
        munmap(src_addr, sbuf.st_size); //解除内存映射关系
        // 响应请求日志
        LOG_INFO(LoggerMgr::GetInstance()->getLogger("SERVER")) << "Response sent: "<<file_name;
        return ANALYSIS_SUCCESS;
    }
    else //其他请求类型返回分析请求错误
        return ANALYSIS_ERROR;
}
// 请求文件没找到时回发错误网页
// 调用代码 handleError(fd, 404, "Not Found!");
void requestData::handleError(int fd, int err_num, string short_msg)
{
    short_msg = " " + short_msg;
    char send_buff[MAX_BUFF];
    string body_buff, header_buff;
    body_buff += "<html><title>TKeed Error</title>";
    body_buff += "<body bgcolor=\"ffffff\">";
    body_buff += to_string(err_num) + short_msg;
    body_buff += "<hr><em> My Web Server</em>\n</body></html>";

    header_buff += "HTTP/1.1 " + to_string(err_num) + short_msg + "\r\n";
    header_buff += "Content-type: text/html\r\n";
    header_buff += "Connection: close\r\n";
    header_buff += "Content-length: " + to_string(body_buff.size()) + "\r\n";
    header_buff += "\r\n";
    sprintf(send_buff, "%s", header_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
    sprintf(send_buff, "%s", body_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
}

// 初始化定时器
mytimer::mytimer(shared_ptr<requestData> _request_data, int timeout): 
    deleted(false), 
    request_data(_request_data)
{
    cout << "mytimer()" << endl;
    struct timeval now;
    gettimeofday(&now, NULL); //获取当前时间
    // 以毫秒计
    expired_time = ((now.tv_sec * 1000) + (now.tv_usec / 1000)) + timeout;
}

// 定时器销毁
mytimer::~mytimer()
{
    cout << "~mytimer()" << endl;
    if (request_data)   //如果请求对象还在，要将其下树
    {
        Epoll::epoll_del(request_data->getFd(), EPOLLIN | EPOLLET | EPOLLONESHOT);
    }
}
// 更新定时器的超时时间，没用到
void mytimer::update(int timeout)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    expired_time = ((now.tv_sec * 1000) + (now.tv_usec / 1000)) + timeout;
}
// 判断定时器是否有效，还未超时，用于handle_expired_event()检查定时器队列
bool mytimer::isvalid()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    size_t temp = ((now.tv_sec * 1000) + (now.tv_usec / 1000));
    if (temp < expired_time)
    {
        return true;
    }
    else
    {
        this->setDeleted();
        return false;
    }
}
// 清除当前定时器的请求对象
void mytimer::clearReq()
{
    request_data.reset();
    this->setDeleted();
}
// 表示当前定时器已被删除，分离或超时都会导致删除
void mytimer::setDeleted()
{
    deleted = true;
}
// 查看当前定时器的删除状态
bool mytimer::isDeleted() const
{
    return deleted;
}
// 获取当前定时器的超时时间
size_t mytimer::getExpTime() const
{
    return expired_time;
}
// 定时器队列比较定时器的超时时间，小顶堆排序
bool timerCmp::operator()(shared_ptr<mytimer> &a, shared_ptr<mytimer> &b) const
{
    return a->getExpTime() > b->getExpTime();
}

MutexLockGuard::MutexLockGuard()
{
    pthread_mutex_lock(&lock);
}
MutexLockGuard::~MutexLockGuard()
{
    pthread_mutex_unlock(&lock);
}

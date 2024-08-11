#pragma once

#include <string>
#include <unordered_map>
#include <memory>


/*
在处理 HTTP 请求时跟踪请求的处理进度：
STATE_PARSE_URI：解析请求的
URI状态。服务器正在解析客户端请求中的URI部分，以确定请求的资源路径
STATE_PARSE_HEADERS：解析请求头状态。服务器正在解析客户端请求中的头部信息
STATE_RECV_BODY：接收请求体状态。服务器正在接收客户端请求的主体部分，例如POST请求的主体内容。
STATE_ANALYSIS：分析和处理请求状态。服务器正在分析客户端请求的内容，并做相应的处理。
STATE_FINISH：请求处理完成状态。服务器已经完成对客户端请求的处理，并准备好返回响应。
*/
const int STATE_PARSE_URI = 1;
const int STATE_PARSE_HEADERS = 2;
const int STATE_RECV_BODY = 3;
const int STATE_ANALYSIS = 4;
const int STATE_FINISH = 5;

const int MAX_BUFF = 4096;

// 有请求出现但是读不到数据,可能是请求终止，或者来自网络的数据没有达到等原因
// 对这样的请求尝试超过一定的次数就断开放弃
const int AGAIN_MAX_TIMES = 200;

// 对于解析请求URI
const int PARSE_URI_AGAIN = -1;   // 需要再次解析 URI，如一次没读完
const int PARSE_URI_ERROR = -2;   // 解析 URI 发生错误
const int PARSE_URI_SUCCESS = 0;  // 解析 URI 成功
// 对于解析请求头
const int PARSE_HEADER_AGAIN = -1;  // 需要再次解析头部，如一次没读完
const int PARSE_HEADER_ERROR = -2;   // 解析头部发生错误
const int PARSE_HEADER_SUCCESS = 0;  // 解析头部成功

const int ANALYSIS_ERROR = -2;   // 分析请求出错
const int ANALYSIS_SUCCESS = 0;  // 分析请求成功

const int METHOD_POST = 1;  // POST请求的标识
const int METHOD_GET = 2;   // GET请求的标识
const int HTTP_10 = 1;      // HTTP/1.0 版本的标识
const int HTTP_11 = 2;      // HTTP/2.0 版本的标识

const int EPOLL_WAIT_TIME =
    500;  // epoll等待事件的最大时间间隔，单位为毫秒，告知对方要在这一时间内保持活跃

// 用于获取文件后缀对应的 MIME
// 类型，禁止外部实例化，只提供getMine方法，都是静态成员，故调用方法也不需要实例化
class MimeType {
 private:
  static pthread_mutex_t lock;
  static std::unordered_map<std::string, std::string>
      mime;  // 存储文件后缀与 MIME 类型的对应关系
  MimeType();
  MimeType(
      const MimeType &
          m);  // 将默认构造和拷贝构造函数都设为private私有化，将禁止外部进行实例化
 public:
  static std::string getMime(
      const std::string &suffix);  // 接收一个字符串，返回文件类型
};

enum HeadersState {
    h_start = 0,           // 当前解析状态，默认从行首开始
    h_key,                 // 正在解析头部中的键
    h_colon,               // 解析到了键值对的冒号
    h_spaces_after_colon,  // 解析到了冒号后的空格
    h_value,               // 正在解析键值对的值
    h_CR,                  // 解析到了回车符
    h_LF,                  // 解析到了换行符
    h_end_CR,              // 解析到了头部结束的回车符
    h_end_LF               // 解析到了头部结束的换行符，此时说明头部解析完成
}; // 定义解析 HTTP 请求头部时每一行的状态

struct mytimer;
class requestData;

// 请求类，封装了用于处理 HTTP请求所需的数据和方法，也就是事件信息ev，最终上树的结点是epv，epv.data.ptr=ev
class requestData : public std::enable_shared_from_this<requestData>
{
private:
    // content的内容边读边清
    std::string content;    // 请求的内容
    int method;             // HTTP 请求的方法（GET、POST 等）
    int HTTPversion;        // HTTP 协议的版本
    std::string file_name;  // 请求的文件名
    int now_read_pos;       // 当前读取位置
    int state;              // 请求的状态
    int h_state;            // 处理请求头的状态
    bool isfinish;          // 请求是否处理完成的标志
    bool keep_alive;        // 是否保持连接的标志
    int againTimes;    // 用于记录请求重新尝试的次数
    std::string path;  // 请求访问的路径。
    int fd;            // 与请求相关联的文件描述符
    int epollfd;       // 与请求相关联的 epoll 文件描述符
    std::unordered_map<std::string, std::string> headers;  // 请求的头部信息
    std::weak_ptr<mytimer> timer; 
    //用weak_ptr管理请求超时的计时器，因为请求类和定时器类里都有成员变量指向对方，故把一个改成弱引用防止循环引用

private:
    int parse_URI();        // 解析请求的 URI
    int parse_Headers();    // 解析请求的头部信息
    int analysisRequest();  // 分析处理请求

public:

    requestData();
    requestData(int _epollfd, int _fd, std::string _path);
    ~requestData();
    void addTimer(std::shared_ptr<mytimer> mtimer); //给请求对象添加计时器
    void reset();          // 重置请求数据
    void seperateTimer();  // 分离计时器
    int getFd();           // 获取文件描述符
    void setFd(int _fd);   // 设置文件描述符
    void handleRequest();  // 处理请求
    void handleError(int fd, int err_num, std::string short_msg);  // 处理错误
};

//定时器对象，用来管理请求对象的超时是否，若超时则进行处理
struct mytimer
{
    bool deleted; //表示定时器是否被删除，1表示已经删除
    size_t expired_time; //定时器的超时时间
    std::shared_ptr<requestData> request_data; //定时器关联的请求对象

    mytimer(std::shared_ptr<requestData> _request_data, int timeout); //接收一个事件请求对象和超时时间来初始化
    ~mytimer();
    void update(int timeout);   // 更新定时器的超时时间
    bool isvalid();             // 检查定时器是否有效，即是否超时
    void clearReq();            // 清除当前的请求对象并调用setDeleted()
    void setDeleted();          // 将定时器标记为已删除
    bool isDeleted() const;     // 检查定时器是否已被删除
    size_t getExpTime() const;  // 获取定时器的超时时间
};

// 定时器优先级队列的排序规则(小顶堆)
struct timerCmp
{
    bool operator()(std::shared_ptr<mytimer> &a, std::shared_ptr<mytimer> &b) const;
};

//定义互斥锁类，实现自动加/解锁
class MutexLockGuard
{
public:
    explicit MutexLockGuard();
    ~MutexLockGuard();

private:
    static pthread_mutex_t lock; // 静态成员变量，锁唯一

private:
    MutexLockGuard(const MutexLockGuard&);
    MutexLockGuard& operator=(const MutexLockGuard&);
};

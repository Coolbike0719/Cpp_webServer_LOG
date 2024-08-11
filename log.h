// 日志系统
#pragma once

#include <iostream>
#include <string>
#include <stdint.h> // int32_t
#include <pthread.h> // pthread_self()
#include <memory> // 智能指针
#include <list>
#include <vector>
#include <map>
#include <functional>
#include <fstream>  // ofstream
#include <sstream>  // stringstream 
#include "singleton.h"

/* 日志信息格式示意
   时间					线程id	线程名称			协程id	[日志级别]	[日志名称]		文件名:行号:           			消息 	换行符号
2023-11-07 10:06:00     2048    thread_name      1024    [INFO]      [logger]   /apps/sylar/tests/test_log.cc:40    消息内容 
*/
class Logger;

//日志级别
class LogLevel {
public:
    enum Level{
        UNKOWN = 0,     //未知
        DEBUG = 1,      //调试
        INFO = 2,       //普通信息
        WARN = 3,       //警告
        ERROR = 4,      //错误
        FATAL = 5       //致命
    };
    //将level数字转换成字符串，更人性化地显示级别
    static const char* ToString(LogLevel::Level level);
};

//生成的日志事件，封装日志信息
class LogEvent {
public:
    //这些类里都定义一个指向自己的智能指针类型，便于实例化和内存管理，并改名为ptr
    typedef std::shared_ptr<LogEvent> ptr;
    
    //有参构造，传入日志所需信息
    LogEvent(const std::string logName, LogLevel::Level level,
             const char* file, int32_t line, uint32_t elapse,
             uint32_t thread_id, uint32_t fiber_id, uint64_t time);

    //私有成员变量get方法
    const std::string& getLogName() const { return m_logName;}
    const char* getFile() const { return m_file;}
    int32_t getLine() const { return m_line;}
    uint32_t getElapse() const { return m_elapse;}
    uint32_t getThreadId() const { return m_threadId;}
    uint32_t getFiberId() const { return m_fiberId;}
    uint32_t getTime() const { return m_time;}
    LogLevel::Level getLevel() const { return m_level;}
    std::string getContent() const { return m_ss.str(); }   //提供流对象转字符串
    std::stringstream& getSS() { return m_ss;}      //获取字符流

private:
    std::string m_logName;          //日志器名称
    LogLevel::Level m_level;        //日志级别
    const char* m_file = nullptr;   //存放日志的文件名
    int32_t m_line = 0;             //行号
    uint32_t m_elapse = 0;          //程序启动到现在的毫秒数
    uint32_t m_threadId = 0;        //线程id
    uint32_t m_fiberId = 0;         //协程id
    uint64_t m_time;                //时间戳
    std::stringstream m_ss;         //字符流，存放日志消息内容
};

//日志格式器（基类），输出到不同地方的日志信息格式可以不同，可以传入指定格式pattern，其实就是实现一个自定义printf的功能，解析更多的%xxx格式字符串并输出
class LogFormatter {
public:
    typedef std::shared_ptr<LogFormatter> ptr;
    LogFormatter(const std::string& pattern);

    //初始化，解析模板字符串
    void init();
    //将LogEvent格式化为字符串给LogAppender去输出
    std::string format(LogEvent::ptr event);
     
    //定义一个内部类作为不同字段解析输出的基类，也就是分别解析并输出%d,%t,%f,%l等等
    class FormatItem {
    public:
        typedef std::shared_ptr<FormatItem> ptr;
        //有子类 需要虚析构
        virtual ~FormatItem() {}
        virtual void format(std::ostream& os, LogEvent::ptr event) = 0;
    };

private:
    std::string m_pattern;                  //存放传入的字符串准备格式化
    std::vector<FormatItem::ptr> m_items;   //存放解析器数组，在init时会解析传入的pattern添加对应符号的解析器
};

//日志适配器（基类），兼容各种输出目标
class LogAppender {
public:
    typedef std::shared_ptr<LogAppender> ptr;

    //会有多种输出方式，定义成虚析构便于子类正确析构
    virtual ~LogAppender() {}; 
    //输出方式，纯虚函数，具体由子类实现
    virtual void log(LogEvent::ptr event) = 0;
    //设置当前适配器的formatter格式
    void setFormatter(LogFormatter::ptr val) { m_formatter = val;}
    //获取当前适配器的formatter格式
    LogFormatter::ptr getFormatter() const { return m_formatter;}

protected: //是基类，要被子类继承使用
    LogFormatter::ptr m_formatter;      //存放要调用的格式
};

//输出到控制台的Appender
class StdoutLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;

    //override指明log为重写基类方法
    void log(LogEvent::ptr event) override;
};

//输出到文件的Appender
class FileLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<FileLogAppender> ptr;
    
    //有参构造，指明输出的文件名
    FileLogAppender(const std::string& filename);
    ~FileLogAppender();

    void log(LogEvent::ptr event) override;
private:
    std::string m_filename;     // 文件路径
    std::ofstream m_filestream; // 文件流
};

//日志器，一个日志可以输出到多个地方，要有多种输出方式，故一个日志器配有多种日志适配器
class Logger {
public:
    typedef std::shared_ptr<Logger> ptr;

    //有参构造，指定日志器名称，const使其不会被修改
    Logger(const std::string& name = "root");

	//获取日志名称 返回引用避免拷贝，用const防止返回值和成员变量被修改
	const std::string& getName() const { return m_name; }

    LogLevel::Level getLevel() const { return m_level; }

    //改变当前日志器能输出的最大日志级别
    void setLevel(LogLevel::Level val) { m_level = val; }

    //调用适配器集合中的适配器log来输出日志LogEvent::ptr,event中含有想要查看的最大日志级别
    void log(LogEvent::ptr event);
    //新增/删除适配器
    void addAppender(LogAppender::ptr appender);
    void delAppender(LogAppender::ptr appender);

private:
    std::string m_name;         //日志器名称
    LogLevel::Level m_level;    //日志器能输出的最大日志级别，将与event中的查看级别做比较
    std::list<LogAppender::ptr> m_appenders; //当前日志器拥有的适配器集合
};

//把LogEvent封装一下，用匿名对象实现RAII调用输出
class LogEventWrap {
public:
    LogEventWrap(Logger::ptr logger, LogEvent::ptr e);
    ~LogEventWrap();
    // 获取封装的 LogEvent 对象
    LogEvent::ptr getEvent() const { return m_event; }
    // 获取封装的 LogEvent 对象的 stringstream，用于写入日志内容
    std::stringstream &getSS();

private:
    Logger::ptr m_logger;
    LogEvent::ptr m_event;
};
//用宏封装下LogEventWrap的调用
#define LOG_LEVEL(logger, level)                                               \
  		if (logger->getLevel() <= level)                                       \
 			 LogEventWrap(logger, LogEvent::ptr(new LogEvent(                  \
                           logger->getName(), level, __FILE__, __LINE__, 0,    \
                           static_cast<uint32_t>(pthread_self()), 0, time(0))))\
      					   .getSS()

//基于默认日志宏的子宏
#define LOG_DEBUG(logger) LOG_LEVEL(logger, LogLevel::DEBUG)
#define LOG_INFO(logger) LOG_LEVEL(logger, LogLevel::INFO)
#define LOG_WARN(logger) LOG_LEVEL(logger, LogLevel::WARN)
#define LOG_ERROR(logger) LOG_LEVEL(logger, LogLevel::ERROR)
#define LOG_FATAL(logger) LOG_LEVEL(logger, LogLevel::FATAL)

// 日志器管理类，管理所有日志器实例，避免重复创建，还可集中配置
class LoggerManager {
public:
    LoggerManager();
    Logger::ptr getLogger(const std::string& name); // 根据日志器名称获取日志器

    Logger::ptr getRoot() const { return m_root;} // 返回根日志器
private:
    std::map<std::string, Logger::ptr> m_loggers; // 日志器容器，根据字符串获取对应日志器
    Logger::ptr m_root; // 根日志器
};

// 日志器管理类采用单例模式
typedef Singleton<LoggerManager> LoggerMgr;

#include "log.h"
#include <iostream>


//LogLevel类
//ToString方法，将level数字转换成字符串
const char* LogLevel::ToString(LogLevel::Level level){
    //如果是某个数字就返回对应字符串，但这样写代码太笨重
    switch(level) {
    //用一个宏函数XX(name)来简化switch语句，#name表示将参数name转换为字符串返回
#define XX(name) \
    case LogLevel::name: \
        return #name; \
        break;
        
    XX(DEBUG);
    XX(INFO);
    XX(WARN);
    XX(ERROR);
    XX(FATAL);
//解除宏函数xx(name)
#undef XX
    default:
        return "UNKNOW";
    }
    return "UNKNOW";
}

//LogEvent类
//有参构造，列表初始化，注意有些编译器要求参数列表和成员变量顺序一致
LogEvent::LogEvent(const std::string logName, LogLevel::Level level,
            const char* file, int32_t line, uint32_t elapse,
            uint32_t thread_id, uint32_t fiber_id, uint64_t time)
            :m_logName(logName)
            ,m_level(level)
            ,m_file(file)
            ,m_line(line)
            ,m_elapse(elapse)
            ,m_threadId(thread_id)
            ,m_fiberId(fiber_id)
            ,m_time(time) {
}

//LogEventWrap类，一个包装器类，自动实现logger和logevent的绑定调用，简化调用的同时，用匿名对象可实现RAII调用输出日志记录，即自动管理日志事件的生命周期
// 构造函数，接收一个 Logger 智能指针和一个 LogEvent 智能指针
LogEventWrap::LogEventWrap(Logger::ptr logger, LogEvent::ptr e)
    : m_logger(logger), m_event(e) {
}
// 析构函数，在对象销毁时自动调用 Logger 的 log 方法
LogEventWrap::~LogEventWrap() { 
    m_logger->log(m_event); 
}
std::stringstream& LogEventWrap::getSS() { return m_event->getSS(); }

//LogAppender类，全都是虚函数，无实现
//StdoutLogAppender子类
void StdoutLogAppender::log(LogEvent::ptr event) {
    // //格式化时间，人性化显示
    // const std::string format = "%Y-%m-%d %H:%M:%S";
    // struct tm tm;
    // time_t t = event->getTime();
    // localtime_r(&t, &tm);
    // char tm_buf[64];
    // strftime(tm_buf, sizeof(tm_buf), format.c_str(), &tm);
    
    // std::cout
    //     //<< event->getTime() << "    " 换成格式化后的tm_buf显示时间
    //     << tm_buf << "    "
    //     << event->getThreadId() << "    "
    //     << event->getFiberId() << "    "
    //     << "["
    //     << LogLevel::ToString(event->getLevel())
    //     << "]    "
    //     << event->getFile() << ":" << event->getLine() << "    "
    //     << "输出到控制台"
    //     << std::endl;
    
    //直接利用日志格式器传入指定的模式输出
    std::cout << m_formatter->format(event);
}

//FileLogAppender子类
FileLogAppender::FileLogAppender(const std::string& filename)
    :m_filename(filename) {
    m_filestream.open(m_filename, std::ios::app | std::ios::out); // 追加|写入模式
    if (!m_filestream.is_open()) {
        std::cerr << "Failed to open log file: " << m_filename << std::endl;
    }
}
FileLogAppender::~FileLogAppender() {
    if (m_filestream.is_open()) {
        m_filestream.close();
    }
}
void FileLogAppender::log(LogEvent::ptr event) {
    if (m_filestream.is_open()) { // 若文件流打开则写入日志信息
        m_filestream << m_formatter->format(event);
        m_filestream.flush(); // 将流内容刷新进文件
    } else {
        std::cerr << "Log file is not open: " << m_filename << std::endl;
    }
}

//LogFormatter类
//在有参构造时就将pattern解析初始化
LogFormatter::LogFormatter(const std::string& pattern)
	:m_pattern(pattern){
	init();
}

//format格式化方法，传入日志事件，提取并格式化字符串
std::string LogFormatter::format(LogEvent::ptr event){
    std::stringstream ss;
    for(auto& i : m_items) {
        i->format(ss, event);
    }
    return ss.str();
}

//解析器，用不同的解析子类去解析对应的字段，大部分直接用流去接收调用LogEvent对应字段的get方法，少部分会做进一步解析处理
class MessageFormatItem : public LogFormatter::FormatItem {
public:
    MessageFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << event->getContent();
    }
};
class LevelFormatItem : public LogFormatter::FormatItem {
public:
    LevelFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << LogLevel::ToString(event->getLevel());
    }
};
class ElapseFormatItem : public LogFormatter::FormatItem {
public:
    ElapseFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << event->getElapse();
    }
};
class NameFormatItem : public LogFormatter::FormatItem {
public:
    NameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << event->getLogName();
    }
};
class ThreadIdFormatItem : public LogFormatter::FormatItem {
public:
    ThreadIdFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << event->getThreadId();
    }
};
class FiberIdFormatItem : public LogFormatter::FormatItem {
public:
    FiberIdFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << event->getFiberId();
    }
};
class DateTimeFormatItem : public LogFormatter::FormatItem {
public:
    DateTimeFormatItem(const std::string& format = "%Y-%m-%d %H:%M:%S")
        :m_format(format) {
        if(m_format.empty()) {
            m_format = "%Y-%m-%d %H:%M:%S";
        }
    }

    void format(std::ostream& os, LogEvent::ptr event) override {
        struct tm tm;
        time_t time = event->getTime();
        localtime_r(&time, &tm);
        char buf[64];
        strftime(buf, sizeof(buf), m_format.c_str(), &tm);
        os << buf;
    }
private:
    std::string m_format;
};
class FilenameFormatItem : public LogFormatter::FormatItem {
public:
    FilenameFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << event->getFile();
    }
};
class LineFormatItem : public LogFormatter::FormatItem {
public:
    LineFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << event->getLine();
    }
};
class NewLineFormatItem : public LogFormatter::FormatItem {
public:
    NewLineFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << std::endl;
    }
};
class StringFormatItem : public LogFormatter::FormatItem {
public:
    StringFormatItem(const std::string& str)
        :m_string(str) {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << m_string;
    }
private:
    std::string m_string;
};
class TabFormatItem : public LogFormatter::FormatItem {
public:
    TabFormatItem(const std::string& str = "") {}
    void format(std::ostream& os, LogEvent::ptr event) override {
        os << "\t";
    }
private:
    std::string m_string;
};

//初始化，解析字符串，貌似要写在前面这些解析子类后面
void LogFormatter::init(){
	//我们粗略的把上面的解析对象分成两类 一类是普通字符串 另一类是可被解析的
	//可以用 tuple来定义 需要的格式 std::tuple<std::string,std::string,int> 
	//<符号,子串,类型>  类型0-普通字符串 类型1-可被解析的字符串 
	//可以用一个 vector来存储不同的解析器
	std::vector<std::tuple<std::string,std::string,int> > vec;
	//解析后的字符串
	std::string nstr;
	//循环中解析，外循环遍历pattern存入nstr，遇到%开始内循环判断是否需要解析%x
    for(size_t i = 0; i < m_pattern.size(); ++i) {
        // 如果不是%号
        // nstr字符串后添加1个字符m_pattern[i]
        if(m_pattern[i] != '%') {
            nstr.append(1, m_pattern[i]);
            continue;
        }
		// m_pattern[i]是% && m_pattern[i + 1] == '%' ==> 两个%,第二个%当作普通字符
        if((i + 1) < m_pattern.size()) {
            if(m_pattern[i + 1] == '%') {
                nstr.append(1, '%');
                continue;
            }
        }
		
		// m_pattern[i]是% && m_pattern[i + 1] != '%', 需要进行解析
        size_t n = i + 1;		// 跳过'%',从'%'的下一个字符开始解析
        int fmt_status = 0;		// 是否解析大括号内的内容: 已经遇到'{',但是还没有遇到'}' 值为1
        size_t fmt_begin = 0;	// 大括号开始的位置

        std::string str;
        std::string fmt;	// 存放'{}'中间截取的字符
        // 从m_pattern[i+1]开始遍历
        while(n < m_pattern.size()) {
        	// m_pattern[n]不是字母 & m_pattern[n]不是'{' & m_pattern[n]不是'}'
            if(!fmt_status && (!isalpha(m_pattern[n]) && m_pattern[n] != '{'
                    && m_pattern[n] != '}')) {
                str = m_pattern.substr(i + 1, n - i - 1);
                break;
            }
            if(fmt_status == 0) {
                if(m_pattern[n] == '{') {
                	// 遇到'{',将前面的字符截取
                    str = m_pattern.substr(i + 1, n - i - 1);
                    //std::cout << "*" << str << std::endl;
                    fmt_status = 1; // 标志进入'{'
                    fmt_begin = n;	// 标志进入'{'的位置
                    ++n;
                    continue;
                }
            } else if(fmt_status == 1) {
                if(m_pattern[n] == '}') {
                	// 遇到'}',将和'{'之间的字符截存入fmt
                    fmt = m_pattern.substr(fmt_begin + 1, n - fmt_begin - 1);
                    //std::cout << "#" << fmt << std::endl;
                    fmt_status = 0;
                    ++n;
                    // 找完一组大括号就退出循环
                    break;
                }
            }
            ++n;
            // 判断是否遍历结束
            if(n == m_pattern.size()) {
                if(str.empty()) {
                    str = m_pattern.substr(i + 1);
                }
            }
        }

        if(fmt_status == 0) {
            if(!nstr.empty()) {
            	// 保存其他字符 '['  ']'  ':'
                vec.push_back(std::make_tuple(nstr, std::string(), 0));
                nstr.clear();
            }
            // fmt:寻找到的格式
            vec.push_back(std::make_tuple(str, fmt, 1));
            // 调整i的位置继续向后遍历
            i = n - 1;
        } else if(fmt_status == 1) {
        	// 没有找到与'{'相对应的'}' 所以解析报错，格式错误
            std::cout << "pattern parse error: " << m_pattern << " - " << m_pattern.substr(i) << std::endl;
            vec.push_back(std::make_tuple("<<pattern_error>>", fmt, 0));
        }
    }

    if(!nstr.empty()) {
        vec.push_back(std::make_tuple(nstr, "", 0));
    }
	
	//输出看下
    // for(auto& it : vec) {
    //     std::cout 
    //         << std::get<0>(it) 
    //         << " : " << std::get<1>(it) 
    //         << " : " << std::get<2>(it)
    //         << std::endl;
    // }

    //定义一个静态map映射字符和解析子类，键是字符，值是函数对象(返回值为FormatItem类指针，参数为string)
    //这样就可以调用format格式化方法时就可以根据字符选择不同的解析器了
#define XX(str, C) {#str, [](const std::string& fmt) { return FormatItem::ptr(new C(fmt)); }}
    static std::map<std::string, std::function<LogFormatter::FormatItem::ptr(const std::string& str)> > s_format_items = {

        XX(m, MessageFormatItem),       //日志内容
        XX(p, LevelFormatItem),         //行号
        XX(r, ElapseFormatItem),        //运行时间
        XX(c, NameFormatItem),          //日志器名称
        XX(t, ThreadIdFormatItem),      //线程id
        XX(n, NewLineFormatItem),       //换行号/n
        XX(d, DateTimeFormatItem),      //当前时间，类中会继续解析为年月日时分秒
        XX(f, FilenameFormatItem),      //文件名称
        XX(l, LineFormatItem),          //行号
        XX(T, TabFormatItem),           //制表符/t
        XX(F, FiberIdFormatItem),       //协程id
    };
#undef XX

    for(auto& i : vec) {
        if(std::get<2>(i) == 0) {
            m_items.push_back(FormatItem::ptr(new StringFormatItem(std::get<0>(i))));
        } else {
            auto it = s_format_items.find(std::get<0>(i));
            if(it == s_format_items.end()) {
                m_items.push_back(FormatItem::ptr(new StringFormatItem("<<error_format %" + std::get<0>(i) + ">>")));
            } else {
                m_items.push_back(it->second(std::get<1>(i)));
            }
        }
    }
}

//Logger类
//有参构造，默认日志级别为DEBUG
Logger::Logger(const std::string& name)
    :m_name(name)
    ,m_level(LogLevel::DEBUG) {
}
//调用适配器的log输出，Logger::log并不直接输出
void Logger::log(LogEvent::ptr event) {
    //要查看的event日志级别大于等于当前日志器的输出级别才可遍历适配器集合输出
    if(event->getLevel() >= this->m_level) {
        for(auto& i : m_appenders) {
            i->log(event);
        }
    }
}
//向日志的适配器集合添加一个适配器
void Logger::addAppender(LogAppender::ptr appender) {
    m_appenders.push_back(appender);
}
//从日志的适配器集合删除一个适配器
void Logger::delAppender(LogAppender::ptr appender) {
    for(auto it = m_appenders.begin();
            it != m_appenders.end(); ++it) {
        if (*it == appender) {
            m_appenders.erase(it);
            break;
        }
    }
    //m_appenders.remove(appender); 这种方法会删除所有匹配的适配器，上面只会删除第一个
}

// 日志器管理类，初始化根日志器
LoggerManager::LoggerManager() {
    m_root.reset(new Logger);

    StdoutLogAppender::ptr stdApd(new StdoutLogAppender());
    LogFormatter::ptr fmt(new LogFormatter(
        "%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
    stdApd->setFormatter(fmt);
    m_root->addAppender(stdApd);
    
    FileLogAppender::ptr fileApd(new FileLogAppender("./logs/log.txt"));
    fileApd->setFormatter(fmt);
    m_root->addAppender(fileApd);

    m_loggers[m_root->getName()] = m_root; // 根日志器添加到容器中
}
// 从日志器容器中根据日志器名称获取日志器
Logger::ptr LoggerManager::getLogger(const std::string& name) {
    // 如果在容器中找到就返回，否则则创建一个日志器返回
    auto it = m_loggers.find(name);
    if(it != m_loggers.end()) {
        return it->second;
    }

    Logger::ptr logger(new Logger(name));

    StdoutLogAppender::ptr stdApd(new StdoutLogAppender());
    LogFormatter::ptr fmt(new LogFormatter(
        "%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
    stdApd->setFormatter(fmt);
    logger->addAppender(stdApd);

    FileLogAppender::ptr fileApd(new FileLogAppender("./logs/log.txt"));
    fileApd->setFormatter(fmt);
    logger->addAppender(fileApd);

    m_loggers[name] = logger;
    
    return logger;
}
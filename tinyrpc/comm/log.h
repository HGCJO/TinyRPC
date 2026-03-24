#ifndef TINYRPC_COMM_LOG_H
#define TINYRPC_COMM_LOG_H

#include <sstream>
#include <sstream>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <unistd.h>
#include <memory>
#include <vector>
#include <queue>
#include <semaphore.h>
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/comm/config.h"

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;         //定义一个智能指针类，指向全局配置对象，先声明

//std::string s = formatString("user=%s, code=%d", "zhangsan", 200);
// s 变成 "user=zhangsan, code=200"
template <typename... Args>
std::string formatString(const char* str, Args&& ... args)          //Args&& ... args 完美转发参数，减少拷贝，效率高。
{
    int size = snprintf(nullptr, 0, str, args...); // 计算格式化字符串所需的长度
    std::string result;
    if (size > 0) {
        result.resize(size); // 调整字符串大小以容纳格式化结果
        snprintf(&result[0], size + 1, str, args...); // 格式化字符串并存储在 result 中
    }
    return result;
}

//日志事件类，包含日志级别、文件名、行号、函数名、日志类型等信息，并提供一个字符串流用于构建日志消息。

#define DebugLog \
	if (tinyrpc::OpenLog() && tinyrpc::LogLevel::DEBUG >= tinyrpc::gRpcConfig->m_log_level) \
		tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG))).getStringStream()

#define InfoLog \
	if (tinyrpc::OpenLog() && tinyrpc::LogLevel::INFO >= tinyrpc::gRpcConfig->m_log_level) \
		tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG))).getStringStream()

#define WarnLog \
	if (tinyrpc::OpenLog() && tinyrpc::LogLevel::WARN >= tinyrpc::gRpcConfig->m_log_level) \
		tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG))).getStringStream()

#define ErrorLog \
	if (tinyrpc::OpenLog() && tinyrpc::LogLevel::ERROR >= tinyrpc::gRpcConfig->m_log_level) \
		tinyrpc::LogTmp(tinyrpc::LogEvent::ptr(new tinyrpc::LogEvent(tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::RPC_LOG))).getStringStream()


#define AppDebugLog(str, ...) \
  if (tinyrpc::OpenLog() && tinyrpc::LogLevel::DEBUG >= tinyrpc::gRpcConfig->m_app_log_level) \
  { \
    tinyrpc::Logger::GetLogger()->pushAppLog(tinyrpc::LogEvent(tinyrpc::LogLevel::DEBUG, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");\
  } \

#define AppInfoLog(str, ...) \
  if (tinyrpc::OpenLog() && tinyrpc::LogLevel::INFO>= tinyrpc::gRpcConfig->m_app_log_level) \
  { \
    tinyrpc::Logger::GetLogger()->pushAppLog(tinyrpc::LogEvent(tinyrpc::LogLevel::INFO, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");\
  } \

#define AppWarnLog(str, ...) \
  if (tinyrpc::OpenLog() && tinyrpc::LogLevel::WARN>= tinyrpc::gRpcConfig->m_app_log_level) \
  { \
    tinyrpc::Logger::GetLogger()->pushAppLog(tinyrpc::LogEvent(tinyrpc::LogLevel::WARN, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");\
  } \

#define AppErrorLog(str, ...) \
  if (tinyrpc::OpenLog() && tinyrpc::LogLevel::ERROR>= tinyrpc::gRpcConfig->m_app_log_level) \
  { \
    tinyrpc::Logger::GetLogger()->pushAppLog(tinyrpc::LogEvent(tinyrpc::LogLevel::ERROR, __FILE__, __LINE__, __func__, tinyrpc::LogType::APP_LOG).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + tinyrpc::formatString(str, ##__VA_ARGS__) + "\n");\
  } \



enum LogType {
	RPC_LOG = 1,
	APP_LOG = 2,
};


pid_t gettid();

LogLevel stringTologLevel(const std::string& str);
std::string logLevelToString(LogLevel level);

bool OpenLog();

//封装单次日志事件核心信息的类，用于记录一条日志产生时的所有上下文（如日志级别、发生位置、进程 / 线程 ID、时间、日志内容等
class LogEvent{
public:
 	
	typedef std::shared_ptr<LogEvent> ptr;
	LogEvent(LogLevel level, const char* file_name, int line, const char* func_name, LogType type); //初始化一条日志事件的核心上下文信息

	~LogEvent();

	std::stringstream& getStringStream();                                                           //返回日志内容的字符串流引用，用于拼接 / 写入日志的具体消息内容

	std::string toString();                                                     //将日志事件的所有信息（包括上下文和消息内容）格式化为一个完整的字符串，准备写入日志文件。  

	void log();                                         //将日志事件写入日志文件的函数，通常会被 Logger 类调用。它会调用 toString() 方法获取完整的日志字符串，并将其写入到指定的日志文件中。                


private:
		
    // 日志事件类的成员变量（存储一条日志的所有信息）
    timeval m_timeval;        // 日志发生的精确时间（包含秒+微秒，高精度时间戳）
    LogLevel m_level;         // 日志级别（DEBUG/INFO/WARN/ERROR）
    pid_t m_pid {0};          // 进程ID（当前日志所属进程）
    pid_t m_tid {0};          // 线程ID（当前日志所属线程）
    int m_cor_id {0};         // 协程ID（当前日志所属协程，无协程时为0）

    const char* m_file_name;  // 日志打印所在的源文件名（来自 __FILE__）
    int m_line {0};           // 日志打印所在代码行号（来自 __LINE__）
    const char* m_func_name;  // 日志打印所在函数名（来自 __func__）
    LogType m_type;           // 日志类型（RPC_LOG 框架日志 / APP_LOG 业务日志）
    std::string m_msg_no;     // 日志消息编号（用于唯一标识、追踪日志）

    std::stringstream m_ss;   // 字符串流，用于拼接日志具体内容（<< 操作）
};
//管理 LogEvent 对象生命周期，利用析构函数自动触发日志输出，简化上层使用
class LogTmp {
public:

    explicit LogTmp(LogEvent::ptr event);   //传入一个 LogEvent 智能指针，由 LogTmp 托管

    ~LogTmp();

    std::stringstream& getStringStream();   //获取日志字符串流引用，供外部拼接日志内容。当 logTmp 对象生命周期结束时（如一行日志输出完成），析构函数会自动调用 LogEvent 的 log() 方法，将日志写入文件。

private:
    LogEvent::ptr m_event;                  //持有的日志事件对象
};

// 异步日志器类（核心：后台线程异步写日志，不阻塞业务主线程）
class AsynLogger
{
public:
    typedef std::shared_ptr<AsynLogger> ptr;  // 智能指针别名，方便使用

    // 构造函数：初始化异步日志器
    // file_name: 日志文件名   file_path: 日志文件路径   max_size: 单文件最大大小   log_type: 日志类型(RPC/APP)
    AsynLogger(const char* file_name,const char*file_path,int max_size,LogType log_type);

    ~AsynLogger();                             // 析构：释放资源、关闭文件、停止线程

    // 批量写入日志缓冲区：把日志内容推给异步线程，不直接写磁盘
    void push(std::vector<std::string>& buffer);

    void flush();                              // 强制刷新缓冲区，把日志落地到文件

    static void* excute(void*);                // 异步线程执行函数（静态，供pthread创建线程）

    void stop();                               // 停止异步日志线程，退出循环

 public:
	std::queue<std::vector<std::string>> m_tasks;
    
private:
    const char* m_file_name;          // 日志文件名
    const char* m_file_path;          // 日志文件存放路径
    int m_max_size{0};                // 单个日志文件最大大小，超过则切分
    LogType m_log_type;               // 日志类型（RPC_LOG / APP_LOG）

    int m_no {0};                     // 日志文件序号（切分文件时用：xxx.1.log、xxx.2.log）
    bool m_need_reopen {false};       // 是否需要重新打开文件（日期变化/切分时置true）
    FILE* m_file_handle {nullptr};    // 文件句柄（指向打开的日志文件）
    std::string m_date;               // 当前日志日期（用于按天切分文件）

    Mutex m_mutex;                    // 互斥锁，保护多线程访问共享数据
    pthread_cond_t m_condition;       // 条件变量，用于线程等待/唤醒
    bool m_stop {false};              // 标记是否停止异步线程

public:
    pthread_t m_thread;               // 异步写日志的后台线程ID
    sem_t m_semaphore;                // 信号量，控制日志缓冲区消费（通知线程有日志可写）
};



class Logger
{
public:
    static Logger* GetLogger();   // 获取全局日志单例实例
public:
    typedef std::shared_ptr<Logger> ptr;  // 智能指针别名，方便使用

    Logger();
	~Logger();

	void init(const char* file_name, const char* file_path, int max_size, int sync_interval);// 初始化日志器，设置日志文件名、路径、单文件最大大小和同步间隔

    void pushRpcLog(const std::string& log_msg);  // 推送一条 RPC 日志事件到异步日志器
    void pushAppLog(const std::string& log_msg);  // 推送一条 APP 日志事件到异步日志器
    void loopFunc();                             // 日志器主循环函数，处理日志写入和文件管理（如切分、重命名等）

    void flush();                              // 强制刷新日志器，确保所有日志都写入文件
    void start();                              // 启动日志器，创建异步线程

    AsynLogger::ptr getAsyncLogger()          // 获取 RPC 日志器实例
    {
        return m_async_rpc_logger;
    }

    AsynLogger::ptr getAsyncAppLogger()          // 获取 APP 日志器实例
    {
        return m_async_app_logger;
    }

public:
    std::vector<std::string>m_buffer;              // 日志缓冲区，存储待写入的日志内容
    std::vector<std::string>m_app_buffer;          // APP 日志缓冲区，存储待写入的业务日志内容

    private:
    AsynLogger::ptr m_async_rpc_logger;            // 异步日志器实例
    AsynLogger::ptr m_async_app_logger;            // 异步日志器实例
    Mutex m_app_buff_mutex;                        // APP 日志缓冲区互斥锁
    Mutex m_buff_mutex;                            // 日志缓冲区互斥锁
    bool m_is_init {false};                     // 标记日志器是否已初始化


};

void Exit(int code);



}
#endif
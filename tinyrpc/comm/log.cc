#include <time.h>
#include <sys/time.h>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>
#include <iostream>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <algorithm>
#include <semaphore.h>
#include <errno.h>

#ifdef DECLARE_MYSQL_PLUGIN 
#include <mysql/mysql.h>
#endif


#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/comm/run_time.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/timer.h"

namespace tinyrpc {


extern tinyrpc::Logger::ptr gRpcLogger;
extern tinyrpc::Config::ptr gRpcConfig;


static std::atomic_int64_t g_rpc_log_index {0};
static std::atomic_int64_t g_app_log_index {0};
//信号处理函数，接收非法信号时，记录日志并优雅地关闭日志系统，确保所有日志都被写入文件后再退出程序。
void CoredumpHandler(int signal_no) {
  ErrorLog << "progress received invalid signal, will exit";
  printf("progress received invalid signal, will exit\n");
  gRpcLogger->flush();
  pthread_join(gRpcLogger->getAsyncLogger()->m_thread, NULL);
  pthread_join(gRpcLogger->getAsyncAppLogger()->m_thread, NULL);

  signal(signal_no, SIG_DFL);
  raise(signal_no);
}

class Coroutine;

static thread_local pid_t t_thread_id = 0;
static pid_t g_pid = 0;


pid_t gettid() {
  if (t_thread_id == 0) {
    //直接调用 Linux 内核系统调用的函数，获取当前线程的 ID，并将其存储在 t_thread_id 中
    t_thread_id = syscall(SYS_gettid);
  }
  return t_thread_id;
}
void setLogLevel(LogLevel level) {
  // g_log_level = level;
}

bool OpenLog() {
  if (!gRpcLogger) {
    return false;
  }
  return true;
}

LogEvent::LogEvent(LogLevel level, const char* file_name, int line, const char* func_name, LogType type)
  : m_level(level),
    m_file_name(file_name),
    m_line(line),
    m_func_name(func_name),
    m_type(type) {
}


LogEvent::~LogEvent() {

}
//给人看,
std::string levelToString(LogLevel level) {
  std::string re = "DEBUG";
  switch(level) {
    case DEBUG:
      re = "DEBUG";
      return re;
    
    case INFO:
      re = "INFO";
      return re;

    case WARN:
      re = "WARN";
      return re;

    case ERROR:
      re = "ERROR";
      return re;
    case NONE:
      re = "NONE";

    default:
      return re;
  }
}
//给程序用,把字符串转换为 LogLevel 枚举值，方便程序根据配置文件或用户输入设置日志级别。
LogLevel stringToLevel(const std::string& str) {
    if (str == "DEBUG")
      return LogLevel::DEBUG;
    
    if (str == "INFO")
      return LogLevel::INFO;

    if (str == "WARN")
      return LogLevel::WARN;

    if (str == "ERROR")
      return LogLevel::ERROR;

    if (str == "NONE")
      return LogLevel::NONE;

    return LogLevel::DEBUG;
}

std::string LogTypeToString(LogType logtype) {
  switch (logtype) {
    case APP_LOG:
      return "app";
    case RPC_LOG:
      return "rpc";
    default:
      return "";
  }
}

std::stringstream& LogEvent::getStringStream() {
    
    //获取当前系统时间，并格式化成「年 - 月 - 日 时：分: 秒」字符串
    gettimeofday(&m_timeval, NULL);

    struct tm time;
    localtime_r(&(m_timeval.tv_sec), &time);
    const char* format = "%Y-%m-%d %H:%M:%S";
    char buf[128];
    //2026-03-19 15:30:25
    strftime(buf, sizeof(buf), format, &time);
    //拼接日志的高精度时间戳并写入日志的字符串流中
    m_ss << "[" << buf << "." << m_timeval.tv_usec << "]\t"; 

    std::string s_level = levelToString(m_level);
    //[2026-03-19 15:30:25.123456]	[INFO]	
    m_ss << "[" << s_level << "]\t";

    //获取当前进程 ID 和线程 ID,协程ID，并将它们写入日志字符串流中，方便日志追踪和分析。
    if (g_pid == 0) {
    g_pid = getpid();
    }
    m_pid = g_pid;  

    if (t_thread_id == 0) {
    t_thread_id = gettid();
    }
    m_tid = t_thread_id;

    m_cor_id = Coroutine::GetCurrentCoroutine()->getCorId();
    //主要作用是把「协程 ID、进程 ID、线程 ID、代码位置」等关键调试信息写入日志的字符串流中
    m_ss << "[" << m_pid << "]\t" 
    << "[" << m_tid << "]\t"
    << "[" << m_cor_id << "]\t"
    << "[" << m_file_name << ":" << m_line << "]\t";
    // << "[" << m_func_name << "]\t";
    //[2026-03-19 15:30:25.123456]	[INFO]	[12345]	[6789]	[1001]	[log.cc:120]	
    RunTime* runtime = getCurrentRunTime();

    //如果当前上下文中存在运行时信息（如消息编号、接口名称等），则将这些信息也写入日志字符串流中，进一步丰富日志内容，方便后续分析和调试。
    if (runtime) {
    std::string msgno = runtime->m_msg_no;
    if (!msgno.empty()) {
      m_ss << "[" << msgno << "]\t";
    }

    std::string interface_name = runtime->m_interface_name;
    if (!interface_name.empty()) {
      m_ss << "[" << interface_name << "]\t";
    }

  }
  //[2026-03-19 15:30:25.123456]	[DEBUG]	[1234]	[5678]	[1]	[rpc_server.cc:200]	[req_9527]	[UserService.Login]	用户登录成功
  return m_ss;
}


std::string LogEvent::toString() {
  return getStringStream().str();
}


//把拼接好的日志，按类型（RPC 日志 / APP 日志）和级别，推送给全局异步日志器，最终写入文件
void LogEvent::log() {
  m_ss << "\n";
  if (m_level >= gRpcConfig->m_log_level && m_type == RPC_LOG) {
    gRpcLogger->pushRpcLog(m_ss.str());
  } else if (m_level >= gRpcConfig->m_app_log_level && m_type == APP_LOG) {
    gRpcLogger->pushAppLog(m_ss.str());
  }
}
LogTmp::LogTmp(LogEvent::ptr event) : m_event(event) {

}

std::stringstream& LogTmp::getStringStream() {
  return m_event->getStringStream();
}

LogTmp::~LogTmp() {
  m_event->log(); 
}

Logger::Logger() {
  // cannot do anything which will call LOG ,otherwise is will coredump

}
Logger::~Logger() {
  flush();
  pthread_join(m_async_rpc_logger->m_thread, NULL);
  pthread_join(m_async_app_logger->m_thread, NULL);
}
Logger* Logger::GetLogger() {
  return gRpcLogger.get();
}
//预分配超大日志缓冲区，初始化两个异步日志器（一个用于 RPC 日志，一个用于 APP 日志），并设置信号处理函数以捕获非法信号，确保日志系统能够优雅地关闭并记录相关信息。
void Logger::init(const char* file_name, const char* file_path, int max_size, int sync_inteval) {
    if (!m_is_init) {
    m_sync_inteval = sync_inteval;
    for (int i = 0 ; i < 1000000; ++i) {
      m_app_buffer.push_back("");
      m_buffer.push_back("");
    }
    m_async_rpc_logger = std::make_shared<AsyncLogger>(file_name, file_path, max_size, RPC_LOG);
    m_async_app_logger = std::make_shared<AsyncLogger>(file_name, file_path, max_size, APP_LOG);

    signal(SIGSEGV, CoredumpHandler);
    signal(SIGABRT, CoredumpHandler);
    signal(SIGTERM, CoredumpHandler);
    signal(SIGKILL, CoredumpHandler);
    signal(SIGINT, CoredumpHandler);
    signal(SIGSTKFLT, CoredumpHandler);

    signal(SIGPIPE, SIG_IGN);
    m_is_init = true;

    }


}

void Logger::start() {
    //创建一个定时器事件，设置为周期性触发，并绑定 Logger 的 loopFunc 作为回调函数。这个定时器事件会定期调用 loopFunc 来处理日志的写入和文件管理。
  TimerEvent::ptr event = std::make_shared<TimerEvent>(m_sync_inteval, true, std::bind(&Logger::loopFunc, this));
  //将定时器事件添加到全局 Reactor 的定时器中，使其能够被 Reactor 调度和执行。
  Reactor::GetReactor()->getTimer()->addTimerEvent(event);
}

//
void Logger::loopFunc() {
  std::vector<std::string> app_tmp;
  Mutex::Lock lock1(m_app_buff_mutex);
  app_tmp.swap(m_app_buffer);
  lock1.unlock();
  
  std::vector<std::string> tmp;
  Mutex::Lock lock2(m_buff_mutex);
  tmp.swap(m_buffer);
  lock2.unlock();
    //投递到异步日志器，异步日志器会在后台线程中处理这些日志，将它们写入文件，并根据需要进行文件切分和重命名等管理操作。
  m_async_rpc_logger->push(tmp);
  m_async_app_logger->push(app_tmp);
}

void Logger::pushRpcLog(const std::string& msg) {
  Mutex::Lock lock(m_buff_mutex);
  m_buffer.push_back(std::move(msg));
  lock.unlock();
}

void Logger::pushAppLog(const std::string& msg) {
  Mutex::Lock lock(m_app_buff_mutex);
  m_app_buffer.push_back(std::move(msg));
  lock.unlock();
}


void Logger::flush() {
  loopFunc();
  m_async_rpc_logger->stop();
  m_async_rpc_logger->flush();

  m_async_app_logger->stop();
  m_async_app_logger->flush();
}
//初始化日志文件相关参数（文件名、路径、单文件最大大小、日志类型）；创建信号量用于线程同步；
//启动异步日志处理线程，并确保线程完全初始化后再返回（通过信号量等待）。
AsyncLogger::AsyncLogger(const char* file_name, const char* file_path, int max_size, LogType logtype)
  : m_file_name(file_name), m_file_path(file_path), m_max_size(max_size), m_log_type(logtype) {
  int rt = sem_init(&m_semaphore, 0, 0);
  assert(rt == 0);
    //创建一个新的线程，执行 AsyncLogger 的 excute 函数，并将当前 AsyncLogger 实例作为参数传递给线程函数。这个线程将负责异步处理日志写入文件的任务。
  rt = pthread_create(&m_thread, nullptr, &AsyncLogger::excute, this);
  assert(rt == 0);
  rt = sem_wait(&m_semaphore);
  assert(rt == 0);

}


AsyncLogger::~AsyncLogger() {

}

//将日志内容写入文件中
void* AsyncLogger::excute(void* arg) {
    //线程入口函数,
    AsyncLogger* ptr = reinterpret_cast<AsyncLogger*>(arg);         //将传入的参数转换为 AsyncLogger 对象指针，以便在线程函数中访问 AsyncLogger 的成员变量和方法。
    int rt = pthread_cond_init(&ptr->m_condition, nullptr);         //初始化条件变量，用于线程间的同步和通信，确保日志处理线程能够正确地等待和被唤醒。
    assert(rt == 0);

    rt = sem_post(&ptr->m_semaphore);                               //在线程完全初始化后，使用信号量通知创建线程的函数（Logger::init）线程已经准备就绪，可以继续执行后续操作。
    assert(rt == 0);


    while(1)
    {
        
        Mutex::Lock lock(ptr->m_mutex);
        while (ptr->m_tasks.empty() && !ptr->m_stop) {                  //如果日志任务队列为空且没有收到停止信号，线程进入等待状态，直到有新的日志任务被添加或收到停止信号。
        pthread_cond_wait(&(ptr->m_condition), ptr->m_mutex.getMutex());//等待条件变量，释放互斥锁，直到被唤醒后重新获取锁继续执行。
        }

        //从异步日志队列里，取出一批日志，准备写入文件。
        std::vector<std::string> tmp;
        tmp.swap(ptr->m_tasks.front());
        ptr->m_tasks.pop();
        bool is_stop = ptr->m_stop;
        lock.unlock();

        //拿当前日期 → 转成 20260319 格式 → 给日志文件当天命名用
        timeval now;
        gettimeofday(&now, nullptr);
        struct tm now_time;
        localtime_r(&(now.tv_sec), &now_time);
        const char *format = "%Y%m%d";
        char date[32];
        strftime(date, sizeof(date), format, &now_time);

        //检查当前日志文件的日期是否与当前日期匹配，如果不匹配，说明已经跨天了，
        //需要切分日志文件。此时重置日志文件序号（m_no）和日期（m_date），并设置 m_need_reopen 标志为 true，以便在下一次写入日志时重新打开新的日志文件。
        if (ptr->m_date != std::string(date)) {
        // cross day
        // reset m_no m_date
        ptr->m_no = 0;
        ptr->m_date = std::string(date);
        ptr->m_need_reopen = true;
        }

        //检查当前日志文件的大小是否超过了预设的最大值，如果超过了，说明需要切分日志文件。
        //此时增加日志文件序号（m_no），并设置 m_need_reopen 标志为 true，以便在下一次写入日志时重新打开新的日志文件。
        if (!ptr->m_file_handle) {
        ptr->m_need_reopen = true;
        }    

        std::stringstream ss;
        //./log/rpc_20260319_rpc_0.log
        ss << ptr->m_file_path << ptr->m_file_name << "_" << ptr->m_date << "_" << LogTypeToString(ptr->m_log_type) << "_" << ptr->m_no << ".log";
        std::string full_file_name = ss.str();

        //如果需要重新打开日志文件（因为跨天或文件过大），则先关闭当前打开的日志文件（如果有的话），然后以追加模式打开新的日志文件，并将 m_need_reopen 标志重置为 false。
        if (ptr->m_need_reopen) {
        if (ptr->m_file_handle) {
            fclose(ptr->m_file_handle);
        }
        ptr->m_file_handle = fopen(full_file_name.c_str(), "a");
        if(ptr->m_file_handle == nullptr) {
            printf("open fail errno = %d reason = %s \n", errno, strerror(errno));
        }
        ptr->m_need_reopen = false;
        }

        //，并检查写入后的文件大小是否超过了预设的最大值。如果超过了，说明需要切分日志文件，此时增加日志文件序号（m_no），并重新打开新的日志文件。
        if (ftell(ptr->m_file_handle) > ptr->m_max_size) {
        fclose(ptr->m_file_handle);
        // single log file over max size
        ptr->m_no++;
        std::stringstream ss2;
        ss2 << ptr->m_file_path << ptr->m_file_name << "_" << ptr->m_date << "_" << LogTypeToString(ptr->m_log_type) << "_" << ptr->m_no << ".log";
        full_file_name = ss2.str();

        // printf("open file %s", full_file_name.c_str());
        ptr->m_file_handle = fopen(full_file_name.c_str(), "a");
        ptr->m_need_reopen = false;
        }

        if (!ptr->m_file_handle) {
            printf("open log file %s error!", full_file_name.c_str());
        }

        //将日志内容写入当前打开的日志文件中
        for(auto i : tmp) {
        if (!i.empty()) {
            fwrite(i.c_str(), 1, i.length(), ptr->m_file_handle);
        }
        }

        tmp.clear();
        fflush(ptr->m_file_handle);
        if (is_stop) {
            break;
        }
    }
    if (!ptr->m_file_handle)
    {
        fclose(ptr->m_file_handle);
    }

    return nullptr;

}



void AsyncLogger::push(std::vector<std::string>& buffer) {
  if (!buffer.empty()) {
    Mutex::Lock lock(m_mutex);
    m_tasks.push(buffer);                                           //将新的日志任务添加到任务队列中，准备被异步日志处理线程消费。
    lock.unlock();
    pthread_cond_signal(&m_condition);                              //唤醒日志处理线程，通知它有新的日志任务需要处理。
  }
}



void AsyncLogger::flush() {
  if (m_file_handle) {
    fflush(m_file_handle);
  }
}


void AsyncLogger::stop() {
  if (!m_stop) {
    m_stop = true;
    pthread_cond_signal(&m_condition);
  }
}

//异常退出处理函数
void Exit(int code) {
  #ifdef DECLARE_MYSQL_PLUGIN
  mysql_library_end();
  #endif

  printf("It's sorry to said we start TinyRPC server error, look up log file to get more deatils!\n");
  gRpcLogger->flush();
  pthread_join(gRpcLogger->getAsyncLogger()->m_thread, NULL);   //阻塞当前线程，等待异步日志线程（m_thread）完全退出

  _exit(code);
}


}
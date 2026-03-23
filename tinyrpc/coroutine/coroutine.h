#ifndef TINYRPC_COROUTINE_COROUTINE_H
#define TINYRPC_COROUTINE_COROUTINE_H

#include <memory>
#include <functional>
#include "tinyrpc/coroutine/coctx.h"
#include "tinyrpc/comm/run_time.h"

namespace tinyrpc {

int getCoroutineIndex();                                    // 获取当前协程索引

RunTime* getCurrentRunTime();                               // 获取当前协程的运行时信息（如消息编号、接口名称等）

void setCurrentRunTime(RunTime* v);                     // 设置当前协程的运行时信息

class Coroutine{

public:

    typedef std::shared_ptr<Coroutine> ptr;

private:
    Coroutine();
public:

    Coroutine(int size,char* stack_ptr);                            // 构造函数，接受协程栈大小和栈指针作为参数，初始化协程上下文和运行时信息

    Coroutine(int size,char* stack_ptr, std::function<void()> cb); // 构造函数重载，除了栈大小和栈指针外，还接受一个回调函数参数，初始化协程上下文、运行时信息，并将回调函数设置为协程的入口函数

    ~Coroutine();

    bool setCallBack(std::function<void()> cb);                      // 设置协程的回调函数，返回设置是否成功

    int getCorId() const { return m_cor_id; }                            // 获取协程 ID

    //设置协程函数执行状态标记：true=协程函数正在执行；false=协程函数已执行完成
    void setIsInCoFunc(const bool v) {
         m_is_in_cofunc = v;
    }
    //获取协程函数执行状态标记：true=协程函数正在执行；false=协程函数已执行完成
    bool getIsInCoFunc() const {
        return m_is_in_cofunc;
    }
    //获取协程关联的请求唯一标识（消息编号），用于日志追踪/链路追踪
    std::string getMsgNo() {
        return m_msg_no;
    }
    //获取协程运行时上下文，存储当前协程的临时变量、日志上下文、请求信息等
    RunTime* getRunTime() {
        return &m_run_time; 
    }
    //设置协程关联的请求唯一标识（消息编号），用于日志追踪/链路追踪
    void setMsgNo(const std::string& msg_no) {
        m_msg_no = msg_no;
    }
    //设置协程在协程池中的索引位置，用于协程池快速管理/复用协程
    void setIndex(int index) {
        m_index = index;
    }
    //获取协程在协程池中的索引位置，用于协程池快速管理/复用协程
    int getIndex() {
        return m_index;
    }
    //获取协程栈内存起始地址，通过malloc/mmap分配内存后初始化，是协程独立栈的核心标识
    char* getStackPtr() {
        return m_stack_sp;
    }
    //获取协程栈内存大小（单位：字节），通常配置为128KB/256KB等
    int getStackSize() {
        return m_stack_size;
    }
    //设置协程是否可恢复执行：true=可resume；false=已终止/不可恢复（如执行完成/异常）
    void setCanResume(bool v) {
        m_can_resume = v;
    }

 public:

    static void Yield();                                           // 协程让出执行权，切换到调度器或其他协程，当前协程进入等待状态

    static void Resume(Coroutine* cor);                            // 恢复指定协程的执行，切换到该协程继续执行，如果该协程已终止或不可恢复，则无法恢复

    static Coroutine* GetCurrentCoroutine();                       // 获取当前正在执行的协程实例指针，如果没有正在执行的协程，则返回 nullptr

    static Coroutine* GetMainCoroutine();                          // 获取主协程实例指针，主协程是程序启动时创建的第一个协程，负责调度其他协程的执行

    static bool IsMainCoroutine();                                 // 判断当前协程是否为主协程，返回 true 表示当前协程是主协程，false 表示当前协程是普通协程



private:
  int m_cor_id {0};             // coroutine' id，协程唯一标识ID，用于区分不同协程实例
  coctx m_coctx;                // coroutine regs，协程上下文（保存寄存器/栈信息），底层通过coctx_swap切换时使用
  int m_stack_size {0};         // size of stack memory space，协程栈内存大小（单位：字节），通常配置为128KB/256KB等
  char* m_stack_sp {NULL};      // coroutine's stack memory space, you can malloc or mmap get some mermory to init this value
                                // 协程栈内存起始地址，通过malloc/mmap分配内存后初始化，是协程独立栈的核心标识
  bool m_is_in_cofunc {false};  // true when call CoFunction, false when CoFunction finished
                                // 协程函数执行状态标记：true=协程函数正在执行；false=协程函数已执行完成
  std::string m_msg_no;         // 协程关联的请求唯一标识（消息编号），用于日志追踪/链路追踪
  RunTime m_run_time;           // 协程运行时上下文，存储当前协程的临时变量、日志上下文、请求信息等
  bool m_can_resume {true};     // 协程是否可恢复执行：true=可resume；false=已终止/不可恢复（如执行完成/异常）
  int m_index {-1};             // index in coroutine pool，协程在协程池中的索引位置，用于协程池快速管理/复用协程

public:
  std::function<void()> m_call_back;  // 协程要执行的核心函数（回调函数），协程启动时会执行该函数


};


}

#endif
#ifndef TINYRPC_NET_EVENT_LOOP_H
#define TINYRPC_NET_EVENT_LOOP_H

#include <sys/socket.h>
#include <sys/types.h>
#include <vector>
#include <atomic>
#include <map>
#include <functional>
#include <queue>
#include "tinyrpc/coroutine/coroutine.h"
#include "fd_event.h"
#include "mutex.h"


namespace tinyrpc {

enum ReactorType{
  MainReactor = 1,    //主 reactor, 负责监听 socket fd 的 accept 事件，分发连接到子 reactor
  SubReactor = 2      //子 reactor, 每个 io 线程都是这种类型
};


class FdEvent;
class Timer;

// typedef std::shared_ptr<Timer> TimerPtr;

class Reactor{

public:
    typedef std::shared_ptr<Reactor> ptr;
    explicit Reactor();

    ~Reactor();

    void addEvent(int fd, epoll_event event, bool is_wakeup = true);

    void delEvent(int fd, bool is_wakeup = true);

    void addTask(std::function<void()> task, bool is_wakeup = true);

    void addTask(std::vector<std::function<void()>> task, bool is_wakeup = true);
    
    void addCoroutine(tinyrpc::Coroutine::ptr cor, bool is_wakeup = true);

    void wakeup();
    
    void loop();

    void stop();

    Timer* getTimer();

    pid_t getTid();

    void setReactorType(ReactorType type);
 
public:
    static Reactor* GetReactor();
  

private:

    void addWakeupFd();                                     // 将唤醒fd注册到epoll中，监听其可读事件，以便在需要唤醒事件循环时能够正确响应

    bool isLoopThread() const;                              // 判断当前线程是否是Reactor所属的线程，只有所属线程才能直接修改事件列表，其他线程需要通过添加待处理事件的方式来修改       

    void addEventInLoopThread(int fd, epoll_event event);   // 在Reactor所属线程中直接添加事件到epoll监听列表中，避免跨线程操作带来的性能损失和复杂性

    void delEventInLoopThread(int fd);                      // 在Reactor所属线程中直接从epoll监听列表中删除事件，避免跨线程操作带来的性能损失和复杂性
    
private:
    int m_epfd {-1};                  // epoll实例的文件描述符，用于IO多路复用
    int m_wake_fd {-1};               // 唤醒fd，用于跨线程唤醒Reactor的事件循环
    int m_timer_fd {-1};              // 定时器fd，用于处理定时任务
    bool m_stop_flag {false};         // 事件循环停止标志位，true表示停止循环
    bool m_is_looping {false};        // 事件循环运行状态标志位，true表示正在循环中
    bool m_is_init_timer {false};     // 定时器初始化标志位，true表示定时器已初始化
    pid_t m_tid {0};                  // 当前Reactor所属线程的ID

    tinyrpc::Mutex m_mutex;                    // 互斥锁，保护下方各类待处理容器的线程安全访问
    
    std::vector<int> m_fds;           // 已注册到epoll中的文件描述符列表（已关注的事件fd）
    std::atomic<int> m_fd_size;       // 已关注的fd数量（原子类型，保证多线程下的原子操作）

    // 待处理的fd操作容器（通过批量处理减少epoll_ctl调用次数，提升性能）
    // 1 - 等待添加到epoll监听的fd及对应事件
    // 2 - 等待从epoll中删除的fd
    std::map<int, epoll_event> m_pending_add_fds;           // 待添加的fd和对应的epoll_event事件映射
    std::vector<int> m_pending_del_fds;                     // 待删除的fd列表
    std::vector<std::function<void()>> m_pending_tasks;     // 待执行的任务队列（如异步投递的回调任务）

    Timer* m_timer {nullptr};                               // 定时器实例指针，用于管理所有定时任务

    ReactorType m_reactor_type {SubReactor};                 // Reactor类型标识，默认是子Reactor，可设置为主Reactor
};





class CoroutineTaskQueue {
 public:
    static CoroutineTaskQueue* GetCoroutineTaskQueue();

    void push(FdEvent* fd);

    FdEvent* pop();

 private:
    std::queue<FdEvent*> m_task;                  // 待执行的协程任务队列，存储需要被调度执行的协程事件,每个事件对应一个协程，当事件被触发时，相关的协程将被调度执行
    tinyrpc::Mutex m_mutex;                    // mutex
};



}


#endif

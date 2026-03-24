#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <assert.h>
#include <string.h>
#include <algorithm>
#include "tinyrpc/comm/log.h"
#include "reactor.h"
#include "mutex.h"
#include "fd_event.h"
#include "timer.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/coroutine/coroutine_hook.h"

extern read_fun_ptr_t g_sys_read_fun;  // sys read func
extern write_fun_ptr_t g_sys_write_fun;  // sys write func

namespace tinyrpc {

static thread_local Reactor* t_reactor_ptr = nullptr;

static thread_local int t_max_epoll_timeout = 10000;     // ms

static CoroutineTaskQueue* t_couroutine_task_queue = nullptr;

// 获取当前线程的 Reactor 实例，如果不存在则创建一个新的实例,并返回该实例的指针,保证每个线程只有一个 Reactor 实例
Reactor::Reactor()
{
    if(t_reactor_ptr != nullptr)
    {
        ErrorLog << "this thread has already create a reactor";
		Exit(0);
    }

    m_tid = gettid();
    DebugLog << "thread[" << m_tid << "] succ create a reactor object";
    //将实例给予指针
    t_reactor_ptr = this;
    
    if((m_epfd = epoll_create(1)) <= 0 )
    {
        ErrorLog << "start server error. epoll_create error, sys error=" << strerror(errno);
        Exit(0);
	} 
    else 
    {
		DebugLog << "m_epfd = " << m_epfd;
	}

    // 创建 eventfd，解决 Reactor 线程的 “唤醒问题”
	if((m_wake_fd = eventfd(0, EFD_NONBLOCK)) <= 0 ) {
		ErrorLog << "start server error. event_fd error, sys error=" << strerror(errno);
		Exit(0);
	}
	DebugLog << "wakefd = " << m_wake_fd;

	addWakeupFd();

}

Reactor::~Reactor() {
  DebugLog << "~Reactor";
	close(m_epfd);
  if (m_timer != nullptr) {
    delete m_timer;
    m_timer = nullptr;
  }
  t_reactor_ptr = nullptr;
}


Reactor* Reactor::GetReactor()
{
    if(t_reactor_ptr == nullptr)
    {
        DebugLog << "Create new Reactor";
        t_reactor_ptr = new Reactor();
    }
    return t_reactor_ptr;
}

void Reactor::addEvent(int fd, epoll_event event, bool is_wakeup) {
    if(fd == -1)
    {
        ErrorLog << "addEvent error, fd=-1";
        return;
    }
    //如果当前线程是 Reactor 所属线程，则直接在 epoll 中添加事件；否则将事件添加到待处理列表中，等待所属线程批量处理
    if(isLoopThread())
    {
        addEventInLoopThread(fd, event);
    }
    else
    {
        Mutex::Lock lock(m_mutex);
        m_pending_add_fds.insert(std::pair<int, epoll_event>(fd, event));           //将待添加的 fd 和对应事件添加到待处理列表中
        
    }
    if(is_wakeup)
    {
        wakeup();
    }

}

void Reactor::delEvent(int fd, bool is_wakeup/*=true*/) {

  if (fd == -1) {
    ErrorLog << "add error. fd invalid, fd = -1";
    return;
  }

  if (isLoopThread()) {
    delEventInLoopThread(fd);
    return;
  }

	{
    Mutex::Lock lock(m_mutex);
		m_pending_del_fds.push_back(fd);
	}
	if (is_wakeup) {
		wakeup();
	}
}


void Reactor::wakeup()
{
    if(!m_is_looping)
    {
        // 如果当前事件循环没有在运行，则直接返回
        return;
    }
    uint64_t tmp =1;
    uint64_t* p = &tmp;
    // 向 eventfd 中写入数据，触发可读事件，从而唤醒 Reactor 的事件循环
    if(g_sys_write_fun(m_wake_fd,p,8) != 8)
    {
       ErrorLog << "write wakeupfd[" << m_wake_fd <<"] error";
    }
}


bool Reactor::isLoopThread() const
{
    if(m_tid == gettid())                 // 判断当前线程的 ID 是否与 Reactor 所属线程的 ID 相同，如果相同则说明当前线程是 Reactor 所属线程
    {
        return true;
    }
    return false;
}


// 将唤醒fd注册到epoll中，监听其可读事件，以便在需要唤醒事件循环时能够正确响应
void Reactor::addWakeupFd()
{
    int op= EPOLL_CTL_ADD;
    epoll_event event;
    event.data.fd = m_wake_fd;
    event.events = EPOLLIN;
    if(epoll_ctl(m_epfd, op, m_wake_fd, &event) != 0)
    {
        ErrorLog << "add wakeupfd to epoll error, sys error=" << strerror(errno);
    }
    m_fds.push_back(m_wake_fd);
}


void Reactor::addEventInLoopThread(int fd, epoll_event event)
{
    assert(isLoopThread());              // 确保当前线程是 Reactor 所属线程，只有所属线程才能直接修改事件列表，其他线程需要通过添加待处理事件的方式来修改
    int op = EPOLL_CTL_MOD;
    bool is_add =true;

    auto it =find(m_fds.begin(), m_fds.end(), fd);
    if(it != m_fds.end())               // 如果 fd 已经在 m_fds 中，说明该 fd 已经注册到 epoll 中，此时需要修改其监听事件                   
    {
        is_add = false;
        op = EPOLL_CTL_MOD;             // 修改事件
    }

    if(epoll_ctl(m_epfd, op, fd, &event) != 0)
    {
        ErrorLog << "add fd[" << fd << "] to epoll error, sys error=" << strerror(errno);
        return;
    }

    if(is_add)                         // 如果 fd 不在 m_fds 中，说明该 fd 之前没有注册到 epoll 中，此时需要将其添加到 m_fds 中
    {
        m_fds.push_back(fd);
    }

    DebugLog << "epoll_ctl add succ, fd[" << fd << "]"; 

}

void Reactor::delEventInLoopThread(int fd)
{
    assert(isLoopThread());              // 确保当前线程是 Reactor 所属线程，只有所属线程才能直接修改事件列表，其他线程需要通过添加待处理事件的方式来修改
    
    bool is_del = true;
    auto it = find(m_fds.begin(), m_fds.end(), fd);
    if(it == m_fds.end())               // 如果 fd 不在 m_fds 中
    {
        is_del = false;
        ErrorLog << "delEvent error, fd[" << fd << "] not exist in reactor";
        return;
    }
    
    int op = EPOLL_CTL_DEL;
    if(epoll_ctl(m_epfd,op,fd,nullptr) != 0)
    {
        ErrorLog << "del fd[" << fd << "] from epoll error, sys error=" << strerror(errno);
        return;
    }
    m_fds.erase(it);                   // 从 m_fds 中删除该 fd
    DebugLog << "epoll_ctl del succ, fd[" << fd << "]";
}

void Reactor::loop() {
    assert(isLoopThread());
    if (m_is_looping) {
        // DebugLog << "this reactor is looping!";
        return;
    }
    m_is_looping = true;
    m_stop_flag = false;
    Coroutine*first_coroutine = nullptr;

    while(!m_stop_flag)
    {
        const int MaxEvents = 10;
        epoll_event re_events[MaxEvents +1];

        if(first_coroutine)
        {
            //切换到当前协程执行，继续处理上次未完成的任务
            tinyrpc::Coroutine::Resume(first_coroutine);
            first_coroutine = nullptr;
        }
        
        if(m_reactor_type != MainReactor)
        {
            // 在子 Reactor 中，处理定时器事件
            FdEvent* ptr = nullptr;
            while(1)
            {
                ptr = CoroutineTaskQueue::GetCoroutineTaskQueue()->pop();
                if(ptr)                                 // 如果有待处理的协程事件，则将其所属 Reactor 设置为当前 Reactor，并切换到对应的协程执行
                {
                    // 将协程事件的所属 Reactor 设置为当前 Reactor，以便在事件被触发时能够正确调度执行对应的协程
                    ptr->setReactor(this);
                    tinyrpc::Coroutine::Resume(ptr->getCoroutine());
                }
                else                                    // 如果没有待处理的协程事件，则跳出循环，继续执行 epoll_wait 等待新的事件到来
                {
                    break;
                }
            }
        }

        //主 Reactor 和子 Reactor 都需要处理定时器事件，主 Reactor 还需要处理 accept 事件
        Mutex::Lock lock(m_mutex);
        std::vector<std::function<void()>> tmp_tasks;
        tmp_tasks.swap(m_pending_tasks);          //m_pending_tasks 被清空，其他线程可以立即继续添加新任务,而 tmp_tasks 中保存了当前需要处理的任务列表，避免在处理任务时持有锁，提升性能和并发性
        lock.unlock();

        for(size_t i = 0; i < tmp_tasks.size(); ++i)
        {
            if(tmp_tasks[i])
            {
                tmp_tasks[i]();                   // 执行所有待处理的任务，这些任务可能是异步投递的回调任务，也可能是定时器事件的回调任务
            }
        }

        int rt = epoll_wait(m_epfd, re_events, MaxEvents, t_max_epoll_timeout);   // 等待 epoll 事件的发生，返回就绪事件的数量
        if(rt < 0)
        {
            ErrorLog << "epoll_wait error, skip, errno=" << strerror(errno);
        }
        else
        {
            for(int i =0;i<rt;++i)
            {
                epoll_event one_event =re_events[i];
                if(one_event.data.fd == m_wake_fd && (one_event.events & READ))  // 如果事件是唤醒事件，则需要读取 eventfd 中的数据，以清除事件并准备下一次唤醒
                {
                    char buf[8];
                    while(1)
                    {
                        if((g_sys_read_fun(m_wake_fd, buf, 8) == -1) && errno == EAGAIN)// 读取 eventfd 中的数据，清除触发事件的状态，以便下一次触发能够正确响应
                        {
                            break;
                        }
                    }
                }
                else
                {
                    tinyrpc::FdEvent* ptr = (tinyrpc::FdEvent*)one_event.data.ptr;  // 如果事件不是唤醒事件，则说明是其他 fd 的事件，此时需要将其转换为 FdEvent 对象，并调用其 handleEvent 方法来处理该事件
                    if(ptr != nullptr)
                    {
                        int fd= ptr->getFd();                   // 获取事件对应的 fd，以便在 handleEvent 中能够正确处理该事件
                        if(!(one_event.events & EPOLLIN) && (!(one_event.events & EPOLLOUT)))  // 如果事件既不是可读事件也不是可写事件，则说明该事件可能是错误事件
                        {
                            ErrorLog << "socket [" << fd << "] occur other unknow event:[" << one_event.events << "], need unregister this socket";
                            delEventInLoopThread(fd);           // 从 epoll 中删除该 fd，以避免后续事件的触发和处理
                        }
                        else                                    //有效事件
                        {
                            if(ptr->getCoroutine())             //
                            {
                                //如果该事件关联了协程，则将其所属 Reactor 设置为当前 Reactor，并切换到对应的协程执行
                                if(!first_coroutine)
                                {
                                    first_coroutine = ptr->getCoroutine();
                                    continue;   // 直接切换到该协程执行，继续处理上次未完成的任务，避免将该事件添加到协程任务队列中等待调度执行，以提升性能和响应速度
                                }
                                if(m_reactor_type == SubReactor)        // 如果当前 Reactor 是子 Reactor，则需要将该事件添加到协程任务队列中，等待所属 Reactor 调度执行对应的协程
                                {
                                    delEventInLoopThread(fd);           // 从 epoll 中删除该 fd，以避免后续事件的触发和处理，因为该事件将由协程来处理，协程会在处理完成后重新添加该 fd 到 epoll 中
                                    ptr->setReactor(NULL);              // 将事件的所属 Reactor 设置为 NULL，以避免在协程处理该事件时再次触发该事件，导致重复处理和性能损失
                                    CoroutineTaskQueue::GetCoroutineTaskQueue()->push(ptr);  // 将该事件添加到协程任务队列中，等待所属 Reactor 调度执行对应的协程
                                }
                                else
                                {
                                    //如果当前 Reactor 是主 Reactor
                                    tinyrpc::Coroutine::Resume(ptr->getCoroutine());  // 直接切换到该协程执行，继续处理上次未完成的任务
                                    if(first_coroutine)                               // 如果 first_coroutine 不为空，说明当前事件循环正在处理一个关联了协程的事件，此时需要将 first_coroutine 置空，以便下一次事件循环能够正确处理新的事件
                                    {
                                        first_coroutine = nullptr;    
                                    }
                                }
                            }
                            else
                            {
                                //如果该事件没有关联协程,则直接调用其可读回调函数和可写回调函数来处理该事件，以便及时响应和处理该事件
                                std::function<void()> read_cb;
                                std::function<void()> write_cb;
                                read_cb = ptr->getCallBack(READ);
                                write_cb = ptr->getCallBack(WRITE);

                                if(fd == m_timer_fd)  
                                {
                                    read_cb();    // 如果事件对应的 fd 是定时器 fd，则说明该事件是定时器事件，此时需要调用可读回调函数来处理该事件
                                    continue;      
                                }
                                if(one_event.events & EPOLLIN)  // 如果事件是可读事件，则调用可读回调函数来处理该事件
                                {
                                    Mutex::Lock lock(m_mutex);
                                    m_pending_tasks.push_back(read_cb);		
                                }
                                if(one_event.events & EPOLLOUT) // 如果事件是可写事件，则调用可写回调函数来处理该事件
                                {
                                    Mutex::Lock lock(m_mutex);
                                    m_pending_tasks.push_back(write_cb);
                                }
                            }
                        }
                    }
                }
            }
            // 处理完所有就绪事件后，批量处理待添加的 fd 和待删除的 fd，以减少 epoll_ctl 调用次数，提升性能
            std::map<int, epoll_event> tmp_add;
            std::vector<int> tmp_del;
            {
                Mutex::Lock lock(m_mutex);
                tmp_add.swap(m_pending_add_fds);           // 将待添加的 fd 和对应事件从 m_pending_add_fds 中交换到 tmp_add 中，以便在不持有锁的情况下批量处理这些 fd 和事件
                m_pending_add_fds.clear();                        // 清空 m_pending_add_fds 中的内容，以便其他线程可以继续添加新的待处理 fd 和事件
                tmp_del.swap(m_pending_del_fds);           // 将待删除的 fd 从 m_pending_del_fds 中交换到 tmp_del 中，以便在不持有锁的情况下批
                m_pending_del_fds.clear();                        // 清空 m_pending_del_fds 中的内容，以便其他线程可以继续添加新的待处理 fd
            }

            for(auto i = tmp_add.begin(); i != tmp_add.end(); ++i)  // 批量处理待添加的 fd 和事件，将它们添加到 epoll 中，以便在下一次事件循环中能够正确响应这些事件
            {
                addEventInLoopThread((*i).first, (*i).second);
            }
            for(auto i = tmp_del.begin(); i != tmp_del.end(); ++i)
            {
                delEventInLoopThread(*i);
            }
        }
    }
    //loop循环结束，将 m_is_looping 置为 false，以便其他线程能够正确判断事件循环的状态，并及时响应停止请求
    DebugLog << "reactor loop end";
    m_is_looping = false;

}







//停止 Reactor 的事件循环，设置停止标志位，并唤醒事件循环以便及时响应停止请求
void Reactor::stop() {
  if (!m_stop_flag && m_is_looping) {
    m_stop_flag = true;
    wakeup();
  }
}

// 添加一个待处理的任务到任务队列中，如果 is_wakeup 参数为 true，则在添加任务后立即唤醒事件循环，以便及时执行该任务
void Reactor::addTask(std::function<void()> task, bool is_wakeup /*=true*/) {

    Mutex::Lock lock(m_mutex);
    m_pending_tasks.push_back(task);
    if (is_wakeup) {
        wakeup();
    }
}

// 添加多个待处理的任务到任务队列中，如果 is_wakeup 参数为 true，则在添加任务后立即唤醒事件循环，以便及时执行这些任务
void Reactor::addTask(std::vector<std::function<void()>> task, bool is_wakeup /* =true*/) {

  if (task.size() == 0) {
    return;
  }
  else
  {
    Mutex::Lock lock(m_mutex);
    m_pending_tasks.insert(m_pending_tasks.end(), task.begin(), task.end());
  }
  if (is_wakeup) {
    wakeup();
  }
}


void Reactor::addCoroutine(tinyrpc::Coroutine::ptr cor, bool is_wakeup /*=true*/) {

  auto func = [cor](){
    tinyrpc::Coroutine::Resume(cor.get());
  };
  addTask(func, is_wakeup);
}


Timer* Reactor::getTimer() {
	if (!m_timer) {
		m_timer = new Timer(this);
		m_timer_fd = m_timer->getFd();
	}
	return m_timer;
}

pid_t Reactor::getTid() {
  return m_tid;
}

// 设置 Reactor 的类型，可以是主 Reactor（MainReactor）或子 Reactor（SubReactor），默认是子 Reactor，主 Reactor 负责监听 socket fd 的 accept 事件，
// 分发连接到子 Reactor；子 Reactor 每个 io 线程都是这种类型，负责处理已分发的连接的 IO,通过设置 Reactor 类型可以区分不同类型的 Reactor 在事件循环中的行为和职责
void Reactor::setReactorType(ReactorType type) {
  m_reactor_type = type;
}


CoroutineTaskQueue* CoroutineTaskQueue::GetCoroutineTaskQueue() {
  if (t_couroutine_task_queue) {
    return t_couroutine_task_queue;
  }
  t_couroutine_task_queue = new CoroutineTaskQueue();
  return t_couroutine_task_queue;

}

void CoroutineTaskQueue::push(FdEvent* cor) {
  Mutex::Lock lock(m_mutex);
  m_task.push(cor);
  lock.unlock();
}

FdEvent* CoroutineTaskQueue::pop() {
  FdEvent* re = nullptr;
  Mutex::Lock lock(m_mutex);
  if (m_task.size() >= 1) {
    re = m_task.front();
    m_task.pop();
  }
  lock.unlock();

  return re;
}













}
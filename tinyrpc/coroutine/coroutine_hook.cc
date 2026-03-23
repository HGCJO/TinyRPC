#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "tinyrpc/coroutine/coroutine_hook.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/timer.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/config.h"

//比如 name=accept 时，变成 accept_fun_ptr_t
#define HOOK_SYS_FUNC(name) name##_fun_ptr_t g_sys_##name##_fun = (name##_fun_ptr_t)dlsym(RTLD_NEXT, #name);

//保存系统原生系统调用的地址
HOOK_SYS_FUNC(accept);
HOOK_SYS_FUNC(read);
HOOK_SYS_FUNC(write);
HOOK_SYS_FUNC(connect);
HOOK_SYS_FUNC(sleep);

namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

static bool g_hook = true;


//全局协程钩子开关的设置接口,当 value=true 时，启用协程钩子功能，替换系统调用为协程非阻塞版本；当 value=false 时，禁用协程钩子功能，恢复系统调用的原生行为。
void SetHook(bool value) {
	g_hook = value;
}




//把 fd_event 注册到 epoll 中，监听指定的事件（READ/WRITE），当事件发生时，协程调度器会被触发，切换到对应的协程继续执行。
void toEpoll(tinyrpc::FdEvent::ptr fd_event, int events) {
	
	tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine() ;
    //读事件
	if (events & tinyrpc::IOEvent::READ) {
		DebugLog << "fd:[" << fd_event->getFd() << "], register read event to epoll";
        //将当前协程与 fd 的读事件绑定。当 epoll 检测到该 fd 有可读数据时，协程调度器能通过fd_event找到对应的cur_cor，并切换到这个协程继续执行，处理读事件。
		fd_event->setCoroutine(cur_cor);
		fd_event->addListenEvents(tinyrpc::IOEvent::READ);
	}
    //写事件
	if (events & tinyrpc::IOEvent::WRITE) {
		DebugLog << "fd:[" << fd_event->getFd() << "], register write event to epoll";


		fd_event->setCoroutine(cur_cor);
		fd_event->addListenEvents(tinyrpc::IOEvent::WRITE);
	}

}


// 协程调用 read
// 没读到数据（没有读行为）
// 把 fd_event 注册到 epoll，监听读事件
// 当前协程直接挂起（Yield），切换到别的协程
// 等 fd 有数据可读了 → epoll 触发 → 唤醒这个协程
ssize_t read_hook(int fd, void *buf, size_t count) {
	DebugLog << "this is hook read";
    // 1. 主线程/主协程直接调用系统原生read（不触发协程切换）
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        DebugLog << "hook disable, call sys read func";
        return g_sys_read_fun(fd, buf, count);
    }
    // 2. 获取当前线程的Reactor（事件循环）
    tinyrpc::Reactor::GetReactor();
        
    // 3. 获取fd对应的事件对象（管理fd的IO事件、协程绑定等）
    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(fd);
    if(fd_event->getReactor() == nullptr) {
        fd_event->setReactor(tinyrpc::Reactor::GetReactor());  
    }
    // 4. 将fd设置为非阻塞模式（核心：避免系统调用真正阻塞）
    fd_event->setNonBlock();


    // 5. 调用系统原生read函数尝试读取数据，如果能读到数据，直接返回（无需协程切换）
    ssize_t n = g_sys_read_fun(fd, buf, count);
    if (n > 0) {
        return n;
    } 
    // 6. 若没读到数据：将fd的读事件注册到epoll，绑定当前协程
    toEpoll(fd_event, tinyrpc::IOEvent::READ);

    // 7. 挂起当前协程（让出CPU，切换到其他协程执行）
    DebugLog << "read func to yield";
    tinyrpc::Coroutine::Yield();

    // 8. 协程被唤醒后：清理读事件、解绑协程
    fd_event->delListenEvents(tinyrpc::IOEvent::READ);
    fd_event->clearCoroutine();
    
    // 9. 再次调用原生read：此时fd已有可读数据，非阻塞读取
    DebugLog << "read func yield back, now to call sys read";
    return g_sys_read_fun(fd, buf, count);

}


int accept_hook(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	DebugLog << "this is hook accept";
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        DebugLog << "hook disable, call sys accept func";
        return g_sys_accept_fun(sockfd, addr, addrlen);
    }
        tinyrpc::Reactor::GetReactor();


    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(sockfd);
    if(fd_event->getReactor() == nullptr) {
        fd_event->setReactor(tinyrpc::Reactor::GetReactor());  
    }

	fd_event->setNonBlock();

    int n = g_sys_accept_fun(sockfd, addr, addrlen);
    if (n > 0) {
        return n;
    } 

	toEpoll(fd_event, tinyrpc::IOEvent::READ);
	
	DebugLog << "accept func to yield";
	tinyrpc::Coroutine::Yield();

	fd_event->delListenEvents(tinyrpc::IOEvent::READ);
	fd_event->clearCoroutine();


	DebugLog << "accept func yield back, now to call sys accept";
	return g_sys_accept_fun(sockfd, addr, addrlen);

}


ssize_t write_hook(int fd, const void *buf, size_t count) {
	DebugLog << "this is hook write";
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        DebugLog << "hook disable, call sys write func";
        return g_sys_write_fun(fd, buf, count);
    }
        tinyrpc::Reactor::GetReactor();


    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(fd);
    if(fd_event->getReactor() == nullptr) {
        fd_event->setReactor(tinyrpc::Reactor::GetReactor());  
    }

	fd_event->setNonBlock();

    ssize_t n = g_sys_write_fun(fd, buf, count);
    if (n > 0) {
        return n;
    }

	toEpoll(fd_event, tinyrpc::IOEvent::WRITE);

	DebugLog << "write func to yield";
	tinyrpc::Coroutine::Yield();

	fd_event->delListenEvents(tinyrpc::IOEvent::WRITE);
	fd_event->clearCoroutine();

	DebugLog << "write func yield back, now to call sys write";
	return g_sys_write_fun(fd, buf, count);

}


//非阻塞 connect → 等待可写事件 + 超时控制 → 协程挂起 → 事件 / 超时唤醒 → 检查结果并返回。
int connect_hook(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	DebugLog << "this is hook connect";
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        DebugLog << "hook disable, call sys connect func";
        return g_sys_connect_fun(sockfd, addr, addrlen);
    }
    tinyrpc::Reactor* reactor = tinyrpc::Reactor::GetReactor();


    tinyrpc::FdEvent::ptr fd_event = tinyrpc::FdEventContainer::GetFdContainer()->getFdEvent(sockfd);
    if(fd_event->getReactor() == nullptr) {
        fd_event->setReactor(reactor);  
    }
    tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();

        
    fd_event->setNonBlock();

    //错误判断逻辑更精细：connect函数返回0表示连接成功，返回-1且errno为EINPROGRESS表示连接正在进行中（非阻塞连接的正常情况），其他错误则直接返回错误。
    int n = g_sys_connect_fun(sockfd, addr, addrlen);
    if (n == 0) {
        DebugLog << "direct connect succ, return";
        return n;
    } else if (errno != EINPROGRESS) {
            DebugLog << "connect error and errno is't EINPROGRESS, errno=" << errno <<  ",error=" << strerror(errno);
        return n;
    }

    DebugLog << "errno == EINPROGRESS";

    //非阻塞 connect 完成时会触发写事件，把当前协程绑定到 fd_event。
    toEpoll(fd_event, tinyrpc::IOEvent::WRITE);

    bool is_timeout = false;		// 是否超时

    // 超时函数句柄,当连接超时发生时，会被定时器触发，执行这个回调函数，设置超时标志，并唤醒当前协程，让它继续执行后续的错误处理逻辑。
    auto timeout_cb = [&is_timeout, cur_cor](){
        // 设置超时标志，然后唤醒协程
        is_timeout = true;
        tinyrpc::Coroutine::Resume(cur_cor);
    };


    tinyrpc::TimerEvent::ptr event = std::make_shared<tinyrpc::TimerEvent>(gRpcConfig->m_max_connect_timeout, false, timeout_cb);   
  
    tinyrpc::Timer* timer = reactor->getTimer();  
    timer->addTimerEvent(event);

    tinyrpc::Coroutine::Yield();

	// write事件需要删除，因为连接成功后后面会重新监听该fd的写事件。
	fd_event->delListenEvents(tinyrpc::IOEvent::WRITE); 
	fd_event->clearCoroutine();
	// fd_event->updateToReactor();

	// 定时器也需要删除
	timer->delTimerEvent(event);

	n = g_sys_connect_fun(sockfd, addr, addrlen);
	if ((n < 0 && errno == EISCONN) || n == 0) {
		DebugLog << "connect succ";
		return 0;
	}

	if (is_timeout) {
    ErrorLog << "connect error,  timeout[ " << gRpcConfig->m_max_connect_timeout << "ms]";
		errno = ETIMEDOUT;
	} 

	DebugLog << "connect error and errno=" << errno <<  ", error=" << strerror(errno);
	return -1;

}


//当协程调用 sleep(seconds) 时，不会真的阻塞线程，而是将当前协程挂起，指定时间后由定时器唤醒；
// 主协程调用时则直接走系统原生 sleep。
unsigned int sleep_hook(unsigned int seconds) {

	DebugLog << "this is hook sleep";
    if (tinyrpc::Coroutine::IsMainCoroutine()) {
        DebugLog << "hook disable, call sys sleep func";
        return g_sys_sleep_fun(seconds);
    }
    //非主协程调用时，获取当前协程，并设置一个定时器事件，在指定的秒数后触发，唤醒当前协程继续执行。
	tinyrpc::Coroutine* cur_cor = tinyrpc::Coroutine::GetCurrentCoroutine();

	bool is_timeout = false;
	auto timeout_cb = [cur_cor, &is_timeout](){
		DebugLog << "onTime, now resume sleep cor";
		is_timeout = true;
		// 设置超时标志，然后唤醒协程
		tinyrpc::Coroutine::Resume(cur_cor);
    };
    
    // 创建定时器事件：延迟 seconds*1000 毫秒（转为毫秒）执行回调
    tinyrpc::TimerEvent::ptr event = std::make_shared<tinyrpc::TimerEvent>(1000 * seconds, false, timeout_cb);
    
    tinyrpc::Reactor::GetReactor()->getTimer()->addTimerEvent(event);

	DebugLog << "now to yield sleep";
	// beacuse read or wirte maybe resume this coroutine, so when this cor be resumed, must check is timeout, otherwise should yield again
	//循环挂起协程：直到超时标志为true才退出
    while (!is_timeout) {
		tinyrpc::Coroutine::Yield();
	}

	// 定时器也需要删除
	// tinyrpc::Reactor::GetReactor()->getTimer()->delTimerEvent(event);

	return 0;

}



}






extern "C" {

//重定义系统调用函数，判断全局协程钩子开关的状态，如果钩子功能未启用，则调用系统原生函数；如果钩子功能已启用，则调用协程版本的函数，实现协程的自动切换和调度。
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	if (!tinyrpc::g_hook) {
		return g_sys_accept_fun(sockfd, addr, addrlen);
	} else {
		return tinyrpc::accept_hook(sockfd, addr, addrlen);
	}
}

ssize_t read(int fd, void *buf, size_t count) {
	if (!tinyrpc::g_hook) {
		return g_sys_read_fun(fd, buf, count);
	} else {
		return tinyrpc::read_hook(fd, buf, count);
	}
}

ssize_t write(int fd, const void *buf, size_t count) {
	if (!tinyrpc::g_hook) {
		return g_sys_write_fun(fd, buf, count);
	} else {
		return tinyrpc::write_hook(fd, buf, count);
	}
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	if (!tinyrpc::g_hook) {
		return g_sys_connect_fun(sockfd, addr, addrlen);
	} else {
		return tinyrpc::connect_hook(sockfd, addr, addrlen);
	}
}

unsigned int sleep(unsigned int seconds) {
	if (!tinyrpc::g_hook) {
		return g_sys_sleep_fun(seconds);
	} else {
		return tinyrpc::sleep_hook(seconds);
	}
}




}
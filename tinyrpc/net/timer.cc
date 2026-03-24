#include <sys/timerfd.h>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <vector>
#include <sys/time.h>
#include <functional>
#include <map>
#include "../comm/log.h"
#include "timer.h"
#include "mutex.h"
#include "fd_event.h"
#include "../coroutine/coroutine_hook.h"


// Timer 构造（初始化资源）→ 业务层 addTimerEvent（添加事件）→ resetArriveTime（同步到内核）
// → 内核 timerfd 到期 → Reactor 触发 onTimer（处理到期事件）
// → ① 清理内核状态 ② 批量删除到期事件 ③ 重置重复事件并重新入队 ④ 执行业务回调 ⑤ 再次调用 resetArriveTime（校准定时器）
// → 循环等待下一次事件触发 / 业务层 delTimerEvent（主动取消事件）
// → Timer 析构（释放资源）
//指向系统 read 函数的函数指针变量
extern read_fun_ptr_t g_sys_read_fun;  // sys read func

namespace tinyrpc {

//获取当前事件
int64_t getNowMs() {
  timeval val;
  gettimeofday(&val, nullptr);
  int64_t re = val.tv_sec * 1000 + val.tv_usec / 1000;
  return re;
}

Timer::Timer(Reactor* reactor) : FdEvent(reactor) {
    //基于 Linux 的timerfd机制实现定时器功能
    m_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);
    DebugLog << "m_timer fd = " << m_fd;
    if (m_fd == -1) {
        DebugLog << "timerfd_create error";  
    }
    
    //绑定定时器触发后的回调函数
    m_read_callback = std::bind(&Timer::onTimer, this);
    //注册监听timerfd的READ事件
    addListenEvents(READ);
    // updateToReactor();

}


Timer::~Timer() {
    unregisterFromReactor();
	close(m_fd);
}

//
void Timer::addTimerEvent(TimerEvent::ptr event, bool need_reset /*=true*/) {

  RWMutex::WriteLock lock(m_event_mutex);
  bool is_reset = false;
  if (m_pending_events.empty()) {
    is_reset = true;
  } else {
    //根据时间取出第一个定时器事件
    auto it = m_pending_events.begin();
    //如果新事件的触发时间比当前最早的事件更早，则需要重置
    if (event->m_arrive_time < (*it).second->m_arrive_time) {
      is_reset = true;
    }
  }
  //按时间添加新事件到容器
  m_pending_events.emplace(event->m_arrive_time, event);
  lock.unlock();
  //修改 Linux timerfd 的触发时间，让内核定时器按新的最早事件时间触发
  if (is_reset && need_reset) {
    DebugLog << "need reset timer";
    resetArriveTime();
  }
  // DebugLog << "add timer event succ";
}


void Timer::delTimerEvent(TimerEvent::ptr event) {
  event->m_is_cancled = true;

  RWMutex::WriteLock lock(m_event_mutex);
  //找到第一个大于等于 event->m_arrive_time（事件触发时间）的迭代器
  auto begin = m_pending_events.lower_bound(event->m_arrive_time);
  //找到第一个大于 event->m_arrive_time 的迭代器，作为查找范围的结束
  auto end = m_pending_events.upper_bound(event->m_arrive_time);
  auto it = begin;
  for (it = begin; it != end; it++) {
    if (it->second == event) {
      DebugLog << "find timer event, now delete it. src arrive time=" << event->m_arrive_time;
      break;
    }
  }
  if (it != m_pending_events.end()) {
    //找到目标事件,并删除
    m_pending_events.erase(it);
  }
  lock.unlock();
  DebugLog << "del timer event succ, origin arrvite time=" << event->m_arrive_time;
}

//重置定时器的触发时间，通常在添加或删除事件后调用，以确保定时器按新的最早事件时间触发
void Timer::resetArriveTime() {

    RWMutex::ReadLock lock(m_event_mutex);
    std::multimap<int64_t,TimerEvent::ptr> tmp = m_pending_events;
    lock.unlock();

    if(tmp.size() == 0) {
        DebugLog << "no timer event, skip reset";
        return;
    }

    int64_t now = getNowMs();
    //取出最早的事件
    auto it = tmp.rbegin();
    if((*it).first< now){           //如果最早事件的触发时间已经过了，则说明所有事件都过期了
        DebugLog<< "first timer events has already expire";
        return;
    }
    //计算最早事件的触发时间与当前时间的差值，作为新的定时器触发时间
    int64_t interval = (*it).first - now;

    //将新的触发时间设置到 Linux timerfd 中，让内核定时器按新的最早事件时间触发
    itimerspec new_value;
    memset(&new_value, 0, sizeof(new_value));
    //设定定时器的初始触发时间（it_value）为 interval 毫秒后，单位转换为秒和纳秒
    timespec ts;
    memset(&ts, 0, sizeof(ts));

    ts.tv_sec = interval / 1000;
    ts.tv_nsec = (interval % 1000) * 1000000;
    //首次触发事件设置为ts，后续触发事件设置为0（非重复定时器）
    new_value.it_value = ts;
    int rt = timerfd_settime(m_fd, 0, &new_value, nullptr);

    if (rt != 0) {
        ErrorLog << "tiemr_settime error, interval=" << interval;
    } else {
        // DebugLog << "reset timer succ, next occur time=" << (*it).first;
    }

}

//定时器触发时的回调函数，负责处理所有已到期的定时器事件，并执行对应的任务回调函数
void Timer::onTimer() {

  // DebugLog << "onTimer, first read data";
  char buf[8];
  while(1) {
    //读取 timerfd 中的数据，清除触发事件的状态，以便下一次触发能够正确响应
    if((g_sys_read_fun(m_fd, buf, 8) == -1) && errno == EAGAIN) {
      break;
    }
  }
  //遍历并收集所有已到期且未被取消的定时器事件
    int64_t now = getNowMs();
    RWMutex::WriteLock lock(m_event_mutex);
    auto it = m_pending_events.begin();
    std::vector<TimerEvent::ptr> tmps;
    std::vector<std::pair<int64_t, std::function<void()>>> tasks;                       //存储所有已到期事件的任务回调函数，按触发时间排序

    for (it = m_pending_events.begin(); it != m_pending_events.end(); ++it) {
        if ((*it).first <= now && !((*it).second->m_is_cancled))                        //如果事件的触发时间已经到达且未被取消，则将该事件添加到待处理列表中
        {
            tmps.push_back((*it).second);
            tasks.push_back(std::make_pair((*it).second->m_arrive_time, (*it).second->m_task));
		}	
        else 
        {
			break;
		}
	}
    //从容器中删除所有已到期的事件,删除的是区间 [begin, it)，即从第一个事件到第一个未到期事件之间的所有事件
	m_pending_events.erase(m_pending_events.begin(), it);
    lock.unlock();
    //对于所有已到期的事件，如果是重复定时器，则重置其触发时间并重新添加到容器中；如果是非重复定时器，则直接执行其任务回调函数
	for (auto i = tmps.begin(); i != tmps.end(); ++i) {
    // DebugLog << "excute timer event on " << (*i)->m_arrive_time;
		if ((*i)->m_is_repeated) {
			(*i)->resetTime();
			addTimerEvent(*i, false);
		}
	}

	resetArriveTime();

	//执行所有已到期事件的任务回调函数，按触发时间排序执行
    for (auto i : tasks) {
    // DebugLog << "excute timeevent:" << i.first;
        i.second();
    }
}






}
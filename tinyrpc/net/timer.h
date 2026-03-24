#ifndef TINYRPC_NET_TIMER_H
#define TINYRPC_NET_TIMER_H

#include <time.h>
#include <memory>
#include <map>
#include <functional>
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/net/reactor.h"
#include "tinyrpc/net/fd_event.h"
#include "tinyrpc/comm/log.h"


namespace tinyrpc {

int64_t getNowMs();
class TimerEvent {
public:

    typedef std::shared_ptr<TimerEvent> ptr;
    TimerEvent(int64_t interval, bool is_repeated, std::function<void()>task) : m_interval(interval), m_is_repeated(is_repeated), m_task(task) 
    {
        m_arrive_time = getNowMs() + m_interval;  	
        DebugLog << "timeevent will occur at " << m_arrive_time;
    }

    void resetTime() {
        
        m_arrive_time = getNowMs() + m_interval;  	
        m_is_cancled = false;
    }
    void wake() {
         m_is_cancled = false;
    }

    void cancle () {
        m_is_cancled = true;
    }

    void cancleRepeated () {
        m_is_repeated = false;
  }
public:
    int64_t m_arrive_time;                  // 定时器事件的触发时间（毫秒级时间戳）
    int64_t m_interval;                     // 重复定时器的时间间隔（毫秒）,非重复定时器该值为0
    bool m_is_repeated {false};             // 标记是否为重复触发的定时器
    bool m_is_cancled {false};              // 标记定时器是否被取消
    std::function<void()> m_task;           // 定时器触发时需要执行的任务回调函数


};


class FdEvent;

class Timer : public tinyrpc::FdEvent {

 public:

  typedef std::shared_ptr<Timer> ptr;
  
  Timer(Reactor* reactor);

	~Timer();

	void addTimerEvent(TimerEvent::ptr event, bool need_reset = true);

	void delTimerEvent(TimerEvent::ptr event);

	void resetArriveTime();

    void onTimer();

 private:

 	std::multimap<int64_t, TimerEvent::ptr> m_pending_events;       //有序的键值对容器,管理所有待触发的定时器事件
    RWMutex m_event_mutex;


};
}
#endif
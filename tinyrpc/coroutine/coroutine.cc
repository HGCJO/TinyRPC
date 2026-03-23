#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <atomic>
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/run_time.h"


namespace tinyrpc {

//thread_local关键字用于声明线程局部变量，即每个线程都有自己独立的变量副本，互不干扰。
static thread_local Coroutine* t_main_coroutine = nullptr;  // 线程局部变量，指向当前线程的主协程

static thread_local Coroutine* t_cur_coroutine = NULL;

static thread_local RunTime* t_cur_run_time = NULL;

static std::atomic_int t_coroutine_count {0};

static std::atomic_int t_cur_coroutine_id {1};


int getCoroutineIndex() {
  return t_cur_coroutine_id;
}

RunTime* getCurrentRunTime() {
  return t_cur_run_time;
}

void setCurrentRunTime(RunTime* v) {
  t_cur_run_time = v;
}

// 协程函数，执行协程的回调函数，并设置协程函数执行状态标记
void CoFunction(Coroutine* co) {
    if (co != nullptr) {
        co->setIsInCoFunc(true);

        // 执行协程的回调函数
        co->m_call_back();

        co->setIsInCoFunc(false);
        
    }

}
// 协程构造函数，初始化协程上下文和运行时信息
Coroutine::Coroutine() {
  // main coroutine'id is 0
  m_cor_id = 0;
  t_coroutine_count++;
  memset(&m_coctx, 0, sizeof(m_coctx));
  t_cur_coroutine = this;
  // DebugLog << "coroutine[" << m_cor_id << "] create";
}



Coroutine::Coroutine(int size, char* stack_ptr) : m_stack_size(size), m_stack_sp(stack_ptr) {

    assert(stack_ptr);

    if(!t_main_coroutine)
    {
        t_main_coroutine = new Coroutine();
    }


    m_cor_id = t_cur_coroutine_id++;
    t_coroutine_count++;
}

Coroutine::Coroutine(int size, char* stack_ptr, std::function<void()> cb)
  : m_stack_size(size), m_stack_sp(stack_ptr) {

    assert(stack_ptr);

    if(!t_main_coroutine)
    {
        t_main_coroutine = new Coroutine();
    }

    setCallBack(cb);
    m_cor_id = t_cur_coroutine_id++;
    t_coroutine_count++;
} 

//给非主协程设置「要执行的任务函数（回调函数）」，并且在设置的同时，初始化协程的运行上下文（栈、寄存器等），让这个协程具备「可以被调度执行（resume）」的条件。
bool Coroutine::setCallBack(std::function<void()> cb) {
    //this 是协程实例，而不是类
    if(this == t_main_coroutine)
    {
        ErrorLog<<"main coroutine can't set callback";
        return false;
    }
    if (m_is_in_cofunc) {
        ErrorLog << "this coroutine is in CoFunction";
        return false;
    }

    m_call_back = cb;
    //计算协程栈的栈顶地址（做16字节对齐，CPU访问更高效）
    char* top = m_stack_sp + m_stack_size;
    top = reinterpret_cast<char*>((reinterpret_cast<unsigned long>(top)) & -16LL);

    //空旧的协程上下文（防止脏数据）
    memset(&m_coctx, 0, sizeof(m_coctx));

    // 初始化协程的上下文（寄存器），核心中的核心
    m_coctx.regs[kRSP] = top;                                        // 设置栈指针（RSP）：指向栈顶
    m_coctx.regs[kRBP] = top;                                        // 设置基指针（RBP）：指向栈顶，协程函数执行时会使用这个寄存器来访问局部变量和函数参数
    m_coctx.regs[kRETAddr] = reinterpret_cast<char*>(CoFunction);    // 设置返回地址：指向CoFunction函数
    m_coctx.regs[kRDI] = reinterpret_cast<char*>(this);              //设置第一个参数（RDI）：把当前协程对象的指针传给CoFunction

    m_can_resume = true;

    return true;
}

Coroutine::~Coroutine() {
  t_coroutine_count--;
  // DebugLog << "coroutine[" << m_cor_id << "] die";
}


Coroutine* Coroutine::GetCurrentCoroutine() {
  if (t_cur_coroutine == nullptr) {
    t_main_coroutine = new Coroutine();
    t_cur_coroutine = t_main_coroutine;
  }
  return t_cur_coroutine;
}

Coroutine* Coroutine::GetMainCoroutine() {
  if (t_main_coroutine) {
    return t_main_coroutine;
  }
  t_main_coroutine = new Coroutine();
  return t_main_coroutine;
}

bool Coroutine::IsMainCoroutine() {
  if (t_main_coroutine == nullptr || t_cur_coroutine == t_main_coroutine) {
    return true;
  }
  return false;
}

//将当前运行的非主协程暂停，切换回主协程继续执行，当前协程进入等待状态，直到被调度器或其他协程恢复执行（resume）
void Coroutine::Yield() {
    // if (!t_enable_coroutine_swap) {
    //   ErrorLog << "can't yield, because disable coroutine swap";
    //   return;
    // }
    if (t_main_coroutine == nullptr) {
        ErrorLog << "main coroutine is nullptr";
        return;
    }

    if (t_cur_coroutine == t_main_coroutine) {
        ErrorLog << "current coroutine is main coroutine";
        return;
    }
    Coroutine* co = t_cur_coroutine;
    t_cur_coroutine = t_main_coroutine;
    t_cur_run_time = NULL;


    //将 CPU 执行流从当前子协程切换到主协程。
    coctx_swap(&(co->m_coctx), &(t_main_coroutine->m_coctx));   // 通过调用底层的 coctx_swap 函数，保存当前子协程的上下文（寄存器状态、栈信息等）到 co->m_coctx 中，
                                                                //并从主协程的上下文（t_main_coroutine->m_coctx）中恢复寄存器状态和栈信息，切换到主协程继续执行。
}

//将 CPU 执行流从当前的主协程切换到目标子协程（co），恢复目标子协程的执行
void Coroutine::Resume(Coroutine* co) {
  if (t_cur_coroutine != t_main_coroutine) {
    ErrorLog << "swap error, current coroutine must be main coroutine";
    return;
  }

  if (!t_main_coroutine) {
    ErrorLog << "main coroutine is nullptr";
    return;
  }
  if (!co || !co->m_can_resume) {
    ErrorLog << "pending coroutine is nullptr or can_resume is false";
    return;
  }

  if (t_cur_coroutine == co) {
    DebugLog << "current coroutine is pending cor, need't swap";
    return;
  }

  t_cur_coroutine = co;
  t_cur_run_time = co->getRunTime();

  coctx_swap(&(t_main_coroutine->m_coctx), &(co->m_coctx));
  // DebugLog << "swap back";

}

}
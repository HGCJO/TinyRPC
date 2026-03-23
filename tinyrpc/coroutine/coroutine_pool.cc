#include <vector>
#include <sys/mman.h>
#include "tinyrpc/comm/config.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/coroutine_pool.h"
#include "tinyrpc/coroutine/coroutine.h"
#include "tinyrpc/net/mutex.h"


namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

static CoroutinePool* t_coroutine_container_ptr = nullptr; 


CoroutinePool* getCoroutinePool()
{

if (!t_coroutine_container_ptr) {
    t_coroutine_container_ptr = new CoroutinePool(gRpcConfig->m_cor_pool_size, gRpcConfig->m_cor_stack_size);
  }
  return t_coroutine_container_ptr;
}


//预创建指定数量的协程，并为这些协程分配内存栈空间。
CoroutinePool::CoroutinePool(int pool_size, int stack_size /*= 1024 * 128 B*/) : m_pool_size(pool_size), m_stack_size(stack_size) {


    Coroutine::GetCurrentCoroutine();
    //创建内存池，为当前协程池分配了一块连续的内存区域，切分成 pool_size 个块，每个块大小为 stack_size，供协程栈使用
    m_memory_pool.push_back(std::make_shared<Memory>(stack_size, pool_size));

    Memory::ptr tmp = m_memory_pool[0];
    //循环创建协程并加入空闲队列，每个协程使用内存池中的一个块作为栈空间，并将协程对象和其可调度状态（初始为 false）一起存储在 m_free_cors 中，供后续调度器分配和管理
    for (int i = 0; i < pool_size; ++i) {
        Coroutine::ptr cor = std::make_shared<Coroutine>(stack_size, tmp->getBlock());
        cor->setIndex(i);
        m_free_cors.push_back(std::make_pair(cor, false));
    }



}



CoroutinePool::~CoroutinePool() {

}

Coroutine::ptr CoroutinePool::getCoroutineInstanse() {


    // 从下标0开始查找第一个满足以下条件的空闲协程：1. 其第二个元素值为false；2. getIsInCoFunc()返回false
    // 尽可能复用已经使用过的协程，尽量不选择从未使用过的协程
    // 原因是：已经使用过的协程已经向物理内存中写入了数据，
    // 而未使用过的协程还没有分配物理内存——我们仅通过mmap获取了虚拟地址，但尚未执行写操作。
    // 因此当我们真正执行写操作时，Linux 内核才会分配物理内存，这会引发页错误中断（page fault interrupt）

    Mutex::Lock lock(m_mutex);
    for (int i = 0; i < m_pool_size; ++i) {
        //协程不在执行协程函数中,协程未被其他线程分配
        if (!m_free_cors[i].first->getIsInCoFunc() && !m_free_cors[i].second) {
        m_free_cors[i].second = true;
        Coroutine::ptr cor = m_free_cors[i].first;
        lock.unlock();
        return cor;
        }
    }
    //当预创建的协程全部被占用时，尝试从扩展的内存池中找空闲内存块创建协程
    for (size_t i = 1; i < m_memory_pool.size(); ++i) {
        char* tmp = m_memory_pool[i]->getBlock();
        if(tmp) {
        Coroutine::ptr cor = std::make_shared<Coroutine>(m_stack_size, tmp);
        return cor;
        }    
    }
    //当所有内存池都无空闲块时，动态创建新的内存池，
    m_memory_pool.push_back(std::make_shared<Memory>(m_stack_size, m_pool_size));
    return std::make_shared<Coroutine>(m_stack_size, m_memory_pool[m_memory_pool.size() - 1]->getBlock());
}


//把用完的协程还给协程池，让它可以被再次复用。
void CoroutinePool::returnCoroutine(Coroutine::ptr cor) {
  int i = cor->getIndex();
  if (i >= 0 && i < m_pool_size) {
    m_free_cors[i].second = false;
  } else {
    for (size_t i = 1; i < m_memory_pool.size(); ++i) {
      if (m_memory_pool[i]->hasBlock(cor->getStackPtr())) {
        //释放这块内存，让别人可以继续用。
        m_memory_pool[i]->backBlock(cor->getStackPtr());
      }
    }
  }
}












}
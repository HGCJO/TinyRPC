#ifndef TINYRPC_COROUTINE_MEMORY_H
#define TINYRPC_COROUTINE_MEMORY_H

#include <memory>
#include <atomic>
#include <vector>
#include "tinyrpc/net/mutex.h"

namespace tinyrpc {
//固定大小的内存池，专门用来批量申请一大块内存，然后切成小块反复分配、回收
class Memory {
 public:
  typedef std::shared_ptr<Memory> ptr;

  Memory(int block_size, int block_count);

  // void free();

  ~Memory();

  // 获取当前内存块的引用计数
  int getRefCount();

  
  char* getStart();         // 获取内存池的起始地址

  char* getEnd();

  char* getBlock();       // 从内存池中分配一个内存块，返回该块的地址

  void backBlock(char* s);   // 将一个内存块归还给内存池，参数 s 是要归还的内存块的地址

  bool hasBlock(char* s);   // 检查内存池中是否包含地址 s 对应的内存块，返回 true 表示包含，false 表示不包含

 private:
  int m_block_size {0};
  int m_block_count {0};

  int m_size {0};
  char* m_start {NULL};
  char* m_end {NULL};

  std::atomic<int> m_ref_counts {0};    // 内存块的引用计数，表示当前有多少个内存块被分配出去但尚未归还
  std::vector<bool> m_blocks;   // 内存块的使用状态，true 表示对应的内存块已被分配出去，false 表示对应的内存块当前可用
  Mutex m_mutex;

};

}

#endif
#include <memory>
#include <sys/mman.h>
#include <assert.h>
#include <stdlib.h>
#include "tinyrpc/comm/log.h"
#include "tinyrpc/coroutine/memory.h"

namespace tinyrpc {

Memory::Memory(int block_size, int block_count) : m_block_size(block_size), m_block_count(block_count) {
    m_size = m_block_count * m_block_size;
    m_start = (char*)malloc(m_size);
    //
    assert(m_start != (void*)-1);                           // mmap 失败会返回 (void*)-1
    InfoLog << "succ mmap " << m_size << " bytes memory";
    m_end = m_start + m_size - 1;
    m_blocks.resize(m_block_count);
    for (size_t i = 0; i < m_blocks.size(); ++i) {
        m_blocks[i] = false;
    }
    m_ref_counts = 0;
}



Memory::~Memory() {
  if (!m_start || m_start == (void*)-1) {
    return;
  }
  free(m_start);
  InfoLog << "~succ free munmap " << m_size << " bytes memory";
  m_start = NULL;
  m_ref_counts = 0;
}


char* Memory::getStart() {
  return m_start;
}

char* Memory::getEnd() {
  return m_end;
}

int Memory::getRefCount() {
  return m_ref_counts;
}

//从预先分配的内存池中分配一个空闲的内存块，并返回该内存块的起始地址
char* Memory::getBlock() {
  int t = -1;
  Mutex::Lock lock(m_mutex);
  for (size_t i = 0; i < m_blocks.size(); ++i) {
    if (m_blocks[i] == false) {
      m_blocks[i] = true;  
      t = i;
      break;
    }
  }
  lock.unlock();
  //检查是否找到空闲块：未找到则返回NULL
  if (t == -1) {
    return NULL;
  }
  m_ref_counts++;
  //起始地址 = 内存池基地址 + 块下标 * 单个块大小
  return m_start + (t * m_block_size);
}



bool Memory::hasBlock(char* s) {
  return ((s >= m_start) && (s <= m_end));
}

// 归还一块内存到内存池
void Memory::backBlock(char* s) {
    if (!hasBlock(s)) {
        ErrorLog << "backBlock error: ptr not in this memory pool";
        return;
    }

    Mutex::Lock lock(m_mutex);

    // 计算这个指针属于第几个块
    int index = (s - m_start) / m_block_size;

    if (index < 0 || index >= m_block_count) {
        ErrorLog << "backBlock error: invalid index";
        return;
    }

    // 标记为空闲
    m_blocks[index] = false;
    // 引用计数 -1
    m_ref_counts--;
}

}
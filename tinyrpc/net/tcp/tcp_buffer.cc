#include <unistd.h>
#include <string.h>
#include "tinyrpc/net/tcp/tcp_buffer.h"
#include "tinyrpc/comm/log.h"

namespace tinyrpc{

TcpBuffer::TcpBuffer(int size)
{
    m_buffer.resize(size);

}

TcpBuffer::~TcpBuffer()
{

}


int TcpBuffer::readAble()                   // 获取可读数据的字节数，即当前缓冲区中已经写入但尚未读取的数据量，通常通过计算写入索引和读取索引之间的差值来实现
{
    return m_write_index - m_read_index;    // 可读数据的字节数等于写入索引和读取索引之间的差值
}

int TcpBuffer::writeAble()
{
    return m_buffer.size() - m_write_index;          // 可写数据的字节数等于缓冲区总大小和写入索引之间的差值
}

int TcpBuffer::readIndex() const
{
    return m_read_index;                    // 获取当前读取索引的位置，即下一个要读取的数据在缓冲区中的位置，通常通过读取索引变量来实现
}

int TcpBuffer::writeIndex() const
{
    return m_write_index;                   // 获取当前写入索引的位置，即下一个要写入的数据在缓冲区中的位置，通常通过写入索引变量来实现
}

void TcpBuffer::resizeBuffer(int size)      //
{
    std::vector<char> tmp(size);
    int c= std::min(size,readAble());           // 计算新的缓冲区大小和当前可读数据量之间的最小值，确保在调整缓冲区大小时不会丢失数据           
    memcpy(&tmp[0],&m_buffer[m_read_index],c);// 将当前缓冲区中可读的数据复制到新的缓冲区中

    m_buffer.swap(tmp);             // 将新的缓冲区与当前缓冲区交换，使当前缓冲区成为新的缓冲区，旧的缓冲区会被自动释放
    m_read_index = 0;
    m_write_index = m_read_index + c;
}


void TcpBuffer::writeToBuffer(const char* buf,int size)         //buf:要写入的数据的指针，size:要写入的数据的字节数
{
    if(size > writeAble())                                    // 如果要写入的数据大小超过当前缓冲区的可写空间
    {
        int new_size = (int)(1.5*(m_write_index +size));    // 计算新的缓冲区大小，通常是当前写入索引加上要写入的数据大小的 1.5 倍，以减少频繁调整缓冲区大小的次数，提升性能
        resizeBuffer(new_size);                             
    }
    memcpy(&m_buffer[m_write_index],buf,size);             // 将要写入的数据复制到缓冲区中，从当前写入索引的位置开始
    m_write_index += size;                                  // 更新写入索引
}


void TcpBuffer::readFromBuffer(std::vector<char>& re,int size)   // re:用于存储读取数据的 vector，size:要读取的数据的字节数
{
    if(readAble == 0)
    {
        DebugLog << "read buffer empty!";
        return;
    }
    int read_size = readAble() > size ? size: readAble();    // 计算实际要读取的数据大小，通常是当前可读数据量和要读取的数据大小之间的最小值，以避免读取超过可读数据量的情况
    std::vector<char> tmp(read_size);                      
    memcpy(&tmp[0],&m_buffer[m_read_index],read_size);   // 将要读取的数据从缓冲区中复制到临时 vector 中，从当前读取索引
    re.swap(tmp);                                       // 将临时 vector 与传入的 vector 交换，使传入的 vector 成为要读取的数据，临时 vector 会被自动释放
    m_read_index += read_size;                          
    adjustBuffer();                                    

}


void TcpBuffer::adjustBuffer()
{
    if(m_read_index > static_cast<int>(m_buffer.size()/3))  // 如果读取索引超过了缓冲区大小的三分之一
    {
        std::vector<char> new_buffer(m_buffer.size());
        int count = readAble();
        memcpy(&new_buffer[0], &m_buffer[m_read_index], count);// 将当前缓冲区中可读的数据复制到新的缓冲区中，从当前读取索引的位置开始，复制 count 字节的数据
        m_buffer.swap(new_buffer);
        m_read_index = 0;
        m_write_index = count;
        new_buffer.clear();
    }

    
}
int TcpBuffer::getSize() {
  return m_buffer.size();
}

void TcpBuffer::clearBuffer() {
  m_buffer.clear();
  m_read_index = 0;
  m_write_index = 0;
}

//手动标记已读
void TcpBuffer::recycleRead(int index) {        // index:要回收的读取字节数,
  int j = m_read_index + index;
  if (j > (int)m_buffer.size()) {
    ErrorLog << "recycleRead error";
    return;
  }
  m_read_index = j;
  adjustBuffer();
}
//手动标记已写
void TcpBuffer::recycleWrite(int index) {
  int j = m_write_index + index;
  if (j > (int)m_buffer.size()) {
    ErrorLog << "recycleWrite error";
    return;
  }
  m_write_index = j;
  adjustBuffer();
}

// 获取当前缓冲区的内容，返回一个 std::string 类型的副本
std::string TcpBuffer::getBufferString() {
  std::string re(readAble(), '0');
  memcpy(&re[0],  &m_buffer[m_read_index], readAble());
  return re;
}

std::vector<char> TcpBuffer::getBufferVector() {
  return m_buffer;
}

}
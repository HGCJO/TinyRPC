#ifndef TINYRPC_NET_TCP_TCP_BUFFER_H
#define TINYRPC_NET_TCP_TCP_BUFFER_H

#include <vector>
#include <memory>

namespace tinyrpc{

class TcpBuffer
{
    public:
    typedef std::shared_ptr<TcpBuffer> ptr;

    explicit TcpBuffer(int size);

    ~TcpBuffer();                                           

    int readAble();                                             // 获取可读数据的字节数，即当前缓冲区中已经写入但尚未读取的数据量，通常通过计算写入索引和读取索引之间的差值来实现

    int writeAble();

    int readIndex() const;                                      // 获取当前读取索引的位置，即下一个要读取的数据在缓冲区中的位置，通常通过读取索引变量来实现                                          

    int writeIndex() const;

    // int readFormSocket(char* buf, int size);

    void writeToBuffer(const char* buf, int size);

    void readFromBuffer(std::vector<char>& re, int size);

    void resizeBuffer(int size);

    void clearBuffer();

    int getSize();

    // const char* getBuffer();

    std::vector<char> getBufferVector();                       // 获取当前缓冲区的内容，返回一个 std::vector<char> 类型的副本

    std::string getBufferString();

    void recycleRead(int index);

    void recycleWrite(int index);

    void adjustBuffer();

    private:

    int m_read_index {0};
    int m_write_index {0};
    int m_size {0};

    public:
    std::vector<char> m_buffer;                                 // 使用 std::vector<char> 来存储缓冲区的数据，提供动态调整大小和自动管理内存的功能，避免手动管理内存带来的复杂性和潜在的内存泄漏问题

};


}

#endif
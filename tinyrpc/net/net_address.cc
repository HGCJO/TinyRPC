#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include "net_address.h"
#include "../comm/log.h"

namespace tinyrpc {

bool IPAddress::CheckValidIPAddr(const std::string& addr) {
    //192.168.1.1:8080
    size_t i = addr.find_first_of(':');
    // 如果没有找到冒号，说明地址格式不正确
    if(i == addr.npos) {
        return false;
    }
    //将端口子串转换为整数
    int port = std::atoi(addr.substr(i+1,addr.size()-i-1).c_str());
    if(port < 0 || port > 65535) {
        return false;
    }
    //转化为网络字节序
    if(inet_addr(addr.substr(0, i).c_str()) == INADDR_NONE) {
        return false;
    }

    return true;
}
// 构造函数：根据 IP 地址和端口号初始化 sockaddr_in 结构体，并将 IP 和端口保存为成员变量。同时输出调试日志，显示创建的地址信息。
IPAddress::IPAddress(const std::string& ip,uint16_t port): m_ip(ip), m_port(port) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family = AF_INET;
    m_addr.sin_port = htons(m_port);
    m_addr.sin_addr.s_addr = inet_addr(m_ip.c_str());
    
    DebugLog << "create ipv4 address succ [" << toString() << "]";
}



// 构造函数：根据端口号初始化 sockaddr_in 结构体，IP 地址默认为 INADDR_ANY（
IPAddress::IPAddress(sockaddr_in addr) : m_addr(addr) {
  // if (m_addr.sin_family != AF_INET) {
    // ErrorLog << "err family, this address is valid";
  // }
  DebugLog << "ip[" << m_ip << "], port[" << addr.sin_port;
  m_ip = std::string(inet_ntoa(m_addr.sin_addr));
  m_port = ntohs(m_addr.sin_port);
}

// 构造函数：根据字符串形式的地址（格式为 "IP:port"）解析出 IP 和端口，并初始化 sockaddr_in 结构体。如果地址格式不正确，输出错误日志。
IPAddress::IPAddress(const std::string& addr) {
  size_t i = addr.find_first_of(":");
  if (i == addr.npos) {
    ErrorLog << "invalid addr[" << addr << "]";
    return;
  }
  m_ip = addr.substr(0, i);
  m_port = std::atoi(addr.substr(i + 1, addr.size() - i - 1).c_str());

  memset(&m_addr, 0, sizeof(m_addr));
  m_addr.sin_family = AF_INET;
  m_addr.sin_addr.s_addr = inet_addr(m_ip.c_str());
  m_addr.sin_port = htons(m_port);
  DebugLog << "create ipv4 address succ [" << toString() << "]";

}


IPAddress::IPAddress(uint16_t port) : m_port(port) {
  memset(&m_addr, 0, sizeof(m_addr));
  m_addr.sin_family = AF_INET;
  m_addr.sin_addr.s_addr = INADDR_ANY;
  m_addr.sin_port = htons(m_port);
 
  DebugLog << "create ipv4 address succ [" << toString() << "]";
}


int IPAddress::getFamily() const {
  return m_addr.sin_family;
}

sockaddr* IPAddress::getSockAddr() {
  return reinterpret_cast<sockaddr*>(&m_addr);
}

std::string IPAddress::toString() const {
  std::stringstream ss;
  ss << m_ip << ":" << m_port;
  return ss.str();
}

socklen_t IPAddress::getSockLen() const {
  return sizeof(m_addr);
}


// 构造函数：根据 Unix 域套接字的路径初始化 sockaddr_un 结构体，并将路径保存为成员变量。同时输出调试日志，显示创建的地址信息。
UnixDomainAddress::UnixDomainAddress(std::string& path) : m_path(path) {

  memset(&m_addr, 0, sizeof(m_addr));
  unlink(m_path.c_str());
  m_addr.sun_family = AF_UNIX;
  strcpy(m_addr.sun_path, m_path.c_str());

}

UnixDomainAddress::UnixDomainAddress(sockaddr_un addr) : m_addr(addr) {
  m_path = m_addr.sun_path; 
}

int UnixDomainAddress::getFamily() const {
  return m_addr.sun_family;
}

sockaddr* UnixDomainAddress::getSockAddr() {
  return reinterpret_cast<sockaddr*>(&m_addr);
}

socklen_t UnixDomainAddress::getSockLen() const {
  return sizeof(m_addr);
}

std::string UnixDomainAddress::toString() const {
  return m_path;
}

std::string IPAddress::toString() const {
  std::stringstream ss;
  ss << m_ip << ":" << m_port;
  return ss.str();
}



}
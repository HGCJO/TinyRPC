#ifndef TINYRPC_NET_ABSTRACT_DISPATCHER_H
#define TINYRPC_NET_ABSTRACT_DISPATCHER_H
//整体作用是为 RPC 框架定义统一的请求分发接口规范，承接解析后的 RPC 请求数据，将其精准分发到对应的服务方法中执行，
#include <memory>
#include <google/protobuf/service.h>
#include "tinyrpc/net/abstract_data.h"
#include "tinyrpc/net/tcp/tcp_connection.h"

namespace tinyrpc {

class TcpConnection;

class AbstractDispatcher {
 public:
  typedef std::shared_ptr<AbstractDispatcher> ptr;

  AbstractDispatcher() {}

  virtual ~AbstractDispatcher() {}

  virtual void dispatch(AbstractData* data, TcpConnection* conn) = 0;

};

}


#endif

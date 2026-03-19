#ifndef TINYRPC_COMM_START_H
#define TINYRPC_COMM_START_H

#include <google/protobuf/service.h>
#include <memory>
#include <stdio.h>
#include <functional>
#include "tinyrpc/comm/log.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/net/timer.h"

namespace tinyrpc {

//注册 HTTP 服务处理器
#define REGISTER_HTTP_SERVLET(path, servlet) \
 do { \
  if(!tinyrpc::GetServer()->registerHttpServlet(path, std::make_shared<servlet>())) { \
    printf("Start TinyRPC server error, because register http servelt error, please look up rpc log get more details!\n"); \
    tinyrpc::Exit(0); \
  } \
 } while(0)\
//注册 Protobuf RPC 服务（框架核心功能）
#define REGISTER_SERVICE(service) \
 do { \
  if (!tinyrpc::GetServer()->registerService(std::make_shared<service>())) { \
    printf("Start TinyRPC server error, because register protobuf service error, please look up rpc log get more details!\n"); \
    tinyrpc::Exit(0); \
  } \
 } while(0)\

//加载并初始化框架配置文件
void InitConfig(const char* file);

// void RegisterService(google::protobuf::Service* service);
//启动整个 RPC 服务器，开始监听端口、处理请求
void StartRpcServer();
//获取全局 TCP 服务器单例
TcpServer::ptr GetServer();
//获取 IO 线程池大小
int GetIOThreadPoolSize();
//获取全局配置对象，读取配置项。
Config::ptr GetConfig();
//向框架添加定时任务
void AddTimerEvent(TimerEvent::ptr event);

}

#endif
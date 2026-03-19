#include <google/protobuf/service.h>
#include "tinyrpc/comm/start.h"
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/net/tcp/tcp_server.h"
#include "tinyrpc/coroutine/coroutine_hook.h"

namespace tinyrpc {

tinyrpc::Config::ptr gRpcConfig;       // 全局配置单例
tinyrpc::Logger::ptr gRpcLogger;       // 全局日志单例
tinyrpc::TcpServer::ptr gRpcServer;    // 全局 TCP 服务器单例
static int g_init_config = 0;          // 标记是否已初始化配置

//加载并初始化框架配置文件,并且根据配置项初始化日志系统和 MySQL 插件（如果启用）。最后设置钩子函数，准备启动 RPC 服务器。
void InitConfig(const char* file) {
  tinyrpc::SetHook(false);

  #ifdef DECLARE_MYSQL_PULGIN
  int rt = mysql_library_init(0, NULL, NULL);
  if (rt != 0) {
    printf("Start TinyRPC server error, call mysql_library_init error\n");
    mysql_library_end();
    exit(0);
  }
  #endif

  tinyrpc::SetHook(true);

  if (g_init_config == 0) {
    gRpcConfig = std::make_shared<tinyrpc::Config>(file);
    gRpcConfig->readConf();
    g_init_config = 1;
  }
}

// void RegisterService(google::protobuf::Service* service) {
//   gRpcServer->registerService(service);
// }

TcpServer::ptr GetServer() {
  return gRpcServer;
}

void StartRpcServer() {
  gRpcLogger->start();
  gRpcServer->start();
}

int GetIOThreadPoolSize() {
  return gRpcServer->getIOThreadPool()->getIOThreadPoolSize();
}

Config::ptr GetConfig() {
  return gRpcConfig;
}

void AddTimerEvent(TimerEvent::ptr event) {

}

}
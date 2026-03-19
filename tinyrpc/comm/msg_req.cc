#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <random>
#include "tinyrpc/comm/log.h"
#include "tinyrpc/comm/config.h"
#include "tinyrpc/comm/msg_req.h"


namespace tinyrpc {

extern tinyrpc::Config::ptr gRpcConfig;

static thread_local std::string t_msg_req_nu;
static thread_local std::string t_max_msg_req_nu;
// static thread_local int t_msg_req_len = 20;

static int g_random_fd = -1;
//为每个 RPC 请求生成唯一、有序、线程安全的数字编号
std::string MsgReqUtil::genMsgNumber() {

  int t_msg_req_len = 20;
  //确定长度
  if (gRpcConfig) {
    t_msg_req_len = gRpcConfig->m_msg_req_len;
  }
  //判断当前线程的消息请求编号是否为空或者已经达到最大值，
  //如果是，则从 /dev/urandom 读取随机数据生成新的消息请求编号，并更新最大值；否则，将当前编号加一，确保唯一性和有序性。
  if (t_msg_req_nu.empty() || t_msg_req_nu == t_max_msg_req_nu) {
        if (g_random_fd == -1) {
        g_random_fd = open("/dev/urandom", O_RDONLY);
        } 
        //创建一个长度为 t_msg_req_len 的字符串 res，初始值全部为 0
        std::string res(t_msg_req_len, 0);
        //从 /dev/urandom 读取 t_msg_req_len 字节的数据到 res 中
        if ((read(g_random_fd, &res[0], t_msg_req_len)) != t_msg_req_len) {
        ErrorLog << "read /dev/urandom data less " << t_msg_req_len << " bytes";
        return "";
        }
        //重置当前线程的 “最大编号” 变量
        t_max_msg_req_nu = "";
        //将 res 中的每个字节转换为一个数字字符（0-9），并将其追加到 t_max_msg_req_nu 中，形成一个新的消息请求编号
        for (int i = 0; i < t_msg_req_len; ++i) {
        uint8_t x = ((uint8_t)(res[i])) % 10;
        res[i] = x + '0';
        t_max_msg_req_nu += "9";
        }
        t_msg_req_nu = res;
    } 
  else {
    //编号为 “123999”，会找到第 3 位（索引 2）的 '3'。
        int i = t_msg_req_nu.length() - 1; 
        while(t_msg_req_nu[i] == '9' && i >= 0) {
        i--;
        }
        if (i >= 0) {
        //“123999” 的第 3 位加 1 后变为 “124999”
        t_msg_req_nu[i] += 1;
        //“124999” 的第 4 到第 6 位重置为 0，最终变为 “124000”
        for (size_t j = i + 1; j < t_msg_req_nu.length(); ++j) {
            t_msg_req_nu[j] = '0';
        }
        }
    }    
  // DebugLog << "get msg_req_nu is " << t_msg_req_nu;
  return t_msg_req_nu;
}

}
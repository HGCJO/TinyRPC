#ifndef TINYRPC_NET_ABSTRACT_DATA_H
#define TINYRPC_NET_ABSTRACT_DATA_H
//所有编解码数据类的顶层抽象基类
namespace tinyrpc {

class AbstractData {
 public:
  AbstractData() = default;
  virtual ~AbstractData() {};

  bool decode_succ {false};
  bool encode_succ {false};
};


}

#endif
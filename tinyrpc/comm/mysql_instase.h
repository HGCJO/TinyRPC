#ifndef TINYRPC_COMM_MYSQL_INSTASE_H
#define TINYRPC_COMM_MYSQL_INSTASE_H

#ifdef DECLARE_MYSQL_PLUGIN 
#include <mysql/mysql.h>
#endif

#include <memory>
#include <map>
#include "tinyrpc/net/mutex.h"
#include "tinyrpc/net/net_address.h"



namespace tinyrpc {

// MySQL 连接配置类，封装 MySQL 连接所需的参数，如地址、用户名、密码、数据库名和字符集等。
struct MySQLOption {
public:
    //只允许显式调用
    explicit MySQLOption(const IPAddress& addr) : m_addr(addr) {};// 构造函数，接受一个 IPAddress 对象作为参数，并将其赋值给成员变量 m_addr。
    ~MySQLOption() {};

    public:
    IPAddress m_addr;
    std::string m_user;
    std::string m_passwd;
    std::string m_select_db;
    std::string m_char_set;
};


#ifdef DECLARE_MYSQL_PLUGIN 
// MySQL 连接池类，管理多个 MySQL 连接实例，提供线程安全的连接获取和释放接口。
class MySQLThreadInit {
 public:

  MySQLThreadInit();
  
  ~MySQLThreadInit();

};

// MySQL 实例类，封装 MySQL 连接和操作，提供执行 SQL 语句、事务管理、结果集处理等功能。
class MySQLInstase {
public:
    // 定义智能指针别名，方便外部使用
    typedef std::shared_ptr<MySQLInstase> ptr;

    // 构造函数：传入MySQL连接配置参数
    MySQLInstase(const MySQLOption& option);

    // 析构函数：释放MySQL连接、资源
    ~MySQLInstase();

    // 判断MySQL实例是否初始化/连接成功
    bool isInitSuccess();

    // 执行SQL语句（增删改查），返回执行结果
    int query(const std::string& sql);

    // 提交事务
    int commit();

    // 开启事务
    int begin();

    // 事务回滚
    int rollBack();

    // 获取查询结果集（把结果从MySQL读到内存）
    MYSQL_RES* storeResult();

    // 从结果集中取出一行数据
    MYSQL_ROW fetchRow(MYSQL_RES* res);

    // 释放结果集内存，防止内存泄漏
    void freeResult(MYSQL_RES* res);

    // 获取结果集的列数（字段数）
    long long numFields(MYSQL_RES* res);

    // 获取最近一次SQL影响的行数（insert/update/delete）
    long long affectedRows();

    // 获取MySQL最近一次的错误信息
    std::string getMySQLErrorInfo();

    // 获取MySQL最近一次的错误码
    int getMySQLErrno();

private:
    // 内部重连函数：连接断开时自动重连MySQL
    int reconnect();

private:
    MySQLOption m_option;        // MySQL连接配置（地址、用户名、密码、库名等）
    bool m_init_succ {false};    // MySQL是否初始化成功
    bool m_in_trans {false};     // 当前是否在事务中
    Mutex m_mutex;               // 互斥锁，保证多线程下操作MySQL安全
    MYSQL* m_sql_handler {NULL}; // MySQL原生连接句柄（C API的操作对象）
};

// MySQL 实例工厂类（单例 + 缓存管理）
class MySQLInstaseFactroy {
 public:
  // 默认构造函数
  MySQLInstaseFactroy() = default;

  // 默认析构函数
  ~MySQLInstaseFactroy() = default;

  // 根据 key 获取/创建对应的 MySQL 实例（智能指针）
  // key 一般是：ip:port 或 数据库名，用于区分不同 MySQL 连接
  MySQLInstase::ptr GetMySQLInstase(const std::string& key);

 public:
  // 获取【线程局部】的 MySQL 工厂单例（每个线程独立一个工厂）
  // 目的：避免多线程竞争，提高并发性能
  static MySQLInstaseFactroy* GetThreadMySQLFactory();

};
#endif

}
#endif
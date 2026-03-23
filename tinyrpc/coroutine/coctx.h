#ifndef TINYRPC_COROUTINE_COCTX_H
#define TINYRPC_COROUTINE_COCTX_H 

//TinyRPC 协程切换的底层汇编接口定义
namespace tinyrpc{

enum {
  kRBP = 6,   // 栈基址指针（标记栈底）
  kRDI = 7,   // 函数调用第1个参数
  kRSI = 8,   // 函数调用第2个参数
  kRETAddr = 9,  // 下一条指令地址（会赋值给 RIP）
  kRSP = 13,  // 栈顶指针
};

//协程上下文结构体,
struct coctx {
  void* regs[14];
};


//协程切换汇编接口，用于在两个协程上下文之间切换执行状态。它会保存当前协程的寄存器状态到第一个 coctx 结构体中，并从第二个 coctx 结构体中恢复寄存器状态，以实现协程之间的切换。
extern "C" {
// save current register's state to fitst coctx, and from second coctx take out register's state to assign register
//禁止 C++ 名字修饰，让 C++ 能调用汇编函数,并且指定函数名为 "coctx_swap"，以便在链接阶段正确解析该函数的调用。
extern void coctx_swap(coctx *, coctx *) asm("coctx_swap");

};

}

#endif

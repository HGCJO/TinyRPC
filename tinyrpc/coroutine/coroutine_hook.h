#ifndef TINYRPC_COROUTINE_COUROUTINE_HOOK_H
#define TINYRPC_COROUTINE_COUROUTINE_HOOK_H
//把系统原生的阻塞函数（read/write/connect/accept/sleep 等）替换成协程非阻塞版本，实现协程的自动切换和调度，让协程在执行这些函数时不会阻塞整个线程，而是让出 CPU 给其他协程继续执行，提高并发性能和资源利用率。
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

//保存系统原来的 read/write/connect 等函数地址
typedef ssize_t (*read_fun_ptr_t)(int fd, void *buf, size_t count);

typedef ssize_t (*write_fun_ptr_t)(int fd, const void *buf, size_t count);

typedef int (*connect_fun_ptr_t)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

typedef int (*accept_fun_ptr_t)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

typedef int (*socket_fun_ptr_t)(int domain, int type, int protocol);

typedef int (*sleep_fun_ptr_t)(unsigned int seconds);


namespace tinyrpc {

int accept_hook(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

ssize_t read_hook(int fd, void *buf, size_t count);

ssize_t write_hook(int fd, const void *buf, size_t count);

int connect_hook(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

unsigned int sleep_hook(unsigned int seconds);

void SetHook(bool);

}

extern "C" {

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

ssize_t read(int fd, void *buf, size_t count);

ssize_t write(int fd, const void *buf, size_t count);

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

unsigned int sleep(unsigned int seconds);

}

#endif
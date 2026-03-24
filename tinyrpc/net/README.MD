客户端连接 → 主Reactor → 子Reactor/主协程 → 子协程 的完整流程：

┌─────────────────────────────────────────────────────────────┐
│ 1. 主Reactor线程（MainReactor）                              │
├─────────────────────────────────────────────────────────────┤
│ • epoll_wait() 监听 server socket（EPOLLIN）                │
│ • 触发 accept 事件                                          │
│ • 调用 accept() 获得新的 client_fd                          │
│ • 创建 FdEvent 对象，绑定 client_fd                         │
│ • 创建/分配子协程（Coroutine）                              │
│ • 轮询分发到子Reactor线程                                   │
└─────────────────────────────────────────────────────────────┘
                        │
                        ↓ addEvent(client_fd, FdEvent*)
┌─────────────────────────────────────────────────────────────┐
│ 2. 子Reactor线程（SubReactor）                               │
├─────────────────────────────────────────────────────────────┤
│ • 从 m_pending_add_fds 获取待添加的 client_fd               │
│ • epoll_ctl(EPOLL_CTL_ADD) 注册 client_fd                   │
│ • epoll_wait() 监听 client_fd（EPOLLIN/EPOLLOUT）           │
│ • 触发 IO 事件时：                                          │
│   - 检查 FdEvent 是否关联协程                               │
│   - 若有协程，调用 CoroutineTaskQueue::push(FdEvent*)      │
│   - 若无协程，直接执行 read_cb/write_cb 回调函数           │
└─────────────────────────────────────────────────────────────┘
                        │
                        ↓ 子Reactor事件循环处理
                        │
        ┌───────────────┴───────────────┐
        │                               │
        ↓ (有关联协程)                   ↓ (无关联协程)
┌──────────────────────────┐   ┌──────────────────────────┐
│ 3. 主协程（Main Coroutine)│   │ 直接执行回调函数          │
├──────────────────────────┤   │ （普通事件处理）          │
│ • 从任务队列 pop FdEvent  │   └──────────────────────────┘
│ • 执行 ptr->setReactor() │
│ • Coroutine::Resume()    │
│   切换到子协程执行       │
└──────────────────────────┘
                │
                ↓ 协程上下文切换
┌──────────────────────────────┐
│ 4. 子协程（Sub Coroutine）    │
├──────────────────────────────┤
│ • 获得执行权                 │
│ • 执行业务逻辑处理           │
│   - read(client_fd, ...)     │
│   - 处理协议/数据            │
│   - write(client_fd, ...)    │
│ • 若需要继续 IO，主动让出    │
│ • Coroutine::Yield()         │
│   控制权返回子Reactor        │
└──────────────────────────────┘
                │
                ↓ IO阻塞或主动让出
                │
        返回子Reactor事件循环
        继续处理其他连接事件












## 主协程 vs 子协程

### 主协程（Main Coroutine）= 调度器
**在子Reactor线程内运行**

职责：
- ✅ 从 CoroutineTaskQueue 监听并取出待处理的任务（FdEvent）
- ✅ 唤醒对应的子协程（Coroutine::Resume）
- ✅ 监督子协程的执行状态
- ✅ 处理子协程完成后的清理工作

```cpp
// 在 reactor.cc 的 loop() 中
while(1) {
    FdEvent* ptr = CoroutineTaskQueue::GetCoroutineTaskQueue()->pop();
    if(ptr) {
        // 主协程就是干这个的
        tinyrpc::Coroutine::Resume(ptr->getCoroutine());  // ← 唤醒子协程
    } else {
        break;
    }
}
```

---

### 子协程（Sub Coroutine）= 业务执行者
**被主协程唤醒，在子Reactor线程内运行**

职责：
- ✅ 执行真正的业务逻辑
- ✅ 读取网络数据（read）
- ✅ 处理RPC协议、业务逻辑
- ✅ 写回响应数据（write）
- ✅ 当遇到IO阻塞时，主动让出（Yield）

```cpp
// 子协程的业务代码示例
void handler_coroutine() {
    // 1. 读取请求
    ssize_t ret = read(client_fd, buffer, size);  // ← IO操作
    
    // 2. 处理请求
    process_rpc_request(buffer);
    
    // 3. 写回响应
    write(client_fd, response, resp_size);  // ← IO操作
    
    // 4. 如果需要继续等待IO
    Coroutine::Yield();  // ← 主动让出CPU，返回主协程
}
```

---
## 协程、Reactor线程、普通线程的关系（直白比喻）

### 🏭 工厂比喻

**普通线程 = 工人**
- 每个工人是独立的个体
- 工人之间互不干扰，各做各的事
- 创建一个工人成本很高（内存、调度）
- 最多几千个工人就爆满了

**Reactor线程 = 工厂经理**
- 专门负责接收和分配任务
- 经理自己不做具体工作，只负责调度
- 通过 epoll 这个"任务看板"监听所有待做任务
- 一个经理可以管理几万个任务（socket）

**协程 = 任务（Task）**
- 不是真正的执行者，而是"待做的事"
- 协程是轻量级的（几KB），成本低
- 同一时刻只有一个协程在运行
- 协程可以主动暂停让出CPU（Yield）

### 🚗 餐厅点餐比喻

```
                 ┌─ 点单员（主Reactor）
                 │  职责：接收订单、分配给厨房
                 │  
        ┌────────┘
        │
   ┌────▼─────────────────────────────┐
   │  厨房（子Reactor线程）              │
   ├────────────────────────────────────┤
   │ • 监听订单列表（m_pending_add_fds）│
   │ • 快速查看哪些订单可以做了         │
   │   （epoll_wait 监听IO就绪）        │
   │ • 分配给具体的厨师或自己做         │
   └────────────────────────────────────┘
        │
        ├─→ 有厨师（协程）负责这个订单
        │   └─→ 厨师执行（Coroutine::Resume）
        │       做菜 → 遇到瓶颈主动休息（Yield）
        │       → 厨房继续分配其他订单
        │
        └─→ 没有厨师负责
            └─→ 厨房经理自己快速处理（回调函数）
```

### 📊 关键对比表

| 维度 | 普通线程 | Reactor线程 | 协程 |
|------|---------|-----------|------|
| **创建成本** | 🔴 高（1MB+） | 🟢 低 | 🟢🟢 极低（KB） |
| **数量上限** | 🔴 几千个 | 🟢 一两个 | 🟢🟢 几万个 |
| **切换方式** | 操作系统强制切换 | 事件驱动切换 | 程序员主动Yield |
| **职责** | 执行任务 | 调度管理 | 被调度的任务 |
| **同时运行** | 多个（真并行） | 一个主线程+ 几个子线程 | 单个Reactor内只有1个 |

### 🔄 执行流程（超简化版）

```
时刻 1: 主Reactor accept 了客户端 → 创建协程 A
时刻 2: 分配给子Reactor处理
时刻 3: 子Reactor 检测到 fd 可读 → 唤醒协程 A（Resume）
时刻 4: 协程 A 读取数据...处理...
时刻 5: 协程 A 需要写数据但缓冲区满 → 主动让出（Yield）
时刻 6: 子Reactor 继续处理其他协程 B、C、D...
时刻 7: fd 缓冲区有空间 → 唤醒协程 A（Resume）
时刻 8: 协程 A 继续执行...
```

### 💡 三句话核心总结

1. **线程是CPU资源的竞争者**，多线程 = 多个工人同时干活
2. **Reactor是事件调度器**，监听事件来临，决定谁来处理
3. **协程是轻量级任务**，Reactor根据事件来唤醒/暂停协程

### ⚙️ 实际代码对应

```cpp
// 时刻 3: 子Reactor 监听到事件，协程关联
if(ptr->getCoroutine())  // ← 检查是否分配了"厨师"（协程）
{
    // 协程存在，唤醒它去处理
    tinyrpc::Coroutine::Resume(ptr->getCoroutine());  // ← 叫醒厨师干活
}
else
{
    // 没有协程，经理自己处理
    read_cb();  // ← 经理快速自己做
    write_cb();
}

// 时刻 5: 协程主动让出
while(read_data_size < NEED_SIZE) {
    ssize_t ret = read(fd, ...);
    if(ret == 0) {  // ← 数据还没到齐
        Coroutine::Yield();  // ← 主动说："我休息，继续管理其他人"
    }
}
```

## 🔄 交互过程（具体例子）

### 场景：客户端发来请求

```
时间轴：

T1: 【子Reactor线程】epoll_wait() 检测到 client_fd 可读
    └─→ FdEvent 关联了协程 A
    └─→ push(FdEvent) 到任务队列

T2: 【主协程】从任务队列 pop FdEvent
    └─→ Coroutine::Resume(协程A)  // ← 唤醒

T3: 【协程A 开始运行】
    ├─→ read(client_fd, buf, size)      // ← 读取RPC请求
    ├─→ parse_protobuf(buf)             // ← 解析协议
    ├─→ execute_business_logic()        // ← 执行业务
    ├─→ write(client_fd, response)      // ← 发送响应
    └─→ Coroutine::Yield()              // ← 主动让出

T4: 【控制权回到主协程】
    └─→ 继续 pop 下一个任务
    └─→ 可能唤醒协程B、协程C...

T5: 【某个时刻】epoll_wait() 又检测到协程A对应的fd有事件
    └─→ 重复T2步骤，继续唤醒协程A
```

---

## 📊 对比表

| 维度 | 主协程 | 子协程 |
|------|-------|-------|
| **数量** | 1个/Reactor | 多个（一个客户端一个） |
| **职责** | 调度 | 业务处理 |
| **谁创建** | Reactor自动创建 | RPC框架为每个连接创建 |
| **谁唤醒** | epoll事件到来时自动 | 主协程主动唤醒 |
| **何时让出** | 任务队列空时 | 执行到IO操作时 |
| **关键方法** | Coroutine::Resume() | read/write/Yield() |

---

## 🚀 为什么要分主从？

**如果没有主协程直接管理：**
```
问题1: 谁来决定哪个子协程该执行？
问题2: 子协程执行完后谁来清理？
问题3: 多个子协程如何调度？
```

**有了主协程：**
```
✅ 主协程是调度决策者（FIFO从任务队列取）
✅ 主协程处理上下文切换
✅ 子协程专注业务逻辑
✅ 模型简洁清晰
```

---

## 💻 核心代码对应

### 主协程运行位置（reactor.cc 中）
```cpp
if(m_reactor_type != MainReactor) {
    // 这里就是主协程的工作场景
    FdEvent* ptr = nullptr;
    while(1) {
        ptr = CoroutineTaskQueue::GetCoroutineTaskQueue()->pop();
        if(ptr) {
            // 主协程：唤醒子协程去干活
            tinyrpc::Coroutine::Resume(ptr->getCoroutine());
        } else {
            break;  // 没任务了，退出
        }
    }
}
```

### 子协程运行位置（用户业务代码中）
```cpp
// RPC handler 就是子协程要执行的代码
void rpc_handler(TcpConnection* conn) {
    // 子协程运行：
    auto request = conn->read_request();  // Yield 点1
    auto response = process(request);     // 业务逻辑
    conn->write_response(response);       // Yield 点2
}
```

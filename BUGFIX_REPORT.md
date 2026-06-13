# 错误检查 — 修改报告

**日期：** 2026-06-13  
**问题：** 客户端连接成功但全部 `recv error`；服务端报 `handleNewConnections failed to add new fd to epoll` 并段错误

---

## 1. 现象

| 端 | 表现 |
|----|------|
| 客户端（100 线程） | `connect` 成功，随后 `recv error` |
| 服务端 | 大量 `handleNewConnections failed to add new fd to epoll`，进程 core dump |

---

## 2. 根因分析

### 2.1 核心问题：`clientInfo` fd 被多次关闭

`clientInfo` 在析构时会 `close(fd)`，但未实现移动语义，`fd` 作为普通 `int` 在拷贝/移动时只会复制数值，不会转移所有权。导致多个对象持有同一 fd，各自析构时重复 `close`。

**问题链路：**

```
accept4() 得到 clientfd
    → queue_.push(std::move(client))     // push 参数析构 close 一次
    → handleAccept 局部 client 析构       // 再 close 一次（移动前仍持有 fd）
    → trypop() 后 queue_.pop() 析构       // 再 close 一次
    → worker 拿到已失效的 fd
    → epoll_ctl(ADD) 失败 (EBADF)
    → 客户端 send/recv 失败
    → 重复 close 可能引发段错误
```

### 2.2 次要问题：epoll 注册成功后未释放 `clientInfo` 所有权

`handleNewConnections` 将 fd 存入 `connections_` 后，局部变量 `client` 仍持有同一 fd。循环下一轮或析构时会再次 `close`，与 `connection` 管理冲突。

### 2.3 未修改项（非本次崩溃根因）

- 多个 worker 线程共用同一 `eventfd`：存在惊群，但不导致 fd 双重关闭，暂未改动。

---

## 3. 修改内容

### 3.1 `include/ThreadSafeQueue.h`

| 改动 | 说明 |
|------|------|
| 禁用拷贝构造 / 拷贝赋值 | 防止两个 `clientInfo` 共享同一 fd |
| 实现移动构造 / 移动赋值 | 转移 fd 后将源对象 `fd` 置为 `-1` |
| `push(const T item)` → `push(T item)` | 允许按值移动，避免 `const` 导致实质拷贝 |

### 3.2 `src/ConnectionProcessor.cpp`

| 改动 | 说明 |
|------|------|
| `handleNewConnections` 末尾增加 `client.fd = -1` | epoll 注册并写入 `connections_` 后，fd 由 `connection` 生命周期管理，`clientInfo` 不再负责关闭 |

---

## 4. 修改后预期行为

1. `accept` 后 fd 仅存在于队列中一份，push/pop 过程不再提前关闭。
2. `epoll_ctl(EPOLL_CTL_ADD)` 成功，服务端打印 `IP:Port connect to server`。
3. 客户端发送的数据被读取并 echo，客户端能收到回显。
4. 不再出现因重复 `close` 导致的段错误。

---

## 5. 验证建议

```bash
# 终端 1：启动服务端
./server

# 终端 2：运行测试客户端
./client
```

**通过标准：**

- [ ] 服务端无 `failed to add new fd to epoll` 报错
- [ ] 服务端无 core dump
- [ ] 客户端多数/全部打印「收到回显」
- [ ] 服务端有对应连接与消息回显日志

---

## 6. 变更文件清单

| 文件 | 类型 |
|------|------|
| `include/ThreadSafeQueue.h` | 修改 |
| `src/ConnectionProcessor.cpp` | 修改 |
| `BUGFIX_REPORT.md` | 新增（本报告） |

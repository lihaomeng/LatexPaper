# M3 增量验收：CEF 异步请求与终态订阅

后续更新：本记录中的公开 JSON 取消／代际关联待办已在 [通用取消增量](M3-wire-cancellation.md) 接入；以下保留本增量的历史验证范围，当前状态以最新记录为准。

日期：2026-09-11。状态：本增量实现及双配置回归通过；M3 整体仍进行中。

## 实际改动与边界

- 新增标准 C++ 的 IKNativeMessageEndpoint、调度回调及 Factory。平台层只依赖该端口，不依赖 RPC Core 或业务实现。
- Bootstrap 注入 CEF Transport Factory；CEF 平台使用 CefPostTask(TID_UI) 排队调度。System 仍是无 I/O 的短操作，不在 UI 线程执行文件扫描或 TeX。
- 每次主页面导航建立随机 256 位会话标识。重载、Renderer 终止及 Browser 关闭时先关闭旧端点，再释放 Router；旧队列仅持有 weak_ptr，不会访问已销毁端点。
- V2 请求接入 KRpcSession：64 个在途请求与未消费终态的总容量、256 个近期 ID 去重窗口、代际 Ticket、5 秒排队截止时间。取消先请求停止，执行槽确认后才发布终态。
- CefQueryCancel → OnQueryCanceled → 端点取消 → stop_token。尚未执行的请求被跳过，不再调用其已取消的 Query Callback。已执行的只读操作不承诺撤销。
- 持久 Query 仅接受严格 Request Envelope 的 rpc.subscribe，且每会话只允许一个订阅。先发送生成契约 rpc.connected，再发送带会话、代际和单调序号的 rpc.completed。
- 订阅取消后保留有界终态队列；容量耗尽时返回 RESOURCE_EXHAUSTED，重新订阅消费后恢复容量，不静默丢弃。
- React NativeApi 管理订阅握手、5 秒超时、过期事件过滤和卸载释放。桌面就绪流程必须完成真实订阅握手和 System 往返；生产模式不使用 Fake。
- 修复订阅报错被迟到的启动成功覆盖的问题，原生故障同时撤销窗口的 Native Ready 标识。

## 契约与兼容性

新增 ConnectedEvent Schema 及 3 个跨语言 Fixture；V2 共 95 个，V1 23 个保持不变。生成文件未手写。

V1 继续同步回显。V2 实际 CEF 会话拒绝近期重复 ID，客户端须为每次调用生成新 ID。Loopback 的无状态 dispatchSystem 仍可重复测试只读请求；它不是有状态会话适配器，不应把两者视为重放策略相同。

已有 Cancellation JSON 模型与核心 cancelMessage 仍未作为公开 CEF 方法接入；当前实际取消使用 CEF 自带、绑定 Query 的取消通道。未声称实现通用 JSON 取消握手、长任务执行器或工作线程 Join。

## 本次验证

环境：Windows x64、VS2019、C++20、Qt 5.15.11、CEF 150、Node 24。工作目录为仓库根目录。

- npm.cmd run check --prefix web：退出码 0；180/180 测试、ESLint、TypeScript 和 Vite 通过。
- cmake --preset core-debug / core-release，分别 build、ctest：退出码 0；各 8/8，6.82 秒／9.40 秒。
- CEF 端点测试：真正排队、取消跳过、成功终态、近期重复 ID、关闭后迟到任务、调度失败、64 项背压、订阅排空恢复容量及非法会话 ID。
- 前端测试：持久握手、错会话／重复序号过滤、同步畸形握手、返回 Query ID 后补偿取消、订阅超时、提前关闭及晚到事件。
- 初次桌面 Debug 构建和 CTest：退出码 0，15/15，17.05 秒，含真实启动及 Renderer 崩溃恢复。此后补充前端连接状态竞态修复，最终回归记录另列。

测试明细保存在 out/<preset>/Testing/Temporary/LastTest.log。构建目录为可再生证据，清理前应另行归档；本 Markdown 保存结论。

### 最终双配置复验

最后的连接状态竞态修复后，依次重新执行 cmake --build --preset desktop-debug、ctest --preset desktop-debug，以及对应 desktop-release 命令，全部退出码 0。Debug 15/15（13.86 秒），Release 15/15（15.19 秒）。两者均包含真实桌面启动与 Renderer 崩溃重载测试。

上述构建通过 lol_frontend 依赖重新执行锁定依赖安装、前端检查及生产打包。没有在文档记录后再修改源码；M3 剩余范围如下，不以通过这些测试代替全部阶段验收。

## 尚未验收

- 当前真实桌面 Smoke 验证订阅握手及请求往返，不等于真实跨进程取消竞态和所有终态事件的端到端故障注入；取消及背压使用可控队列的 CEF 编解码适配器测试。
- 5 秒原生截止时间在排队任务运行时检查，没有后台计时器，不能强制中断阻塞的 Use Case。后续长任务必须使用独立 Worker/Executor 与停止等待协议。
- Cancellation JSON 的公开路由、开始事件／代际关联及跨传输会话合约需要补齐；不能以当前增量将 M3 整体标为完成。
- M2 桌面视觉、中文 IME 和 Worker 执行证据仍未补齐。M4～M8 尚未进入完整业务实现。
- 保留 CEF SDK 第三方未使用参数警告、VS2022 推荐提示和 Vite 大 chunk 提示；未升级依赖或放宽安全策略。
- 未安装 TeX，不提供真实编译／PDF 成功结论。

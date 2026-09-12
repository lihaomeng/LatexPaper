# M3 增量：通用取消路由与受理关联

日期：2026-09-11。状态：本增量验证通过，M3 整体仍进行中。依据项目开发 skill 保持协议单一来源、核心无 CEF 依赖及停止原因唯一所有权。

## 本次实现

- 新增 AcceptedEvent 和 CancellationResponse Schema，C++ 与 TypeScript 统一生成；V2 共享 Fixture 从 95 增至 117，V1 23 个保持不变。
- 受理通知包含 sessionId、id、generation、sequence。CEF 会话在受理后通知订阅者；受理与终态共用向外递增序号，保留安全整数与待完成事件的容量余量。
- CEF 端点接入已有 Cancellation 模型及核心 cancelMessage。仅非持久 Query 可发送取消；错误会话、错误代际、已完成请求返回 accepted=false。
- accepted=true 仅代表已对匹配的在途请求请求停止或已有停止信号，不意味着已执行操作被撤销，也不是终态。执行槽确认停止后才发送 rpc.completed。
- JSON 取消保留原请求 Query，随后以 RPC_CANCELLED 或 RPC_TIMEOUT 结束；原有 CefQueryCancel 仍释放 Query Callback。
- 新增 CancellationRpcClient：严格校验应答关联、64 个应答等待上限、超时清理、关闭清理和迟到应答过滤。NativeEventSubscription 暴露受理通知及 cancelRequest(ticket)，先快照化 Ticket，避免等待握手期间调用方篡改关联信息。
- KRpcSession 新增只读 stopReason(ticket)，验证 stop_token 身份。停止原因由核心持有；超时先发生不能被后续取消改写，反之亦然。传输层不再维护独立取消原因副本。

## 协议示意

```text
订阅 rpc.subscribe → rpc.connected(sessionId)
发送 System 请求 → rpc.accepted(id, generation, sequence)
发送 rpc.cancel(sessionId, id, generation)
收到 CancellationResponse(accepted)
执行槽确认 → rpc.completed(outcome, sequence)，同时结束原 Query
```

受理通知仅实时发送，不重放。调用方应先等待订阅握手，再发送需要 Ticket 的请求。短 System 操作可能在前端收到受理通知前已经完成，此时取消合法返回 false。

## 验证内容

- 共享 Schema 正反例：缺失字段、额外字段、无效版本、非法会话、非法代际及缺失事件序号。
- CEF 编解码端点测试：真实生成的 Ticket 关联、错会话／错代际拒绝、取消确认不冒充终态、原 Query 正确结束、晚到取消拒绝，以及受理／终态的统一顺序。
- 前端测试：true/false 应答、关联错误、非法类型、超时释放、64 项上限、关闭全部等待、Ticket 快照、跨会话拒绝和跨事件类型过滤。
- 核心测试：取消／超时先后顺序、其他会话不能读取停止原因、完成后释放原因。

最终测试命令与计数见本节后续结果。期间曾在契约样例更新后遇到旧生成文件漂移，已停止源码修改并重新 Configure/Build/CTest；不得引用该失败运行作为最终验收。

## 尚未完成与后续入口

### 最终验证结果

工作目录为仓库根目录，Windows x64／VS2019／Qt 5.15.11／CEF 150／Node 24 环境：

- npm.cmd run check --prefix web：退出码 0；ESLint、212/212 测试、TypeScript、Vite 生产构建通过。
- cmake --preset core-debug，随后对应 build／ctest：退出码 0，8/8（7.17 秒）。
- core-release 对应 Configure／Build／CTest：退出码 0，8/8（6.33 秒）。
- desktop-debug 对应 Configure／Build／CTest：退出码 0，15/15（12.63 秒）。
- desktop-release 对应 Configure／Build／CTest：退出码 0，15/15（14.94 秒）。
- 最终双配置包含代码生成漂移检查、非法依赖测试、CEF 编解码取消测试、真实启动和 Renderer 崩溃恢复。

之前一次 Release 漂移失败后已重新生成并完整通过上述最终测试；之后仅修改文档。Vite 大 chunk 和既有 CEF/VS 版本提示仍保留。未做新的视觉验收或打包。

### 下一步

实际 CEF 端点和前端均已支持 JSON 取消。当前取消正例使用可控调度队列的适配器测试，不是跨进程强制竞态测试；真实桌面 Smoke 仍主要覆盖启动、订阅握手、System 往返及 Renderer 重载。

M3 仍需补齐跨进程取消／终态故障注入和有状态 Loopback 替换合约，不能将当前无状态 Loopback 等同于完整会话适配器。M2 视觉／IME／Worker 未验收项保留。未进入 M4，未实现后台业务 Worker 或 TeX 进程停止。

本轮不修改打包脚本，不覆盖先前自解压包。旧包是旧版本快照；开发产物以 out/desktop-release/bin/Release 为准。

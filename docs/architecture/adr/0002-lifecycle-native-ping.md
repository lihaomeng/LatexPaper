# ADR 0002：生命周期恢复与真实 Native Ping

状态：已采用，2026-09-11。

## 决策

- 沿用 Qt/CEF 共用主线程，通过纯 C++ `KBrowserLifecycle` 管理启动、就绪、恢复、关闭和超时。时间由 Qt 单调时钟注入，策略可脱离 Qt/CEF 测试。
- 每次应用启动最多恢复 Renderer 两次，避免永久重启循环。每次恢复须重新等待 React 完成原生请求验证；关闭中不再恢复。
- 保留入口现有紧急退出保护。关闭超时不是成功退出，不允许在活跃 Browser 上调用 `CefShutdown`。
- CEF 平台实现只持有通过组合根注入的 `KNativeMessageHandler`，不依赖业务模块或 RPC Core。
- `lol_transport_cef` 使用 CEF 自带 JSON 解析器，将输入映射为生成的传输无关值树，再调用 `lol_rpc_core`。因此 CEF 是 Transport 的私有依赖，核心构建仍不查找 CEF。
- Ping 使用现有生成 Schema 的请求回显作为确认，CEF Query 成功/失败通道携带结果。这是现有 `system.ping` 启动探针的过渡契约，不宣称已经实现 V2 的通用响应、事件、取消和多方法路由。
- 桌面生产页面必须使用真实 CEF 通道；只有 Vite 开发模式可显式使用 Fake。缺少原生通道时显示失败，不能回退成成功。
- 前端验证回显 Schema、请求 ID 和 Client Sequence，并在超时后取消 CEF Query。Native Ping 无业务副作用。
- CEF 只给受控主 Frame 注入 Query API，Browser 再检查主 Frame、Origin、非持久查询和输入大小。

## 测试与范围

单元测试覆盖重复关闭、迟到就绪、加载/关闭/恢复超时和恢复上限。Qt 集成测试注入 Fake 浏览器；真实 Smoke 测试通过专用 CLI 参数触发本应用 Renderer 的 `Page.crash`，恢复后再次完成 Native Ping 才算成功。

当前自动恢复会重建 Renderer。Monaco 未接入，因此尚无未保存内容恢复保证；M4 接入前须实现编辑草稿检查点，再继续宣称文档恢复能力。

后续 M3 将由独立响应 Schema 生成通用响应校验器，接入业务 Inbound Port、事件流与取消状态。现有消息处理回调接口可继续复用。

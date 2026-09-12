# ADR 0004：M3 System RPC V2 与传输替换

日期：2026-09-11。状态：System 子集已采用并验证，完整 M3 仍进行中；结果见 M3 验收记录。

## 执行顺序与范围

用户最新要求“请你继续 M3”，本轮据此在保留 M2 未验收项的前提下进入 M3。这是本次明确的顺序调整，不将 M2 自动标为完成，也不进入 M4。

本增量建立 Ping／Capabilities 的请求—响应闭环。服务端事件、长任务取消、跨请求去重及背压尚未实现，不以此增量代替整个 M3 验收。

## 兼容性

V1 Ping 已约定返回原请求回显。直接改为成功／错误 Envelope 会破坏既有调用方，因此新增 `contracts/rpc/v2/system.schema.json`，保留 V1 Schema、生成器输出和回显行为。

V2 包含版本、ID、clientSequence、method 和空 params；响应回传 ID 与 clientSequence。成功响应含 ok、method、result；失败响应含稳定 code 与 messageKey。未知方法返回 METHOD_NOT_FOUND，非法字段返回 INVALID_ARGUMENT，能力提供者异常映射 INTERNAL_ERROR。

V2 当前没有 serverSequence 或事件流。不能把回传 clientSequence 当作服务端事件序号。后续协议扩展需继续通过 Schema 和共享 Fixture，不维护手写重复 DTO。

## 模块边界

- System Inbound 拥有 IKGetCapabilities 与只读 KRuntimeCapabilities；System Application 报告组合根已接入的服务，不扫描磁盘或发现 TeX。
- 组合根显式注入能力实例。当前原生文件、编译、PDF、SyncTeX 均为 false；“安装了 TeX”不等于“应用已接通编译”。
- RPC Core 接收项目自有 KValue，只依赖生成契约和 System Inbound，负责验证、路由与 DTO 转换。
- Loopback 在内存中调用同一核心；CEF Transport 负责有界 JSON 编解码和传输错误，不承载能力判断。
- V2 C++／TypeScript 类型与校验器由单一 Schema 生成，63 个共享正反 Fixture 在两端执行。
- 架构检查同时扫描 RPC Core 源码，拒绝 Qt／CEF／Win32 等框架泄漏。

## 生命周期与安全

当前两个 System 方法是同步、无 I/O、只读操作；允许已完成请求 ID 重复，不承诺业务命令幂等性或跨重启去重。

前端单个 SystemRpcClient 限制 64 个在途请求，拒绝重复在途 ID，快照化请求关联信息，验证版本／ID／序号／响应方法，丢弃迟到回调。超时或 AbortSignal 只取消 CEF Query 及前端等待，不表示原生长任务已取消。

CEF 消息上限仍为 4096 字节，转换深度不超过 8、对象字段不超过 32。前端响应上限 8192 个 UTF-16 码元。参数为空且字段数固定，Schema 拒绝未知字段与非安全整数；ID 和方法采用受限 ASCII，避免两种语言的字符串长度语义分叉。

没有修改 CSP、扩大本地文件权限、安装 TeX 或启用网络服务。损坏 JSON 和未知协议版本作为传输失败；可识别的 V2 非法请求返回结构化错误，无法使用的 ID／序号采用 invalid-request／0，仅用于诊断，不应被合法客户端接受为其成功响应。

## 后续 M3 工作

以下清单为初始增量的后续计划。2026-09-11 当前实现覆盖范围以文末更新为准。

1. 明确长任务取消 Envelope、任务状态和实际服务端取消语义。
2. 定义带单调序号的事件 Schema、订阅释放、乱序过滤和背压。
3. 实现跨请求去重／重放窗口与 Renderer 重载时的会话边界。
4. 扩充 Loopback／CEF 的完整适配器合约与故障注入；当前仅验证 System 子集。

## 2026-09-11 更新：实际 CEF 会话

以下描述为异步通道首个增量。后续已新增 AcceptedEvent、CancellationResponse 与公开 JSON 取消路由，见 [通用取消记录](../../acceptance/M3-wire-cancellation.md)。取消确认不等于终态；停止原因由核心唯一持有，受理与终态使用统一向外序号。原有 JSON 取消“待接入”的描述保留为历史。

原先同步 CEF Handler 已由标准 C++ 端点 Factory 替换，组合根注入 Transport，平台仅提供排队 Dispatcher 和随机会话标识。请求在 CEF UI 上延后执行；目前只允许无 I/O 的 System 短操作，不作为长任务执行器。

真实 CEF V2 请求使用 KRpcSession 的近期去重窗口，不再承诺同会话重复 ID 可再次执行。V1 回显与无状态 Loopback 不变。React 每次请求生成新 ID。

事件握手使用新增 ConnectedEvent Schema，后续为已有 RequestEvent；rpc.subscribe 使用严格 Request Envelope，且仅允许持久 Query。取消当前映射自 OnQueryCanceled，不新增不带确认语义的 JSON 取消成功响应。公开 Cancellation 路由留待开始事件／代际关联一并完善。

关闭、导航和 Renderer 终止先清理端点，队列只捕获弱引用；不将这一方案误称为后台 Worker Join。完整证据和限制见 [CEF 异步验收](../../acceptance/M3-cef-async.md)。

## 2026-09-11 更新：端点迁出平台与基线验收

会话编排现由 lol_rpc_core 的 KRpcEndpoint 唯一拥有；CEF 仅编解码并委托，Loopback 注入同一实现与测试时钟。核心端口使用生成的 KValue，不依赖平台消息端口，线上文本限制仍在 CEF 适配器执行。

同一组传输替换场景已分别运行两种适配器；CLI 专用延迟注入验证真实跨进程取消。旧文中的“JSON 取消待接入”已由后续增量实现，历史段落不表示当前状态。M3 System 基线验收见 [端点记录](../../acceptance/M3-endpoint-substitution.md)。

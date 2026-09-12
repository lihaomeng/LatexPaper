# M3 RPC 基线验收：独立端点与真实取消

日期：2026-09-11。接续 M3-wire-cancellation.md；本记录不追认 M2 未验收项。

## 本次实现

- 将会话排队、去重、取消、订阅、终态和统一序号从 CEF 移至纯 C++ KRpcEndpoint。
- IKRpcEndpoint 仅使用生成的 KValue、标准库回调、调度器和时钟；不引用 Qt、CEF、平台端口。
- CEF 保留 4096 字节输入限制和编解码；有状态 Loopback 注入同一端点。旧同步接口兼容保留。
- 增加同一组替换场景分别运行 Loopback 和 CEF：握手、受理、取消确认、取消终态、序号、后续成功、关闭后无回调。
- 增加真实 Browser/Renderer 跨进程取消冒烟。仅 --smoke-test-rpc 使用受控本地域名的 #rpc-smoke 页面和 1000ms 排队延迟。
- 前端必须观察取消确认、原请求 RPC_CANCELLED、相同代际且序号递增的 cancelled 终态，才继续正常启动握手；不存在 Fake 成功。
- 新增探针失败回归：拒绝取消、错误终态、提前关闭。

## 实际验证

环境沿用 VS2019、Qt 5.15.11、CEF 150、Node 24；未升级依赖。

| 命令／范围 | 结果 |
| --- | --- |
| core-debug 构建、ctest | 9/9，8.69s |
| core-release 构建、ctest | 9/9，8.13s |
| desktop-debug 构建、ctest | 17/17，19.90s |
| desktop-release 构建、ctest | 17/17，18.97s |
| 后增共用替换测试，双配置单独构建并运行 | Debug 1/1，0.22s；Release 1/1，0.20s |
| npm test --prefix web | 216/216 |
| 桌面构建中的前端 lint、类型检查和 Vite | 通过 |

以上 17 项与后增 1 项分开执行，不伪称一次运行了 18 项。原生测试日志保存在对应 out 配置的 Testing/Temporary 中，单独运行会更新 LastTest.log。

## 验收结论与边界

M3 的 System RPC 基线通过，进入 M4。RPC Core 无 CEF 链接，状态逻辑只有一份，实现可被有状态 Loopback 替换；真实取消与 Renderer 崩溃恢复均有桌面测试。

当前排队只执行无 I/O 的 System 短操作。文件、搜索、编译必须在后续阶段接入后台执行器和有界停止协议；本次不声称验证了 TeX 进程树终止、大日志流或文件保存。M2 桌面完整视觉、中文 IME、Worker 证据仍保留待办。

既有 CEF 头文件 C4100、VS2022 推荐和前端大 chunk 警告未消除。未重新生成旧的自解压开发包；当前运行程序应从新的 desktop-debug/desktop-release 输出目录启动。

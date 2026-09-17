# M3：System RPC V2 第一条闭环

日期：2026-09-11。状态：M3 进行中；M3.1 System 请求／响应闭环已验证通过，不代表整个 M3 完成。

## 范围与阶段入口

用户最新明确要求继续 M3，本轮据此调整默认顺序。M2 的桌面视觉、中文 IME 等未验收项仍保留，未标为完成；未进入 M4。设计与协议兼容性见 [ADR 0004](../architecture/adr/0004-system-rpc-v2.md)。

## 已实现

- 新增 V2 Schema，生成 C++／TypeScript DTO 与校验器；保留 V1 Ping 回显行为。
- System Inbound 能力端口和纯 Application 实现，通过组合根显式注入；未接入的原生文件、编译、PDF、SyncTeX 均报告 false。
- 纯 C++ 路由校验请求，返回带 ID／clientSequence 的成功结果或结构化错误。
- Loopback 与 CEF 复用同一 RPC Core；核心不链接 CEF，源码也纳入框架泄漏扫描。
- 前端 V2 传输可替换，校验响应关联、限制在途请求、拒绝重复在途 ID、处理超时／AbortSignal／迟到回调。
- 桌面就绪检查依次完成 V1 Ping、V2 Ping 和 V2 Capabilities，不用 Fake 通过生产 Smoke。

主要文件：`contracts/rpc/v2/system.schema.json`、`tools/contractcodegen/protocol.py`、`src/modules/system/application/`、`src/transport/rpc/`、`src/transport/loopback/`、`src/transport/cef/`、`frontend/web/src/native-api/system.ts`。

## 实际验证

环境：Windows x64、VS2019 MSVC 19.29、SDK 10.0.19041、CEF 150、Qt 5.15.11、Node 24、Python 3.12。命令工作目录为仓库根目录。

| 命令 | 结果 |
| --- | --- |
| `npm.cmd run check --prefix web` | 退出码 0；ESLint、143/143 测试、TypeScript 和 Vite build 通过 |
| `cmake --fresh --preset core-debug`、`cmake --build --preset core-debug`、`ctest --preset core-debug` | 退出码 0，7/7 测试通过 |
| `cmake --fresh --preset core-release`、对应 build／ctest | 退出码 0，7/7 测试通过 |
| `cmake --fresh --preset desktop-release`、对应 build／ctest | 构建成功；修复独立测试入口后最终 14/14 通过，11.89 秒 |
| `cmake --fresh --preset desktop-debug`、对应 build／ctest | 修复测试目标配置后重新构建退出码 0，最终 14/14 通过，17.07 秒 |

V2 的 63 个共享 Fixture 在 C++／TypeScript 两端均执行。覆盖请求／响应缺失字段、未知字段、版本、负数／小数／越界序号、非 ASCII 或非法 ID、能力布尔类型和未知错误码。

Loopback 验证 Fake 能力替换、只读重复调用、未知方法、非法参数、提供者异常及真实 Application 默认禁用能力。前端覆盖关联错误、响应类型错误、超时、取消、同步取消竞态、调用者修改请求、64 个在途上限和传输失败。

真实 CEF 独立测试覆盖 V1 兼容、V2 Ping／Capabilities、最大安全序号、未知方法／字段、损坏 JSON、未知版本及 4096 字节上限。桌面 Smoke 覆盖实际 V2 握手和 Renderer 崩溃重载。

本地测试证据位于各 `out/<preset>/Testing/Temporary/LastTest.log`；共享输入位于 `tests/fixtures/system-v2.json`。未使用 git diff 空结果作为实现无差异证据：当前仓库文件仍主要处于未跟踪状态。

## 本轮发现并修复的问题

1. 独立 CEF 测试未经过 Bootstrap，缺少 API 版本握手，触发 invalid version -1；现按安装头文件要求调用 cef_api_hash 并验证 ABI。
2. 最大安全序号由 CEF 编码为科学计数法，原字符串断言失败；改为解析后校验数值，协议未放宽。
3. 测试直接包含 CEF 头文件后需要独立 Include 路径及 NOMINMAX／WIN32_LEAN_AND_MEAN；已补齐专用测试目标配置。构建过程中旧 Debug 配置曾失败，必须以最终重跑结果为准。

保留的环境提示：CEF 150 建议 VS2022，当前仍按现有 VS2019 兼容配置验证；前端大 chunk 和部分依赖弃用提示未在本轮升级处理。

## 未完成项与下一步

### 会话生命周期核心增量（2026-09-11）

新增 `src/transport/rpc/include/lightoverleaf/rpc/krpcsession.h`、`core/krpcsession.cpp` 与 `tests/krpcsessiontests.cpp`。它仍属于纯 RPC Core，不引入 Qt、CEF、系统 I/O 或工作线程。

- 单个 Dispatcher 线程拥有会话；Worker 仅持有 stop_token，通过 Dispatcher 返回完成结果，不持有会话裸指针。
- 最多 64 个在途请求加未消费终态事件。受理时预留终态容量，避免完成事件被静默丢弃。
- 保留最近 256 个已完成 ID；窗口淘汰后允许复用 ID，但生成号阻止旧完成回调或旧取消指令影响新请求。
- 取消／超时／关闭发出协作停止信号；收到任务确认前保留在途项，不伪称任务已停止。
- 注入毫秒时刻，拒绝倒退时间、截止时间溢出与超过 60 秒的单次超时；阶段后续长任务应通过明确协议调整上限。
- request_stop 可能同步触发回调，使用独立 stop_source 副本并避免继续访问已被回调删除的容器元素。
- close／析构发出停止请求，但不具备 Worker join 保证；工作线程所有者必须先停止并等待完成，再销毁 Dispatcher。

本次实际执行 `cmake --fresh --preset core-debug`、对应 build／ctest，以及 core-release 对应命令：最终均退出码 0；Debug 8/8（7.48 秒），Release 8/8（8.29 秒）。首次编译因 const stop_source 无法调用 request_stop 失败，改为可修改的局部副本后重跑通过。

本次未改前端，未重新运行前端或桌面全套测试；上方 143 项及桌面 14 项结果属于上一增量。本次完成的仅为会话核心及其测试，尚未接入 CEF 异步传输或业务任务，不能声称已完成完整服务端取消和事件通道。

- 当前 System 方法为同步、只读、不可中断操作；AbortSignal 取消的是前端等待与 CEF Query，不是服务端长任务。
- 事件契约与单调序号已在核心实现；CEF 异步订阅释放和传输背压仍未接通。
- 会话核心已具备在途请求与重放窗口；原生任务与 CEF 取消路由尚未接通。
- 重复完成的 System 请求允许再次执行，不宣称具备业务写入幂等保证。
- 未进行全部畸形 JSON／重复 JSON 键／并发 Renderer 的传输故障注入；现有测试不等于完整安全审计。
- 后续先完成上述 M3 剩余协议与生命周期内容，补齐合约测试和阶段记录，再进入 M4；M2 待验收项不自动清除。

### 取消／终态事件契约与会话隔离（2026-09-11）

本次新增 Cancellation 和 RequestEvent Schema，包含 sessionId、请求 ID、generation；终态事件另含 sequence 与 outcome。C++／TypeScript 由原生成器统一生成；新增 29 个共享 Fixture，V2 共 92 个（V1 23 个保持不变）。

修复跨会话 Ticket 隔离：单凭 ID 和 generation 不能唯一识别所属会话，现在 cancel／complete 同时比较 stop_token 所属停止状态。回归测试构造两个具有相同 ID／generation 的会话，确认外来 Ticket 不能取消或完成当前请求。

KRpcSession 新增 cancelMessage 与 nextEventMessage：先验证生成契约及宿主持有的 sessionId，再请求协作停止；终态消息验证成功后才从队列消费。宿主必须保管可信 sessionId，不能直接采用请求提供的值作为其自身身份。当前没有将这些方法暴露为 CEF RPC 路由。

前端新增 RpcEventCursor：拒绝其他会话、未知字段、重复及旧序号；返回事件副本；生成并校验取消消息。它是供传输接入的组件，目前尚未订阅真实桌面事件。

本次验证（仓库根目录，既有 Windows／VS2019／Node 24 环境）：

- `npm.cmd run check --prefix web`：退出码 0；ESLint、174/174 测试、TypeScript、Vite 生产构建通过。
- `cmake --preset core-debug`，对应 build／ctest：最终退出码 0，8/8（7.74 秒）。
- `cmake --fresh --preset core-release`，新增消息处理后再次 build／ctest：最终退出码 0，8/8（6.72 秒）。
- 原生测试覆盖错会话取消、非法消息、真正 stop_token 触发、非法订阅标识不消费事件、终态消息与 Schema 一致及单次消费。
- 本次未重新构建桌面、未验证 CEF 异步事件投递，也没有 UI 视觉变更；不得沿用历史桌面测试作为本次端到端证据。Vite 大 chunk 提示仍存在。

结论：取消／事件的契约与核心处理已实现；M3 仍进行中，下一步是 CEF 异步请求及事件传输装配、Renderer 重载清理和真实取消端到端验收。未进入 M4。

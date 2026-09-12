# ADR 0008：Workspace 显式刷新与编辑模型协调

日期：2026-09-12。状态：已实施，M4 整体验收进行中。

## 背景

项目打开后，外部工具可能新增、修改、重命名或删除文件。重新执行 `workspace.open` 会混淆“选择新项目”和“刷新当前项目”的生命周期，也可能在扫描失败时丢失现有树。前端若无条件采用磁盘内容，则会覆盖尚未保存或读取期间产生的编辑。

## 决策

Workspace Inbound/Outbound Port 分别增加显式 `refresh`。Application 只允许刷新已打开项目，调用 Store 对当前选择重新扫描；仅当扫描成功、结果合法且 Workspace ID 与当前根身份一致时提交新树。取消、异常、失败和身份变化保持旧状态。LocalFS Adapter 复用打开项目的有界扫描规则，RPC 只暴露不透明 Workspace ID 与相对 File ID。

前端先比较树 Revision，再只协调 EditorSession 中已加载的模型；该集合已有 32 个模型上限。Clean 模型在文档 Revision 变化时重新读取并替换，磁盘消失时关闭。Dirty 模型不读取、不关闭。读取前后均检查会话身份、模型版本与 Dirty 状态，窗口销毁会提升序列号并清空会话引用，使迟到结果失效。

因 Dirty、并发编辑或短暂文件竞态而未完成的协调会设置延迟标记。后续刷新即使树 Revision 相同也会再次尝试，直至安全完成。扫描与读取之间出现 `FILE_NOT_FOUND` 时，仅关闭仍属于当前会话、版本未变且 Clean 的模型。自动保存若在刷新期间触发，则登记待保存请求，并在刷新释放串行门后立即重试。

当前 UI 在页面可见时每 5 秒调用一次刷新，并保留手动命令。轮询只是应用层触发策略；未来 `IKFileWatcher` 可作为独立 Platform Adapter 触发同一用例，不修改 Workspace/Document Use Case、RPC DTO 或协调规则。

## 结果与约束

- 刷新、扫描和读取继续在有界 Worker 上执行，Qt/CEF UI 线程不做文件 I/O。
- 前端永远不接收项目根绝对路径，刷新不会放宽路径、容量、深度、项目身份或 Reparse Point 防护。
- 外部变化不会静默覆盖 Dirty 或并发编辑内容；Clean 文件可以自动跟随磁盘。
- 5 秒全树扫描在大型项目上有固定开销，但受 10000 项／32 层边界限制。原生事件合并、退避和监视器故障恢复留给后续 Adapter。
- CMake 配置依赖显式包含 Workspace/Document Schema 与 Fixture；业务合约变化必须触发生成，Codegen Drift 测试继续作为门禁。
- 本决策不提供冲突三方合并、另存副本、目录管理或回收站恢复 UI，也不代表 M4 已全部验收。

# ADR 0013：Search 与 Build 的端口化后端

- 状态：已接受
- 日期：2026-09-12

## 决策

Search 通过 `IKSearchSource` 读取有界文档集合；本地文件扫描仅存在于 LocalFS Adapter。Build 通过 `IKBuildSnapshotStore` 与 `IKCompilerBackend` 完成不可变快照和编译，Application 不接触 Win32、路径或进程句柄。

编译器发现覆盖显式配置根、随包 `texlive` 根和系统 `PATH`。进程使用参数向量和 `CreateProcessW` 启动，不经过 Shell；每个进程先挂入带 `KILL_ON_JOB_CLOSE` 的 Job Object，再恢复执行。取消和超时均终止整个进程树。

RPC Handler 继续串行化 Workspace、Document 与 Search；`build.start` 和 `build.cancel` 是唯一允许重叠的命令，使取消不会排在长时间 TeX 任务之后。Build DTO、Compiler DTO 分离，由 Application 显式映射。

## 结果

- 切换系统 TeX、Portable TeX 或测试 Fake 不修改 Use Case。
- Search/Build 核心可脱离 CEF 和真实 TeX 测试。
- 编译只读取快照，不直接在用户项目中产生中间文件。
- 当前机器没有真实 TeX，真实论文编译与发行版 Portable TeX 验收继续保留为环境项。


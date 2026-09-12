# ADR 0012：Windows 原生 Workspace 变化探测

- 状态：已采纳
- 日期：2026-09-12

Workspace LocalFS Adapter 使用 `FindFirstChangeNotificationW` 监视活动根目录及子目录，只输出布尔变化信号。Application 拥有 `pollChanges` 用例，RPC 暴露 `workspace.pollChanges`；React 每秒读取一次轻量信号，仅在变化时调用已有安全 `workspace.refresh`。这样不把 Win32 事件、绝对路径或文件内容暴露给内层和前端，也复用 Dirty/并发编辑保护。

通知句柄由 Adapter 唯一拥有，切换项目、关闭和析构时释放；一次信号可能合并多项变化，调用方不得把它当作精确事件列表。失败时保留手动刷新入口。

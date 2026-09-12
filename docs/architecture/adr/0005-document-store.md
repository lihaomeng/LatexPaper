# ADR 0005：文档 Store 的提交边界

日期：2026-09-11。状态：核心与本地适配器已实施，RPC 接入未完成。

## 决策

文档应用只依赖本模块 Domain、Inbound、Outbound；不访问 Workspace 实现、浏览器或磁盘。Store 在组合根绑定授权工作区，用例接收相对 File ID。

- IKDocuments 输出独立应用快照和保存结果，不复用存储 DTO 作为 RPC DTO。
- IKDocumentStore::replace 将 Revision 比较与替换定义为同一个操作。用例禁止先 read 再 write。
- Store 的成功返回表示提交已经完成，晚到取消不得将已成功提交改为失败；取消发生在提交前则保持原文不变。
- 用例不持有第二份可变文档正文。未保存内容仍属于 Editor 的 Model，保存响应带 Client Sequence。
- 文本上限为 4 MiB，UTF-8 校验拒绝无效码点与 NUL。相对标识的词法校验不等于磁盘授权校验。
- 当前同步接口仅供受控 Worker 使用。接入 RPC 前必须建立有界后台执行与停止回收，禁止直接在 UI 调用。

## 本地 Adapter 落地

本地 Store 工厂规范化并固定根目录；每次访问验证相对 File ID、逐级拒绝 Reparse Point，并对最终 Canonical Path 做不区分大小写的组件级根包含判断，不使用字符串前缀判断。

Revision 为原始字节（包含 BOM）的 SHA-256。保存创建同目录随机临时文件，写入后 Flush，在 ReplaceFileW 前再次校验 Revision，失败由 RAII 删除临时文件。已覆盖顺序外部修改、只读文件、中文路径、BOM、取消、容量和失败清理。

Windows API 无条件比较并替换原语不可用，最终 Revision 复检与 ReplaceFileW 之间仍存在很小的外部进程 TOCTOU 窗口；在形成更强的文件身份／共享锁策略前保留为风险。本机未启用无特权符号链接，真实 Reparse Point 夹具未创建成功，因此该分支只有实现审查，尚无运行证据。长路径和崩溃中断也未验收。

下一步为受控文件夹选择、后台执行器、生成的 RPC 和前端冲突交互。完成这些闭环前，当前不得把 nativeFiles 能力改为 true。

2026-09-12 更新：lol_platform_worker 已提供标准库有界执行、任务取消与异常隔离；当前只完成独立测试，尚未注入 RPC。任务必须协作响应 stop 并拥有自己的超时，否则关闭 Join 可能被任务延长。

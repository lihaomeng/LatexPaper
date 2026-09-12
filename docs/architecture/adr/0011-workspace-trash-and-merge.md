# ADR 0011：带索引的项目回收恢复与前端三方合并

- 状态：已采纳
- 日期：2026-09-12
- 范围：M4 Workspace/Document

## 决策

1. 可恢复删除由 Workspace 模块拥有。LocalFS 为每个删除项生成 128 位随机 Trash ID，正文保存为 `.payload`，恢复元数据保存为隐藏 `.restore`；元数据只包含版本、类型、删除时间和十六进制编码的原始相对路径。
2. `workspace.listTrash` 和 `workspace.restoreTrash` 使用独立 Inbound/Outbound 方法与 V2 RPC DTO。恢复使用非覆盖句柄移动，只回到记录的原路径；目标存在时返回冲突，令牌恢复后失效。
3. 回收索引缺失、损坏、指向 Reparse Point 或正文不存在时不对前端公开。最多返回 1000 项，不向 React 暴露 `.lightoverleaf-trash` 绝对路径。
4. 三方合并属于前端 Document 协调能力：EditorSession 保存最近一次真实持久化正文作为基线；磁盘版本、本地版本和基线按行合并。无法安全判断的结构变化或同一行双向修改生成显式冲突标记。
5. 含冲突标记的结果不能应用。用户清理标记后，应用结果成为 Dirty Monaco 内容，并以磁盘 Revision 作为下一次预期 Revision；迟到响应或并发编辑不能覆盖模型。

## 后果

- 回收恢复可由 Fake Store 独立测试，且不引入永久删除能力。
- 保存基线只在真实保存成功、采用磁盘版本或安全另存重键控后前移。
- 历史上没有 `.restore` 索引的旧回收内容仍可人工恢复，但不会出现在新 UI 列表。
- 当前行数变化采取保守冲突，不猜测复杂 diff；后续可替换合并算法而不修改 RPC。

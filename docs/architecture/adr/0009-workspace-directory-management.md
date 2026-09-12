# ADR 0009：目录命令与可恢复子树移动

日期：2026-09-12。状态：已实施，M4 整体验收进行中。

## 背景

文件操作与目录操作的系统语义不同：目录重命名会影响全部后代，目录删除应处理整个子树，空目录必须独立出现在树中。若只在文件命令上增加 `isDirectory`，Application、Store 和 RPC 都会形成难以验证的条件分支，并容易把普通文件交给目录删除路径。

## 决策

Workspace 定义独立的 `KWorkspaceDirectoryMutation` 和 `KStoredDirectoryMutation`，Store 暴露独立 `mutateDirectory`；RPC 使用 `workspace.manageDirectory` 和专用 Schema。三种操作为 Create、Rename、Remove，目标父目录必须已经存在，且永不覆盖同名目标。

Application 在调用 Adapter 前构造下一状态。Create 添加显式目录项；Rename 改写目录及全部已知后代的相对 ID；Remove 移除目录及全部已知后代。只有 Adapter 成功才提交下一状态，失败保留旧状态。目录不能移动到自身后代。

Windows LocalFS Adapter 重新验证 Workspace 根身份和所有相对路径，逐级打开并检查父目录，拒绝 Reparse Point。创建使用 `CreateDirectoryW`。重命名和删除以带 `FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT` 的目录句柄确认类型，再通过 `SetFileInformationByHandle(FileRenameInfo)` 非覆盖移动。Remove 的目的地是项目内 `.lightoverleaf-trash/<随机前缀>-<原目录名>`，整个子树被移动，不执行递归永久删除。

React Explorer 接受文件和显式目录两个投影，因此空目录可见。目录操作期间禁止 Dirty、保存、刷新和冲突并发；重命名同步已加载模型及 Revision 键，删除关闭对应 Clean 模型。

## 结果与限制

- 文件和目录契约可分别替换、测试和演进，RPC Handler 只验证与映射。
- 删除可从资源管理器手动恢复内容，但当前没有原路径索引和应用内恢复 UI。
- 跨卷移动不在项目根内发生；所有目标均受同一根目录和父目录句柄验证。
- Application 只知道最近树快照中的后代。外部工具同时产生的未知条目由下一次 Workspace 刷新纳入，最多受当前 5 秒可见窗口轮询延迟影响。
- 当前不支持递归创建缺失父目录、永久删除、目录复制、批量操作和撤销目录操作。
- 本决策不完成原生文件监视、冲突合并/另存或 M4 人工验收。

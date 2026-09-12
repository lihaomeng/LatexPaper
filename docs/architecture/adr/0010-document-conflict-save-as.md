# ADR 0010：Document 冲突副本的原子非覆盖另存

- 状态：已采纳
- 日期：2026-09-12
- 范围：M4 Workspace/Document

## 背景

Revision 保存发现外部修改后，用户需要保留当前编辑内容，但不能先创建空文件再调用普通保存。两次独立调用会暴露空文件、留下半完成状态，并且目标在两次调用之间可能被其他进程占用。该能力也不能绕过 Document 用例直接放进 React、CEF Handler 或 Workspace Store。

## 决策

1. Document Inbound 增加独立的 `saveAs` Command，Document Outbound 增加语义明确的 `createExclusive`；普通 `save` 的预期 Revision 语义保持不变。
2. LocalFS Adapter 在目标父目录创建临时文件，写入正文与可选 UTF-8 BOM，刷新文件缓冲区后使用不带替换标志的同目录移动完成提交。目标已存在或并发出现时返回 `FILE_CONFLICT`，绝不覆盖。
3. Adapter 在解析时拒绝绝对路径、穿越、Reparse Point、非法父目录与越界路径；提交前持有已验证父目录句柄。成功结果使用最终字节计算 SHA-256 Revision，并回传 BOM 元数据。
4. RPC 只负责 `document.saveAs` DTO 校验和映射；WorkspaceWorkflow 只组合已打开会话与 Document Inbound，不承担磁盘实现。
5. React 冲突协调器捕获源模型版本与会话。磁盘提交成功后，仅当模型未继续编辑且会话仍有效时，才把同一个编辑模型重新键控到副本 File ID；否则保留原模型并明确提示副本已经落盘。
6. Document 用例不隐式修改 Workspace 树。前端在另存成功后显式调用 `workspace.refresh`；若刷新失败，报告“副本已保存、文件树刷新失败”，不把已提交操作伪报为失败。
7. 提交后的取消属于晚到取消，返回成功；响应 File ID 无法确认时使用 `UNCONFIRMED_COMMIT`，提示用户刷新核对，避免自动重试造成重复副本。

## 结果

- Document、Workspace、RPC、CEF 与 React 仍可分别使用 Fake 替换验证。
- 同目录临时文件使提交保持在同一卷内，目标不存在检查与最终非覆盖移动共同防止静默覆盖。
- 另存与工作区刷新是两个可观察步骤；后者失败不会回滚已经持久化的副本。
- 当前不提供三方合并，也不把 `MoveFileExW` 扩展为跨卷移动；目标父目录必须已存在。

## 验证

纯核心测试覆盖成功、目标冲突、提前取消与提交后晚到取消；Windows Adapter 测试覆盖中文路径、BOM、重复目标不覆盖和缺失父目录；Composition 测试通过真实 RPC/LocalFS 验证磁盘字节。前端测试覆盖单次原子调用、并发编辑保护、旧会话隔离和无法确认提交。

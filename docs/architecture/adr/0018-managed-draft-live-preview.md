# ADR 0018：托管草稿、Overlay Snapshot 与仅导出时选择目标路径

- 日期：2026-09-13
- 状态：接受，等待实现
- 关联：ADR 0003、ADR 0013、ADR 0014、ADR 0015

## 背景

当前草稿点击“保存并编译”时，必须先选择一个空文件夹，将草稿转为本地项目后才能创建 Build Snapshot。这把“预览当前内容”和“决定用户文件保存位置”错误地绑定在一起；目录选择或导入失败还会让编译根本没有开始。它也无法满足编辑器停止输入后自动更新 PDF 的准实时体验。

## 决策

1. 新建文档立即获得应用内部的稳定 Draft ID，并由应用托管草稿检查点；创建、编辑、恢复、手动编译和实时预览都不询问用户文件路径。
2. Monaco Model 继续是未保存 Buffer 的唯一可修改事实来源。原生端收到的内容仅作为带 Generation 的短生命周期 Overlay Snapshot，不成为第二份可编辑状态，也不等同于导出成功。
3. 编译快照由“只读基础快照 + 当前 Dirty Overlay”组成。新草稿没有外部 Workspace 时，基础快照为空，草稿的全部文件作为 Overlay；已有项目只传 Dirty 文件，其余文件来自只读 Workspace Snapshot。
4. Compile Workflow 只把合法的相对 File ID、有界内容和 Generation 交给 Build 端口。Build、Preview、Navigation 与 MiKTeX Adapter 不依赖 React、Monaco 或导出路径。
5. 只有用户显式执行“导出项目”或“另存为本地项目”时，才选择目标目录。普通草稿编译、实时编译、草稿检查点和已有项目保存不得弹出目标路径选择器。
6. “打开已有项目”仍可要求用户选择源目录；这是输入授权，不是草稿导出。已有项目的普通保存继续写回已授权 Workspace，不重复询问路径。
7. 预览采用 Latest-wins：同一会话最多一个活动 Build 和一个合并后的最新待编译 Generation；旧结果不得覆盖新结果。
8. 新编译期间与编译失败后保留上一份成功 PDF。只有新 Artifact 已完整读取且 PDF.js 成功接受后才原子切换。

## 路径与数据边界

- 托管草稿、Overlay Snapshot、TeX 中间文件和临时 Artifact 只能位于应用专属 AppData/Cache，不写入源码目录或用户未授权目录。
- RPC 不接收或返回任意绝对路径；文件使用相对 File ID。
- Qt 选出的导出绝对路径只保留在原生授权注册表中；React 仅获得安全显示名和会话绑定、用途绑定、有期限、单次消费的 destinationToken。
- Draft Overlay 沿用草稿容量上限，并在 RPC、Application 和 Adapter 三层验证文件数量、单文件大小、总字节数、重复路径和路径穿越。
- 导出目标必须是新建目录或空目录。导出采用逐文件原子非覆盖写入；失败时返回结构化 Partial Export 清单，不删除任何非本次创建的内容。
- 编译和导出都不得自动下载 TeX 宏包、开启不受控 Shell Escape 或上传正文。

## 影响

优点是草稿可以直接编译和实时预览，用户在真正导出之前无需决定文件位置；已有项目、Build Backend 和 PDF Artifact 边界保持可替换。代价是需要新增 Overlay Snapshot 契约、Generation 调度、导出用例和迁移测试，应用 Cache 也需要明确的容量与清理策略。

## 迁移与回滚

- Preferences 将布尔 autoCompile 迁移为 manual、onSave、live 三态。已有显式 true 映射为 onSave，已有 false 映射为 manual；没有旧记录的新用户默认 live。
- 旧 IndexedDB 草稿先迁移到新 Draft ID，确认原生检查点成功后才允许清理旧记录；迁移失败继续保留旧数据并进入只读恢复提示。
- 回滚时保留 Draft 数据和导出能力，可退回手动 Overlay 编译；不得恢复“编译前强制选择导出目录”。

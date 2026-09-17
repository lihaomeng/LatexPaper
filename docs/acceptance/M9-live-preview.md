# M9 托管草稿与实时预览验收记录

- 更新日期：2026-09-14
- 状态：已完成开发并通过自动化验收；M9.0～M9.6 的产品代码与 Full 绿色目录均已落地
- 对应需求：只有显式导出/另存为才要求目标路径；新草稿可无路径手动编译和 900 ms 实时预览。
- 设计依据：[ADR 0018](../architecture/adr/0018-managed-draft-live-preview.md)
- 实施计划：[实时预览与托管草稿开发计划](../development/实时预览与托管草稿开发计划.md)

## 2026-09-14：M9.1 Overlay Snapshot 核心增量

### 实际实现

- `build.start` 增加有界 `overlayFiles`，由 `contracts/rpc/v2/build.schema.json` 单一生成 C++ 与 TypeScript 校验代码；每项只包含相对 `fileId` 与正文，不包含原生绝对路径。
- Build Inbound 与 Snapshot Outbound 分别定义边界 DTO；Build Application 显式映射，不让 Inbound DTO 穿透到 Adapter。
- Application 同时校验最多 32 个文件、单文件最多 4 MiB、总 Overlay 最多 16 MiB、规范化相对 File ID，以及 Windows ASCII 大小写不敏感的重复路径。
- Windows Snapshot Store 允许“无 Workspace 的空基础快照”，也允许在已授权 Workspace 的只读副本上覆盖 Overlay；编译快照位于应用缓存，完成后释放，异常和取消路径清理 `.partial`。
- Snapshot 合并只写应用缓存，不写回 Monaco、草稿缓存或用户 Workspace；缓存根与 Workspace 重叠仍被拒绝。
- `KWorkspaceWorkflow` 可在未打开 Workspace 时惰性创建 Build 服务；`build.detect` 与 Overlay Build 不再以目录选择为前置条件。

### 解耦结论

- Build Domain/Application 不依赖 Qt、CEF、React、Workspace Adapter 或 MiKTeX 具体类型。
- Windows Snapshot 与 Compiler 仍通过 `IKBuildSnapshotStore`、`IKCompilerBackend` 替换；空基础快照的测试使用 Fake 编译器，未修改 Use Case。
- RPC 只负责 Schema 校验与 DTO 映射；文件路径解析、快照合并和 TeX 进程仍在各自 Application/Adapter 内。

## 2026-09-14：M9.2 草稿直接编译核心增量

### 实际实现

- React 的“编译”命令直接捕获当前 EditorSession/Monaco 全部文件并发送 Overlay，不再调用 `workspace.open`，也不再先创建本地项目。
- 顶部“新建项目”入口改为“导出草稿”；只有该显式命令请求目标目录。打开已有项目仍可请求源目录，已有项目普通保存不重复选择。
- 取消导出、目标非空或导出失败只更新导出错误，不再覆盖编译状态或清空现有 PDF。
- 草稿和本地项目均支持停止输入 900 ms 后触发编译；新安装的兼容布尔默认值设为开启。
- 编译开始、探测失败、TeX 失败、取消或无新 Artifact 时保留上一份 `artifactId` 与 PDF 字节；新 Artifact 字节读取成功后才替换 BuildView。
- 编译按钮依赖独立 `build` capability，不再依赖 `nativeFiles` capability；Workspace 与 Build 的前端能力开关已分离。
- 界面文案明确为“草稿直接在应用隔离 Snapshot 中编译；只有导出项目时才选择目录”。

### 自动化证据

工作目录均为 `D:/CodeMyself/LightOverLeaf`。

1. `scripts/internal/configure.ps1 -Preset desktop-release`：退出码 0；Contract 23 个共享 Fixture、V2 163 个共享 Fixture，架构检查 86 个显式 Target。
2. `scripts/build.ps1 -Preset desktop-release`：退出码 0；前端 ESLint、291/291 测试、TypeScript 与 Vite 生产构建通过；完整 C++/Qt/CEF Release 目标通过。
3. `scripts/test.ps1 -Preset desktop-release`：退出码 0；CTest 33/33，通过 Build、Workspace Workflow RPC、Contract、Architecture、Illegal Dependency、Codegen Drift 与桌面 CEF smoke。
4. `scripts/legacy/package-full.ps1 -SkipBuild -SmokeTimeoutSeconds 180`：退出码 0；再次执行 CTest 33/33，生成展开式 Full 绿色目录，无归档、无 7-Zip、运行时零解压。

本轮新发布证据：

- 目录：`out/distributions/full-20260914-212415-8515f29d/LightOverLeaf-Full-win64`
- 入口：`LightOverLeaf.exe`，Windows GUI Subsystem，目录 smoke 通过，用时 1942 ms。
- 内置 Runtime：源码版 MiKTeX；清单文件 19562 项，载荷 1438310587 bytes。
- Runtime Manifest SHA-256：`558379FB634D6F0F94EC3B81EC54992BC1E31FE7734435161AF49B2EC2745DD7`。
- `runtimeExtraction=false`、`archiveCreated=false`、`bundledArchiveTool=false`、`consoleApplication=false`。

### 失败与修复记录

- 首次 Release Build 的产品目标已通过，但新增 `kworkspaceworkflowrpctests.cpp` 中过深的嵌套 `KValue` 初始化在 MSVC 2019 解析失败。
- 将 Overlay 测试数据拆成具名 `KValue::KArray` 后重新构建成功；随后完整 CTest 连续两轮 33/33 通过。该失败没有进入发布结论。

## 历史未完成项（已由后续 M9.0/M9.3/M9.4/M9.5/M9.6 关闭）

- M9.0 尚未完整完成：RPC 还没有正式 Draft ID、Generation、失败 `phase`；Preferences 仍是兼容布尔值，尚未迁移为 `manual/onSave/live` 三态。
- M9.3 尚未完成：前端已有 900 ms 防抖、请求序号与迟到结果保护，但原生侧尚未实现“每会话一个 Active + 一个合并 Pending”的权威 Latest-wins 协调器，也没有把 Generation 写入契约。
- M9.4 尚未完成：编译失败会保留旧 PDF，但新 PDF 字节仍在 PDF.js 完整加载/首屏渲染前进入 BuildView；真正的 PDF.js 双缓冲原子提交和 render phase 诊断仍待实现。
- M9.5 尚未完成：当前“导出草稿”复用已有受信任 Workspace 选择与逐文件保存流程；独立 Export Application、会话绑定的一次性 `destinationToken`、原子全量/Partial Export 尚未实现。
- M9.6 尚未完成：本轮 Full 目录 smoke 证明程序直接启动，历史证据证明该 MiKTeX Runtime 能真实生成 PDF/SyncTeX；但尚未在本轮通过可见桌面人工操作验证“新草稿输入 → 900 ms → PDF.js 连续预览 → 显式导出”。代码签名、干净 Windows 虚拟机和完整论文宏包集合仍待验收。

## 历史阶段结论（已被文末最终结论替代）

用户截图中的核心阻塞已从架构上解除：草稿编译不再要求目标目录，Overlay 可在空基础 Snapshot 上进入现有 MiKTeX 编译链；目录选择已收敛到“打开已有项目”或显式“导出草稿”。自动化与新 Full 绿色目录验证通过，但由于原生 Latest-wins、三态迁移、PDF.js 原子切换和独立 Export Application 尚未完成，M9 状态保持“进行中”，不能标记整体完成。

### 2026-09-14：M9.0 三态偏好与 Generation 契约

- Preferences 从 `autoCompile` 升级为 `manual/onSave/live`；新数据库默认 `live`，旧 SQLite 布尔值幂等迁移为 `true -> onSave`、`false -> manual`。
- React 设置页改为三态选择；`live` 使用 900 ms 防抖，`onSave` 仅在成功保存/检查点后触发，`manual` 只响应按钮。已有项目实时编译直接使用不可变 Overlay，不再为了预览先写磁盘。
- Build V2 契约加入 `scopeId`、安全整数 `generation` 和 `phase`；托管草稿使用持久化 Draft ID，React/RPC 仍不接触绝对路径。
- 验证：前端 291/291；desktop-release 构建通过；Preferences、Contract、Codegen Drift 3/3 通过。

### 2026-09-14：M9.3 原生 Latest-wins 调度

- Build Application 按草稿/项目 scope 维护一个 Active 和一个合并后的 Pending；新 generation 请求停止 Active，并替换更旧 Pending。
- Pending 被替换或取消会可靠唤醒并返回 cancelled；旧 generation 在 Snapshot 物化前以冲突拒绝；完成状态保留 generation 与 phase。
- React 不再用 `activeBuildJob` 阻止新 generation 进入原生调度；请求序号仍阻止迟到日志、诊断或 Artifact 覆盖最新界面。
- 新增连续三代压力 Fixture：前两代取消，第三代成功且 Artifact generation=3；随后 generation=2 被拒绝。
- 验证：Build、WorkspaceWorkflowRpc、Contract、Architecture、Codegen Drift 5/5 通过；desktop-release 构建通过。

### 2026-09-14：M9.4 PDF.js 双缓冲与结构化阶段

- Build 契约与状态增加 `snapshot/detect/compile/artifact/render/complete` phase 和 generation；日志面板显示失败阶段及旧预览保留状态。
- 新 Artifact 仅进入 `candidate`；PDF.js 在离屏 Canvas 完成加载与首屏渲染后才原子替换可见 Canvas、artifactId、PDF 字节与 SyncTeX 元数据。
- 候选加载或渲染失败会销毁候选 LoadingTask、保留上一份已提交文档，并把失败记录为 render phase；翻页和缩放也先离屏渲染再提交。
- 新增 rendering 状态回归；前端测试 292/292、TypeScript 与 Vite 生产构建通过。

### 2026-09-14：M9.5 独立 Export Application 与原子目录提交

- 新增 `export/domain`、`export/application/inbound`、`export/application/outbound` 和 `export/adapters/localfs`，与 Workspace/Document/Build 完全分离；Composition Root 只装配端口。
- 新增 `export.selectDestination` 与 `export.project` V2 契约。Qt 只负责选择空目录；绝对路径不返回 React，前端只持有 64 字符随机 `destinationToken`、显示名和到期时间。
- Token 绑定 CEF session、10 分钟到期、仅可消费一次；跨会话、重复消费、过期、非空目录、重解析点和危险相对路径均拒绝。CEF endpoint 关闭时立即撤销该会话未使用 Token。
- React 导出当前托管草稿的不可变 Generation 快照，不切换 Workspace，不逐文件调用 Document 保存。LocalFS Adapter 先写同父目录 staging、逐文件 `CREATE_NEW` 和 flush，全部成功后以目录重命名提交；失败或取消保留目标空目录且清理 staging。
- Export 自动化覆盖：成功原子提交、单次消费、跨会话、过期、非空目录、路径穿越、取消不改目标；前端覆盖无绝对路径协议与跨方法响应拒绝。

### 2026-09-14：M9.6 最终自动化与 Full 绿色目录验收

- Preferences 三态的真实旧 SQLite schema fixture 通过：旧 `auto_compile=0` 幂等迁移为 `manual`；新库默认 `live`，应用保存 `onSave` 通过。
- 更新旧 Build V2 fixture，补齐 `scopeId/generation/phase`；Contract 23 个共享 Fixture、V2 163 个共享 Fixture保持双端生成一致。
- 前端 ESLint、TypeScript、Vite 生产构建和 294/294 测试通过。
- `desktop-release` 完整构建通过；CTest 34/34 通过，包含 GUI smoke、Renderer 恢复、RPC 取消/载荷、Build Latest-wins、Export、Persistence、Architecture、Illegal Dependency 和 Codegen Drift。
- Full 绿色目录打包与包内 smoke 通过：`out/distributions/full-20260914-234447-bf7bcab0/LightOverLeaf-Full-win64`。
- 最终入口 `LightOverLeaf.exe` 可直接运行；不创建归档，不调用 7-Zip/tar，不在启动时解压。Runtime 清单共 19562 个文件、1438377026 bytes，`runtimeExtraction=false`。
- `runtime-manifest.json` SHA-256：`BF858E092C57A67755801D4918C88A4587A65E2FD870D4F33ACBCF1AEA9A1305`。

## M9 最终结论

M9 开发任务已完成：三态编译策略、持久 Draft ID、原生 Latest-wins 调度、结构化 Generation/Phase、PDF.js 双缓冲原子切换，以及独立 Export Application 均已落地并通过自动化。外部发布前仍建议执行代码签名、干净离线 Windows 机器人工验收和目标论文宏包矩阵测试；这些属于发布签收，不再是本次 M9 产品代码的未完成项。
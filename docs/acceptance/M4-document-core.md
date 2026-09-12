# M4 文档核心增量记录

日期：2026-09-11，最新验证：2026-09-12。状态：自动化实现完成；环境边界与人工验收保留。

## 已实现

- KResult/KError 通用结果、IKDocuments 应用端口、IKDocumentStore 存储端口。
- 打开快照显式映射 File ID、正文、Revision 和 UTF-8 BOM 元数据。
- 保存携带 Expected Revision 和 Client Sequence，单次委托 Store 比较并替换，不在用例执行 read-then-write。
- UTF-8 标量、NUL、4 MiB 上限、相对路径词法、保留设备名称及 Revision 边界检查。
- 提前取消不访问 Store；成功提交后的晚到取消保持成功结果；Adapter 异常转换为错误。
- Document 不链接 Workspace、Qt、CEF、文件系统 Adapter；构造不执行 I/O。
- Workspace 独立实现打开、只读状态快照、成功后替换、失败保留旧状态和幂等关闭；不依赖 Document。
- lol_workspace_adapter_localfs 对目录树施加 10000 项／32 层默认边界，支持取消，输出相对 UTF-8 File ID、Opaque Workspace ID 和 Tree Revision，并跳过 Reparse Point。
- lol_document_adapter_localfs 使用原始字节 SHA-256 Revision、同目录随机临时文件、FlushFileBuffers、提交前复检和 ReplaceFileW；保留 UTF-8 BOM。
- IKWorkerExecutor/lol_platform_worker 提供 1～16 Worker、最多 1024 个待处理任务的显式边界；覆盖排队、资源耗尽、取消、任务异常隔离、关闭拒绝新任务和协作式 Join。

Fake 测试验证冲突不覆盖、保存不额外读取、中文文本、非法编码、容量、取消与异常映射。Fake 只存在测试中，不接入桌面生产组合根。

## 上一增量验证（历史）

| 范围 | 结果 |
| --- | --- |
| core-debug 构建及 CTest | 14/14，7.92s |
| core-release 构建及 CTest | 14/14，8.59s |
| desktop-debug 构建及 CTest | 23/23，19.96s |
| desktop-release 构建及 CTest | 23/23，21.86s |
| 前端 | M3 最终测试 216/216；M4 未修改前端业务 |

以上来自最终代码的实际双配置构建和回归；桌面构建同时运行锁定安装、前端检查与构建，216 项前端测试通过。

## 上一增量未完成项（历史，最新状态见下文）

1. Qt 受控文件夹选择及生产组合根注入；当前 Adapter 已通过临时目录测试，但桌面尚未调用。
2. Worker 已具备有界执行和协作式停止回收，但尚未注入 RPC；当前同步文件端口仍未被 UI 调用。若未来任务不响应 stop，close 可能延长，因此业务任务必须有自己的 I/O／进程超时。
3. 生成的业务 RPC、打开项目、真实文件保存和前端冲突交互。
4. Reparse Point 拒绝逻辑已实现，但本机 `CreateSymbolicLinkW` 返回失败，真实 Junction／符号链接夹具未验收。
5. 长路径、崩溃中断和外部进程在最终 Revision 复检与 ReplaceFileW 之间竞态的更强处置仍未完成。

记录完成后再次运行 Debug／Release 的 Architecture 与 IllegalDependency：各 2/2 通过；新增 Adapter 跨模块依赖负例会被主动拒绝。

因此 nativeFiles 仍为 false，界面仍是草稿工作台；不能将本记录标记为“本地保存完成”。M5～M8 尚未进入实施验收。后续直接接续上述清单，无需再次询问是否继续。

设计理由见 ADR 0005。未修改用户项目正文、未下载 TeX、未重打旧自解压包。

## 2026-09-12：本地项目业务链路与多文件保存

状态：M4 进行中，基础本地打开、编辑和保存链路已接通。

- Workspace/Document V2 Schema 统一生成 C++/TypeScript DTO 与验证器；新增数组、严格 UTF-8、正文 4 MiB 字节边界和共享 Fixture。CEF 异步 JSON 通道限制为 32 MiB，容纳转义后的正文与文件树。
- 纯 C++ RPC Endpoint 支持注入异步分发器；覆盖执行中取消、迟到完成、超时、关闭隔离和重复完成。System V1 兼容保留。
- WorkspaceWorkflow 仅组合两个模块的 Inbound 端口；RPC Handler 映射 DTO 与业务错误；桌面组合根注入本地 Adapter 和单线程有界 Worker，文件 I/O 不在 UI 线程执行。
- Qt 提供原生目录选择，目录绝对路径仅在原生组合根内传递。前端接收相对 File ID 和不透明 Workspace ID。
- React 接入本地文件树、按需读取、多标签、Revision 保存和冲突提示。nativeFiles 为 true；build/pdf/syncTex 仍为 false。
- 自动保存与 Ctrl+S 串行处理全部已修改文件，包括非活动和已关闭标签；保存期间的新修改保留 Dirty。未保存或保存中禁止切换项目。不同编辑会话使用独立 Monaco URI。
- 集成测试通过真实 CEF 值转换、临时中文目录、本地读取/保存、外部修改冲突及关闭；目录选择采用注入返回值，不能替代人工 QFileDialog 验收。

本轮证据（工作目录为仓库根，前端命令在 web）：

| 命令/范围 | 结果 |
| --- | --- |
| core-debug 构建与 CTest，业务链路增量 | 15/15 通过 |
| desktop-debug 构建与 CTest，业务链路增量 | 25/25 通过，24.14s；早于后续多文件保存修复 |
| npm.cmd run check，含多文件保存修复 | 233/233，通过 ESLint、TypeScript 与 Vite 构建 |
| cmake --build --preset core-release；ctest --preset core-release --output-on-failure | 退出码 0；15/15 通过，10.08s |
| cmake --build --preset desktop-release；ctest --preset desktop-release --output-on-failure | 退出码 0；25/25 通过，21.99s；包含多文件保存修复后的前端资源 |

运行入口：`out/desktop-release/bin/Release/LightOverLeaf.exe`。应用逻辑位于同目录 `LightOverLeaf.dll`，部署的 `web/index.html` 与本轮 `web/dist/index.html` 均引用 `index-DOfOMR00.js`。运行时须保留同目录 DLL、CEF/Qt 与 web 资源；这次更新的是运行目录，旧自解压包未重打。

最新未完成项：本地文件新建/重命名/删除；冲突后的重新加载或另存交互；CEF 桌面视觉和中文 IME；完整 4 MiB 真实进程传输边界；Reparse Point 实物夹具；长路径、崩溃中断及提交复检到替换之间的外部竞态强化。Worker 关闭依赖任务协作停止，仍需更强 I/O 超时机制。Vite 大块体积警告尚存在。

M4 尚未整体验收，M5～M8 继续保持待执行。旧打包文件尚未更新，不应作为此增量运行证据。

## 后续增量：保存冲突对比与恢复

状态：冲突恢复基础交互已实现，M4 仍进行中。

- `LocalDocumentSaveError` 携带失败 File ID。冲突时暂停自动保存，提示文件并提供“对比版本”入口。
- 对比只读取磁盘；“保留当前编辑”、关闭对话框和读取失败不修改编辑内容。仅明确点击“采用磁盘版本”才替换编辑区，同时更新保存 Revision。
- 采用前检查编辑模型单调版本和当前会话。读取期间、对比后继续编辑，以及关闭对话框后的迟到结果均不能覆盖新内容。
- Monaco 替换使用独立撤销边界，支持撤销找回原编辑；未新增原生路径能力或 RPC 方法。协调代码放在 app 层，模型操作留在 Editor Feature。
- 对比区限制显示前 32768 字符；采用使用完整已读内容。磁盘在对比后再次变化，由下一次 Revision 保存继续检测冲突。

验证：在 `web` 执行 `npm.cmd run check`，退出码 0；240/240 测试通过，ESLint、TypeScript、Vite 构建通过。新增 7 项测试覆盖只读对比、明确采用、编辑版本变化、会话关闭、读取失败、响应错配和失败文件定位。

桌面验证：`cmake --build --preset desktop-release -- /verbosity:quiet` 退出码 0；`ctest --preset desktop-release -R 'DesktopSmoke|RendererRecoverySmoke|CompositionRpcTests|ArchitectureTests|IllegalDependencyTests' --output-on-failure` 为 5/5 通过，9.69s。Release 运行目录已更新，旧自解压包未重新打包。此处为针对性回归，不表示本增量重新执行了全部 25 项桌面测试。

未验证：真实 CEF 对话框人工操作、Monaco 撤销恢复的端到端交互、极大文件对比性能。冲突合并/另存、本地文件新建/重命名/删除等 M4 余项继续保留。Vite 大块警告仍存在。本增量无 C++/Schema 修改，因此没有重复纯核心双配置测试。

## 后续增量：普通文件新建、重命名与可恢复删除

状态：实现已贯通，M4 整体验收仍进行中。

- Workspace 入站和出站分别定义管理命令，由用例映射；保持原依赖白名单，不增加端口到 Domain 的依赖。
- 新增 `workspace.manageFile`，携带 Workspace ID、操作和相对路径；请求/响应仍统一生成，增加 FILE_OPERATION_FAILED 区分文件 I/O 失败与项目未打开。
- 本地适配器支持空文件 CREATE_NEW、句柄非覆盖重命名和项目内 `.lightoverleaf-trash` 可恢复删除。拒绝目录删除、保留设备名、通配符、路径穿越、回收目录及旧项目 ID；父目录须存在。
- 删除保留完整内容，用户可从资源管理器将随机前缀文件移回原位置，再重新打开项目刷新。不会清空已有回收内容。
- UI 提供新建/重命名/删除对话框；未保存和冲突期间阻止操作，执行期间编辑器只读；成功同步文件树与标签。创建后从文件树打开新文件。
- 架构说明见 ADR 0007。树 mutation-N 为会话状态标识；当前没有外部文件监视，不作为实时磁盘内容 Revision。

已执行：core-debug 构建与 CTest 15/15 通过，7.63s，覆盖本地中文创建/改名、同名保护、取消、目录拒绝、可恢复正文与保留路径。前端第一轮检查 243/243、TypeScript/ESLint/Vite 通过；后续增加 RPC 客户端映射测试与 Fake 替换测试，最终结果另记。

剩余：目录创建/管理、外部文件变化刷新、回收界面及恢复索引、冲突合并/另存、完整桌面人工与 IME 验收、极端长路径、4 MiB 真实跨进程边界、Reparse Point 实物夹具。未完成 M4，不据此进入 M5 验收。

最终验收：`npm.cmd run check` 退出码 0，244/244；`cmake --build --preset desktop-release -- /verbosity:quiet` 退出码 0；`ctest --preset desktop-release --output-on-failure` 25/25，19.29s。包括新增的 Fake Store 管理替换测试、Windows 保留设备名校验，以及 Composition 经真实 RPC 创建/改名/删除临时文件的集成测试。Release 运行目录已更新；单独的历史自解压包未重新打包。所有删除测试仅作用于自建临时目录，未删除用户项目正文。

## 2026-09-12：外部文件变化安全刷新

状态：刷新闭环已实现并回归，M4 整体验收仍进行中。

- Workspace Application 新增显式 `refresh` 用例，通过既有 Outbound Store 重新扫描当前项目；未打开、取消、扫描失败或根身份变化时不提交新状态。
- LocalFS Adapter 复用 10000 项／32 层、取消及 Reparse Point 边界，不把绝对路径传给前端。刷新与文档读取继续由有界 Worker 执行，不阻塞 Qt UI 线程。
- `workspace.refresh` 已进入统一 Schema、C++/TypeScript 生成代码、Fixture、RPC Handler、Workflow 和 Composition。修复 `cmake/Contracts.cmake`，使 Workspace/Document Schema 与 Fixture 变化能触发 CMake 重新配置，防止业务协议生成物漂移。
- React 在文档可见时每 5 秒检查一次，并提供“刷新”命令。只协调当前会话已加载的最多 32 个模型：Revision 变化时重载 Clean 文件，磁盘已删除时关闭 Clean 文件；Dirty 文件、读取期间并发编辑、已退休会话及窗口销毁后的迟到结果均保持原内容。自动保存若恰逢刷新会登记待保存请求，并在刷新结束后补跑。
- 因 Dirty 或并发编辑而延迟的文件会保留待刷新标记；即使树 Revision 未继续变化，下一轮仍会重试。扫描与读取之间文件消失仅关闭版本未变且仍为 Clean 的模型。
- 当前轮询是可替换的过渡入口，不等同于原生实时文件监视；设计与边界见 ADR 0008。

本增量最终验证：

| 命令/范围 | 结果 |
| --- | --- |
| `cmake --preset core-debug`、构建与 `ctest --preset core-debug --output-on-failure` | 15/15 通过，6.86s |
| `npm.cmd run check` | 249/249 通过；ESLint、TypeScript、Vite 构建通过 |
| `cmake --build --preset desktop-release -- /verbosity:quiet` | 退出码 0；Release 运行目录已更新 |
| `ctest --preset desktop-release --output-on-failure` | 25/25 通过，19.97s |

测试覆盖 Fake Store 替换、失败保留旧树、根身份防护、提前取消、真实临时目录外部新增、CEF/Loopback RPC、Clean 重载、Clean 删除、Dirty/并发编辑保护、旧会话隔离和读取竞态。Vite 仍报告约 3.49 MiB 主块的体积警告。

Release 入口仍为 `out/desktop-release/bin/Release/LightOverLeaf.exe`，必须保留同目录 DLL、CEF/Qt 与 web 资源；本轮未重打历史自解压包。

M4 剩余项：目录创建/管理、原生实时文件监视、回收站恢复界面与索引、冲突合并/另存、真实桌面视觉与中文 IME 人工验收、4 MiB 真实跨进程边界、Reparse Point 实物夹具、极端长路径和崩溃中断。不得据此将 M4 标为完成或跳过剩余验收。

## 2026-09-12：独立目录管理链路

状态：目录管理增量已实现并回归，M4 整体验收仍进行中。

- 文件与目录使用独立的 Application 命令、Outbound Store DTO/方法及 `workspace.manageDirectory` Schema/RPC；没有在文件命令上增加 `isDirectory` 布尔分支，Fake Store 仍可单独替换。
- 支持目录非覆盖创建、重命名/移动和可恢复删除。重命名在 Application 状态中同步投影全部已知后代；删除从当前树移除目录及后代。本地 Adapter 把整个目录树原子移动到项目内 `.lightoverleaf-trash` 的随机名称下，不递归永久删除。
- 拒绝根目录、路径穿越、Windows 保留设备名、通配符、回收目录、Reparse Point、普通文件冒充目录、旧 Workspace ID、目标已存在以及把目录移动到自身后代。目标父目录必须已存在，取消在提交前生效，提交成功后的晚到取消不伪报失败。
- React 文件树接收显式目录列表，可显示和选择空目录；“目录”对话框提供新建、重命名/移动和可恢复删除。存在 Dirty、保存中、刷新中或冲突时不执行目录操作。
- 目录重命名同步 EditorSession 中已加载文件的相对路径与 Revision 映射；目录删除关闭其下 Clean 模型。由于操作前禁止 Dirty，目录变更不会丢失未保存编辑。
- 前端目录校验与核心路径策略保持相同的 1024 UTF-8 字节上限和 Windows 名称边界。Schema 保留通道级 4096 字节上限，Application 的更严格业务上限仍是最终判定。
- 架构与 Windows 提交语义见 ADR 0009。

本增量最终验证：

| 命令/范围 | 结果 |
| --- | --- |
| `cmake --build --preset core-debug -- /verbosity:quiet` | 退出码 0 |
| `ctest --preset core-debug --output-on-failure` | 15/15 通过，6.29s |
| `npm.cmd run check` | 265/265 通过；ESLint、TypeScript、Vite 构建通过 |
| `cmake --build --preset desktop-release -- /verbosity:quiet` | 退出码 0；Release 运行目录已更新 |
| `ctest --preset desktop-release --output-on-failure` | 25/25 通过，18.89s |

测试覆盖 Fake Store 目录替换、后代路径投影、自身后代拒绝、真实中文目录创建/改名、普通文件类型拒绝、取消、回收目录保护、子树内容恢复、空目录树显示、前端路径边界、独立 RPC 客户端，以及经真实 CEF Composition 的目录创建/子文件/改名/删除。

中途新增的 Workspace 单测曾错误地从两个临时状态快照取得 `begin/end`，导致未定义行为和超时；已改为持有单一快照后遍历，目标单测及上述全量回归均通过。

最新剩余项：原生实时文件监视、回收站恢复界面与原路径索引、冲突合并/另存、真实桌面视觉与中文 IME 人工验收、4 MiB 真实跨进程边界、Reparse Point 实物夹具、极端长路径和崩溃中断。外部工具在目录操作前后制造的未扫描条目可能要到下一次 5 秒刷新才出现在树中。M4 保持进行中。

## 2026-09-12：冲突原子另存副本

状态：冲突另存增量已实现并完成自动化回归，M4 整体验收仍进行中。

- Document 模块新增独立 `saveAs` Inbound Command 和 `createExclusive` Outbound Store；普通 Revision 保存语义不变，Workspace/Document 仍可独立替换。
- `document.saveAs` 已进入 V2 Schema、生成代码、共享 Fixture、RPC Handler、Workflow 和 Composition。V2 共享 Fixture 共 137 项，代码生成漂移检查通过。
- Windows LocalFS 在目标父目录写入隐藏临时文件，写入可选 UTF-8 BOM 并 Flush 后，以非覆盖同目录移动提交。目标已存在或并发出现时返回 `FILE_CONFLICT`，原内容保持不变；提前取消不生成目标，提交后晚到取消不伪报失败。
- React 冲突对话框增加副本相对路径与“另存副本”。协调器只发送一次原子 RPC；保存期间继续编辑、模型退休或会话切换时，已落盘副本不会反向替换当前编辑内容。
- 安全时复用并重新键控原 Monaco 模型，不额外占用第 33 个模型名额。另存成功后显式刷新 Workspace；刷新失败与无法确认提交使用独立提示，不自动重试可能已提交的写操作。
- 架构与部分提交语义见 ADR 0010。未引入前端文件系统访问、CEF 业务逻辑或跨模块 Store 依赖。

本增量最终验证（工作目录为仓库根，前端命令在 `web`）：

| 命令/范围 | 结果 |
| --- | --- |
| `cmake --preset core-debug`、构建及 `ctest --preset core-debug --output-on-failure` | 退出码 0；15/15 通过，8.17s |
| `npm.cmd run check` | 退出码 0；271/271 通过；ESLint、TypeScript 与 Vite 构建通过 |
| `cmake --build --preset desktop-release -- /verbosity:quiet` | 退出码 0；Schema 变化触发重新配置，Release 运行目录已更新 |
| `ctest --preset desktop-release --output-on-failure` | 退出码 0；25/25 通过，19.73s |

测试覆盖成功与响应关联、Fake Store 替换、目标冲突、提前/晚到取消、中文路径、BOM、缺失父目录、响应错配、并发编辑、旧会话隔离，以及真实 CEF Composition 经 LocalFS 创建副本并验证原始字节不被覆盖。Vite 仍报告约 3.50 MiB 主块体积警告。

Release 入口为 `out/desktop-release/bin/Release/LightOverLeaf.exe`；运行时必须保留同目录 `LightOverLeaf.dll`、CEF/Qt 和 `web` 资源。本增量更新可直接运行的 Release 目录，未重打历史自解压包。

M4 最新剩余项：原生实时文件监视、回收站恢复界面与原路径索引、冲突三方合并、真实桌面视觉与中文 IME 人工验收、4 MiB 真实跨进程边界、Reparse Point 实物夹具、极端长路径和崩溃中断。M4 保持进行中，不据此提前启动 M5 验收。

## 2026-09-12：带索引回收恢复与三方合并

状态：实现和自动化回归完成，M4 仍进行中。

- Workspace 新增独立回收列表/恢复 Inbound、Outbound、Workflow 和 V2 RPC，V2 共享 Fixture 增至 140 项。
- LocalFS 删除生成随机 Trash ID、正文 `.payload` 与隐藏 `.restore` 索引，记录原始相对路径、文件/目录类型和删除时间。列表忽略损坏、缺失或 Reparse Point 项，最多返回 1000 项。
- 恢复使用非覆盖句柄移动，只回到原路径；同名目标不覆盖，恢复令牌单次有效。React 增加项目回收站列表和恢复入口。
- EditorSession 保存最近一次真实持久化正文。冲突窗口生成三方合并草稿；独立行修改自动合并，同一行双向修改或行数变化保守地产生冲突标记，全部清理后才能应用。
- 架构与恢复格式见 ADR 0011。旧版无索引回收项继续保留但只能人工恢复。

本增量验证：`npm.cmd run check` 279/279 通过，ESLint、TypeScript、Vite 通过；core-debug 15/15 通过；desktop-release 25/25 通过，18.93s。Composition 经真实 CEF/LocalFS 删除、列出、恢复并核对正文原始字节。Vite 仍有约 3.51 MiB 主块警告。

剩余：原生实时文件监视、4 MiB 真实跨进程边界、Reparse Point/长路径/崩溃中断夹具，以及 CEF 桌面视觉和中文 IME 人工验收。M4 尚未整体验收。

## 2026-09-12：Windows 原生变化通知

状态：实现和自动化回归完成，M4 的自动刷新不再执行固定周期全量扫描。

- Workspace 新增可替换 `pollChanges` Inbound/Outbound；LocalFS 使用 `FindFirstChangeNotificationW` 监视根目录和全部子目录，句柄在切换项目、关闭和析构时释放。
- 新增 `workspace.pollChanges` V2 RPC，V2 Fixture 共 142 项。React 每秒读取轻量布尔信号，只有收到变化才调用既有安全刷新；监视失败仍保留手动刷新。
- 原生通知允许合并多项变化，业务层不依赖具体 Win32 事件路径，Dirty、并发编辑和已退休会话保护继续由刷新协调器负责。
- 设计见 ADR 0012。

验证：`npm.cmd run check` 282/282 通过；core-debug 相关重建与测试通过；desktop-release 全量 25/25 通过，18.63s。LocalFS 测试在真实临时目录写入新文件，验证通知触发、批量信号排空和生命周期。

M4 仍需 4 MiB 真实跨进程、特权 Reparse Point/极端长路径、崩溃中断，以及真实 CEF 中文 IME/视觉人工验收。自动化能力实现不等同于这些环境验收完成。

## 2026-09-12：后续阶段完成后的最终回归

- M5～M8 合入后重新配置完整 Release；架构检查通过，共 85 个显式 Target。
- 前端 `npm.cmd run check` 为 287/287，通过 ESLint、TypeScript 和 Vite 构建。
- 最终 `ctest --test-dir out/desktop-release -C Release --output-on-failure` 为 33/33，通过 Desktop Smoke、Renderer Recovery、真实 CEF Composition、4 MiB Renderer 往返、Workspace/Document、性能、架构、非法依赖与生成漂移测试。
- M4 的业务实现与自动化回归已完成；4 MiB 真实跨进程边界随后已关闭。当前未关闭项需要真实可见桌面、特权文件系统或崩溃注入环境；未取得这些条件，不将其表述为人工/环境验收通过。

## 2026-09-12：4 MiB 真实 CEF Renderer 往返收口

- Bootstrap 新增仅供自动验收的 `--smoke-test-payload`，创建独立临时 Workspace，并通过真实 CEF Browser/Renderer、生产 Transport、RPC Core、Workflow 与 LocalFS Adapter 执行完整链路。
- React 探针生成恰好 4 MiB UTF-8 文本，经 `document.saveAs` 原子保存，再用 `document.open` 读取并逐字节比较；任何长度或内容偏差都会使桌面进程返回非零。
- 测试结束无论成功或失败均关闭 Workspace；Bootstrap RAII 清理进程专属临时目录。本轮最终 `LightOverLeafRpcPayloadSmoke` 通过，耗时 1.62 秒，测试后未发现残留目录。
- 该测试关闭“4 MiB 真实跨进程边界”遗留项，不把 Loopback 或纯 C++ 调用冒充 CEF 跨进程证据。

M4 当前外部验收项缩减为：特权 Reparse Point 实物夹具、系统级极端长路径、崩溃中断注入，以及可见 CEF 中文 IME/视觉人工验收。业务实现、自动化回归及 4 MiB 跨进程边界已完成。

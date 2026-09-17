# LightOverLeaf 架构与开发要求（V2，Electron 迁移补充）

| 属性 | 内容 |
|---|---|
| 文档状态 | 项目架构基线 |
| 目标平台 | Windows 10/11 x64 |
| 技术栈 | C++20 后端、Electron、React、TypeScript、Monaco、PDF.js、CMake |
| 产品形态 | 本地单用户、离线优先的桌面 LaTeX 编辑器 |
| 更新日期 | 2026-09-13 |

## 2026-09-17：Electron 桌面架构决定

用户已要求用 Electron + TypeScript 替换 Qt/CEF 外壳，C++ 保留编译与原生业务。现行规则以 [ADR 0019](../architecture/adr/0019-electron-native-backend.md) 为准；下文涉及 Qt/CEF 的进程、资源和发布说明保留为旧版记录，不约束新的 Electron 外壳。模块边界、业务安全和 RPC 契约规则继续生效。

- Renderer：React + TypeScript，通过沙箱 preload 暴露的 NativeBridge 调用原生能力。
- Electron main：窗口、目录选择、受控资源加载与 C++ 后端生命周期。
- C++ backend：现有项目、文档、编译、预览、导航与持久化模块；不依赖 Electron ABI、Qt 或 CEF。
- 默认唯一日常编译命令仍为 `scripts/build.ps1 --dev`，现在选择 `electron-dev`，关闭测试构建和执行。
- 输出入口：`out/electron-dev/bin/Release/LightOverLeaf.exe`。
- 旧 Qt/CEF 仅显式 `-Preset desktop-release --dev` 构建；历史测试不代表 Electron 已验收。

## 1. 文档定位

本文档定义 LightOverLeaf 的产品边界、模块职责、依赖方向、通信契约、线程模型、工程结构和验收标准。它是架构实现和代码审查的基线。

开发 C/C++、Qt、CEF、插件或 CMake 代码时，还必须遵守 [`docs/Codex-Rules.md`](../Codex-Rules.md) 中的 K 风格、生命周期、安全和验证规则。

当实现需要突破本文档中的架构不变量时，必须先新增或修改 ADR，说明动机、替代方案、迁移路径和回滚方式。禁止用临时跨层调用绕开约束。

## 2. “绝对解耦”的工程定义

软件模块不可能完全没有依赖。LightOverLeaf 所要求的“绝对解耦”，定义为所有依赖都必须满足以下可验证条件：

1. 依赖方向单向、显式，并由 CMake Target 表达。
2. 业务模块只依赖自己拥有的 Domain、Inbound Port 和 Outbound Port，不依赖其他模块实现。
3. 跨业务模块调用只能经过对方的 Inbound Port，或由独立 Workflow Coordinator 编排。
4. 外部技术能力只能通过 Application 所拥有的 Outbound Port 接入。
5. Qt、CEF、Win32、JSON、SQLite、文件系统和 TeX 类型不得进入 Domain 或 Application 公共接口。
6. 不共享可变全局状态，不使用全局 Service Locator，不通过单例隐藏依赖。
7. 每个 Adapter 都能被 Fake 或另一实现替换，而不修改 Use Case。
8. 每个传输层都能被替换，而不修改 RPC Core、Application 或 Domain。
9. 每个模块可单独编译、单独测试，并可通过依赖图检查证明没有非法边。
10. 模块构造不得产生隐式 I/O、线程、进程或网络副作用。

“解耦”不等于为每个类创建 DLL。默认使用静态库和接口 Target；只有 CEF Bootstrap、明确插件 ABI 或独立发布单元才使用动态库。

### 2.1 解耦验收标准

一个模块只有同时满足以下条件才算完成解耦：

- 公共头文件不包含外层框架类型。
- CMake 只链接允许的 Target。
- 测试可以只使用 Fake Outbound Port 运行。
- 更换 Adapter 不需要修改 Application 和 Domain。
- 更换 CEF Transport 不需要修改 RPC Handler 和 Use Case。
- 关闭可选模块后，其他模块仍可配置、编译和测试。
- 跨模块数据通过明确 DTO 或稳定 ID 传递，不共享内部实体地址。
- 没有循环依赖、反向 Include、隐式注册和跨模块私有头文件引用。

## 3. 产品目标与首版边界

### 3.1 首版闭环

1. 打开或创建本地 LaTeX 项目。
2. 管理项目文件和目录。
3. 使用 Monaco Editor 编辑多文件 LaTeX 源码。
4. 原子保存并处理外部修改冲突。
5. 调用本地 TeX 工具链进行可取消编译。
6. 展示结构化错误、警告和日志。
7. 使用 PDF.js 预览编译产物。
8. 使用 SyncTeX 完成源码与 PDF 双向定位。
9. 保存设置、布局和会话，并在异常退出后恢复。

### 3.2 首版不包含

- 用户账号、云项目和远程同步。
- 实时多人协作、评论、聊天和修订跟踪。
- AI 写作、AI 改写或自动编译修复。
- GitHub、Dropbox、Zotero 等云端集成。
- 可视化/WYSIWYG 编辑。
- macOS 和 Linux 正式发布。

未经用户明确扩展范围，不得提前建立上述功能的框架、接口或占位模块。

## 4. 已确认的开发环境

| 项目 | 当前基线 | 约束 |
|---|---|---|
| 第三方库根目录 | `D:/CodeMyself/QTBest/thirdparty_install` | 仅作为当前默认值，通过 `LIGHTOVERLEAF_THIRDPARTY_ROOT` 覆盖 |
| CEF | 150.0.14 / Chromium 150.0.7871.129 | Windows x64 Standard Binary Distribution |
| Qt | 5.15.11 x64-windows | 不向核心层泄漏 Qt 类型 |
| CMake | 3.28.1 | 项目最低版本建议 3.21 |
| C++ | C++20 | 不退回 C++11 设计 |
| Node.js | 24.14.0 | 用于 React/Vite 构建 |
| npm | 11.9.0 | 提交 `package-lock.json`，Release 使用 `npm ci` |
| TeX | 当前未检测到 | 编辑功能不得依赖 TeX 已安装 |

参考工程：

- `D:/CodeMyself/QTBest/LSnap`：只参考 Application Core 和功能拆分思路。
- `D:/CodeMyself/QTBest/CEF`：参考 Qt5 + CEF Bootstrap、Sandbox、线程切换和运行时复制。

禁止复制 LSnap 的 C++11、递归目录扫描、无边界 DLL 拆分和隐式模块注册方式。

## 5. 总体架构

### 5.1 编译期依赖

箭头表示“左侧允许 Include 或链接右侧”：

~~~text
React Feature
    └──> Frontend Application Service
            └──> Generated NativeApi
                    └──> CEF Transport Adapter
                            └──> RPC Core
                                    └──> Module Inbound Port
                                            └──> Module Application
                                                    ├──> Module Domain
                                                    └──> Module Outbound Port <── Module Adapter

Composition Root
    ├──> Module Application Factory
    ├──> Module Adapter Factory
    ├──> RPC Core / CEF Transport
    ├──> Qt Shell / CEF Platform
    └──> Technical Infrastructure
~~~

依赖始终从外层指向内层。运行时返回值和事件可以向外传播，但不能因此形成反向编译依赖。

### 5.2 运行时边界

~~~text
Renderer Process
  React + Monaco + PDF.js
          │ versioned RPC
          ▼
Browser Process
  CEF Transport -> RPC Core -> Inbound Ports -> Use Cases
                                         │
                              Outbound Ports
                 ┌──────────────┼──────────────┐
                 ▼              ▼              ▼
             File Adapter   TeX Adapter   Storage Adapter
                 │              │              │
                 ▼              ▼              ▼
              Windows       TeX Process      SQLite/AppData

Qt Shell --------------> Inbound Ports
CEF Platform -----------> Browser lifecycle only
Composition Root -------> creates and wires all concrete objects
~~~

Qt Shell 和 React 是两个独立 Inbound Adapter。两者可以调用相同 Use Case，但不得直接互相调用或共享可变界面状态。

## 6. 业务模块划分

按业务上下文垂直拆分，而不是把整个项目做成一个巨型 Domain 和一个巨型 Application。

| 模块 | 拥有的业务能力 | 主要 Inbound Port | 主要 Outbound Port | 禁止承担 |
|---|---|---|---|---|
| System | 能力发现、版本、运行模式 | `IKGetCapabilities` | `IKRuntimeProbe` | 项目与编辑业务 |
| Workspace | 项目根、目录树、创建/重命名/删除 | `IKOpenWorkspace`、`IKManageWorkspaceEntry` | `IKWorkspaceStore`、`IKFileWatcher` | 编辑器 Buffer、TeX 进程 |
| Document | 打开内容、Revision、保存与冲突 | `IKOpenDocument`、`IKSaveDocument` | `IKDocumentStore` | 文件树扫描、PDF 渲染 |
| Search | 文件名和正文搜索、取消 | `IKStartSearch` | `IKSearchIndex` | 直接修改文件 |
| Build | 工具链发现、编译任务、诊断 | `IKDetectCompiler`、`IKStartBuild` | `IKCompilerBackend`、`IKBuildSnapshotStore` | UI 日志渲染、PDF 页面渲染 |
| Preview | PDF Artifact 发布与访问授权 | `IKGetPreviewArtifact` | `IKArtifactStore` | 解析 LaTeX 源码 |
| Navigation | SyncTeX 正向与反向定位 | `IKForwardSync`、`IKReverseSync` | `IKSyncTexBackend` | 直接启动编译器 |
| Preferences | 用户设置和工具链配置 | `IKGetPreferences`、`IKUpdatePreferences` | `IKPreferencesStore` | 会话标签状态 |
| Session | 标签、布局、光标和恢复点 | `IKRestoreSession`、`IKSaveSession` | `IKSessionStore`、`IKHistoryStore` | 接管用户源码 |

### 6.1 跨模块规则

- 一个模块不得 Include 另一个模块的 Domain、Application 实现或 Adapter 头文件。
- 简单调用依赖对方的 Inbound Port。
- 涉及两个及以上模块的流程由 `orchestration/` 中的 Workflow Coordinator 编排。
- Coordinator 只依赖各模块 Inbound Port 和集成 DTO，不依赖 Domain 或 Adapter。
- Domain Event 只在所属模块内部使用，不直接跨模块发布。
- 跨模块通知使用显式、带版本的 Application Event Contract。
- 禁止用全局 Event Bus 隐藏关键业务流程。事件适合通知，不代替必须成功的命令调用。
- 禁止创建含有业务语义的 `common`、`utils` 或 `manager` 巨型模块。

## 7. 单个模块的标准结构

每个业务模块采用相同结构：

~~~text
modules/<module>/
├── domain/
│   ├── include/lightoverleaf/<module>/domain/
│   ├── src/
│   └── CMakeLists.txt
├── application/
│   ├── inbound/
│   ├── outbound/
│   ├── include/lightoverleaf/<module>/application/
│   ├── src/
│   └── CMakeLists.txt
├── adapters/
│   └── <technology>/
│       ├── include/
│       ├── src/
│       └── CMakeLists.txt
└── tests/
    ├── domain/
    ├── application/
    └── adapters/
~~~

### 7.1 Domain

- 只包含实体、值对象、领域服务、领域错误和模块内部 Domain Event。
- 只依赖 C++ 标准库和 `lol_kernel`。
- 不包含 Qt、CEF、Win32、JSON、SQLite、文件系统、线程池或日志框架。
- 不依赖其他业务模块的 Domain。
- 不执行 I/O，不创建线程，不读取环境变量。

### 7.2 Inbound Port

- 描述模块向外提供的 Use Case。
- 接口由提供能力的模块拥有，调用方只依赖接口 Target。
- 参数和返回值使用 Application DTO、稳定 ID 和 `KResult<T>`。
- 不暴露 Application 实现类和 Domain 实体的可变引用。

### 7.3 Outbound Port

- 描述 Application 完成用例所需的外部能力。
- 接口由消费该能力的 Application 模块拥有，而不是由 Adapter 拥有。
- 不以具体技术命名，例如使用 `IKDocumentStore`，不使用 `IKQtFileStore`。
- 不暴露 `QString`、`QObject*`、`CefRefPtr`、`HANDLE`、数据库连接或 JSON 对象。

### 7.4 Application

- 实现 Inbound Port，编排 Domain 和 Outbound Port。
- 负责权限、状态转换、事务边界、取消和错误转换。
- 不依赖具体 Adapter、Qt、CEF、Win32 或 React。
- 对外只暴露 Inbound Port 和 Factory；实现类保持私有。

### 7.5 Adapter

- 实现某个模块的 Outbound Port。
- 可以依赖对应 Outbound Port、技术基础设施和必要第三方库。
- 不得调用其他 Adapter，不得编排跨模块业务。
- 将平台错误转换为 Application 可理解的稳定错误。
- 每种技术实现独立 Target；开发态系统 TeX Adapter 与产品态严格内置 MiKTeX Adapter 分开。

### 7.6 Composition Root

- 唯一负责创建对象、选择 Adapter、注入依赖和控制销毁顺序的位置。
- 可以依赖所有 Factory 和平台模块。
- 不包含业务判断、路径规则、RPC Handler 或文件操作。
- 通过显式构造注入，不使用全局注册表和静态自注册。

## 8. 推荐项目目录

~~~text
LightOverLeaf/
├── .agents/skills/lightoverleaf-development/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
│   ├── ArchitectureRules.cmake
│   ├── Dependencies.cmake
│   ├── CefBootstrap.cmake
│   ├── CefRuntime.cmake
│   ├── Frontend.cmake
│   ├── ProjectOptions.cmake
│   └── Warnings.cmake
├── contracts/
│   └── rpc/v1/
│       ├── envelope.schema.json
│       ├── system.schema.json
│       ├── workspace.schema.json
│       ├── document.schema.json
│       ├── search.schema.json
│       ├── build.schema.json
│       ├── preview.schema.json
│       ├── navigation.schema.json
│       ├── preferences.schema.json
│       └── session.schema.json
├── apps/
│   ├── bootstrap/
│   │   ├── CMakeLists.txt
│   │   └── main.cpp
│   └── desktop/
│       ├── CMakeLists.txt
│       ├── kapplicationcomposition.h
│       ├── kapplicationcomposition.cpp
│       └── resources/
├── src/
│   ├── kernel/
│   │   ├── include/lightoverleaf/kernel/
│   │   ├── src/
│   │   └── CMakeLists.txt
│   ├── modules/
│   │   ├── system/
│   │   ├── workspace/
│   │   ├── document/
│   │   ├── search/
│   │   ├── build/
│   │   ├── preview/
│   │   ├── navigation/
│   │   ├── preferences/
│   │   └── session/
│   ├── orchestration/
│   │   ├── compileworkflow/
│   │   └── shutdownworkflow/
│   ├── transport/
│   │   ├── rpc/
│   │   │   ├── core/
│   │   │   └── generated/
│   │   └── cef/
│   ├── infrastructure/
│   │   ├── filesystem/
│   │   ├── process/
│   │   ├── sqlite/
│   │   └── logging/
│   └── platform/
│       ├── qt/
│       ├── cef/
│       └── windows/
├── web/
│   ├── package.json
│   ├── package-lock.json
│   ├── vite.config.ts
│   ├── tsconfig.json
│   ├── .generated/rpc/
│   └── src/
│       ├── app/
│       ├── native-api/
│       ├── features/
│       │   ├── workspace/
│       │   ├── explorer/
│       │   ├── editor/
│       │   ├── search/
│       │   ├── build/
│       │   ├── preview/
│       │   ├── navigation/
│       │   ├── preferences/
│       │   └── session/
│       └── shared/
├── tests/
│   ├── architecture/
│   ├── contracts/
│   ├── integration/
│   ├── endtoend/
│   └── fixtures/
├── tools/
│   ├── architecture/
│   └── contractcodegen/
├── docs/
│   ├── development/
│   ├── architecture/adr/
│   └── acceptance/
├── resources/
│   ├── icons/
│   └── licenses/
└── scripts/
    ├── configure.ps1
    ├── build.ps1
    ├── test.ps1
    └── package.ps1
~~~

`.generated/`、构建目录、CEF Cache、LaTeX 辅助文件和会话数据必须加入忽略规则，不得作为手写源码维护。

## 9. CMake Target 模型

### 9.1 每个业务模块的 Target 模板

以 Document 模块为例：

| Target | 类型 | 允许依赖 | 职责 |
|---|---|---|---|
| `lol_document_domain` | Static | `lol_kernel` | Document 领域模型和规则 |
| `lol_document_inbound` | Interface | `lol_kernel`、只读 Application DTO | Use Case 接口 |
| `lol_document_outbound` | Interface | `lol_kernel`、必要领域值类型 | 存储与外部能力接口 |
| `lol_document_app` | Static | 上述三个 Target | Use Case 实现与编排 |
| `lol_document_adapter_localfs` | Static | `lol_document_outbound`、文件基础设施 | 本地文件实现 |
| `lol_document_tests` | Executable | Domain、Application、Fake Ports | 单元与模块测试 |

其他业务模块使用相同模式：

- `lol_<module>_domain`
- `lol_<module>_inbound`
- `lol_<module>_outbound`
- `lol_<module>_app`
- `lol_<module>_adapter_<technology>`
- `lol_<module>_tests`

没有领域规则的模块可以省略 Domain Target，但不得把业务逻辑塞进 Adapter。

### 9.2 跨模块与平台 Target

| Target | 类型 | 允许依赖 |
|---|---|---|
| `lol_kernel` | Static | C++ 标准库 |
| `lol_workflow_app` | Static | 各模块 Inbound Port |
| `lol_rpc_contract_cpp` | Generated Static/Interface | `lol_kernel` |
| `lol_rpc_core` | Static | RPC Contract、模块 Inbound Port |
| `lol_transport_cef` | Static | `lol_rpc_core`、CEF |
| `lol_platform_qt` | Static | Qt、必要 Inbound Port |
| `lol_platform_cef` | Static | CEF、生命周期接口 |
| `lol_platform_windows` | Static | Win32、平台端口 |
| `lol_infra_process` | Static | Win32/C++ 标准库，无业务语义 |
| `lol_infra_sqlite` | Static | SQLite，无业务语义 |
| `LightOverLeafApp` | Shared | Composition Root 和所有具体模块 |
| `LightOverLeafBootstrap` | Win32 Executable | CEF Bootstrap 所需最小依赖 |
| `LightOverLeafTests` | Executable | 集成测试装配 |

### 9.3 依赖白名单

~~~text
<module>_domain   -> kernel
<module>_inbound  -> kernel [+ own read-only value types]
<module>_outbound -> kernel [+ own read-only value types]
<module>_app      -> own domain + own inbound + own outbound
<module>_adapter  -> own outbound + approved infrastructure
workflow_app      -> module inbound ports only
rpc_core          -> generated rpc contract + module inbound ports
transport_cef     -> rpc_core + cef platform
qt_shell          -> module inbound ports + qt platform
composition       -> factories + adapters + transports + platforms
~~~

任何未列出的依赖默认禁止。尤其禁止：

- Application 链接 Adapter。
- Domain 链接 Application、Qt、CEF 或 Infrastructure。
- Adapter 链接另一个 Adapter。
- RPC Core 链接 CEF。
- 业务模块直接链接另一个业务模块的 Domain 或 App 实现。
- 为方便 Include 而把仓库根目录加入公共 Include Path。

### 9.4 CMake 约束

- 使用显式 Source 列表和显式 `add_subdirectory`。
- Target 的 Include、Compile Definition 和 Link 必须使用 `PRIVATE`、`PUBLIC`、`INTERFACE` 正确表达。
- 公共头文件放在模块自己的 `include/lightoverleaf/...` 下，只暴露必要目录。
- 第三方路径集中在 `Dependencies.cmake`，业务 CMake 不出现绝对路径。
- 使用 `ArchitectureRules.cmake` 为 Target 标注层级和允许依赖。
- Configure 或测试阶段生成依赖图并拒绝非法边和环。
- 禁止递归 `GLOB`、目录自动注册和链接整个第三方根目录。

## 10. 类型、接口与错误模型

### 10.1 类型隔离

| 类型 | 所属位置 | 可跨越边界 |
|---|---|---|
| Domain Entity/Value Object | 模块 Domain | 仅模块 Domain/Application |
| Application Command/Result | Inbound Port | 调用方与模块 Application |
| Outbound Port DTO | Outbound Port | Application 与对应 Adapter |
| Integration DTO/Event | Provider 模块的集成契约 | Workflow 和订阅方 |
| RPC DTO | `contracts/rpc` 生成代码 | RPC Core 与前端 NativeApi |
| React View Model | Frontend Feature | 仅前端 Feature |
| Qt/CEF/Win32 类型 | Platform/Adapter | 不得进入内层公共接口 |

不得把同一个结构体同时当作 Domain Entity、数据库记录、RPC DTO 和 React View Model。边界处必须显式映射。

### 10.2 K 风格接口

- 自有 `class`、`struct`、`enum` 和类型别名使用 `K` + PascalCase。
- 纯抽象接口使用 `IK` + PascalCase。
- 成员使用 `m_` + camelCase，方法和参数使用 camelCase。
- 自有 C/C++ 文件全小写、无下划线和连字符，并与主类型对应。
- 旧文档中的 `IFileStore` 等概念名称，实际实现使用 `IKFileStore`。

详细规则以 [`docs/Codex-Rules.md`](../Codex-Rules.md) 为准。

### 10.3 结果与错误

- 核心层统一使用项目自有 `KResult<T>` 或等价 Result 类型。
- 常规业务失败不使用 C++ 异常控制流程。
- Adapter 捕获平台或第三方异常，并映射为稳定 Application Error。
- RPC Error 由 RPC Core 在最外层映射，不让 JSON 错误码进入 Domain。
- 错误至少包含稳定 Code、面向用户的 Message Key、可诊断 Context 和可重试标记。
- 日志可记录关联 ID，但不得记录密钥、令牌、完整项目正文或不必要的绝对路径。

## 11. RPC 契约与代码生成

### 11.1 单一事实来源

`contracts/rpc/v1/*.schema.json` 是 RPC 的唯一事实来源。

生成内容：

- C++ DTO、校验器和 Method/Event 常量生成到 CMake Binary Directory。
- TypeScript DTO、Validator 和 `NativeApi` 基础代码生成到 `frontend/web/.generated/rpc/`。
- 生成文件不得手工修改，也不得与手写 DTO 并行维护。
- `npm run dev`、`npm test` 和 CMake Configure 必须在需要时先执行 Contract Codegen。
- CI/本地验证要检查 Schema 生成后工作区没有非预期差异。

### 11.2 传输解耦

`lol_rpc_core` 只接收和输出项目自有的字节串/文本消息抽象，不包含任何 CEF 类型。

`lol_transport_cef` 只负责：

- CEF Process Message 与 RPC Envelope 之间的转换。
- Renderer/Browser Process 生命周期。
- 消息大小限制、连接状态和发送失败。
- 将消息交给 `lol_rpc_core`，不执行业务逻辑。

测试中使用 `KLoopbackTransport` 替换 CEF Transport，完整测试 RPC Core 和 Use Case。

### 11.3 Envelope

~~~json
{
  "version": 1,
  "id": "uuid",
  "method": "document.save",
  "params": {},
  "clientSequence": 42
}
~~~

成功响应：

~~~json
{
  "version": 1,
  "id": "uuid",
  "ok": true,
  "result": {},
  "serverSequence": 108
}
~~~

失败响应：

~~~json
{
  "version": 1,
  "id": "uuid",
  "ok": false,
  "error": {
    "code": "FILE_CONFLICT",
    "messageKey": "error.fileConflict",
    "details": {}
  }
}
~~~

事件：

~~~json
{
  "version": 1,
  "event": "build.output",
  "sequence": 109,
  "payload": {
    "jobId": "job-id",
    "stream": "stderr",
    "text": "..."
  }
}
~~~

### 11.4 契约规则

- Schema 默认 `additionalProperties: false`。
- 每个请求验证版本、ID、Method、Payload 大小、字段类型和业务状态。
- 未知 Method、字段或枚举值必须拒绝。
- 破坏兼容性的变更新增版本，不原地改变既有语义。
- 每个异步请求支持取消或明确标记不可取消。
- 事件具有单调递增 Sequence，前端忽略过期事件。
- 高频日志事件批量发送并设置有界队列；队列满时采用明确背压策略。
- RPC 不传递原始 Shell 命令、任意绝对路径、指针、句柄或平台对象。

首批 Method：

- `system.getCapabilities`
- `workspace.open`、`workspace.close`、`workspace.getState`
- `workspace.list`、`workspace.create`、`workspace.rename`、`workspace.remove`
- `document.open`、`document.save`
- `search.start`、`search.cancel`
- `build.detect`、`build.start`、`build.cancel`
- `preview.getArtifact`
- `navigation.forwardSync`、`navigation.reverseSync`
- `preferences.get`、`preferences.update`
- `session.restore`、`session.save`

稳定错误码至少包括：

- `INVALID_ARGUMENT`
- `METHOD_NOT_FOUND`
- `WORKSPACE_NOT_OPEN`
- `PATH_OUTSIDE_WORKSPACE`
- `FILE_NOT_FOUND`
- `FILE_CONFLICT`
- `COMPILER_NOT_FOUND`
- `BUILD_FAILED`
- `BUILD_TIMEOUT`
- `BUILD_CANCELLED`
- `SYNCTEX_NOT_AVAILABLE`
- `RESOURCE_EXHAUSTED`
- `INTERNAL_ERROR`

## 12. 状态所有权

| 状态 | 唯一权威拥有者 | 前端可持有内容 |
|---|---|---|
| Workspace Root 和文件树 Revision | Workspace Application | 快照和展开状态 |
| 已保存文档内容和 Revision | Document Application/Store | 打开时快照 |
| 未保存编辑 Buffer | Editor Feature 的 Monaco Model | 唯一未保存副本 |
| 编译 Job 状态 | Build Application | 只读投影 |
| PDF Artifact | Preview/Artifact Store | Artifact ID、页码和缩放 |
| SyncTeX 映射 | Navigation Application | 最近一次定位结果 |
| 用户配置 | Preferences Application | 只读投影和编辑草稿 |
| 标签、布局和光标 | Session Application | 当前 UI 投影 |

禁止在多个层同时维护可独立修改的同一份业务状态。

### 12.1 文档保存协议

1. `document.open` 返回内容、Encoding、Revision 和 File ID。
2. Monaco Model 成为该标签页未保存内容的唯一拥有者。
3. 自动保存发送完整内容、Expected Revision 和 Client Sequence；MVP 不引入增量协同协议。
4. Native 执行原子写入，成功后返回新 Revision。
5. 前端只接受不早于当前 Client Sequence 的响应。
6. 外部修改造成 Revision 不一致时返回 `FILE_CONFLICT`，不得静默覆盖。
7. 用户选择覆盖、重新加载或另存时，必须通过新的明确 Command 完成。

## 13. CEF、Qt、线程与生命周期

### 13.1 进程

- Bootstrap Process：初始化 CEF Sandbox 并加载应用模块。
- Browser Process：运行 Qt Shell、Composition Root、Application 和 CEF Browser。
- Renderer Process：运行 React、Monaco 和 PDF.js。
- GPU/Utility Process：由 CEF 管理。
- TeX Process Tree：由 Build Adapter 独立监管。

### 13.2 调度器接口

跨线程只能使用项目拥有的调度器端口：

- `IKQtUiDispatcher`
- `IKCefUiDispatcher`
- `IKWorkerExecutor`
- `IKCompilerSupervisor`

规则：

- 只在 Qt GUI 线程访问 `QWidget`、`QPixmap` 和界面状态。
- CEF Browser 生命周期和相关回调只在要求的 CEF 线程执行。
- Worker 不持有裸 `QObject*`；使用稳定 ID、值对象、`QPointer` 或 Context-bound Callback。
- Qt 到 CEF 使用 `CefPostTask` 或 CEF Dispatcher。
- CEF 到 Qt 使用 Queued Connection 或 Qt Dispatcher。
- Application 的异步公共接口不暴露 Qt Signal、CEF Callback 或 Win32 Handle。
- 文件扫描、数据库、搜索、编译、日志读取和进程等待不得阻塞 UI 线程。
- Shutdown Workflow 按“停止接收请求、取消任务、终止进程树、断开 Renderer、关闭 Browser、销毁 Qt 对象”顺序执行。
- 所有等待必须有明确退出条件；可能阻塞的等待必须有超时和诊断。

## 14. 关键工作流

### 14.1 打开项目

~~~text
React/Qt -> IKOpenWorkspace
         -> Workspace Application
         -> IKWorkspaceStore Adapter
         -> canonical root + tree revision
         -> Application DTO
         -> RPC/Qt projection
~~~

Workspace Adapter 负责路径规范化和根目录安全；UI 不保存可用于任意文件访问的绝对路径权限。

### 14.2 编辑与保存

~~~text
Monaco buffer
    -> document.save(fileId, content, expectedRevision, clientSequence)
    -> RPC validation
    -> IKSaveDocument
    -> IKDocumentStore
    -> same-directory temp + flush + atomic replace
    -> new revision
~~~

### 14.3 编译

~~~text
Compile Workflow Coordinator
    1. 请求 Document 模块保存所有 Dirty Buffer
    2. 请求 Workspace 模块创建只读 Snapshot
    3. 调用 Build 模块启动 Job
    4. Build Adapter 在隔离目录运行 TeX
    5. 诊断通过 Application Event 输出
    6. 成功产物登记到 Preview Artifact Store
    7. Navigation 模块登记对应 SyncTeX Artifact
~~~

Build 模块不读取 Monaco 状态，也不直接询问 React。编译输入必须来自已确认的 Workspace Snapshot，避免保存与编译竞态。

### 14.4 PDF 和 SyncTeX

- Preview 返回不透明 Artifact ID 和受控应用 URL，不暴露 PDF 绝对路径。
- CEF Resource Handler 通过 `IKArtifactStore` 读取指定 Artifact。
- PDF.js 只负责显示、页码、缩放和点击坐标。
- Navigation 将 File ID/源码位置与 Artifact ID/PDF 坐标相互转换。
- 旧 Job 的 Artifact 和 SyncTeX 事件不得覆盖当前 Job。

## 15. 文件系统与编译器 Adapter

### 15.1 文件安全

- 所有路径先规范化，再验证最终路径仍位于 Workspace Root。
- 必须处理 `..`、Junction、符号链接、盘符、UNC 和大小写差异。
- RPC 使用相对 File ID；绝对路径只存在于 Workspace/File Adapter 内。
- 文本默认 UTF-8，读取时检测 BOM；不把字节数当 Unicode 字符数。
- 保存使用同目录临时文件、Flush 和原子替换。
- 会话和历史写入应用数据目录，不写入用户源码。
- 编译输出写入应用 Cache 下的 Workspace/Job 隔离目录。

### 15.2 编译器发现

产品版只有一种发现规则：若应用旁存在 `runtime/miktex`，Build 与 SyncTeX 只允许使用该源码构建 Runtime，并关闭用户配置、系统目录与 PATH 回退。Runtime 不完整时，编辑、保存和项目管理仍可用，但编译明确返回 `COMPILER_NOT_FOUND`/Runtime 不完整提示。

未携带 Runtime 的开发构建可按测试 Root、`LIGHTOVERLEAF_TEX_ROOT`、开发配置和系统常见目录进行发现。该回退只服务开发和 Adapter 替换测试，不构成发布形态。

### 15.3 编译任务

每个 Job 必须具有：

- 唯一 Job ID。
- 不可变 Workspace Snapshot ID。
- 主文件 File ID。
- 编译引擎和工具链 ID。
- 隔离的输入/输出目录。
- Timeout 和 Cancellation Token。
- 有界 stdout/stderr 缓冲与背压。
- 结构化 Diagnostic。
- PDF 和 SyncTeX Artifact ID。
- 可查询的终态和清理策略。

启动进程必须传递“Executable + Argument Vector”，不得拼接 Shell 字符串。默认不启用不受控 Shell Escape，不静默联网安装宏包。

编译编排优先使用 `latexmk`；直接调用 XeLaTeX、pdfLaTeX 或 LuaLaTeX 作为明确的备用 Backend。Application 不知道具体发行版。

## 16. React 前端架构

~~~text
frontend/web/src/
├── app/          组合、路由、Provider、全局布局
├── native-api/   Generated DTO 的薄封装、请求、事件、取消
├── features/     按用户能力隔离的 Feature
└── shared/       无业务语义的 UI、Hook 和纯工具
~~~

规则：

- 每个 Feature 只通过自己的 `index.ts` 暴露公共 API。
- Feature 不导入其他 Feature 的内部文件。
- 跨 Feature 编排放在 `app/` 的 Controller/Coordinator 中。
- `native-api/` 不包含界面状态和业务判断。
- React Component 不解析 TeX 日志、不访问本地文件、不构造原生命令。
- Monaco Model 生命周期由 Editor Feature 管理。
- PDF.js/Monaco Worker 通过受控资源 URL 加载。
- 每个异步状态明确区分 Idle、Loading、Success、Error 和 Cancelled。
- 请求和事件使用 Sequence/Revision 防止过期结果覆盖新状态。
- 使用 ESLint `no-restricted-imports` 强制 Feature 和 Native API 边界。
- 前端测试使用 Fake NativeApi，不依赖 CEF 和本地文件系统。

## 17. 安全基线

- 所有外部输入均视为不可信，包括 RPC、文件、日志、设置、环境变量和持久化数据。
- 检查长度、容量、下标、枚举、整数转换、溢出和资源上限。
- 禁止外部格式串、无界缓冲区操作和用指针 `sizeof` 推断数组长度。
- 禁止把外部数据拼接到 Shell、SQL、动态库路径和配置表达式中。
- SQLite 使用参数化语句。
- 动态库只从规范化、可信白名单目录加载。
- Release 启用 CSP，禁止远程脚本，默认关闭 DevTools。
- CEF Cache 使用应用专属目录。
- TeX 编译限制并发、时间、日志大小和进程树。
- 不上传项目内容，不静默联网下载宏包。
- 发布包包含 Qt、CEF、Monaco、PDF.js、SQLite 和其他依赖许可证。

## 18. 架构强制执行

仅在文档中写规则不算完成，M0 必须建立自动检查。

### 18.1 原生依赖检查

`tools/architecture/` 提供脚本读取 CMake File API Codemodel 或 Target Graph，检查：

- Target 是否具有已知 Layer 和 Module 标记。
- 依赖是否在白名单中。
- 是否存在循环依赖。
- Domain/Application 是否错误链接 Qt、CEF、Win32、SQLite 或 Adapter。
- RPC Core 是否错误链接 CEF。
- 是否出现跨模块 Domain/App 实现依赖。

检查作为 `LightOverLeafArchitectureTests` 运行，并纳入默认测试流程。

### 18.2 Include 边界检查

- 每个 Target 只暴露自己的 Public Include 根。
- 禁止全仓库 `src` Include Path。
- 架构测试扫描公共头文件中的 `QString`、`QObject`、`CefRefPtr`、`HWND` 和 JSON 类型。
- `clang-tidy` 或等价静态检查作为可选开发工具；缺少工具时必须明确报告，不得假装执行。

### 18.3 前端边界检查

- ESLint 禁止 Feature 深层互相导入。
- ESLint 禁止 Feature 直接导入 CEF/Transport 实现。
- TypeScript 编译必须使用生成的 RPC DTO。
- Contract 测试验证 C++ 和 TypeScript 对同一 Fixture 的接受/拒绝结果一致。

### 18.4 替换性测试

每个 Outbound Port 至少有一个 Fake。关键 Adapter 通过同一套合约测试：

- `KInMemoryDocumentStore` 与本地文件 Adapter。
- Fake Compiler、开发态系统 TeX Adapter 与产品态严格内置 MiKTeX Adapter。
- In-memory Preferences/Session Store 与 SQLite Adapter。
- Loopback Transport 与 CEF Transport。

替换测试的标准是：更换 Composition Root 中的 Factory 后，Use Case、RPC Handler 和前端代码无需修改。

## 19. 测试策略

### 19.1 Domain

- 纯内存、确定性、无 Qt/CEF/文件系统。
- 覆盖边界值、不变量和错误转换。

### 19.2 Application

- 只使用 Fake Outbound Port。
- 覆盖成功、失败、取消、超时、重复请求和状态冲突。
- 跨模块 Workflow 使用 Fake Inbound Port 验证调用顺序与补偿路径。

### 19.3 Adapter

- 使用临时目录或隔离数据库。
- 覆盖中文路径、长路径、只读文件、外部修改、Junction 和异常退出。
- Process Adapter 覆盖参数构建、超时、取消和进程树终止。

### 19.4 RPC Contract

每个 Method 至少覆盖：

- 正常请求。
- 缺失字段、未知字段和类型错误。
- 未知 Method 和版本。
- 稳定错误码。
- 取消和重复 ID。
- 大日志事件顺序、批处理和背压。
- C++ 与 TypeScript Validator 一致性。

### 19.5 Frontend

- 文件树和搜索状态。
- 多标签、Dirty 状态和自动保存。
- 外部修改冲突。
- 编译中、成功、失败和取消。
- Diagnostic 跳转、PDF 重载和 SyncTeX。
- 设置、布局和会话恢复。

### 19.6 端到端

固定 Fixture 包含多文件、中英文、图片、BibTeX、可成功编译版本、可定位错误版本和可取消场景。

验收链路：打开项目、编辑、保存、编译、预览、制造错误、跳转、修复、重新编译、SyncTeX、重启恢复。

## 20. 性能与稳定性目标

以下均为需要实际测量的首版目标：

- 冷启动到可交互不超过 3 秒。
- 普通文件切换反馈不超过 100 毫秒。
- 自动保存完成不超过 500 毫秒，不含异常慢磁盘。
- 点击编译后 200 毫秒内进入 Running 状态。
- 正常取消后 2 秒内终止 TeX 进程树。
- 1000 个项目文件时文件树和搜索仍可操作。
- Renderer 崩溃不得损坏项目源文件。
- 正常编辑时异常退出最多丢失约 5 秒未保存输入。

每个指标必须记录设备、数据集、构建类型、测量方法和结果。未测量不得声称达标。

## 21. 构建与发布

### 21.1 原生构建

- 使用 CMake Presets 管理编译器、架构、第三方根目录和输出目录。
- 中央依赖变量为 `LIGHTOVERLEAF_THIRDPARTY_ROOT`。
- Debug/Release 不混用 Qt、CEF 或 MSVC Runtime。
- CEF Runtime、Resources、Locales 和 Sandbox 文件由专用 Helper 复制。
- 构建脚本失败必须返回非零退出码。

### 21.2 前端构建

- Development 使用 Vite Dev Server。
- Release 使用 `npm ci`、Contract Codegen、测试和 `npm run build`。
- CMake 只消费确定的 `frontend/web/dist`。
- Release 使用受控应用 URL，不依赖本地 Web Server。
- 前端构建失败必须使发布构建失败。

### 21.3 发布形态

只发布 `Full` Windows x64 绿色目录，并且只包含独立源码构建、经过验证的 MiKTeX Runtime。Lite、Portable TeX Live、单文件自解压 EXE 和运行时归档均不再是产品发布形态。

最终目录保留 Qt、CEF、Web、许可证与 `runtime/miktex` 的原始层级；用户直接运行 `LightOverLeaf.exe`。启动过程不得创建或展开 `payload.zip`/`payload.7z`，不得调用归档工具、CMD 或 PowerShell。发布目录可整体复制，不能只分发入口 EXE。

Build Application、RPC 和 React 继续只依赖 `IKCompilerBackend` 等端口。MiKTeX 源码必须在独立工作区构建并安装到隔离目录，不得作为 LightOverLeaf 默认 CMake 子目录或业务静态库。打包只消费安装后的 Runtime；运行时禁止自动下载缺失宏包。详细决策见 [ADR 0017](../architecture/adr/0017-full-green-directory-release.md) 与 [MiKTeX 源码构建与集成](MiKTeX源码构建与集成.md)。

## 22. 分阶段实施计划

| 阶段 | 目标 | 主要产物 | 解耦验收 |
|---|---|---|---|
| M0 架构骨架 | 建立目录、Target、规则与测试入口 | CMake、模块模板、架构检查、Contract Codegen 骨架 | 非法依赖测试可主动失败 |
| M1 Qt/CEF Shell | 建立 Bootstrap、窗口和浏览器生命周期 | Bootstrap、App DLL、Qt/CEF Platform | Shell 不包含业务逻辑 |
| M2 React 与资源 | 建立前端边界和离线资源加载 | Vite、React、Fake NativeApi、资源 Handler | 前端可脱离 CEF 测试 |
| M3 RPC Core | 建立 Schema、生成代码、路由和 Transport | Ping/Capabilities、Loopback/CEF Transport | RPC Core 不链接 CEF |
| M4 Workspace/Document | 文件树、编辑、多标签和原子保存 | 两个独立模块及 Adapter | Fake Store 可替换本地文件 |
| M5 Search/Build | 搜索、Snapshot、编译、取消和诊断 | Search/Build 模块及多个 Backend | 切换 TeX Backend 不改 Use Case |
| M6 Preview/Navigation | PDF Artifact、PDF.js 和 SyncTeX | Preview/Navigation 模块 | PDF 路径不暴露给前端 |
| M7 Preferences/Session | 设置、恢复、历史和质量完善 | SQLite Adapter、恢复 Fixture | In-memory Store 可替换 SQLite |
| M8 发布 | Full 绿色目录和干净环境验证 | 完整目录、许可证、清单、验收记录 | 发布装配严格内置 MiKTeX，业务层保持端口化 |
| M9 托管草稿与实时预览 | 草稿无路径编译、Latest-wins 与独立导出 | Overlay Snapshot、实时调度、Export Application | 编译不依赖导出路径，Adapter 不互调 |

不得用大量 UI 占位掩盖底层能力和解耦验收未完成。

## 23. 完成定义

一项任务只有同时满足以下条件才算完成：

1. 需求行为已经实现。
2. 模块职责和依赖方向没有被破坏。
3. 没有新增隐式全局状态、跨层调用或 Adapter 互调。
4. 公共接口、Schema 和生成代码已经同步。
5. 相关 Domain、Application、Adapter、Contract 或 Frontend 测试通过。
6. 错误、取消、超时、乱序、资源释放和 Shutdown 路径经过验证。
7. 构建或运行证据来自本次修改后的实际执行。
8. 文档、ADR 和实现一致。
9. 明确报告未测试项和剩余风险。

## 24. M0 立即执行清单

初始阶段执行以下 M0 清单，不提前实现完整编辑器；当前实施状态以第 25 节和对应验收记录为准：

1. 初始化 Git 和 `.gitignore`。
2. 创建 V2 推荐目录和模块模板。
3. 创建 `lol_kernel`、System/Workspace/Document 的 Domain、Inbound、Outbound、App 空 Target。
4. 创建 `lol_rpc_core`、`lol_transport_cef`、Platform 和 Composition 空 Target。
5. 建立 `ArchitectureRules.cmake` 和非法依赖测试样例。
6. 增加 `LIGHTOVERLEAF_THIRDPARTY_ROOT` 检测与明确错误信息。
7. 建立 RPC V1 Envelope Schema 和最小 Contract Codegen。
8. 建立 C++/TypeScript 对同一 Ping Fixture 的契约测试入口。
9. 建立 React/Vite 空应用、Fake NativeApi 和 Import Boundary 检查。
10. 完成 Debug/Release 全新 Configure 与 Build。
11. 验证 Bootstrap 启动、React 空页面显示和安全关闭。
12. 记录实际命令、环境、结果和未完成项。

M0 不安装 TeX、不实现 Monaco、不实现 PDF.js、不建立云端/AI/协作模块，也不引入通用 Event Bus 或插件系统。

## 25. 实施状态与下一条完整业务链路

更新日期：2026-09-13。本节区分计划与已实现代码，不降低前述验收要求。

- M0：架构骨架、显式 Target、依赖检查、契约生成和双语言 Fixture 已建立，历史验证见 `docs/acceptance/M0.md`。
- M1：Qt/CEF 外壳新增独立纯 C++ 生命周期策略、两次 Renderer 恢复上限、加载与关闭超时。Qt 只依赖浏览器端口；CEF 不调用 Qt 实现。CEF 子窗口通过父 HWND 的原生客户区尺寸适配 Qt 高 DPI，不把逻辑像素直接用于 Win32 子窗口。当前 Smoke 覆盖草稿工作台就绪与重载，不承诺未保存原生项目文档恢复。
- M2：React/Vite 与离线资源加载可运行；开发模式允许 Fake，桌面生产模式要求真实 CEF 通道。用户已授权仅放开 style-src-attr，Monaco 行定位已修复且浏览器视觉复验通过；桌面完整视觉、中文 IME 与 Worker 执行证据仍需补齐。PDF Worker 在 M6 接入，不声称全部资源类型已验收。
- M3：System RPC 基线已验收。会话逻辑提取到纯 C++ KRpcEndpoint，CEF/Loopback 运行共用替换场景，真实跨进程取消与 Renderer 恢复通过双配置桌面测试；见 `docs/acceptance/M3-endpoint-substitution.md`。
- M4：独立 Workspace/Document 已接通本地项目、多标签保存和冲突恢复；文件与目录管理、带索引回收恢复、冲突三方合并、原子非覆盖另存以及 Windows 原生变化通知驱动的安全刷新均已贯通。文件、目录、回收、刷新与另存使用独立 DTO、Store 方法和 RPC Schema；Dirty、并发编辑和旧会话结果不得覆盖。真实 CEF Renderer 的 4 MiB 保存/读取逐字节往返已通过；特权 Reparse Point/极端长路径/崩溃中断夹具及可见桌面中文 IME 仍是环境/人工验收项，最新证据见 `docs/acceptance/M4-document-core.md`。
- M5：Search/Build 的端口、快照、latexmk 优先与直接引擎回退、Windows 进程后端、取消、超时、实时有界状态/日志和分级诊断已贯通；源码版 MiKTeX XeLaTeX 已真实生成 PDF 与 SyncTeX。完整论文宏包、BibTeX/Biber 编排和干净机流程仍待发布级验收。
- M6：Preview Artifact、分块 RPC、PDF.js 离线 Worker、50%～300% 缩放、编辑器/PDF 双向定位和 Windows SyncTeX 命令适配器已贯通。真实适配器通过私有临时目录物化 PDF/`.synctex.gz`、无 Shell 启动、Job Object、超时、输出上限和严格解析；Basic Fake 仅用于测试，生产无命令时使用 Unavailable Adapter。Artifact 缓存有 ID 校验、并发保护及 20 项/512 MiB 治理。运行时只在发现 `synctex.exe` 后报告 `syncTex=true`；本机无 TeX，真实论文端到端仍待外部环境验收。
- M7：Preferences/Session 独立模块、SQLite Adapter、RPC、设置页、新建项目、保存后 900 ms 自动编译，以及布局/标签/编辑区宽度/光标/PDF 缩放恢复和最近项目已贯通；可见 CEF 窗口人工复验待补。
- M8：发布形态已收敛为源码版 MiKTeX Full 绿色目录。单文件自解压启动器、Lite、Portable TeX 与 7-Zip 运行链路均已移除；入口为 Windows GUI Subsystem，直接目录 smoke 与内置 XeLaTeX PDF/SyncTeX 实编译通过。代码签名与独立干净虚拟机人工编辑/编译/预览仍待完成。
- M9：用户已确认只有显式导出/另存为才要求目标路径；打开已有项目仍可选择源目录，已有项目普通保存不重复选择。2026-09-14 已实现有界 Overlay Snapshot、空基础草稿编译、草稿手动/900 ms 前端防抖入口、旧 PDF 字节保留和显式“导出草稿”入口，Release 自动化与新 Full 绿色目录通过。原生 Generation/Latest-wins、Preferences 三态、PDF.js 原子提交、独立 Export Application/destinationToken 及发布级人工验收仍未完成，不得描述为 M9 整体完成。

最新逐阶段状态、证据和产物路径统一见 `docs/development/阶段进度.md`、`docs/acceptance/性能与遗留收口.md`、`docs/acceptance/M5-search-build.md`～`M8-release.md` 与 `docs/acceptance/M9-live-preview.md`，不得用本节历史段落覆盖最新验收结论。

2026-09-11 界面增量：参考用户截图写入深色三栏工作台、文件树、大纲和 Monaco 草稿编辑。草稿恢复适配器与原生项目文件服务分离；设计见 ADR 0003。64 项前端测试和桌面双配置各 11 项测试通过，但 Monaco style 属性仍被严格 CSP 阻止，视觉验收未通过，等待用户确认样式权限。不能据此将 M2/M4 标记为全部完成。详见 `docs/acceptance/M2-draft-workbench.md`。

本轮决策见 `docs/architecture/adr/0002-lifecycle-native-ping.md`，实际命令与验证结果见 `docs/acceptance/M1-native-ping.md`。

授权后更新：用户于 2026-09-11 明确确认最小样式权限调整；修复后前端 65 项测试、桌面双配置构建及各 11 项测试通过，浏览器宽／窄窗口及中文草稿恢复复验通过。上段为修复前历史状态，最新证据见 M2 验收记录末尾。

默认先完成 M2 剩余桌面视觉／输入与离线资源复验。2026-09-11 用户随后明确要求“请你继续 M3”，本次允许先推进 M3 并保留 M2 未验收项；不据此将 M2 标记完成。后续业务链路如下：

1. 完善 M3 单一 Schema：请求、响应、错误、事件与取消；通过共享 Fixture 和 Loopback/CEF 适配器契约测试。
2. Workspace/Document 先建立纯核心模型、Inbound/Outbound 和 Fake 测试，再添加本地文件 Adapter；不得在 CEF Handler 内读取项目文件。
3. 实现打开项目、文件树、读取 Revision、编辑、预期 Revision 保存与冲突提示；保存成功之后才清除 Dirty。
4. 接入 Monaco 多标签及草稿检查点，再扩展 Renderer 恢复为可恢复文档状态。
5. 按 M5～M8 完成可插拔 TeX、诊断、PDF、SyncTeX、持久化及发布验收。未安装 TeX 时明确报告能力不可用，不能模拟编译成功，也不能静默下载安装。

## 26. 顺序执行与本地阶段记录

根据用户要求，自 2026-09-11 起严格按第 22 节顺序推进；2026-09-13 起阶段范围扩展为 M0 → M9。阶段状态统一维护于 [阶段进度](阶段进度.md)。

1. 开始阶段前，核对其前置阶段验收记录、现有实现和剩余问题；不重复冒领历史成果。
2. 当前阶段达到第 23 节完成定义后，先在 `docs/acceptance/` 写入实际改动、模块边界、测试命令与结果、证据位置、风险和未验证项，再更新阶段总表，之后进入下一阶段。
3. 验收必须包含本阶段解耦要求。功能测试通过不能替代依赖检查、Adapter 替换测试或必要的视觉验收。
4. 历史上提前实现的原型可复用，但后续阶段仍须独立验收；空 Target、占位 UI、Fake 成功和草稿缓存不代表真实业务交付。
5. 阶段失败或阻塞时立即记录原因、已尝试的安全方案、缺少的权限或条件；不跳过、不伪报完成。涉及新增安全权限时必须取得明确授权。
6. 每次修改后按风险执行相关回归；历史命令不能冒充本次验证。仅修改文档时明确记载未重新构建或测试。
7. 每阶段完成后向用户简要报告阶段名称、验收结论及本地记录路径。若后续修改破坏已验收能力，记录回归并修复后再继续推进。
8. 用户此前授权连续执行至 M8；2026-09-14 已明确要求开始修改 M9 的无路径编译问题。实施仍须按 M9 子阶段记录，不能把核心增量自动扩展为新增安全权限，也不能在 Latest-wins、独立导出和发布级验收前宣称 M9 整体完成。

## 27. 托管草稿、实时预览与独立导出

本节是 M9 的规范性要求。对于草稿预览，它取代第 14.3 节“先保存全部 Dirty Buffer 再创建 Build Snapshot”的旧前置条件；已有本地项目的显式保存与 Revision 冲突规则保持不变。详细设计见 [ADR 0018](../architecture/adr/0018-managed-draft-live-preview.md) 和 [M9 开发计划](实时预览与托管草稿开发计划.md)。

1. 新建内容立即获得稳定 Draft ID。新建、编辑、检查点、恢复、手动编译与实时预览不得要求用户目标路径。
2. 打开已有项目可以选择源目录；只有用户显式导出或另存为时才选择目标目录。已有项目普通保存使用既有授权 Workspace，不重复弹窗。
3. Monaco Model 是未保存正文的唯一可修改事实来源。Build 只消费带 Generation 的不可变 Overlay Snapshot，不得回写编辑器、Draft Store 或 Workspace。
4. 新草稿快照由空基础快照加全部草稿 Overlay 构成；已有项目由固定 Revision 的只读基础快照加 Dirty Overlay 构成。任何编译准备都不得修改用户文件。
5. 编译调度默认使用 900 ms 停止输入防抖；同一会话最多一个活动 Job 和一个合并后的最新 Pending Generation。旧任务必须协作取消，所有迟到事件和 Artifact 必须按 Generation 丢弃。
6. 新编译开始、失败或取消时保留上一份成功 PDF。只有 Artifact 完整校验且 PDF.js 加载成功后才能原子切换；错误必须区分 snapshot、detect、compile、artifact 和 render。
7. Preferences 使用 manual、onSave、live 三态。已有布尔设置按 true 到 onSave、false 到 manual 迁移；没有旧记录的新用户默认 live。
8. Export 是独立 Application Use Case，不得依赖或启动 Build。Qt 只负责选择目录与登记授权；Local Filesystem Adapter 负责校验、原子非覆盖写入和 Partial Export 报告。
9. React 和通用 RPC 不得接收绝对路径。原生目录授权以会话绑定、用途绑定、有期限、单次消费的 destinationToken 表示；取消、过期、重复使用和跨会话使用必须失败。
10. M9 当前已完成 Overlay Snapshot 与草稿直接编译的核心增量及自动化，但原生 Latest-wins、Preferences 三态、PDF.js 原子切换、独立 Export Application/destinationToken 和发布级人工验收仍未完成。只有 [M9 验收记录](../acceptance/M9-live-preview.md) 满足第 23 节完成定义后，才能宣称 M9 整体交付。

## 2026-09-15：日常开发构建规则

用户要求后续日常修改仅以 `scripts/build.ps1 --dev` 构建通过作为默认验证，不编译测试目标、不执行 CTest 或 npm test。

在项目根目录运行：

```powershell
.\scripts\build.ps1 --dev
```

默认构建 desktop-release。可指定其他预设，例如 `scripts/build.ps1 -Preset desktop-debug --dev`。开发模式显式设置 BUILD_TESTING=OFF，前端执行 npm ci 和 npm run build（含 TypeScript 与协议生成），保留配置阶段架构检查。产物仍在所选 preset 的 bin/Release 或 bin/Debug 中，现存测试二进制不会被删除，也不会参与本次构建。

不带 --dev 的 build.ps1 会重新配置为 BUILD_TESTING=ON、LIGHTOVERLEAF_DEV_BUILD=OFF，恢复原构建流程。测试与打包脚本仍保留为显式命令；后续不自动调用，除非用户另行要求。
## 2026-09-17：后端目录统一

现行原生源码根为 backend/latexlocalservice，下设 texengine、workspace、persistence、transport、app；目录名不含连字符。历史文件树仅作设计背景，实际映射见 backend/latexlocalservice/README.md。C++ 头文件命名空间、Target 元数据与依赖规则保持不变；架构检查必须扫描新位置，不能因旧目录不存在而跳过校验。共享协议与夹具仍位于根目录 contracts 和 tests/fixtures。

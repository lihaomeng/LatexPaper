---
name: lightoverleaf-development
description: 为使用 C++20、Qt5、CEF、React、TypeScript、Monaco、PDF.js 和可插拔 TeX 工具链构建的 LightOverLeaf Windows 桌面 LaTeX 编辑器进行设计、实现、审查、测试或打包。适用于本仓库的架构、功能、缺陷、构建、依赖、RPC、编译、SyncTeX、测试、安全或发布工作。
---

# LightOverLeaf 开发准则

将 LightOverLeaf 构建为严格遵守依赖边界、本地优先的 Windows LaTeX 编辑器。保持用户要求的范围，优先采用范围最小、可验证且不破坏架构边界的变更。

## 规则来源与优先级

根据任务类型读取以下文档：

- 架构、功能、依赖、RPC、编译、打包或跨模块变更：修改文件前完整阅读[架构与开发要求](../../../docs/development/LightOverLeaf_开发要求.md)。
- 新增或修改 C/C++、Qt、插件、平台和 CMake 代码：同时完整阅读[C++ / Qt K 风格质量规范](../../../docs/Codex-Rules.md)。
- 仅涉及 React/TypeScript 时，K 命名规则不适用，但架构边界、安全、输入校验和验证要求仍适用。

规则冲突时，依次服从用户当前要求、适用的 `AGENTS.md`、LightOverLeaf 项目规则，再使用通用 K 风格规范。不得静默选择冲突规则；若冲突会改变公共接口、持久化格式或既有兼容性，应先说明影响。

开发要求中的接口名和文件树属于概念示例。新建纯抽象 C++ 接口使用 `IK` 前缀；新建自有 C/C++ 文件遵守 K 风格文件命名。不得为了统一风格而批量重命名任务范围之外的已有代码。

## 开始工作前

先检查：

1. 当前路径适用的 `AGENTS.md`、格式化配置和仓库规则。
2. 待修改模块的公共接口、实现、测试、CMake Target 和调用方。
3. 工作区已有变更，并保留与任务无关的用户工作。
4. 依赖与工具链的实际路径、版本和可用性，不得仅凭文档假设已经安装。
5. 受影响模块、公共契约、依赖方向、数据所有权、线程归属、外部输入、失败模式和验证计划。

只修改完成当前任务所需的文件。不要顺手进行无关的全局格式化、重命名、依赖升级或架构迁移。

## 产品边界

首个版本定位为 Windows 10/11 x64、本地运行、单用户、离线优先的 LaTeX 编辑器。

首版包含项目文件管理、Monaco 源码编辑、本地编译、结构化诊断、PDF.js 预览、SyncTeX、设置、会话恢复和打包。

除非用户明确扩大范围，否则不得增加云账户、远程同步、实时协作、聊天、AI 写作、可视化/WYSIWYG 编辑、第三方云集成或跨平台正式发布。

## 架构不变量

采用端口与适配器架构，依赖只能向内：

`domain <- application <- adapters/platform <- composition root`

必须遵守以下边界：

- Domain 只依赖 C++ 标准库和领域层自有类型。
- Application 只依赖 Domain 和 Ports，不得依赖 Qt、CEF、Win32、JSON 传输或具体 Adapter。
- Adapter 实现文件、项目、编译器、SyncTeX、设置、历史、时钟和后台执行端口；Adapter 之间不得直接编排业务。
- Bridge 只负责 RPC 校验、DTO 转换、事件和取消，并调用 Application Use Case；不得直接操作文件或启动进程。
- Qt 负责原生外壳、对话框和生命周期；CEF 负责浏览器嵌入、资源加载和进程集成。两者不得进入 Domain 或 Application 公共头文件。
- React 只能通过带版本、强类型的 `NativeApi`/RPC 契约调用原生能力。
- 桌面应用 Target 是唯一组合根，负责将具体 Adapter 注入 Use Case；组合根不得承载业务规则。
- 内部原生模块默认使用静态库；仅在 CEF Bootstrap 或明确 ABI/独立发布需求下使用动态库。
- 禁止循环 Target 依赖、全局可变 Service Locator，以及 CEF Handler、Qt Widget 或 React Component 中的业务逻辑。
- 文件扫描、搜索、历史、编译、SyncTeX 和进程等待不得阻塞 Qt 或 CEF UI 线程。

若变更与不变量冲突，应先说明冲突，并提出端口、适配器或契约层的解决方案。

## C++ / Qt K 风格基线

以下规则适用于新增和被实质修改的自有 C/C++、Qt、插件与平台代码；第三方代码和生成代码除外：

- `class`、`struct`、`enum` 和类型别名使用 `K` + PascalCase；纯抽象接口使用 `IK` + PascalCase。
- 非静态成员使用 `m_` + camelCase；方法、自由函数、参数和局部变量使用 camelCase。
- 自有 C/C++ 文件名全小写，不含下划线和连字符，并与主类型对应；入口文件可使用 `main.cpp`。
- 多语句控制块使用 Allman 风格；`catch` 始终带大括号。功能修改不得制造无关的全文件空白差异。
- 同一访问级别下，成员函数和成员变量必须分区，并分别重复写出 `public:`、`protected:` 或 `private:`。
- 头文件默认使用 `#pragma once`，优先前置声明，减少不必要的 Include。
- 编译期常量使用 `constexpr`；当类型影响单位、所有权或错误语义时不得滥用 `auto`。
- 所有指针、句柄、描述符、布尔值、数值和状态成员必须显式初始化。
- 公共 API 使用运行时校验；断言只验证内部不变量，不得包含副作用。
- 常规业务失败通过结果对象、返回值或稳定错误码传递，不使用异常作为普通控制流。

开发文档中的 `IProjectRepository`、`IFileStore` 等概念接口，在新增代码中对应 `IKProjectRepository`、`IKFileStore`。目录示例中的 `rpc_dispatcher.cpp` 等概念文件，在新增代码中应按主类型改为 `krpcdispatcher.cpp` 等 K 风格名称。

## 正确性、所有权与线程

- 优先栈对象、RAII、智能指针和容器值语义；每个资源只能有一个明确所有者。
- Qt 对象使用 Parent 所有权或智能指针二选一，不得重复释放；异步回调观察 `QObject` 生命周期时使用 `QPointer` 或受控 Context Object。
- 禁止 `delete this`；构造函数不执行可能失败的业务操作，也不随意创建线程。
- 动态库、文件、COM、进程、线程、锁、句柄和 CEF 对象必须覆盖成功与失败路径的释放。
- 只在 GUI 线程访问 `QWidget`、`QPixmap` 和界面状态；后台结果通过 Queued Connection、Dispatcher 或 `CefPostTask` 回到所属线程。
- 应用退出前必须请求后台任务停止，并通过有边界的协议完成 Join/Wait；不得依赖偶然时序。
- UTF-8 是跨层默认文本表示。仅在 Windows API Adapter 边界使用 `std::wstring`/`wchar_t`，不得让其进入 Domain、跨平台模型或 RPC。
- 一份业务状态只保留一个事实来源，不维护可能分叉的 UI 与原生重复副本。

## 输入、路径与进程安全

把文件、RPC、命令行、环境变量、设置、缓存、TeX 日志和用户输入全部视为不可信数据。

- 外部值用于长度、容量、下标、偏移、枚举、分配或循环次数前，检查上下界、符号、转换、溢出和合理上限。
- 缓冲区操作必须显式携带容量并检查结果；禁止无界复制、外部格式串和用指针 `sizeof` 推断数组长度。
- 路径必须规范化，并验证解析后的真实路径仍位于活动项目根目录内；处理 `..`、Junction、符号链接、盘符、UNC 和大小写差异。
- RPC 长期文件标识使用相对项目路径，不向 React 暴露任意绝对路径能力。
- 禁止把外部输入拼接到 Shell、SQL、格式串、动态库路径或配置表达式中。
- TeX 进程必须通过“可执行文件 + 参数数组”启动，不能通过插值 Shell 命令启动。
- 日志不得泄露密钥、令牌、项目正文或不必要的用户绝对路径。

## RPC、文件与编译任务

- 统一维护 RPC 请求、响应、事件、错误和取消模型，并在 Bridge 边界拒绝未知方法、未知字段、非法类型和无效状态。
- 使用稳定方法名与机器可读错误码；传输 DTO 与领域实体分离；破坏兼容性的契约变更必须升级版本。
- C++ 与 TypeScript 契约必须同步更新并通过契约测试，不得维护无校验的重复类型。
- 文件读取返回 Revision；保存携带预期 Revision，使用同目录临时文件和原子替换。冲突返回 `FILE_CONFLICT`，不得静默覆盖。
- 每次编译具有唯一 Job ID、项目与主文件快照、隔离输出目录、取消令牌、超时、有界日志、结构化诊断和进程树终止。
- 系统 TeX Live、MiKTeX 和可选 Portable TeX 是可互换 Adapter；Application 不得依赖具体发行版。
- 不得静默下载 TeX 宏包、启用不受控 Shell Escape 或传输项目内容。

## React 前端边界

- `app/` 负责组合，`native-api/` 负责 RPC，`features/` 按用户能力拆分，`shared/` 只放无业务语义的公共代码。
- Feature 不得导入其他 Feature 的内部文件；跨 Feature 协调通过 `app/` 或明确公共接口完成。
- React Component 不解析 LaTeX 日志、不操作本地文件、不构造原生命令，也不持有原生实现细节。
- Monaco Model、PDF.js Worker 和 Monaco Worker 由所属 Feature 管理，并通过受控应用资源 URL 加载。
- 异步界面必须明确表示 Loading、Success、Error 和 Cancelled 状态，并防止过期响应覆盖新状态。

## 构建与依赖

- 使用 C++20、显式 CMake Target、显式 Source 列表和显式 `add_subdirectory`；禁止递归 GLOB 自动纳入模块。
- 从可配置的 `LIGHTOVERLEAF_THIRDPARTY_ROOT` 解析原生依赖，业务代码不得硬编码 `D:/CodeMyself/QTBest/thirdparty_install`。
- 不得把 CEF 或其他第三方源码树、二进制发行包复制到仓库。
- CEF Runtime 复制和 Bootstrap 细节集中到专用 CMake Helper。
- 前端依赖由 `package-lock.json` 锁定；Release 使用 `npm ci` 和 `npm run build`，前端构建失败必须使发布构建失败。
- 生成文件、构建产物、CEF Cache、LaTeX 辅助文件和用户会话数据不得写入源码目录。
- 验证 Debug/Release 的 MSVC Runtime、Qt DLL、CEF 二进制、Resources、Locales、Sandbox 和许可证是否匹配实际产物。

## 实施与验证流程

实施期间：

1. 优先修改或新增范围最小的公共接口。
2. 在可行时，先为新增行为添加失败的单元测试或契约测试。
3. 通过 Use Case 或 Adapter 实现，不得绕过分层。
4. 错误、取消、超时、资源释放和异常退出路径与成功路径同等重要。
5. 架构不变量、公共契约或已确认决策变化时，同步更新开发文档或 ADR。

宣布完成前，根据风险执行：

1. 最小相关单元测试和静态检查。
2. RPC 变更对应的 C++/TypeScript 契约测试。
3. CMake 变更后的全新 Configure 与 Build。
4. 编辑流水线变更后的编译、PDF 和 SyncTeX Fixture。
5. 打包变更后的干净目录启动、运行时文件和许可证检查。

报告实际命令、结果、未执行项和剩余风险。明确区分已复现、已编译、已运行、静态推断和需要人工验收；不得因为代码存在或静态检查通过就声称功能可用。

## 审查输出

审查时先报告可操作发现，再给摘要。按以下顺序排序：

1. 安全漏洞、未定义行为、崩溃、数据损坏和线程竞态。
2. 错误结果、状态不一致、资源泄漏和平台/ABI 不兼容。
3. 性能退化、可维护性、命名和格式问题。

每条发现包含精确文件与位置、触发条件、实际影响、对应规则、最小修复和证据边界。不要把猜测写成已确认缺陷。

## 完成定义

任务只有在需求行为完成、依赖方向未破坏、公共接口或 RPC 已同步、相关验证通过、失败与释放路径得到处理、文档与实现一致，并报告未测试项和风险后，才可宣布完成。

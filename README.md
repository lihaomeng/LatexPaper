# LightOverLeaf

Windows 本地 LaTeX 编辑器。Qt/CEF 外壳、React 深色三栏工作台、Monaco 多标签、本地项目、搜索、可取消 TeX 编译、PDF.js 预览、SyncTeX 适配器、SQLite 偏好与会话恢复均已接入。无 TeX 时应用会明确报告编译与 SyncTeX 不可用，不生成伪造结果。

当前进展、自动化证据和外部环境阻塞见 [阶段进度](docs/development/阶段进度.md)。最新 Lite 单文件 EXE 见 [M8 发布记录](docs/acceptance/M8-release.md)。

按 M0 → M8 顺序执行的状态、阻塞与本地记录入口见 [阶段进度](docs/development/阶段进度.md)。

M3 System RPC 基线已通过：纯 C++ 会话端点、Loopback/CEF 共用替换场景、真实跨进程取消、重载隔离与 V1 兼容；见 [最新 M3 记录](docs/acceptance/M3-endpoint-substitution.md)。进入 M4；M2 未验收项仍保留。

M4 已通过原生目录选择、业务 RPC 和有界 Worker 接入独立的 Workspace/Document Adapter，`nativeFiles=true`。支持多文件保存、冲突对比/三方合并、原子非覆盖另存、文件与目录管理、带索引的项目回收恢复，以及 Windows 原生变化通知驱动的安全刷新。剩余项仅是 4 MiB 真实跨进程极值、特权 Reparse Point/极端长路径/崩溃中断夹具和可见桌面中文 IME 人工验收；详见 [M4 记录](docs/acceptance/M4-document-core.md)。

## 构建

需要 CMake ≥ 3.21、Python ≥ 3.10、C++20 编译器。仓库 Presets 对应本机 Visual Studio 2019 x64；其他机器可使用 CMakeUserPresets.json 覆盖生成器和依赖根目录。

```powershell
cmake --preset core-debug
cmake --build --preset core-debug
ctest --preset core-debug
```

纯核心构建不查找 Qt、CEF、TeX 或 Node。Release 使用对应的 `core-release` 预设。

## 桌面验证

需要 Qt 5.15、CEF 150 Windows x64、Node 24 与 npm。默认原生依赖路径集中在 CMake cache 变量中：

```powershell
cmake --preset desktop-debug -DLIGHTOVERLEAF_THIRDPARTY_ROOT=D:/CodeMyself/QTBest/thirdparty_install
cmake --build --preset desktop-debug
ctest --preset desktop-debug
& ./out/desktop-debug/bin/Debug/LightOverLeaf.exe
```

CEF 路径可单独通过 `CEF_ROOT` 覆盖；Qt 可通过 `Qt5_DIR` 指定。
`desktop-release` 同样使用独立目录，产物在 `out/desktop-release/bin/Release/`。
首次构建会编译 CEF wrapper，并执行 `npm ci`、前端检查与生产构建。构建不下载原生库或安装 TeX。

`LightOverLeafDesktopSmoke` 等待 React 完成真实 CEF/C++ Ping 往返，再走浏览器正常关闭协议；45 秒内未退出即失败。
`LightOverLeafRendererRecoverySmoke` 通过测试参数使本应用 Renderer 崩溃，验证重载、再次原生通信和正常关闭。
纯 C++ 生命周期测试使用注入时钟验证启动 20 秒、关闭 10 秒的超时边界；Qt 集成测试使用 Fake 浏览器验证回调顺序与两次恢复上限。

## 开发版 EXE 打包

在 PowerShell 运行 `./scripts/package-lite.ps1`，生成可直接运行的 Lite 单文件 EXE；已有最新 Release 时可加 `-SkipBuild`。输出位于 `out/packages/`，附带清单、哈希和解压启动验证结果。Full 包需显式提供 Portable TeX 根目录；详见 [M8 发布记录](docs/acceptance/M8-release.md)。

## 前端独立开发

```powershell
cd web
npm ci
npm run dev
npm run check
```

Vite 仅监听 127.0.0.1，可使用显式 Fake NativeApi。生产构建由 CEF 受控资源地址加载，不启动 HTTP 服务，且必须使用真实原生通道；通道缺失时报告失败，不回退到 Fake。

## 架构与契约

- System、Workspace、Document 各自拥有 Domain、Inbound、Outbound 和 App Target。
- `cmake/ArchitectureRules.cmake` 导出实际 Target 链接并检查标记、宽泛 Include 根。
- `tools/architecture/check.py` 检查白名单、循环和核心框架泄漏；负面测试让真实非法 CMake Configure 失败。
- `contracts/rpc/v1/envelope.schema.json` 是 M0 Ping 请求的唯一来源；CMake 和 npm 自动生成 C++ 验证器与 TypeScript DTO/验证器。
- C++ 核心验证输入是传输无关的值树；CEF Transport 已提供有大小/深度限制的 JSON 解析，并通过组合根注入平台。核心不依赖 CEF。
- V1 Ping 保留经校验的请求回显；V2 使用结构化响应、Capabilities 和带会话序号的终态事件。CEF 取消可跳过排队请求，但尚非长任务或通用业务取消实现。
- `tests/fixtures/ping.json` 为两种语言共享的接受/拒绝样例。
- 生成代码、依赖和构建产物均忽略，不手写维护。

实施范围见 [开发要求](docs/development/LightOverLeaf_开发要求.md)、[生命周期与通信 ADR](docs/architecture/adr/0002-lifecycle-native-ping.md) 与 [本轮验收记录](docs/acceptance/M1-native-ping.md)。[M0 验收记录](docs/acceptance/M0.md) 保留原阶段历史，不代表当前全部能力。

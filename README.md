# LightOverLeaf

Windows 本地 LaTeX 编辑器。当前桌面架构为 **Electron + TypeScript + React**，原生业务后端为 **C++20**。Monaco 负责源码编辑，PDF.js 负责预览；项目文件、原子保存、编译、SyncTeX、SQLite 设置与会话服务继续由 C++ 模块提供。

## 开发构建

在项目根目录运行唯一日常构建入口：

```powershell
.\scripts\build.ps1 --dev
```

生成可直接打开的：

```text
out/electron-dev/bin/Release/LightOverLeaf.exe
```

请保留同目录的 Electron 资源、`LightOverLeafBackend.exe`、SQLite 和 `runtime/miktex`，不能只复制入口 EXE。构建执行 CMake 架构检查、契约生成和 TypeScript 类型检查，不编译测试目标、不运行 CTest 或 npm test。

需要 Windows x64、CMake ≥ 3.21、C++20/MSVC、Python ≥ 3.10、Node.js/npm，以及 `LIGHTOVERLEAF_THIRDPARTY_ROOT` 下的 SQLite 和独立 MiKTeX Runtime。新桌面不需要 Qt/CEF。首次构建按锁文件下载 Electron/npm 依赖，并下载带 SHA-256 校验的 nlohmann/json 3.12.0 头文件到忽略目录；依赖就绪后可复用。

## 模块边界

- `apps/electron/src/`：窗口、原生目录对话框、沙箱 preload、资源加载、C++ 进程生命周期。
- `apps/backend/`：C++ 管道进程入口和端点调度。
- `apps/desktop/`：复用的 C++ 业务装配。
- `web/src/`：React/TypeScript、Monaco、PDF.js 与 NativeApi。
- `src/modules/`：业务 Domain、Application、Ports 与 Adapters。
- `src/transport/json/`：不依赖 Qt/CEF 的 JSON 传输适配。
- `contracts/rpc/`：前后端共用 RPC 契约。
- `scripts/`：开发命令入口；`tools/`：契约生成和架构检查实现。

界面通过 `window.lightoverleaf` 的窄接口发送 RPC。Electron 校验来源和契约，使用私有 stdio 管道连接后端，不开本地 HTTP 端口。原生选择的绝对路径只存在于主进程与后端之间，不由 Renderer 提供。

设计和迁移边界见 [ADR 0019](docs/architecture/adr/0019-electron-native-backend.md)。SQLite 设置和原生项目格式保持兼容；Electron 与 CEF 的浏览器存储相互独立，旧 CEF 草稿缓存尚未自动迁入，请先在旧版显式导出需要保留的草稿。

## 旧版与验证记录

Qt/CEF 源码和 `desktop-debug` / `desktop-release` 预设暂时保留，用于迁移对照。显式旧版开发构建为 `scripts/build.ps1 -Preset desktop-release --dev`；它不再是默认桌面。现有发布脚本和历史 M0–M9 验收记录属于旧版，不能作为 Electron 运行或发布验收证据。

Electron 本轮记录见 [迁移记录](docs/acceptance/electron-migration.md)。业务历史见 [阶段进度](docs/development/阶段进度.md) 与 [开发要求](docs/development/LightOverLeaf_开发要求.md)。

浏览器独立开发仍可在 `web/` 运行 `npm run dev`，仅明确的浏览器开发模式允许 Fake NativeApi；桌面构建通道缺失时报告错误。

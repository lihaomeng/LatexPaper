# LightOverLeaf

面向 Windows 的本地 LaTeX 写作工作台，采用 **Electron + React + TypeScript** 前端和 **C++20** 原生后端。使用 Monaco 编辑源码，通过 PDF.js 连续预览编译结果；本地项目内容通过桌面进程间通信处理。

> 当前以开发构建为主。日常统一使用 `--dev`，不自动编译测试目标或运行测试套件。各项功能的验证范围以 [验收记录](docs/acceptance/) 为准。

## 功能概览

- **项目与文件**：打开本地项目、多文件编辑、目录管理、搜索、回收站及保存冲突处理。
- **写作辅助**：章节大纲、源码查找、LaTeX 结构插入、未保存状态与草稿缓存。
- **编译与诊断**：本地 TeX 编译、取消、超时、问题筛选、原始日志和源码行定位。
- **PDF 阅读**：连续页面、页码跳转、适宽／整页／自定义缩放，以及工具链支持时的双向 SyncTeX。
- **工作台**：可调整分栏、编辑／阅读布局、最近项目、设置和会话恢复。

草稿缓存不等于磁盘备份，重要内容请通过项目菜单导出。当前桌面构建使用内置 MiKTeX Runtime；编译核心与工具链实现通过端口分离。

## 快速开始

### 1. 准备环境

| 依赖 | 要求与用途 |
|---|---|
| Windows | x64 开发与运行环境 |
| Visual Studio / Build Tools | 当前预设使用 **Visual Studio 2019 x64** 生成器；需安装 C++ 工具链和 Windows SDK |
| CMake | 工程声明最低 3.21；构建脚本的缓存刷新使用 `--fresh`，建议安装 **3.24 或以上** |
| Python | 3.10 或以上，用于协议生成及架构检查 |
| Node.js / npm | Vite 7 要求 Node **20.19+（20.x）或 22.12+**；前端依赖通过锁文件安装 |
| SQLite | 原生头文件、x64 库和运行时 DLL |
| MiKTeX Runtime | 已准备的独立运行时及 `lightoverleaf-miktex-runtime.json` 清单 |

Python、Node.js/npm 和 CMake 需要能够从 PowerShell 调用。Electron 日常构建不依赖 Qt/CEF。

首次构建可能联网下载 npm 包、Electron 和 nlohmann/json。应用的本地编译能力不意味着首次开发环境安装完全离线。

### 2. 配置第三方依赖

原生依赖根目录由 CMake 缓存变量 `LIGHTOVERLEAF_THIRDPARTY_ROOT` 指定。当前默认值为开发机路径 `D:/CodeMyself/QTBest/thirdparty_install`；其他机器需要改为实际路径。

依赖布局包含：

```text
<第三方依赖根目录>/
├── vcpkg/installed/x64-windows/
│   ├── include/                 # SQLite 头文件
│   ├── lib/sqlite3.lib
│   └── bin/sqlite3.dll
└── miktex/                      # 已准备的 MiKTeX Runtime
```

如需覆盖依赖目录，从仓库根目录执行一次配置，将下面路径替换为实际位置：

```powershell
Push-Location backend/latexlocalservice
try {
    cmake --preset electron-dev -DLIGHTOVERLEAF_THIRDPARTY_ROOT="D:/Dependencies/thirdparty_install"
} finally {
    Pop-Location
}
```

此步骤只配置工程，不构建应用。MiKTeX 源码准备与集成方法见 [开发说明](docs/development/MiKTeX源码构建与集成.md)；仓库不包含完整第三方运行时。

### 3. 构建与启动

在仓库根目录运行：

```powershell
.\scripts\build.ps1 --dev
```

构建成功后启动：

```powershell
.\out\electron-dev\bin\Release\LightOverLeaf.exe
```

`--dev` 会执行 CMake 配置、架构检查、协议生成、C++ 后端构建、前端与 Electron TypeScript 构建及运行时准备；**不会运行 CTest 或 npm test**。产物位于 `Release` 子目录是当前预设行为。

重新构建前，请保存内容并关闭运行中的 LightOverLeaf，避免可执行文件被占用。启动时应保留整个产物目录，不能只复制 EXE。

### 4. 打开项目

在应用中点击“打开项目”，选择包含 LaTeX 源码的文件夹。仓库中的 [示例目录](support/examples/) 可用于了解项目结构，当前包含 `paper001`。

| 快捷键 | 操作 |
|---|---|
| `Ctrl + S` | 保存本地文件／缓存草稿 |
| `Ctrl + Enter` | 编译／取消编译 |
| `Ctrl + F` | 查找当前文档 |
| `Ctrl + Z` / `Ctrl + Y` | 撤销／重做 |

## 常用开发命令

以下命令均从仓库根目录执行：

```powershell
# 日常完整开发构建
.\scripts\build.ps1 --dev

# 指定其他已准备的 MiKTeX Runtime
.\scripts\build.ps1 --dev -MiKTeXRoot "D:/Dependencies/miktex"

# 将已有 Electron 产物整理为分发目录
.\scripts\package.ps1 -SkipBuild
```

打包结果以脚本打印的路径为准，保留 Electron 资源、C++ 后端、SQLite 和 MiKTeX Runtime 的完整目录结构。`-SkipBuild` 不更新已有产物，请先确保构建成功。

仅调试浏览器界面时：

```powershell
Push-Location frontend/web
try {
    npm run dev
} finally {
    Pop-Location
}
```

前端依赖需先完成安装，例如先运行一次完整 `--dev` 构建。浏览器开发模式使用 Fake NativeApi，不能替代 Electron 下真实文件操作和 TeX 编译验收。

更多脚本职责与历史入口见 [scripts/README.md](scripts/README.md)。

## 目录结构

```text
LightOverLeaf/
├── frontend/
│   ├── electron/               # 桌面窗口、preload、原生进程通信
│   └── web/                    # React、Monaco、PDF.js 工作台
├── backend/
│   ├── latexlocalservice/      # 当前 C++ 本地服务与 CMake 工程
│   │   ├── app/                # 入口、装配、公共类型、平台与原生测试
│   │   ├── texengine/          # 编译、PDF 产物与 SyncTeX
│   │   ├── workspace/          # 项目、文档、搜索与导出
│   │   ├── persistence/        # 设置与会话存储
│   │   ├── transport/          # RPC 与传输适配
│   │   ├── cmake/              # 构建辅助逻辑
│   │   └── tools/architecture/ # 原生依赖边界检查
│   └── latexhttpservice/       # 独立目录，当前桌面入口不通过 HTTP 通信
├── support/
│   ├── contracts/              # C++ / TypeScript 共享协议
│   ├── examples/               # 示例论文项目
│   ├── patches/                # 第三方依赖补丁
│   └── resources/              # TeX 支持资源与打包模板
├── scripts/                    # 构建、运行时准备、打包入口
├── tools/                      # 协议生成、MiKTeX 构建等工具
├── tests/fixtures/             # 共享验证夹具
├── docs/                       # 开发规范、架构决策与验收记录
└── out/                        # 构建产物、下载缓存与验证输出（忽略提交）
```

原生工程的 `CMakeLists.txt` 与 `CMakePresets.json` 位于 `backend/latexlocalservice/`。日常构建脚本自动切换目录，无需在根目录补回 CMake 文件。

## 架构约定

桌面调用链为：

```text
React 工作台 → NativeApi → Electron preload / 主进程
            → 私有 stdio 通道 → C++ RPC → 应用用例 → 适配器
```

- React 负责界面和编辑状态，不直接访问文件系统或启动 TeX。
- Electron 负责窗口、授权目录选择、通信边界与后端生命周期。
- C++ 负责文件、编译、诊断、PDF 产物、设置与会话等原生业务。
- 原生业务维持 `domain ← application ← adapters` 依赖方向，由组合根装配。
- RPC Schema 是共享契约来源，生成代码不手工维护。

详细边界见 [前端说明](frontend/README.md)、[本地服务说明](backend/latexlocalservice/README.md) 和 [Electron 架构决策](docs/architecture/adr/0019-electron-native-backend.md)。

## 常见问题

| 现象 | 原因与处理 |
|---|---|
| `LNK1104`，无法写入 `LightOverLeafBackend.exe` | 常见原因是应用仍在运行。先保存并退出，再重新构建。 |
| 下载报 `Couldn't resolve host name` | 检查网络、DNS 或代理。JSON 依赖当前从 `raw.githubusercontent.com` 获取。 |
| `nlohmann/json 3.12.0 checksum mismatch` | 检查 `out/dependencies/nlohmann/json.hpp` 是否为空或不完整；恢复网络后移除损坏缓存再构建，或提供校验通过的相同版本文件。不要关闭哈希校验。 |
| 清理 `out/` 后又需要下载依赖 | 当前 JSON 缓存也位于 `out/`。清理会同时删除缓存，离线构建前需准备依赖。 |
| 打开最近项目报 `FILE_NOT_FOUND` | 项目目录可能已移动或重命名；通过“打开项目”重新选择现有目录。 |
| 预览仍显示旧 PDF | 编译或渲染失败时会保留上一份成功结果。查看“问题／原始日志”，修复源码后重新编译。 |
| 找不到 Visual Studio 生成器或 SQLite | 核对安装的 C++ 工具链、当前 CMake 预设及第三方根目录，而不是反复清理全部产物。 |

## 文档与兼容性

- [开发要求](docs/development/LightOverLeaf_开发要求.md)：产品范围、依赖边界和构建约定。
- [C++ 质量规范](docs/Codex-Rules.md)：原生代码命名、资源管理与安全要求。
- [前端体验优化方案](docs/development/前端体验与视觉优化开发方案.md)：问题清单与分阶段计划，不代表全部已完成。
- [验收记录](docs/acceptance/)：实际命令、结果与未验证项。
- [配套内容说明](support/README.md)：共享协议、示例与资源归属。

Qt/CEF 源码与 `desktop-debug` / `desktop-release` 预设暂时保留用于历史对照，当前默认桌面为 Electron。旧 CEF 草稿缓存尚未自动迁移，需在旧版显式导出后再使用；历史 M0–M9 记录不能作为当前 Electron 版本的完整验收证据。

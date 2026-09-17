# MiKTeX 源码构建与 LightOverLeaf 集成

更新日期：2026-09-13。

## 1. 目标与边界

本方案将 MiKTeX 当作独立的 TeX Runtime，而不是链接进 LightOverLeaf 的业务库：

```text
LightOverLeaf Build Application
        │  IKCompilerBackend
        ▼
Windows TeX Process Adapter
        │  CreateProcessW / Job Object
        ▼
runtime/miktex/texmfs/install/miktex/bin/x64/*.exe
        │
        └── runtime/miktex/texmfs
```

- MiKTeX 在独立目录下载、配置、编译和安装；不加入 LightOverLeaf 的默认 CMake Target 图。
- Build Application 只依赖 `IKCompilerBackend`，不知道 MiKTeX、TeX Live、CEF 或 React。
- 产品只发布 Full 绿色目录，并固定携带源码构建的 MiKTeX；Lite 与 Portable TeX Live 不再是发布形态。
- 编译期间禁止 Shell Escape，直接 MiKTeX 引擎显式传入 `--disable-installer`；运行时配置同时写入 `AutoInstall=0`。
- 宏包准备只允许在显式的 Runtime 构建阶段联网执行，应用运行时不得静默下载宏包。

## 2. 固定源码与目录

当前固定版本：MiKTeX `26.5`。

```text
来源：https://miktex.org/download/ctan/systems/win32/miktex/source/miktex-26.5.tar.xz
SHA-256：a1fd2223ef04a0268b0718536f7b55a04b5c9d1f0f721d1c0b2749b3af95115d
源码工作区：D:/CodeMyself/LightOverLeaf/out/dependencies/miktex
隔离安装根：D:/CodeMyself/QTBest/thirdparty_install/miktex
```

`out/` 已被 Git 忽略，第三方源码、构建中间物和宏包仓库不会进入业务源码。最终 Runtime 也不复制进仓库，只在 Full 打包阶段作为外部载荷读取。

## 3. 构建依赖

- Visual Studio 2019/2022 x64 C++ 工具链；
- CMake 3.18 或更高；
- Git for Windows 提供的 `patch.exe` 与 `perl.exe`；
- vcpkg x64-windows 中的 ICU 与 Boost.Locale；
- 脚本固定并校验 WinFlexBison 2.5.24、MSYS2 libxml2 2.15.3-1 和
  libxslt 1.1.45-1，仅把它们作为构建工具使用，不写入业务模块；
- 足够的磁盘空间。源码、双配置依赖、构建树、宏包仓库和 Runtime 会明显大于单个 `xelatex.exe`。

本机中文用户名会使旧版 Boost.Build 把 `%TEMP%` 转换成乱码路径。构建脚本和依赖安装均把 `TEMP/TMP` 指向仓库内 `out/tmp` 的 ASCII 路径，禁止回退到用户临时目录。

依赖准备：

```powershell
$env:TEMP = 'D:\CodeMyself\LightOverLeaf\out\tmp\vcpkg'
$env:TMP = $env:TEMP
D:\CodeMyself\QTBest\thirdparty_install\vcpkg\vcpkg.exe install boost-locale:x64-windows
```

## 4. 一键源码构建

```powershell
cd D:\CodeMyself\LightOverLeaf
$env:LIGHTOVERLEAF_THIRDPARTY_ROOT = 'D:\CodeMyself\QTBest\thirdparty_install'
.\tools\miktex\build-runtime.ps1
```

脚本依次执行：校验工具、下载固定源码、校验 SHA-256、CMake 配置、Release 构建、隔离安装、显式准备 Basic 宏包、恢复源码构建的二进制、关闭自动安装并写入来源清单。

常用诊断模式：

```powershell
# 只验证依赖和 CMake 配置
.\tools\miktex\build-runtime.ps1 -ConfigureOnly -SkipDownload

# 只构建可执行层，暂不下载宏包集合
.\tools\miktex\build-runtime.ps1 -SkipPackageProvision

# 完整宏包集合；体积和耗时显著增加
.\tools\miktex\build-runtime.ps1 -PackageSet complete
```

成功后至少应存在：

```text
D:/CodeMyself/QTBest/thirdparty_install/miktex/
├── lightoverleaf-miktex-runtime.json
└── texmfs/
    ├── config/...
    ├── data/...
    └── install/miktex/bin/x64/
        ├── xelatex.exe
        ├── pdflatex.exe
        ├── lualatex.exe
        └── synctex.exe
```

## 5. LightOverLeaf 发现与调用

桌面 Composition Root 检测到应用旁 `runtime/miktex` 后进入严格模式：Build 与 SyncTeX 只注入该根，并关闭系统 PATH、用户设置和其它发行版回退。未携带 Runtime 的开发构建才允许读取 `LIGHTOVERLEAF_TEX_ROOT`、测试 Root 和系统常见目录。Windows Adapter 仍通过统一 `IKCompilerBackend` 返回能力并执行，因此此策略不会把 MiKTeX 类型带入 Use Case、RPC 或 React。

开发阶段可在设置中直接指定：

```text
D:/CodeMyself/QTBest/thirdparty_install/miktex
```

## 6. Full 绿色目录打包

统一入口：

```powershell
.\scripts\package.ps1 -MiKTeXRoot 'D:\CodeMyself\QTBest\thirdparty_install\miktex'
```

`package-full.ps1` 与 `package-miktex.ps1` 仅保留为兼容别名。打包器验证 XeLaTeX、pdfLaTeX、LuaLaTeX、SyncTeX 与来源清单，然后把 Runtime 固定复制到 `LightOverLeaf-Full-win64/runtime/miktex`。不生成 archive、自解压 EXE 或 Runtime Launcher；最终用户直接运行目录内 `LightOverLeaf.exe`。

报告写入 `delivery=expanded-green-directory`、`runtimeExtraction=false`、`archiveCreated=false`、`bundledArchiveTool=false`、`texRuntimeKind=MiKTeX` 与 `texRuntimeSource=source-built`。目录同时包含 `runtime-manifest.json` 和 `package-files.sha256`。

## 7. 验收标准

1. MiKTeX CMake 配置、Release 编译和所需 Runtime/独立安装器目标均为退出码 0。
2. 来源清单记录版本、下载地址、SHA-256、生成器、宏包集合和 `builtFromSource=true`。
3. 三种引擎与 `synctex.exe` 可执行；Basic Fixture 能生成非空 PDF 和 `.synctex.gz`。
4. LightOverLeaf 能报告 MiKTeX 能力，保存后编译、Artifact 发布和 PDF.js 预览成功。
5. 缺宏包时明确失败且日志可见，不弹出安装器、不联网、不伪造 PDF。
6. Full 绿色目录清单、禁止归档/启动器检查、GUI Subsystem 检查和目录直接 smoke 通过。
7. 在干净 Windows 环境中验证中文路径、字体、BibTeX/Biber、取消、超时和卸载/临时目录清理。

## 8. 当前执行记录

- 已从官方地址取得 `26.5` 源码归档并验证 SHA-256 完全一致。
- 已将源码解压到隔离工作区；未把 MiKTeX 源码加入 LightOverLeaf 构建图。
- 已完成 Boost.Locale 安装。首次失败原因是中文 `%TEMP%` 被 Boost.Build 错误编码；改用 `out/tmp/vcpkg` 后成功。
- 已使用固定 MSYS2/WinFlexBison 构建工具完成 CMake 配置和 Release 源码编译；源码树应用
  `26.5-windows-shlwapi-declarations.patch` 与 `26.5-cairo-configurable-dwrite.patch`，
  后者在当前 Windows SDK 10.0.19041 下关闭不可用的 Cairo DirectWrite 新接口。
- 已从源码构建集成下载器和 `miktexsetup_standalone.exe`，安装 `basic` 集合并初始化官方
  `texmfs/install` 便携布局；来源清单记录 `builtFromSource=true`、`autoInstall=false`。
- 真实 XeLaTeX 验收已生成 `out/acceptance/miktex/main.pdf`（5172 bytes）和
  `main.synctex.gz`（520 bytes），退出码 0。`basic` 不保证任意论文宏包闭包；例如当前镜像
  的该集合缺少 `kvsetkeys`，需要常见论文宏包时发布者应使用 `-PackageSet complete`。
- 前端 288/288、Release CTest 33/33 通过。Full 绿色目录已生成；入口为 GUI Subsystem，目录直接 smoke 为 2028 ms，且目录内 XeLaTeX 再次生成 5172-byte PDF 与 538-byte SyncTeX。精确产物信息见 M8 验收记录。

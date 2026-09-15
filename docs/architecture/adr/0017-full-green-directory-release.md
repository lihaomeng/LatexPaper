# ADR 0017：仅发布 Full 绿色目录并在运行时零解压

- 日期：2026-09-13
- 状态：接受
- 取代：ADR 0016

## 背景

单文件方案需要先把约 1.44 GB 的 Qt、CEF、Web 与 MiKTeX 载荷解压到临时目录。它增加首次启动等待、临时空间占用、杀毒扫描和异常清理成本，也让“一个 EXE”掩盖了 CEF 与 TeX 本来就依赖目录结构的事实。

## 决策

LightOverLeaf 只发布一个产品形态：`Full` Windows x64 绿色目录。目录中的 `LightOverLeaf.exe` 是唯一入口，但它依赖同目录的 Qt、CEF、Web 资源和 `runtime/miktex`，因此不得单独复制 EXE。

```text
LightOverLeaf-Full-win64/
├── LightOverLeaf.exe
├── LightOverLeaf.dll
├── Qt5*.dll、CEF 文件、platforms/、locales/、web/
├── licenses/
├── runtime-manifest.json
├── package-files.sha256
└── runtime/miktex/
    ├── lightoverleaf-miktex-runtime.json
    └── texmfs/install/miktex/bin/x64/
        ├── xelatex.exe
        ├── pdflatex.exe
        ├── lualatex.exe
        └── synctex.exe
```

发布入口为 `scripts/package.ps1`。`package-full.ps1` 和 `package-miktex.ps1` 只作为兼容别名；Lite、Portable TeX Live 和单文件自解压入口被移除。打包阶段可以复制并哈希文件，但应用启动阶段不得创建或展开 `payload.zip`、`payload.7z`，不得调用 7-Zip、tar、IExpress、CMD 或 PowerShell。

产品装配检测到应用旁 `runtime/miktex` 后，Build 与 SyncTeX Adapter 进入严格发现模式，只允许该 Runtime。缺失工具时明确失败，不回退系统 PATH、用户配置或另一套 TeX。未携带 Runtime 的开发构建仍可通过端口配置系统 TeX，以保留 Adapter 替换性与测试能力。

## 发布契约

- MiKTeX 必须来自独立源码构建和隔离安装目录，不进入 LightOverLeaf 默认 CMake Target 图。
- 发布前验证 XeLaTeX、pdfLaTeX、LuaLaTeX、SyncTeX、来源清单、许可证和 GUI PE Subsystem。
- 每次生成唯一目录，不覆盖既有产物；输出 `package-report.json`、运行时清单和 SHA-256 文件清单。
- 从最终目录直接执行 `LightOverLeaf.exe --smoke-test`；另用最终目录内 XeLaTeX 生成真实 PDF 与 SyncTeX。
- 缺宏包时明确失败；运行时保持 `AutoInstall=0` 和 `--disable-installer`，不得静默联网。

## 影响

优点是启动路径确定、没有解压等待或黑色命令行窗口、调试和完整性检查更直接。代价是交付目录约 1.44 GB、文件约两万个，复制和打包哈希较慢。传输系统若自行压缩目录，解压属于分发介质操作，不得由 LightOverLeaf 启动器执行。

应用仍会正常写入 AppData/Cache 中的设置、会话、Snapshot、编译中间文件和 MiKTeX 用户数据；这些是运行数据，不是 Runtime 解压。

## 回滚

回滚只需恢复发布脚本与 Composition Root，不改 Build/Preview Use Case 或 RPC。但除非产生新的 ADR，禁止恢复运行时自解压或 Lite 产品分支。
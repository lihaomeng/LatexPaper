# M8 发布阶段验收记录

- 日期：2026-09-13
- 当前状态：Full 绿色目录已完成本机自动化验收；干净 Windows 虚拟机人工全流程与代码签名待补
- 现行决策：[ADR 0017](../architecture/adr/0017-full-green-directory-release.md)

## 1. 唯一发布形态

当前只发布 `Full` Windows x64 绿色目录，固定内置源码构建的 MiKTeX 26.5：

```text
D:/CodeMyself/LightOverLeaf/out/distributions/
  full-20260913-202937-c0a407a0/
    package-report.json
    LightOverLeaf-Full-win64/
      LightOverLeaf.exe
      runtime/miktex/
      runtime-manifest.json
      package-files.sha256
```

运行入口：

```text
D:/CodeMyself/LightOverLeaf/out/distributions/full-20260913-202937-c0a407a0/LightOverLeaf-Full-win64/LightOverLeaf.exe
```

必须复制整个 `LightOverLeaf-Full-win64` 目录，不能只复制 EXE。运行时不创建或解压 Runtime 归档，不调用 7-Zip、tar、IExpress、CMD 或 PowerShell。

## 2. 生成命令

```powershell
.\scripts\package.ps1 -MiKTeXRoot 'D:\CodeMyself\QTBest\thirdparty_install\miktex'
```

`package-full.ps1` 与 `package-miktex.ps1` 是兼容别名。`package-lite.ps1`、`package-onefile.ps1` 与 `LightOverLeafRuntimeLauncher` 已删除。

## 3. 自动化证据

最终 `package-report.json`：

| 字段 | 结果 |
| --- | --- |
| `edition` | `Full` |
| `delivery` | `expanded-green-directory` |
| `runtimeExtraction` | `false` |
| `archiveCreated` | `false` |
| `bundledArchiveTool` | `false` |
| `texRuntimeKind` | `MiKTeX` |
| `texRuntimeSource` | `source-built` |
| `manifestedFiles` | 19562 |
| `payloadBytes` | 1438298495 |
| `manifestSha256` | `438748B5F43617312151848D55E402AFA3A82633F41BB8BCB784AE75ECE8987B` |
| `consoleApplication` | `false` |
| `directorySmoke` | `passed` |
| `directorySmokeElapsedMilliseconds` | 1857 |

`manifestedFiles` 与 `payloadBytes` 是生成清单前的 Runtime 内容统计；随后写入的 `runtime-manifest.json` 和 `package-files.sha256` 不自我哈希。

本轮自动化结果：

- 前端 ESLint、291/291 测试、TypeScript 和 Vite 生产构建通过。
- Release C++/集成 CTest 33/33 通过。
- Build、Windows SyncTeX、Architecture 与 Illegal Dependency 针对性回归 4/4 通过。
- CMake 架构检查为 86 个目标；旧 Runtime Launcher 目标已移除。
- 最终入口 PE Subsystem 为 Windows GUI，不启动黑色命令行窗口。
- 最终目录禁止文件扫描未发现 `payload.7z`、`payload.zip` 或 `LightOverLeafRuntimeLauncher.exe`。
- 最终目录直接执行 `LightOverLeaf.exe --smoke-test`，退出码 0，耗时 1857 ms。

## 4. 真实 PDF/SyncTeX 验收

使用最终目录内：

```text
runtime/miktex/texmfs/install/miktex/bin/x64/xelatex.exe
```

以 `--disable-installer -no-shell-escape -synctex=1` 编译 `tests/fixtures/tex/basic/main.tex`，退出码 0，生成：

```text
out/acceptance/full-green-20260913-194005/main.pdf        5172 bytes
out/acceptance/full-green-20260913-194005/main.synctex.gz  538 bytes
out/acceptance/full-green-20260913-194005/main.log         2866 bytes
```

日志含 MiKTeX “尚未检查更新”的提示，但未联网、未弹出安装器，也不影响本次 PDF 与 SyncTeX 成功生成。

## 5. 严格 Runtime 边界

产品装配检测到 `runtime/miktex` 后：

- Build 与 SyncTeX 只使用内置 Runtime；
- 用户设置中的旧 TeX Root、系统常见目录和 PATH 均不参与回退；
- 支持源码版 MiKTeX 的 `texmfs/install/miktex/bin/x64` 官方布局；
- 内置文件缺失时明确失败，不借用目标机器上的另一套 TeX。

开发构建未携带 Runtime 时仍可使用注入 Root 与系统发现，以维持端口替换测试；该行为不是发布形态。

## 6. 未完成的发布级外部验收

- 代码签名尚未完成，`signed=false`。
- 尚未在独立干净 Windows 虚拟机中人工完成：启动、中文路径项目、保存、编译、PDF.js 预览、正反向 SyncTeX、取消与超时。
- 当前 Runtime 是 `basic` 宏包集合，不保证常见论文模板闭包；正式广泛分发前应以 `-PackageSet complete` 重建并复验 BibTeX/Biber、字体与典型模板。

## 7. 历史方案

ADR 0016 的 Lite/Full 单文件、IExpress、7-Zip 和启动时临时解压方案已被废止。旧 `out/packages` 产物仅作历史证据，不得作为当前发布版本继续分发。

## 8. 目录选择与草稿编译交互修复

- 日期：2026-09-13
- 触发条件：在尚未保存为本地项目的草稿中点击“保存并编译”。
- 已确认原因：Qt `QFileDialog` 使用空父窗口，原生目录选择器可能出现在 LightOverLeaf 后方；前端同时静默忽略 `USER_CANCELLED`，用户返回后仍看到原始空预览状态，因此表现为“点击后没有编译”。
- 原生修复：`KQtRuntime` 在应用事件循环期间用 `QPointer<QWidget>` 观察主窗口，目录选择前激活主窗口并把它作为 `QFileDialog` 父窗口；启动失败和事件循环退出时清空观察指针。Qt 类型仍只存在于 Platform 私有实现。
- 前端修复：增加纯 `PreviewPresentation` 状态模型和 `preparing` 状态；选择目录期间显示“等待选择目录”、禁止重复提交；取消显示“保存并编译已取消”，非空目录和导入失败显示明确错误。
- 自动化验证：`npm.cmd run check` 通过，291/291；`desktop-release` 构建通过；Release CTest 33/33；Full 绿色目录生成及 smoke 通过。
- 真实 TeX 验证：使用新目录内置 MiKTeX 26.5 的 `xelatex.exe` 生成 `out/acceptance/compile-ui-fix-20260913-202937/main.pdf`（4380 bytes）、`main.synctex.gz`（583 bytes）和 `main.log`（8021 bytes）。
- 新发布目录：`out/distributions/full-20260913-202937-c0a407a0/LightOverLeaf-Full-win64`；`package-report.json` 记录 `runtimeExtraction=false`、`archiveCreated=false`、`consoleApplication=false`、`directorySmoke=passed`。
- 未自动化项：当前环境不能自动点击 Windows 原生目录选择器，因此“对话框始终显示在主窗口前方”仍需在可见桌面执行一次人工确认；不能把编译器实测替代该项视觉验收。

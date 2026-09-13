# M8 发布阶段验收记录

- 日期：2026-09-13
- 状态：Lite 与源码版 MiKTeX Full 单文件 EXE 均已完成本机自动化验收

## Lite 产物

```text
D:/CodeMyself/LightOverLeaf/out/packages/lite-20260913-103955-6ca2ef54/LightOverLeaf-0.1.0-lite-win64.exe
大小：152023040 bytes
SHA-256：08FE4838723C1865F4528735593466583AFEC0A89894FCA12AD4E04B7A2255EC
```

最终生产组合含真实 Windows SyncTeX Adapter 与独立 Unavailable Adapter；本轮打包入口运行 `ctest`，33/33 通过。内部 7z 归档测试通过，包含 5 个目录、268 个文件；最终外层 EXE 的自动 smoke 明确退出码为 0。单文件运行时解压到唯一临时目录，启动 `LightOverLeaf.exe`，退出后清理。

2026-09-13 修复旧包执行 `run.cmd` 而显示黑色命令行窗口的问题。外层包现在使用 `LightOverLeafRuntimeLauncher.exe`：该文件为 Windows GUI 子系统、静态运行库，只依赖系统 `KERNEL32.dll`/`USER32.dll`；7-Zip 通过隐藏的 `CreateProcessW` 启动，不经过 Shell。外层文件清单已验证只有 `payload.7z`、`7z.exe`、`7z.dll` 和原生启动器，不包含 `.cmd`。打包脚本同时校验启动器与最终 IExpress EXE 的 PE GUI Subsystem，并记录 `consoleLauncher=false`、`packagedSmoke=passed`。

同轮修复 Qt/CEF 高 DPI 铺满问题：CEF 创建和调整子窗口时直接读取父 HWND 的原生 `GetClientRect`，避免 Qt 逻辑像素被当作 Win32 设备像素。受影响 CEF/Qt/架构测试 9/9 与完整 Release 33/33 均通过。Codex 可见界面工具因 Windows sandbox helper 错误连续两次无法启动，最终截图复验仍需用户确认。

许可证目录包含 CEF、Qt Runtime 原有许可证，以及 React、React DOM、Scheduler、Monaco、PDF.js、SQLite 和 7-Zip 许可证。产物同时生成 `.sha256` 和 `package-report.json`。

## Full Runtime

Full 打包现要求显式提供且只提供一种 Runtime。Portable TeX Live 仍可使用：

```powershell
.\scripts\package-full.ps1 -PortableTexRoot 'D:\portable-texlive'
```

源码构建 MiKTeX 使用：

```powershell
.\scripts\build-miktex-runtime.ps1
.\scripts\package-miktex.ps1 -MiKTeXRoot 'D:\CodeMyself\QTBest\thirdparty_install\miktex'
```

MiKTeX 会复制到 `payload/runtime/miktex`，报告记录 `texRuntimeKind=MiKTeX` 和 `texRuntimeSource=source-built`。

本机源码版 Full 产物：

```text
D:/CodeMyself/LightOverLeaf/out/packages/full-20260913-142310-3a4e46a2/LightOverLeaf-0.1.0-full-win64.exe
大小：426319872 bytes
SHA-256：B039018F60A376DF935B4F0143BA5BF4911E48E89BC121B9B561FF06776306B7
```

- 内部载荷 1383 个目录、19564 个文件、1442427220 bytes，7z 压缩后 424905261 bytes；完整性测试通过。
- `package-report.json` 记录 `runtimeFiles=19563`、`portableTexIncluded=true`、
  `texRuntimeKind=MiKTeX`、`texRuntimeSource=source-built`、`consoleLauncher=false`、
  `packagedSmoke=passed`。
- 打包入口再次执行 Release CTest 33/33；最终 IExpress 外层 PE GUI Subsystem 检查和直接 EXE smoke 均通过。
- Runtime 来源清单、真实 XeLaTeX 5172-byte PDF 与 520-byte SyncTeX 已验收。尚未执行独立干净 Windows
  虚拟机中的人工编辑/编译/预览与代码签名，因此这些发布级外部验收仍保留。

## 发布限制

- 当前 EXE 未进行代码签名。
- 已完成本机构建、目录保真、解包运行时和直接单文件 smoke；尚未在独立干净 Windows 虚拟机中人工完成编辑/编译/预览全流程。
- Lite 不含 TeX，用户需配置系统 TeX 或自备工具链；无 TeX 时应用明确报告不可用。
- 本轮 `package-report.json` 记录 `runtimeFiles=267`（清单生成前的 Runtime 内容数），归档验证报告 268 个文件（包含随后生成的 `manifest.json`）；两者统计口径不同，不是丢失文件。
## 2026-09-13 PDF 编译入口修复后的 Lite 包

- 单文件 EXE：`out/packages/lite-20260913-112357-e171003e/LightOverLeaf-0.1.0-lite-win64.exe`
- 大小：152023040 bytes
- SHA-256：`C099C007E73E0EBBA162BB13122E3BC16A6BB59ABF6F927049588FAECB1D3535`
- 打包脚本再次执行 Release CTest 33/33、7-Zip 268 文件归档测试和最终外层 EXE smoke，均通过。
- 该包是 Lite，不包含 TeX。草稿保存编译入口、动态 Root 与常见安装路径发现已经包含；真实 PDF 仍需要外部 TeX 或 Full 包的 Portable TeX 载荷。

## 2026-09-13 Full 包 smoke 超时误判修复

- 用户本地 Full 打包的内部归档校验成功：19564 个文件、1442798920 bytes，压缩后
  424923843 bytes；随后旧脚本在固定 45 秒 smoke 上限触发超时。
- 复用同一个 `full-20260913-170208-80b3226f` EXE，以 600 秒上限隐藏运行，实际
  46735 ms 完成并返回退出码 0。因此根因是约 1.44 GB 载荷首次解压超过 45 秒，不是归档损坏或程序启动失败。
- `package-onefile.ps1` 的默认 smoke 上限改为 600 秒，允许 `60..1800` 秒显式覆盖；
  Lite、Full、MiKTeX 包装脚本均透传 `-SmokeTimeoutSeconds`。
- 真正超时时使用精确 PID 调用 `taskkill /T /F` 清理测试进程树，并保留单进程 Kill 回退；
  报告新增 `packagedSmokeTimeoutSeconds` 和 `packagedSmokeElapsedMilliseconds`。
- 四个 PowerShell 入口均通过解析器检查；既有 Full EXE 的延长烟测通过。

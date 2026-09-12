# M8 发布阶段验收记录

- 日期：2026-09-12
- 状态：Lite 单文件 EXE 已验收；Full 被 Portable TeX 外部载荷阻塞

## Lite 产物

```text
D:/CodeMyself/LightOverLeaf/out/packages/lite-20260912-225317-a114aa18/LightOverLeaf-0.1.0-lite-win64.exe
大小：151900160 bytes
SHA-256：906DD732A46379E985D9C552A541C2068A55ED4555B648593B007A5F9F550678
```

最终生产组合含真实 Windows SyncTeX Adapter 与独立 Unavailable Adapter；本轮打包入口重新配置、构建并运行 `ctest`，33/33 通过（32.67 秒）。内部 7z 归档测试通过，包含 5 个目录、268 个文件；通过 `Start-Process -Wait` 直接运行外层 EXE 的自动 smoke 模式，明确退出码为 0。单文件运行时解压到唯一临时目录，启动 `LightOverLeaf.exe`，退出后清理。

许可证目录包含 CEF、Qt Runtime 原有许可证，以及 React、React DOM、Scheduler、Monaco、PDF.js、SQLite 和 7-Zip 许可证。产物同时生成 `.sha256` 和 `package-report.json`。

## Full 阻塞

本机 PATH 未发现 TeX 命令，也未提供满足 `bin/windows/xelatex.exe` 的 Portable TeX 根目录。`package-full.ps1` 因此要求显式 `-PortableTexRoot`，不会联网下载或生成伪 Full 包。取得载荷后执行：

```powershell
.\scripts\package-full.ps1 -PortableTexRoot 'D:\portable-texlive'
```

## 发布限制

- 当前 EXE 未进行代码签名。
- 已完成本机构建、目录保真、解包运行时和直接单文件 smoke；尚未在独立干净 Windows 虚拟机中人工完成编辑/编译/预览全流程。
- Lite 不含 TeX，用户需配置系统 TeX 或自备工具链；无 TeX 时应用明确报告不可用。
- 本轮 `package-report.json` 记录 `runtimeFiles=267`（Runtime 内容数，不含外层打包辅助文件），归档验证报告 268 个文件；两者统计口径不同，不是丢失文件。

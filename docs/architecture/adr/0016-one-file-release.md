# ADR 0016：单文件可运行 EXE 封装

- 日期：2026-09-12
- 状态：接受

## 决策

发布入口使用两层结构：内部 `payload.7z` 保留 Qt、CEF、Locales、Platforms、Web 和可选 Portable TeX 的目录树；外层 IExpress 只封装 `payload.7z`、`7z.exe`、`7z.dll` 与 `run.cmd`。启动器解压到每次唯一的 `%TEMP%` 目录，等待应用退出后清理。

直接把整个 CEF Runtime 放入 IExpress 会压平子目录，生成的 EXE 无法运行，因此不作为发布实现。`package-onefile.ps1` 在打包前强制运行 CTest、验证必需运行时和许可证、测试内部归档，并输出 SHA-256 报告。

Lite 不包含 TeX；Full 必须显式传入含 `bin/windows/xelatex.exe` 的 Portable TeX 根目录，不自动联网下载。

## 结果

- 用户得到一个可直接双击运行的 EXE，而不是需要手工解压的开发归档。
- 两种包共享同一 Application 和运行时，仅载荷与能力发现不同。
- 当前未签名；正式发布仍需代码签名和干净机人工验收。

# 开发版自解压 EXE 打包记录

日期：2026-09-11。用户最新要求新增打包脚本并生成 EXE。本次按项目 skill 保持打包逻辑独立，不调整应用依赖方向，不将此工作计为 M8 正式发布完成。

## 产物

- 脚本：scripts/package.ps1。
- 用法：docs/development/打包说明.md。
- EXE：out/packages/20260911-220228-4e196d63/LightOverLeaf-0.1.0-dev-win64.exe。
- 大小：151,543,039 字节，约 144.5 MiB。
- SHA-256：F66E7903CDD377A80B9DD84FA1E4E8D6199976CB3B343D2E0EB42F4292999AC6。
- 包目录同时保存 package-report.json、校验文件、stage、verify 与额外自解压测试目录 sfx-check。

## 实际验证

执行 scripts/package.ps1 -SkipBuild，退出码 0。本轮重用上一轮通过验证的 Release 二进制，没有重新编译；脚本默认不加此参数时会执行配置与构建。

- 本次重新执行 Release CTest，15/15 通过（12.35 秒）。
- 收集应用、CEF、Qt、Qt 依赖、MSVC Release CRT、前端资源与依赖许可证。
- 7-Zip 26.02 生成自解压 EXE，归档测试通过。
- 解压后校验 288 个清单文件 SHA-256，全部一致；另含 manifest.json。
- 限制 PATH 为 Windows 系统目录、指定解压目录 Qt 插件后，运行解压版 --smoke-test，退出码 0。
- 额外直接执行生成的 EXE，使用 -y 与 -o 指定新的 sfx-check 目录，自解压退出码 0，主程序存在。

所有工作目录均新建，未覆盖或删除旧文件。中间目录保留供复验。

## 边界

外层 EXE 是自解压包，不是安装向导；解压后运行 LightOverLeaf.exe，不能移走其依赖。无代码签名、卸载注册、自动升级或快捷方式。

未执行干净虚拟机、Windows 10、无开发工具机器或完整 UI 视觉验收；本机限制 PATH 不等于干净系统。第三方许可证文本已随包提供，正式对外发布还需审查完整来源／许可义务和代码签名。

当前仍是草稿工作台开发版，不含 TeX；打包不代表本地项目保存、编译或 PDF 功能已完成。M3 和后续阶段状态保持原有记录。

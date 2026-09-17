# 配套目录迁移验收

日期：2026-09-17。

将 contracts、patches、resources 分别迁入 support 同名目录；paper1 迁入 support/examples/paper1。同步更新 CMake 协议输入、双语言生成工具、MiKTeX 补丁和资源路径、打包模板、论文脚本与说明文档。共享测试夹具仍在 tests/fixtures；运行时 resources/app 结构不变。

验证：

- `./scripts/build.ps1 --dev` 成功：CMake 架构检查（63 targets）、协议生成、C++ 后端、React 与 Electron TypeScript 构建通过。
- 构建自动完成 MiKTeX 增量部署与离线中文 pdfLaTeX 准备，生成中文检查 PDF。
- PowerShell 脚本语法检查、git diff --check 通过。
- 核对 39 个迁移的受版本管理文件均存在，除明确修改的论文 README 与编译脚本外内容保持一致。

未运行 CTest、npm test、旧 CEF 打包或示例论文独立编译；未执行界面验收。Vite 仍有大体积分块提示。应用保存的 paper1 旧路径需通过“打开项目”重新选择新目录。

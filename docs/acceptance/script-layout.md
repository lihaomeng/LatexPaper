# 脚本目录收敛（2026-09-17）

- scripts 顶层只保留 build.ps1、package.ps1、test.ps1 和 README.md。
- 内部构建与配置归 internal；MiKTeX 部署归 runtime；兼容入口归 legacy。
- MiKTeX 源码构建移入 tools/miktex；分发说明移入 resources/packaging。
- 同步脚本调用、CMake 前端目标、文档命令和资源路径；移除 build.ps1 中不可达的旧默认分支。
- 兼容 MiKTeX 打包入口不再默认传入固定路径，避免与 SkipBuild 冲突。
- 浏览器检查产物移入 out/verification/playwright；移除原来的空 output 目录。paper1 与工具状态 .playwright-cli 保留。

验证：scripts/build.ps1 --dev 成功；配置检查 63 个 Target，前端与 Electron TypeScript 构建通过。Runtime 脚本内容指纹变化触发一次部署，中文资源、格式初始化与既有离线就绪检查通过。所有 PowerShell 脚本语法和字面相对路径检查通过，git diff --check 通过。

未执行测试套件、正式打包、旧 CEF 构建或 MiKTeX 自身源码重编译。外部使用者如果直接调用旧辅助脚本路径，需要按 scripts/README.md 迁移；公共 build/package/test 入口保持原路径。

# 开发构建 MiKTeX 部署修复

日期：2026-09-15。

原因：build.ps1 --dev 原来只部署 Qt、CEF 和 Web，没有复制 MiKTeX；前端将未检测到引擎统一误报为 Runtime 不完整。

修改：桌面构建完成后调用 scripts/deploy-miktex.ps1，从 CMakeCache 的 LIGHTOVERLEAF_THIRDPARTY_ROOT/miktex 读取安装树（可通过 -MiKTeXRoot 覆盖），增量复制至 EXE 旁 runtime/miktex。检查元数据、四个引擎/导航程序，并逐项核对源文件与目标文件大小。复制失败不报告构建成功。不执行下载、解压或测试；保留原生严格内置 Runtime 策略。

package.ps1 跳过开发目录的 runtime，再按显式 MiKTeXRoot 部署，避免重复嵌套复制。该打包路径仅修改，未执行打包验证。

验证：scripts/build.ps1 --dev 成功，复制并核对 19,296 个文件。日志：out/dev-miktex-deploy.log。入口：out/desktop-release/bin/Release/LightOverLeaf.exe。

边界：文件大小校验不是内容哈希校验；增量部署不删除目标额外文件。未执行自动化测试、真实 LaTeX 编译或可见 PDF 预览验收。构建检查不保证任意论文宏包齐全。重新启动新 EXE 后再进行用户编译验证。

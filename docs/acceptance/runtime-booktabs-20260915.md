# booktabs / geometry 缺包修复

日期：2026-09-15。

## 原因

用户日志表明 CTEX 已加载，但缺少 booktabs.sty；紧接着使用 geometry。检查专用源 Runtime 后，geometry.sty 也未找到。这是内置宏包集合不完整，不是 PDF.js 渲染失败。

## 实施

- 在 prepare-chinese-runtime.ps1 的显式安装清单和离线文件校验中加入 booktabs、geometry。
- 修复重复运行准备脚本因“ctex 已安装”提前失败的问题：查询包安装状态，只安装缺失项。
- 扩展 resources/tex/chinese-check.tex，包含 CTEX 中文、booktabs 三线表和 geometry 页面布局。
- 已将两个宏包安装到专用源 Runtime，并通过开发构建部署到程序旁的 runtime/miktex。
- 仅显式 -AllowDownload 准备过程联网；应用、普通构建的部署检查继续禁用自动安装和 Shell Escape。未修改用户文档、公共接口或业务层。

## 本次验证

- 显式准备脚本成功：跳过已安装中文包，安装 booktabs、geometry。日志 out/provision-booktabs.log。
- scripts/build.ps1 --dev 返回 0，部署校验 25,767 个文件，受控中文三线表文档实际生成 PDF。日志 out/dev-booktabs-runtime.log。
- PDF：out/desktop-release/bin/Release/runtime/miktex/lightoverleaf-deployment-logs/chinese-f688fdbb4f534808a5cef81171c9d0c2/main.pdf。
- git diff --check 通过；未执行单元测试、CTest 或正式打包。

仅取得用户错误日志，没有完整论文源码，因此未验证其后续所有依赖，也未执行桌面预览交互验收。修复不能保证任意论文不再缺包；新增依赖应显式加入准备清单并验证，不应开放应用自动下载。

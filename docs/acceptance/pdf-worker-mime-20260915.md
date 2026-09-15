# PDF Worker MIME 修复

日期：2026-09-15。

## 根因与修改

用户日志显示 main.pdf 已生成（2 页，362509 字节），失败发生在 render 阶段：PDF.js 无法加载本地 pdf.worker.min-Dswkl-cV.mjs。

部署目录中 Worker 文件存在（1265413 字节），但 CEF 资源处理器仅将 .js 映射为 text/javascript，.mjs 落入 application/octet-stream。已补充 .mjs 的 JavaScript MIME 映射，保留 CSP、nosniff、资源路径白名单和文件大小限制。

预览面板将 failed + render 显示为“PDF 已生成，但预览失败”，避免与 TeX 编译失败混淆。未改动业务端口与 RPC。

参考文献缺少 main.bbl、暂不自动执行 BibTeX/Biber 是独立问题，本次不新增参考文献处理引擎，也不将该警告视为渲染错误。

## 验证与阻塞

- git diff --check 通过。
- 执行 scripts/build.ps1 --dev，前端构建与原生编译阶段完成，但最终链接遇到 LNK1104，无法写入 LightOverLeaf.dll。
- 已确认同一路径 LightOverLeaf.exe 仍在运行，包含可见主窗口。为保护用户未保存编辑，未强制结束应用。
- 本次构建日志：out/dev-pdf-worker.log。
- 需用户保存内容并关闭应用后重跑 --dev；目前尚未完成新版 DLL 部署，未进行桌面预览复验、单元测试或正式打包，不能宣称运行问题已验收解决。

## 关闭应用后复验

用户确认继续后，检查已无 LightOverLeaf 进程。重新执行 `scripts/build.ps1 --dev` 返回 0，原生 DLL 链接成功，前端与 Runtime 部署完成，中文三线表检查生成 PDF。构建日志为 `out/dev-pdf-worker-retry.log`。

确认部署后的 PDF Worker 模块文件存在（1265413 字节），`git diff --check` 通过。修复版入口为 `out/desktop-release/bin/Release/LightOverLeaf.exe`，需与同目录依赖一起保留。此前文件占用阻塞已解除。

按用户要求未执行测试套件或正式打包；尚未执行桌面预览交互验收，因此只确认本次构建及部署成功，不将中文编译检查等同于 PDF.js 渲染验证。
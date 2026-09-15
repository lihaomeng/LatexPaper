# 无引用文献的正常状态提示

用户确认只列出正文引用的文献，不自动列出文献库全部条目。

最新日志为编译成功 complete，生成两页 PDF。原来的 No bibliography entries requested 由本项目主动添加：检测到 bibdata，但未检测到 citation 或子 AUX 引用时触发，并非 BibTeX 执行失败。

本次将该状态从 Warning 改为 Info。前端展示中文普通说明“正文尚未引用文献，参考文献列表暂为空（仅显示已引用条目）”；项目级诊断不再渲染 main.tex:0，也不提供无效的第零行跳转。保留有行号诊断的定位功能。

未修改用户论文、未插入 nocite、未生成虚构引用，不改动编译、Snapshot 或 RPC 边界。字体替代和 MiKTeX 更新提示不在本次修改范围内。

验证：scripts/build.ps1 --dev 返回 0，原生 DLL 和前端部署成功；Runtime 的 pdfLaTeX/BibTeX/重排检查生成 PDF。日志 out/dev-bibliography-info.log。git diff --check 通过。未运行测试套件或桌面交互复验。

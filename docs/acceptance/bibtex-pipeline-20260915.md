# BibTeX 参考文献处理增量

日期：2026-09-15。

## 实现

- 排版引擎仍仅使用 pdfLaTeX。首轮生成 AUX 后，若存在 BibTeX 数据及引用声明，运行选定引擎同目录的 bibtex.exe，之后请求 Application 的既有多轮循环重排。
- 内部 KCompilerRun 增加 m_firstPass 标志，由 Application 传递；未更改 RPC、前端契约或持久化格式。工具执行留在 Adapter 内，Application 不依赖 MiKTeX。
- 复用同一进程监管逻辑：无窗口、无 Shell 拼接执行、Job Object、取消、日志上限；本轮两个工具共享截止时间，整个编译仍受 Application 的总截止时间与最多五轮约束。
- MiKTeX BibTeX 显式禁用包安装器，不查找其他 Runtime 的 BibTeX。缺少工具、非零退出或未产生可读取的非空 BBL 时返回明确失败。
- 不再对所有 BibTeX 文档固定提示“不支持自动处理”。无引用声明时提示添加 cite/nocite；不会自动修改用户文档以列出所有条目。
- Biber 未在本增量接入，检测到 BCF 时仍明确提示不支持，不能将其当作 BibTeX 文档执行。

## 本次验证

`scripts/build.ps1 --dev` 返回 0，原生链接和 Runtime 部署成功，日志为 `out/dev-bibtex.log`。

部署就绪检查已扩展为 pdfLaTeX → BibTeX → pdfLaTeX → pdfLaTeX。真实内置 Runtime 成功处理受控中文三线表与参考文献文档，并验证 main.bbl 包含 runtimecheck 条目，生成 main.pdf：

`out/desktop-release/bin/Release/runtime/miktex/lightoverleaf-deployment-logs/chinese-f68ea07fdaef400ba93f06eb5366c8a9/`

未执行单元测试、CTest、正式打包或桌面端到端交互验收；部署脚本的工具链检查不等同于原生 Adapter 取消、错误、超时和多文件引用的动态验证。

## 保留限制

中文倾斜字形仍可能使用直立字形替代；MiKTeX 更新检查提醒保留。这两类提示不是参考文献缺失原因，本次不通过隐藏警告、更换字体或自动更新来掩盖它们。用户完整论文尚未复验。

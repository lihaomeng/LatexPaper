# M5 Search/Build 阶段验收记录

- 日期：2026-09-12
- 状态：实现完成；真实 TeX 环境验收待补

## 已实现

- 项目正文搜索：UTF-8 校验、大小/数量/结果上限、取消、行列与预览、忽略回收站及重解析点。
- Build 快照：应用缓存内独立复制、64 MiB 上限、失败清理、构建结束释放。
- TeX 发现：显式配置根、随包 `texlive`、系统 `PATH`；支持 pdfLaTeX、XeLaTeX、LuaLaTeX。
- 进程隔离：无 Shell、禁用 shell escape、Job Object 进程树、超时、显式取消、1 MiB 日志背压和结构化诊断。
- RPC 与 React：搜索、能力发现、编译、取消、日志和诊断跳转；无 TeX 时明确显示不可用，不生成伪造 PDF。
- 前端资源部署改为显式依赖 `lol_frontend`，避免 DLL 未重链时桌面加载旧哈希资源。

## 可替换性证据

- Search Application 使用 Fake Source 测试；LocalFS 是独立 Target。
- Build Application 只依赖 Snapshot/Compiler Outbound Port；真实 Windows 进程后端用 Fake 编译器 EXE 验证。
- CEF 与 Loopback 继续共享 RPC Core，Build 取消允许独立并发。

## 实际验证

工作目录：`D:/CodeMyself/LightOverLeaf`。

```text
ctest --preset desktop-release --output-on-failure
27/27 passed, exit code 0, 22.46 s

npm.cmd run check
284/284 frontend tests passed; ESLint、TypeScript、Vite build passed
```

覆盖成功、编译失败、缺少引擎、非法 UTF-8 日志、超时、并发取消、快照清理、搜索边界、CEF 启动/恢复和非法依赖检查。

## 未验证项

- 本机未检测到 TeX，尚无真实 LaTeX 文档、宏包、字体和 SyncTeX 产物验收。
- M5 不负责渲染 PDF；Artifact 发布与 PDF.js 属于 M6。

结论：M5 代码和自动化验收完成，可以进入 M6；真实 TeX 环境项不被伪造为通过。

## 2026-09-12：生产编译链与实时状态收口

- Windows Backend 优先发现并使用 `latexmk.exe`，按 pdfLaTeX/XeLaTeX/LuaLaTeX 选择 `-pdf`、`-pdfxe` 或 `-pdflua`；没有 latexmk 时回退到原直接引擎。两条路径均不经过 Shell，继续禁用 Shell Escape 并受 Job Object、取消、超时和快照边界约束。
- 日志诊断分为 error/warning/info，覆盖传统 `! LaTeX Error` 与包警告；不再把全部输出错误标红。
- Build Application 新增 `status(jobId)` 入站能力。活动任务保存至多 1 MiB 有效 UTF-8 日志，终态仅保留最近 20 项；Windows Adapter 只通过增量回调报告字节，不依赖 RPC 或 UI。
- RPC v2 与 TypeScript NativeApi 增加严格 `build.status`，React 以 200 ms 有界轮询展示运行状态，终态和异常都清理定时器。未知任务、取消、日志截断、非法 UTF-8 扩张和 Fixture 漂移均有自动测试。
- 搜索同时匹配文件名和正文，文件名命中使用 `[文件名]` 预览；规则位于 Search Application，不下沉到 LocalFS 或 React。

本轮最终验证：前端 287/287；Debug 与 Release CTest 均为 33/33；V2 Fixture 共 163 项。真实 TeX 仍未安装，因此 latexmk 多轮论文、BibTeX/Biber 和实际日志节奏仍保留为外部工具链验收。

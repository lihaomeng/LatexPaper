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


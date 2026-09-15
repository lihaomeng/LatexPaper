# 编译 RPC 超时及 pdfLaTeX 初始化修复

日期：2026-09-15。

## 根因

原生 Endpoint 对所有请求固定使用 5 秒超时，Session 另有 60 秒硬上限，与编译请求最长 300 秒预算不一致。部署的 Runtime 缺少 pdflatex.fmt，首轮编译触发格式初始化，更容易暴露超时。

## 修改

- 普通 RPC 保持 5 秒。通过 Schema 校验的 build.start 使用 timeoutMs + 60 秒准备/回传余量，最大 360 秒。
- Session 最大允许时限同步为 360 秒，保留容量、取消、溢出和过期检查。
- Authoring 前端连接超时为 365 秒；普通连接默认不变。
- 部署与打包调用 initialize-pdflatex.ps1，在目标 Runtime 中执行禁用安装器的格式生成。初始化限时 180 秒，失败保留日志且部署失败，成功检查非空 pdflatex.fmt。
- 不下载宏包、不删除用户项目、不修改编译引擎总超时。

## 验证

命令：scripts/build.ps1 --dev。日志：out/dev-rpc-timeout.log。

未执行测试或真实文档 PDF 预览验收。用户旧程序若占用 DLL，必须保存并关闭后再构建，不强制结束用户应用。

实际结果：首次链接因应用占用 DLL 失败；确认应用退出后重试 --dev 成功（退出码 0）。格式初始化成功退出，并生成非空 texmfs/data/miktex/data/le/pdftex/pdflatex.fmt。初始化日志位于目标 Runtime 的 lightoverleaf-deployment-logs 目录。

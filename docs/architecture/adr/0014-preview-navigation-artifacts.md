# ADR 0014：Preview Artifact 与 Navigation 解耦

- 日期：2026-09-12
- 状态：接受

## 决策

Build Application 只依赖 `IKBuildArtifactPublisher`，成功后发布有界 PDF／SyncTeX 字节；它不知道 Preview 的存储位置。Composition Root 使用 Bridge 将该端口连接到 Preview 入站端口。Preview 负责验证 `%PDF-`、生成不透明 `artifactId`、原子持久化和分块读取，真实文件路径不进入 RPC 或 React。

Navigation 只依赖 `IKSyncTexDataSource` 与 `IKSyncTexBackend`。Composition Bridge 从 Preview 同时读取 PDF 与 SyncTeX 字节，不读取或暴露其真实缓存路径。Windows Adapter 在每次查询的私有临时目录中写入 `document.pdf` 和 `document.synctex.gz`，以参数数组语义调用 `synctex.exe view/edit`；进程使用 Job Object、5 秒超时和 64 KiB 输出上限，完成后删除目录。输出文件名必须归一化为安全相对 File ID。

Basic Adapter 解析确定性的 `LOLSYNC1` 格式，只用于测试可替换性。生产组合发现真实 `synctex.exe` 后才注入 Windows Adapter 并报告 `syncTex=true`；否则注入独立 Unavailable Adapter 并报告 false，不把 Fake 放入生产降级路径。

React 使用 PDF.js 及随包 Worker 渲染由 `preview.read` 分块取得的字节。RPC 使用有界十六进制块，避免向前端暴露路径或无限制消息。

## 结果

- Build、Preview、Navigation 可分别替换和测试。
- PDF 路径、缓存目录与文件权限不会穿过前端边界。
- 没有真实命令时不会伪报 SyncTeX 可用，测试 Fake 不进入生产组合。

# Electron 桌面迁移记录

日期：2026-09-17。

## 修改范围

新增 Electron + TypeScript 主进程、沙箱 preload、受控本地资源协议及 C++ 独立后端入口。React/Monaco/PDF.js 和现有 V1/V2 RPC、编译、文档、项目、预览、SyncTeX、设置与会话业务模块复用。

默认 `scripts/build.ps1 --dev` 改为 `electron-dev`。C++ 后端不链接 Qt/CEF。旧版作为显式迁移对照保留，未删除用户数据或旧产物。

主进程验证 IPC 来源、请求结构、大小和并发；目录选择通过主进程原生对话框完成。管道按长度分帧；后端端点在单一调度线程运行，业务工作保留后台执行。重载销毁旧会话并等待后端退出，新会话不接收旧响应。退出先关闭输入触发 C++ 取消，宽限期后终止未退出的后端。

## 验证

最终执行命令：`scripts/build.ps1 --dev`，退出码 0。

- `electron-dev`：`BUILD_TESTING=OFF`、`LIGHTOVERLEAF_BUILD_DESKTOP=OFF`、`LIGHTOVERLEAF_BUILD_ELECTRON=ON`。
- 配置阶段架构检查通过：63 个 Target。实际后端链接图没有 Qt/CEF Target。
- C++20/MSVC Release 后端构建成功。
- React/Vite 构建与 TypeScript 检查成功；Electron main/preload 的 TypeScript 检查与打包成功。
- Electron 44.4.1 固定版本运行时已部署。首次发现该版本没有自动 postinstall 下载，因此构建脚本显式调用其 install.js；修复后成功。
- MiKTeX Runtime 首次部署完成（25,768 个文件按大小校验），构建脚本内置离线中文 pdfLaTeX 准备成功；最终增量构建复用运行时。这不代表 Electron RPC 编译链路已经运行验收。
- 产物：`out/electron-dev/bin/Release/LightOverLeaf.exe` 与同目录 `LightOverLeafBackend.exe`。
- `git diff --check` 通过。
- 未运行 CTest、npm test 或额外测试构建。

已有 Vite 大 chunk 提示仍存在，不影响本次构建成功。

## 未验收事项

- 可见 Electron 窗口、中文 IME、Monaco/PDF Worker 和完整编辑/编译/SyncTeX 人工验收。
- 后端异常退出、正在编译时关闭、Renderer 重载等运行场景。
- CEF 浏览器草稿缓存迁移。SQLite 和项目文件沿用原格式，CEF IndexedDB/localStorage 不会自动进入 Electron Profile。
- 正式 Electron 发布打包、干净机和许可证完整性验收。现有发布脚本仍针对 Qt/CEF，不能用于新桌面发布。

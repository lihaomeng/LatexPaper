# ADR 0001：M0 启动验证边界

状态：已采用（2026-09-11）。

M0 清单同时要求空 Platform Target 和 Bootstrap/React 启动验证。因此实现一个仅用于启动验收的最小 Shell；完整生命周期适配、RPC 传输和编辑能力仍按 M1–M4 推进。

- 使用 CEF 150 发行版自带的 Sandbox Bootstrap，复制为 LightOverLeaf.exe，并加载同目录 LightOverLeaf.dll。`LightOverLeafBootstrap` 是部署目标，不重复编译第三方 Bootstrap 源码。
- Qt 和 CEF 共用主线程。Qt 定时器驱动 `CefDoMessageLoopWork`，浏览器生命周期和窗口访问都在所属线程上执行。暂不引入双 UI 线程、跨线程 Dispatcher 或 Worker。
- 两个平台实现仅依赖无框架类型的 `IKBrowserSurface` 生命周期接口，由入口组合。该接口不进入 Domain/Application。
- React 资源使用 `https://app.lightoverleaf.local/` 的 Scheme Handler，在 CEF I/O 线程读取受限的发布资源；没有本地 HTTP 服务。页面标记 Fake NativeApi，不宣称存在原生 RPC。
- 用户关闭窗口后等待浏览器 `OnBeforeClose`，再退出 Qt，最后执行 CefShutdown。超时返回错误；入口走紧急进程退出，避免浏览器存活时卸载 CEF。此分支不能作为正常关闭成功。
- Smoke 测试等待 React 完成 Fake 契约检查后更新页面标题，再自动关闭。仅资源 LoadEnd 不作为 React 启动成功证据。

替代方案：多线程 CEF 消息循环增加线程切换和关闭协议成本，留待 M1 的实际需求确定；禁用 Sandbox 或使用开发服务器不满足离线启动验收。

迁移与回滚：M1 可替换平台实现，保留生命周期接口；关闭 `LIGHTOVERLEAF_BUILD_DESKTOP` 可独立构建核心并运行测试。M3 将 Fake 显式替换为实际 RPC Transport。

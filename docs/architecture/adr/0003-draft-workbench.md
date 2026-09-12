# ADR 0003：截图工作台、Monaco 与草稿恢复检查点

状态：草稿架构已采用；用户于 2026-09-11 明确授权仅放开 Monaco 所需的样式属性，复验进行中。

## 本轮范围

参考用户给出的深色三栏界面，实现文件树/大纲、Monaco 源码、多标签、可调整面板和预览区域。原生 Workspace/Document/Build 服务尚未接通，因此只提供明确标记的应用内草稿，不冒充本地项目或编译结果。云协作、账户和 Visual 模式不在范围内。

## 状态与依赖

- Editor Feature 拥有 Monaco Model，是未缓存文本的唯一可修改副本；React 只接收文件、标签、光标、Dirty 和大纲投影。
- Explorer 接收只读文件投影和打开回调，不操作 Editor 实现。Preview 显示真实未就绪状态，不构造假 PDF。
- Session Feature 提供 DraftRepository 端口、快照验证器与串行 CheckpointWriter；BrowserDraftRepository 使用受控 Origin 的 IndexedDB，不直接访问原生文件系统。
- app/ 编排加载、缓存确认、快捷键与跨 Feature 协作。更换 DraftRepository 不需要修改 Monaco Model 或 Explorer。
- 这是开发要求第 25 节“接入 Monaco 前先建立草稿检查点”的增量实现，不是原生 Session Application，也不是 document.save。不能把缓存成功标记为项目保存成功。
- 草稿最多 32 个文件、合计 100 万 UTF-16 码元。快照验证版本、未知字段、重复/冲突路径、标签和活动文件。
- 编辑后约 600ms 防抖写入；Ctrl+S 立即写入。串行写入与捕获的 Model Version 防止旧确认将后续修改标记为已缓存。
- 同 Origin 用 Web Lock 取得唯一写入权。缓存读取失败或内容不合法时提示错误，不静默覆盖原数据。
- 关闭标签不删除文件；Model 保留到工作区销毁。自动缓存不提供原生 Shutdown Flush 保证。
- 缓存属于当前应用配置；清除缓存可能丢失草稿，异常退出可能丢失未确认的最后输入。不能据此宣称 M7 或 5 秒恢复性能指标完成。
- Smoke 使用独立的 -smoke 缓存配置，不打开或修改正常用户的草稿缓存。

## Monaco 与严格 CSP

Monaco 0.56.0 锁定到 package-lock.json，ESM、Worker、CSS 和字体打包到本地 /assets/。遵循官方导出路径和 Model dispose 生命周期：

- https://github.com/microsoft/monaco-editor/blob/main/docs/integrate-esm.md
- https://microsoft.github.io/monaco-editor/typedoc/interfaces/editor_editor_api.editor.ITextModel.html

样式表继续使用 nonce；不放宽任意内联样式表。已实现：

1. CEF 每个 HTML 响应通过 Windows BCryptGenRandom 生成 256 位随机值，替换构建占位符，HTML 禁止缓存。
2. Vite 开发/预览服务器使用 node:crypto 生成每次响应的随机 nonce。
3. 构建插件只改写锁定版本中的两个 Monaco 样式工厂，为其动态 style 元素附加 nonce；不修改 node_modules 原文件。匹配失效时构建报错，要求重新审查。
4. script-src 仍为 self，CEF connect-src 仍为 none；不启用 unsafe-inline 脚本、unsafe-eval、远程脚本或 CDN。
5. 生成的 HTML 不能直接静态部署成正式页面，必须由受控宿主替换 nonce 占位符。Vite preview 已实现此替换。

实测发现：Monaco 还使用 style 属性定位代码行，nonce 不适用于这些属性。当前严格策略会阻止其定位，造成代码行重叠。浏览器截图与控制台已证实，因此本轮不能宣布视觉验收通过。

2026-09-11 用户针对具体授权问题回复“可以”。据此只在 HTML meta 和 CEF 响应头增加 `style-src-attr 'unsafe-inline'`，保留样式表 nonce、脚本 self、Worker self 和桌面 connect-src none。该调整降低样式属性注入防护，但不允许内联脚本、eval 或远程脚本。新增 CSP 回归测试防止误扩大到 script-src 或 style-src；视觉与构建复验结果另见 M2 验收记录。

## 后续迁移

接入原生 Session Store 时替换 DraftRepository，并保留旧检查点直到迁移确认成功。接入 Workspace/Document 后另行实现路径授权、Revision 与原子保存，不用 IndexedDB 代替原生文件保存。

回滚工作台时保留 IndexedDB 数据；不得删除用户草稿。恢复原启动页面后才可一并回滚 Monaco 专用 nonce 适配和字体 MIME。Native Ping 契约及纯核心依赖未改变。

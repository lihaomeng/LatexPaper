# 三栏草稿工作台：实施与验证记录

日期：2026-09-11。当前状态：样式权限已授权并修复；浏览器视觉复验通过，M2 完整验收仍进行中。不是完整编辑器交付。下文原始验证保留为历史证据，最新结果见末尾。

## 已写入本地的内容

- 截图风格的深色工作台：顶部菜单、活动栏、文件树、大纲、标签、源码工具栏、预览/日志区域和状态栏。
- Monaco 0.56.0：LaTeX 语法着色、撤销/重做、查找、粗体/斜体命令、自动换行、光标状态和多 Model 生命周期。
- 新建中文相对路径草稿、目录分组、文件名筛选、标签切换/关闭、大纲提取与跳转、面板拖拽/键盘调整。
- 版本化 IndexedDB 草稿检查点、串行写入、恢复、错误提示和单写者锁。
- CEF HTML 随机 nonce、受控 Worker/字体资源、独立 Smoke 缓存配置。
- 未实现本地项目打开/保存、TeX、PDF.js、SyncTeX。预览面板明确说明没有编译产物。

## 实际验证

| 验证 | 结果及证据范围 |
|---|---|
| 前端 | 单元测试 64/64 通过；ESLint、TypeScript 和 Vite 生产构建通过 |
| Desktop Debug | 全新 Configure，Build 通过，CTest 11/11 通过 |
| Desktop Release | 全新 Configure，Build 通过，CTest 11/11 通过 |
| 契约/架构 | 包含在上述 CTest；共享 Ping Fixture、非法依赖与生成漂移通过 |
| 浏览器新建文件 | ../escape.tex 被拒绝；章节/验收.tex 创建成功并显示标签 |
| 浏览器真实编辑 | 用工具栏聚焦 Monaco 后输入中文章节，观察到对应大纲和行列更新 |
| 浏览器缓存 | Ctrl+S 后显示草稿已缓存；重载后重新打开中文文件，章节内容及大纲仍在 |
| 浏览器写入互斥 | 同一浏览器第二个标签被 Web Lock 拒绝，提示关闭其他窗口，未覆盖第一份草稿 |
| 视觉验收 | 未通过：严格 CSP 阻止 Monaco 的内联 style 属性，代码行重叠 |
| 原生 Smoke 限制 | 证明启动、缓存就绪、Native Ping 和崩溃后恢复，不证明样式正确或编辑器视觉可用 |

本轮首次桌面 Build 因预览服务占用 esbuild.exe 导致 npm ci 的 EPERM 失败；停止该预览服务后重建通过。不要在 Windows 上同时运行本仓库 Vite 服务和会执行 npm ci 的桌面构建。

## 命令与日志

执行过：

~~~powershell
npm.cmd run check --prefix web
npm.cmd test --prefix web
cmake --fresh --preset desktop-debug
cmake --build --preset desktop-debug -- /verbosity:quiet
ctest --preset desktop-debug
cmake --fresh --preset desktop-release
cmake --build --preset desktop-release -- /verbosity:quiet
ctest --preset desktop-release
~~~

浏览器使用 Playwright CLI 独立 lol-workbench 会话，在 127.0.0.1:4173 测试生产资源。生产包在普通浏览器没有 CEF 时按设计显示原生通信异常，不回退为 Fake。

本地忽略的证据：

- out/workbench-unit-tests.log
- out/workbench-debug-build.log、out/workbench-release-build.log
- out/workbench-edit-snapshot.txt、out/workbench-restored-document.txt
- output/playwright/workbench-strict-csp.png
- 各构建目录的 Testing/Temporary/LastTest.log

## 修复前阻塞与后续（历史记录）

安全审查未授权放宽样式限制；随机 nonce 能授权动态 style 元素，但不能授权 Monaco 的 style 属性。已询问用户是否允许仅调整 style-src-attr。未获确认前保持限制；当前产物不适合正式写作，也不应将其展示为视觉验收完成的版本。

授权后仍须重新执行：CSP 控制台、代码行布局、中文输入、查找、撤销/重做、草稿恢复、面板尺寸与窄窗口视觉检查，以及桌面双配置测试。

本轮未测正式中文 IME 组合输入、多机 DPI、原生强制关闭前 Flush、损坏/配额异常下的真实浏览器存储故障注入、完整性能基准和发行包。不得宣称 M4～M8 已完成。

## 2026-09-11：授权后 CSP 修复与复验

用户针对仅放开 `style-src-attr 'unsafe-inline'` 回复“可以”。本次同步修改 `web/index.html` 和 `src/platform/cef/kcefruntime.cpp`，未放宽 script-src、style-src、Worker 来源或桌面 connect-src；随机 nonce 保留。样式注入防护降低的风险见 ADR 0003。新增 `web/tests/csp.test.ts` 防止权限误扩大。

实际执行环境：Windows、VS2019 x64、CEF 150、Node 24；工作目录为仓库根目录，预览服务工作目录为 web。

| 本次实际命令／操作 | 结果 |
| --- | --- |
| `cmake --build --preset desktop-release` | 退出码 0；内置 npm ci、lint、65/65 测试、TypeScript 与 Vite build 通过 |
| `cmake --build --preset desktop-debug -- /verbosity:quiet` | 退出码 0 |
| `ctest --preset desktop-release` | 退出码 0，11/11 通过，12.43 秒 |
| `ctest --preset desktop-debug` | 退出码 0，11/11 通过，14.91 秒 |
| Vite preview，127.0.0.1:4173；Playwright CLI 会话 lol-csp-review | 生产资源正常加载，独立浏览器无 CEF 时显示原生通信异常，未回退 Fake |
| 1600×1000 实际截图 | 中文正文、行号与语法着色正常，原代码行重叠消失 |
| 新建 `章节/样式验收.tex`，工具栏粗体、键盘输入章节及中文正文、Ctrl+S、重载 | 文件、正文与章节大纲恢复；显示草稿已缓存；不是磁盘项目保存 |
| 700×800 实际截图 | 预览按窄窗口规则隐藏，编辑区、文件树和大纲无重叠 |
| 控制台 | 初次加载仅 favicon.ico 404；重载后 0 errors、0 warnings，无此前 CSP 样式报错 |

证据：`output/playwright/workbench-csp-fixed.png`、`output/playwright/workbench-csp-narrow.png`；重载快照 `.playwright-cli/page-2026-09-11T12-06-54-407Z.yml`；双配置 `Testing/Temporary/LastTest.log`。截图已实际查看，不仅检查文件存在。

本次为增量构建，未声称全新 Configure。仍存在第三方 CEF 未使用参数警告、Vite 大体积 chunk 提示及依赖弃用提示；未升级依赖。首次批量浏览器脚本因命令行转义报 SyntaxError，改用显式 CLI 操作后完成验证，未作为应用缺陷计入。

完成结论：授权的样式修复已完成，浏览器视觉阻塞解除。M2 保持进行中，尚需 CEF 桌面实际视觉／中文 IME、Worker 实际执行与离线加载证据、查找及撤销重做完整复验；不得以 Smoke 就绪替代这些检查。M3 尚未开始。M4～M8 和正式写作闭环均不在本次完成范围。

# 架构收敛记录（2026-09-17）

## 本次范围

- `useProjectSearch` 拥有搜索输入、结果、忙碌与错误状态。项目会话变化时取消请求并清空状态；迟到结果不会覆盖新会话。
- `useWorkbenchSettings` 拥有已保存设置、编辑草稿、保存互斥、输入校验和请求生命周期。设置表单独立；最近项目按钮不再意外提交设置表单。
- `usePdfPageRaster` 统一普通页面的位图渲染、取消、错误与释放。候选 PDF 首帧仍由文档提交流程验证；已提交首帧通过 published 记录复用，不再立即重复渲染。
- 布局 v2 的唯一读取来源仍为 renderer profile。原生 Session 的旧布局字段仅兼容回传；布局拖动不再触发原生 Session 保存。
- 清除 code-pill、editor-breadcrumb、preview-tabs、preview-bottom 的未使用样式。
- 默认 `package.ps1` 面向 Electron + C++ 的已有开发构建；需要构建时仅调用 `build.ps1 --dev`。`-SkipBuild` 打包已有产物。新目录包含完整 runtime、资源、第三方许可证、哈希清单和验证范围报告。
- 旧打包实现移至 `package-legacy-cef.ps1`，仅显式 `package.ps1 -LegacyCef` 使用。仍保留 Qt/CEF 源码与兼容预设。

## 验证与限制

- `scripts/build.ps1 --dev` 成功：TypeScript、前端构建、Electron 构建和 C++ 后端通过，配置阶段检查 63 个架构 Target。
- PowerShell Parser 检查新的默认打包脚本，无语法错误；`git diff --check` 通过。
- 未执行测试套件、实际打包、桌面 PDF/SyncTeX 或离线机器验收；脚本语法检查不等于发布产物验收。
- Vite 仍报告主包体积警告；本次不涉及依赖升级或代码分包。

## 剩余工作

主组件中的文档保存、冲突恢复和项目切换仍共享状态，需要以完整工作流继续提取。C++ 的 RPC 分发器、组合根和重复 Windows 进程/编码实现尚未重构。旧 Qt/CEF 尚未退役。本次不将这些项标为完成。

# Monaco 查找栏遮挡与错位修正

2026-09-17。用户确认问题为查找栏或关闭提示遮挡、错位，不是语言或匹配结果。

源码确认：应用全局 box-sizing、input、button 等规则会匹配 Monaco 内部控件；Monaco 查找按钮自带宽高和 padding，不能使用应用表单重置样式。编辑区祖先同时存在 overflow:hidden，浮动控件需要独立宿主。

修改：

- 全局盒模型和原生控件样式排除 monaco-editor、monaco-hover 及后代，保留 Monaco 自带控件布局。
- 使用 Monaco overflowWidgetsDomNode 与 fixedOverflowWidgets，将溢出控件放在 body 下独立宿主；编辑器释放时移除宿主。
- 启用 find.addExtraSpaceOnTop，为查找栏提供正文上方空间。

验证：最终 ./scripts/build.ps1 --dev 完整通过，tsc --noEmit 与 git diff --check 通过；没有运行测试套件。未完成桌面截图复验，不声称所有 DPI／窄栏场景均已实机验证。需核对 Ctrl+F、关闭按钮悬停、Escape、分栏拖拽和上下查找；搜索语义未改变。日志：out/verification/findwidgetbuild.log。

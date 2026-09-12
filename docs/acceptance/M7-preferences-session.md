# M7 Preferences／Session 阶段验收记录

- 日期：2026-09-12
- 状态：自动化实现与验收完成；桌面人工视觉复验待补

## 已实现与边界

- Preferences、Session 分为两个完整端口与适配器模块；SQLite 仅存在于各自 Adapter Target。
- `state.sqlite3` 保存 TeX 根目录、引擎、超时、自动编译偏好、布局、打开标签、活动文件和最近 20 个项目。
- Session 保存使用事务；所有文本、数组、宽度和数量均有界。
- RPC Core 只依赖 Inbound Target，Fake 端口覆盖 get/update/save/restore/history 映射。
- React 设置页使用生成 DTO；引擎和超时用于下一次编译，TeX 根目录由下一次启动的 Composition Root 读取。
- 重新选择同一项目时恢复仍存在的标签和活动文件；不会绕过原生目录选择器静默打开磁盘路径。

## 实际验证

```text
V2 codegen OK (158 shared fixtures)
npm.cmd run check: 287/287 passed
cmake --build --preset desktop-release: passed
ctest --preset desktop-release --output-on-failure: 30/30 passed
```

后续 M6 Windows SyncTeX Adapter 合入后的最终总回归为 31/31；本阶段 Persistence 测试继续通过，前端仍为 287/287。

`LightOverLeafPersistenceTests` 使用隔离临时数据库验证默认值、更新、会话恢复和历史上限；RPC 测试使用 Fake，未污染用户数据库。架构检查确认 SQLite 没有进入 Domain、Application 或 RPC。

## 未验证项

- 尚未在可见 CEF 窗口中人工操作设置表单并重启截图复验。
- `autoCompile` 当前只持久化偏好，仍由用户手动编译；界面已明确标注，不冒充已启用。

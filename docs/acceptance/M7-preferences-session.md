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
ctest --preset desktop-release --output-on-failure: 33/33 passed
```

后续全部收口修改后的最终 Debug/Release 总回归均为 33/33；本阶段 Persistence 测试继续通过，前端仍为 287/287。

`LightOverLeafPersistenceTests` 使用隔离临时数据库验证默认值、更新、会话恢复和历史上限；RPC 测试使用 Fake，未污染用户数据库。架构检查确认 SQLite 没有进入 Domain、Application 或 RPC。

## 未验证项

- 尚未在可见 CEF 窗口中人工操作设置表单并重启截图复验。
- 本轮桌面控制运行时无法启动，因此中文 IME 与可见重启复验仍未完成。

## 2026-09-12：项目创建、完整会话与自动编译收口

- “新建项目”继续通过 Qt 原生选择器授予空目录能力，并用 `document.saveAs` 原子创建标准 `main.tex`；React 不接触绝对路径。
- Session 在原布局、标签和活动文件之外，新增编辑区宽度、光标行列与 PDF 缩放；500 ms 防抖持久化，SQLite Adapter 通过 `PRAGMA table_info` 对旧数据库执行增量迁移。
- 自动编译偏好已真正生效：成功保存后 900 ms 防抖启动构建，并对冲突、保存中、已有任务和已切换会话做保护。
- 空 Workspace 不进入最近项目；最近项目仍保存不透明 ID，重新打开仍要求原生目录选择确认。

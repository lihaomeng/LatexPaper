# ADR 0015：Preferences／Session 持久化边界

- 日期：2026-09-12
- 状态：接受

## 决策

Preferences 与 Session 是两个独立模块，各自拥有 Domain、Inbound、Outbound、Application 与 SQLite Adapter Target。Application 不包含 SQLite 头文件；RPC Core 只链接两个 Inbound Target。SQLite 只在两个持久化 Adapter 中允许，架构检查对具体 Target 做精确例外。

两个 Adapter 可共享同一 `state.sqlite3` 文件，但表、端口和事务互不泄漏。Session 保存布局、打开标签、活动文件与最近 20 个项目；Preferences 保存 TeX 根目录、首选引擎、超时和自动编译偏好。Composition Root 在创建 Build Adapter 前读取 TeX 根目录，因此该项重启生效；引擎和超时由前端每次构建传入，立即生效。

## 结果

- CEF、React、Build Application 均不依赖 SQLite。
- Fake Port 可直接替换 SQLite 并验证 RPC 映射。
- 会话保存采用事务和有界字段；前端以 500 ms 防抖写入。

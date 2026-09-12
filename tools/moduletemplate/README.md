# 模块模板

M0 的可编译模板位于 `src/modules/document/`（System、Workspace 采用同样结构）。

新增业务模块时显式创建并注册以下 Target：

| 层 | Target | 链接 |
|---|---|---|
| Domain | `lol_<module>_domain` / STATIC | `lol_kernel` |
| Inbound | `lol_<module>_inbound` / INTERFACE | `lol_kernel` |
| Outbound | `lol_<module>_outbound` / INTERFACE | `lol_kernel` |
| App | `lol_<module>_app` / STATIC | 自己的前三个 Target |

每个 Target 使用 `lol_register(target layer module)`，根 CMakeLists 显式添加模块。
纯抽象接口使用 IK 前缀，类型使用 K 前缀，文件名全小写且无分隔符。
端口公开头文件仅放在各自 include 根；App 的实现不对外公开。
存在实际需求后再创建 Adapter 和 Fake，不预先声明尚未确定的业务接口。
架构规则变更必须同时添加允许及拒绝样例。

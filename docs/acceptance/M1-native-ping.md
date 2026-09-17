# 桌面生命周期与真实 Native Ping：实施和验收记录

日期：2026-09-11。本文记录 M0 基础之上的增量开发，不代表 M0～M8 全部完成。

## 本轮交付

- 独立 `lol_platform_lifecycle` 静态 Target，以纯 C++ 状态机实现 Loading、Ready、Recovering、Closing、Closed 和 Failed。
- 时钟由 Qt 注入；加载/恢复等待 20 秒，关闭等待 10 秒；一次启动最多恢复 Renderer 两次，重复关闭和迟到就绪不重置关闭期限。
- Qt 依赖 `IKBrowserSurface`；CEF 回传就绪、失败、关闭和 Renderer 终止。未增加 Qt → CEF 实现依赖，也未将业务逻辑放进窗口。
- 真实 CEF Renderer/Browser Message Router，受控主 Frame 校验、非持久请求限制与导航/崩溃/关闭时查询清理。
- 组合根注入 `KNativeMessageHandler`。CEF 平台只认识标准库消息回调，Transport 负责 JSON 转值树，RPC Core 使用生成的 Schema 验证器。
- 当前 Ping 请求限制 4096 字节、转换深度 8、单对象字段数 32；非法请求返回稳定错误。它是过渡性的启动回显协议，不是完整通用 RPC。
- React 生产模式使用真实 CEF NativeApi；前端验证回显 Schema、ID 和 Client Sequence，处理失败、超时、取消待定查询及迟到回调。
- Vite 模式可显式使用 Fake；生产模式缺少原生通道时失败，不回退为 Fake 成功。
- 更新 README、开发要求第 25 节及 ADR 0002，保留 M0 历史验收记录。

## 本轮实际验证

环境沿用 M0 的 Windows x64、VS2019 / MSVC 19.29、Qt 5.15.11、CEF 150、CMake 3.28、Python 3.12、Node 24 / npm 11。

执行命令：

```powershell
cmake --preset desktop-debug
cmake --build --preset desktop-debug -- /verbosity:quiet
ctest --preset desktop-debug

cmake --fresh --preset desktop-release
cmake --build --preset desktop-release -- /verbosity:quiet
ctest --preset desktop-release

cmake --fresh --preset core-debug
cmake --build --preset core-debug -- /verbosity:quiet
ctest --preset core-debug

cmake --fresh --preset core-release
cmake --build --preset core-release -- /verbosity:quiet
ctest --preset core-release

npm.cmd run check --prefix web
```

| 检查 | 实际结果 |
|---|---|
| Desktop Debug | 构建通过，CTest 11/11 通过 |
| Desktop Release | 全新配置、构建通过，CTest 11/11 通过 |
| Core Debug / Release | 分别全新配置、构建通过，CTest 各 5/5 通过，无 Qt/CEF 依赖查找 |
| 前端 | ESLint、39 项测试、TypeScript、Vite 生产构建全部通过 |
| 契约 | 23 个共享 Fixture；C++/TS 生成与漂移检查通过 |
| 架构 | 桌面 26 个 Target、纯核心 24 个 Target 检查通过；非法依赖配置测试通过 |

每个桌面配置的 11 项测试包含：真实启动/原生往返/正常关闭、真实 Renderer 崩溃后恢复、核心契约、纯生命周期、4 项 Qt Fake 浏览器测试、架构检查、非法依赖配置、Codegen 漂移。

前端新增测试覆盖错误 JSON、错误 ID/Sequence、额外字段、原生错误、同步通道错误、超时取消与迟到回调、生产模式禁止 Fake、开发模式显式 Fake、原生通道优先及非法超时参数。

本轮修复了一处 CEF 头文件的 Windows `max` 宏冲突：在 CEF Transport Target 私有定义 `NOMINMAX` 等平台编译参数。没有更改第三方安装内容。

## 启动与证据

```powershell
& D:/CodeMyself/LightOverLeaf/out/desktop-release/bin/Release/LightOverLeaf.exe
```

启动后页面显示 `CEF 原生通信`，往返成功时显示 `接口往返校验通过`。目前页面仍是启动基础页，没有伪装成已具备编辑器功能的 UI 占位。

自动测试会自行退出；正常启动不加 Smoke 参数。Debug 产物位于 `out/desktop-debug/bin/Debug/`。运行时 DLL、CEF 资源与 `frontend/web/` 必须保留在产物目录，不应单独复制 EXE。

详细证据为本地忽略产物：

- `out/native-debug-build.log`、`out/native-release-build.log`。
- `out/native-frontend-check.log`。
- 四个构建目录中的 `Testing/Temporary/LastTest.log`。

## 尚未完成或尚未验证

- 未实现文件树、Monaco 编辑、Revision/原子保存、TeX 编译、诊断、PDF.js、SyncTeX、设置、草稿与会话恢复、安装包。
- 未实现通用 RPC 响应/事件 Schema、Capabilities、重复请求管理、多方法路由、业务取消和 Loopback/CEF 完整替换性测试。
- 原生 JSON 大小/深度限制已实现，但尚未对真实 CEF 通道进行完整恶意字节 Fixture 或模糊测试；核心值树 Fixture 不等同于 JSON 字节集成测试。
- 真实 Renderer 崩溃恢复通过；关闭超时边界由纯状态机测试验证，未在真实 CEF 上强行制造卡死关闭。不得据此宣称全部异常生命周期矩阵通过。
- 自动重载不包含未保存文档恢复保证；接入 Monaco 前须增加草稿检查点。
- 尚未安装或验证 TeX；未静默下载宏包或工具链。
- 未进行多机 Windows 10/11、DPI/视觉、性能、长时间稳定性或干净机器分发验证，未执行 clang-tidy。
- 保留 CEF 对 VS2022 的工具链建议警告和第三方头文件未使用参数警告；当前本机成功不等同于全部兼容性认证。

下一步按开发要求第 25 节补齐 M3，并建立 Workspace/Document 的 Fake Store 合约与完整打开、读取、编辑、保存链路。

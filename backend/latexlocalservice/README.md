# LaTeX 本地服务

所有目录名不使用连字符。目录分组不改变原有 C++ 命名空间、头文件接口、RPC 协议或 CMake Target 名称。

- texengine：build 编译调度与工具链、preview 产物管理、navigation/SyncTeX。
- workspace：project 项目管理、document 文件读写、search 搜索、export 导出、workflow 跨模块协调。
- persistence：preferences 设置与 session 会话持久化。
- transport：rpc、json、loopback 和旧 cef 传输。
- app：backend 原生入口、composition 装配、bootstrap 旧外壳入口、kernel 公共类型、platform 平台适配、tests 原生测试。

业务模块内部维持 domain/application/adapters 分层，目录分组不会授权新的跨模块依赖。
共享协议定义和夹具留在根目录 contracts/ 与 tests/fixtures/。
根目录 CMakeLists.txt 负责工程配置，本目录 CMakeLists.txt 负责原生模块装配。
从项目根目录使用 scripts/build.ps1 --dev 构建；入口产物仍为 out/electron-dev/bin/Release/LightOverLeaf.exe。

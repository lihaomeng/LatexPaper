# 后端目录迁移

原 src 与 apps 中的 C++ 实现统一迁入 backend/latexlocalservice，按 texengine/workspace/persistence/transport/app 组织。原生测试迁入 app/tests，根目录 tests/fixtures 保留共享协议夹具。新后端所有目录均不含连字符。

根 CMake 进入服务级 CMake 装配模块；原生 Target、头文件接口和 RPC 不变。平台源文件路径、前端 CSP 夹具路径及文档引用同步更新。架构检查改为按新目录映射模块，并在源码根缺失时报错，避免迁移后检查空目录。

scripts/build.ps1 --dev 构建成功：63 个 Target 的架构检查、迁移后的 C++ 后端、TypeScript/Vite 和 Electron 构建通过。git diff --check 通过。未运行测试套件、旧 Qt/CEF 构建或打包验收。

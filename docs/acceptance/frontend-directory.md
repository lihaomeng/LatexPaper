# 前端目录统一（2026-09-17）

React 界面、NativeApi、前端测试与配置从 web/ 整体移入 frontend/web/；Electron 主进程、preload、通信和构建配置从 apps/electron/ 整体移入 frontend/electron/。

已同步开发构建、Electron 与旧 CEF 资源部署、打包许可证路径、协议生成输出、Electron 的协议导入、前端 codegen 命令、测试夹具相对路径、忽略规则和文档源码路径。

根目录 contracts、C++ backend 与 src 保持职责。旧 Qt/CEF 平台代码仍作为原生兼容实现保留。分发内 resources/app/web 不改名，避免改变资源加载协议。

验证：scripts/build.ps1 --dev 成功，CMake 检查 63 个 Target、协议生成、前端 TypeScript/Vite 和 Electron 构建通过。未运行测试套件、旧 CEF 构建或实际打包；测试夹具仅修正静态路径。Vite 主包体积警告仍存在。

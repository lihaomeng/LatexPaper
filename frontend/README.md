# 前端

- `web/`：React + TypeScript 界面、Monaco 编辑器、PDF.js 预览、NativeApi 和前端测试。
- `electron/`：Electron 主进程、preload、窗口生命周期和 C++ 后端通信。

日常构建仍从项目根目录执行 `./scripts/build.ps1 --dev`。
浏览器独立开发在 `frontend/web/` 执行 `npm run dev`。
协议源码位于 `support/contracts/`，TypeScript 生成文件写入 `frontend/web/.generated/`。
产物位于 `out/electron-dev/`；分发目录中的 `resources/app/web` 是运行时资源路径，不是源码目录。
C++ 后端统一位于 `backend/latexlocalservice/`；旧 Qt/CEF 平台适配器仍在原生兼容目录，不属于当前 Electron 界面实现。

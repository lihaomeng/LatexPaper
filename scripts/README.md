# 开发脚本

## 日常入口

在项目根目录运行：

- `./scripts/build.ps1 --dev`：默认 Electron + C++ 开发构建，关闭测试目标和测试执行。
- `./scripts/package.ps1 -SkipBuild`：将已有 Electron 开发产物整理为分发目录；省略该参数时仍仅用 `--dev` 构建。
- `./scripts/test.ps1`：仅显式执行历史测试，不属于日常构建。

## 内部职责

| 目录 | 职责 |
|---|---|
| `internal/` | Electron、前端构建与独立 CMake 配置 |
| `runtime/` | MiKTeX 增量判断、部署、中文支持与格式初始化 |
| `legacy/` | 旧版 CEF 打包和旧命令兼容转发 |

`ensure-dev-runtime.ps1` 判断是否需要部署；`deploy-miktex.ps1` 执行部署并调用准备与初始化。两者职责不同，不合并为重复流程。

MiKTeX 自身的源码构建工具位于 `tools/miktex/build-runtime.ps1`；打包说明模板位于 `resources/packaging/README.txt`。

## 旧路径迁移

原 `scripts/build-electron.ps1`、`build-frontend.ps1`、`configure.ps1` 移入 `scripts/internal/`。
原四个 MiKTeX 部署/准备脚本移入 `scripts/runtime/`。
原 `package-full.ps1`、`package-miktex.ps1` 移入 `scripts/legacy/`，推荐改用统一 `scripts/package.ps1`。
旧 CEF 打包使用 `scripts/package.ps1 -LegacyCef`；兼容路径仅显式调用，其历史测试流程不参与默认构建。
`-SkipBuild` 使用已部署 Runtime，不能同时指定 `-MiKTeXRoot`；需要更换 Runtime 时先重新构建。

所有构建产物放入 `out/`，浏览器检查截图放入 `out/verification/playwright/`。
`.playwright-cli/` 是已忽略的工具状态，`paper1/` 是受版本管理的论文项目，不属于可清理构建产物。

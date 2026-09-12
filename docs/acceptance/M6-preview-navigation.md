# M6 Preview／Navigation 阶段验收记录

- 日期：2026-09-12
- 状态：自动化实现与验收完成；真实 TeX 论文端到端待外部环境

## 已实现与边界

- Build 成功后经 `IKBuildArtifactPublisher` 发布有界 PDF／SyncTeX 字节；Bridge 只存在于 Composition Root。
- Preview 独立模块验证 PDF 头，限制 PDF 64 MiB、SyncTeX 16 MiB，原子写入应用缓存，并按最大 512 KiB 分块读取。
- RPC 只返回不透明 `artifactId` 和有界十六进制块，不返回磁盘路径。
- React 使用本地 `pdfjs-dist` 和随包 Worker 渲染、翻页，并在能力可用时提供正向与反向定位。
- Navigation Application 只依赖 `IKSyncTexDataSource` 与 `IKSyncTexBackend`。DataSource 同时提供 PDF 与 `.synctex.gz` 字节，存储路径不穿过端口。
- Windows SyncTeX Adapter 在唯一私有缓存目录中物化 `document.pdf` 与 `document.synctex.gz`，无 Shell 调用 `synctex.exe view/edit`；使用 Job Object、5 秒超时、64 KiB 输出上限和完成后清理。
- 解析器验证页码、有限非负坐标、行列与安全相对 File ID；拒绝绝对路径、盘符和路径穿越。
- Basic `LOLSYNC1` Adapter 只用于确定性单元测试。生产组合只使用 Windows Adapter 或独立 Unavailable Adapter，不把 Fake 当作降级实现。
- Composition Root 从环境变量、持久化 TeX 根目录、随包 `texlive` 和 PATH 发现 `synctex.exe`；只有发现成功才报告 `syncTex=true`。当前机器没有该命令，因此成品诚实报告 false。

## 可替换性与失败路径证据

- Basic Adapter、Windows Adapter、Unavailable Adapter 是三个独立 Target；Application 和 RPC 不链接 Win32。
- `LightOverLeafWindowsNavigationTests` 使用独立 Fake `synctex.exe` 黑盒验证命令行与进程边界，而不是把 Fake 注入生产 Composition。
- 覆盖正向/反向查询、缺失程序、非法 PDF、非零退出、绝对输出路径、超过 64 KiB 输出、5 秒超时、进程树终止和临时目录清理。

## 最终实际验证

工作目录：`D:/CodeMyself/LightOverLeaf`。

```text
cmake --preset desktop-release
cmake --build --preset desktop-release --parallel 4
结果：通过；Architecture OK，85 targets

npm.cmd run check（由桌面构建执行）
287/287 passed；ESLint、TypeScript、Vite build passed

ctest --test-dir out/desktop-release -C Release --output-on-failure
31/31 passed，0 failed，26.45 s

package-lite.ps1 内再次运行 CTest
31/31 passed，0 failed；随后归档测试与单文件 EXE smoke 均通过
```

## 未验证项与结论

- 当前 PATH、配置根与随包目录均无 `pdflatex`、`xelatex`、`lualatex` 或 `synctex`，无法产生真实论文 PDF／`.synctex.gz` 做工具链端到端验证。
- 该缺口不再是代码 Backend 缺失；补入 TeX 后无需修改 Use Case 或 RPC，只需重启让 Composition 重新发现命令。
- 本阶段代码、替换边界和自动化失败路径验收完成；真实宏包、字体与 SyncTeX 精度属于外部 TeX 环境验收，不伪造通过。

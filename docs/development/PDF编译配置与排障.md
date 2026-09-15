# LightOverLeaf PDF 编译配置与排障

更新日期：2026-09-13。

## 1. 当前编译流程

LightOverLeaf 只编译已经保存到本地项目目录的内容。应用内草稿点击“保存并编译”后，先选择空文件夹并落盘，再执行：

```text
保存文件 → 只读 Snapshot → 内置 MiKTeX → PDF/SyncTeX Artifact → PDF.js 预览
```

项目内容不会上传。编译进程由 Windows Adapter 直接创建，不经过 Shell；Shell Escape 与 MiKTeX 自动安装均被禁用。

## 2. 产品版固定使用内置 MiKTeX

当前只发布 `Full` 绿色目录，不再发布 Lite 或 Portable TeX Live 版本。必须保留完整目录，不能只复制 `LightOverLeaf.exe`：

```text
LightOverLeaf-Full-win64/
├── LightOverLeaf.exe
└── runtime/miktex/texmfs/install/miktex/bin/x64/
    ├── xelatex.exe
    ├── pdflatex.exe
    ├── lualatex.exe
    └── synctex.exe
```

检测到 `runtime/miktex` 后，编译和双向定位只使用该 Runtime，不读取设置中的旧 TeX Root，也不回退系统 `PATH`。因此用户不需要另装 TeX；若内置文件缺失，应用会明确报告 Runtime 不完整。

## 3. 生成 Full 绿色目录

默认 MiKTeX 根为 `D:\CodeMyself\QTBest\thirdparty_install\miktex`：

```powershell
.\scripts\package.ps1
```

指定其它隔离安装根时：

```powershell
.\scripts\package.ps1 -MiKTeXRoot 'D:\thirdparty\miktex'
```

产物位于 `out/distributions/full-<时间>-<随机值>/LightOverLeaf-Full-win64`。双击其中的 `LightOverLeaf.exe`；启动时不解压文件，也不调用 7-Zip、tar、CMD 或 PowerShell。

## 4. 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 按钮显示“保存并编译” | 当前仍是应用内草稿 | 选择空目录，把全部草稿文件保存为本地项目 |
| “内置 MiKTeX Runtime 不完整” | 只复制了 EXE，或 `runtime/miktex` 被删改 | 重新复制整个 Full 目录，并用 `package-files.sha256` 核验 |
| 编译失败但有日志 | LaTeX 源码、字体或宏包错误 | 切换到“日志”，按诊断定位源码 |
| 宏包缺失 | 构建 Runtime 时选择的宏包集合不足 | 在 Runtime 构建阶段用 `-PackageSet complete` 重建；应用运行时不会下载 |
| 编译成功但不能双向定位 | `synctex.exe` 缺失，或未生成 `.synctex.gz` | 核验内置 Runtime，确认编译参数含 `-synctex=1` |
| 引用/参考文献未更新 | Runtime 未包含 latexmk/BibTeX/Biber，直接引擎只运行一次 | 在 Runtime 构建阶段补齐工具；不要在用户运行时安装 |
| 启动失败 | 目录结构被压平或 CEF/Qt 文件缺失 | 保持整个绿色目录层级，不要单独移动入口 EXE |

## 5. 开发态说明

未携带 `runtime/miktex` 的开发构建仍允许通过 `LIGHTOVERLEAF_TEX_ROOT`、测试 Root 或系统目录发现 TeX，以便替换 Adapter 和运行测试。这只是开发回退，不是产品发布形态。Build Application、RPC 和 React 始终只依赖抽象端口，不依赖 MiKTeX 类型。

## 6. M9 目标流程（尚未实现）

第 1 节记录的是当前已实现行为。用户已确认后续目标为：新草稿编译和实时预览不再选择目录，只有显式“导出项目”或“另存为”才选择目标目录；打开已有项目仍可选择源目录。设计已确定，但当前版本尚未完成代码迁移。

目标编译流程为：

~~~text
Monaco 最新 Generation
  -> 停止输入 900 ms 或手动编译
  -> 只读基础快照 + Dirty Overlay Snapshot
  -> 内置 MiKTeX
  -> PDF/SyncTeX Artifact
  -> Generation 仍为最新且 PDF.js 加载成功
  -> 原子替换预览
~~~

每个草稿或项目只允许一个活动编译与一个最新 Pending Generation。新输入请求协作取消旧任务；任何迟到结果都被丢弃。编译开始、失败或取消时继续显示上一份成功 PDF，日志必须区分 snapshot、detect、compile、artifact、render，并优先显示首条可操作诊断。

导出使用独立流程和错误状态。Qt 选择的绝对目录只保留在原生侧，React 通过短期一次性 destinationToken 请求 Export Application；目录取消或导出失败不得触发编译、清空 PDF 或显示成 TeX 错误。完整实施顺序与验收标准见 [实时预览与托管草稿开发计划](实时预览与托管草稿开发计划.md)。

# LightOverLeaf PDF 编译配置与排障

## 1. 当前编译流程

LightOverLeaf 只编译本地项目，不直接编译仅存在于浏览器内存中的草稿。草稿界面点击“保存并编译”后，会要求选择空文件夹，将全部草稿文件写入该文件夹，再依次执行：

```text
保存文件 → 创建只读 Snapshot → 发现 TeX → latexmk/直接引擎 → 发布 PDF Artifact → PDF.js 显示
```

项目内容不会上传。编译不经过 Shell，默认禁用 Shell Escape，不会静默下载 TeX 宏包。

## 2. Lite 版需要的外部工具

Lite EXE 不包含 TeX。至少需要以下一个引擎：

- `xelatex.exe`（中文文档推荐）；
- `pdflatex.exe`；
- `lualatex.exe`。

推荐同时安装 `latexmk.exe`。存在 latexmk 时，LightOverLeaf 会优先使用它处理多轮引用、目录和 BibTeX/Biber 编排；不存在时回退到一次直接引擎调用。

## 3. 配置方式

1. 打开“设置”。
2. 在“TeX 根目录”填写安装根目录，例如 `C:\texlive\2026`、`C:\texlive` 或 `C:\Program Files\MiKTeX`。
3. 中文论文优先选择 XeLaTeX。
4. 保存设置。当前项目下一次编译会立即重新发现工具链，不需要重启应用。
5. 点击“重新编译”；若当前仍是草稿，点击“保存并编译”并选择空目录。

应用还会检查：

- EXE 同目录下的 `texlive`（Full/便携布局）；
- `C:\texlive\<年份>\bin\windows`；
- Program Files 与 LocalAppData 下的 MiKTeX；
- AppData 下的 TinyTeX；
- 系统 `PATH`。

## 4. 常见失败

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 按钮显示“保存并编译” | 当前是应用内草稿 | 选择空目录，把草稿保存为本地项目 |
| “未检测到 TeX 编译器” | Lite 版没有外部 TeX，或 Root 填错 | 检查 Root 下能否找到所选引擎 EXE |
| 编译失败但有日志 | LaTeX 源码、宏包或字体错误 | 切换到“日志”，点击诊断跳到源码 |
| 编译成功但不能双向定位 | 缺少 `synctex.exe` 或 `.synctex.gz` | 安装完整 SyncTeX 工具并重新启动应用刷新导航能力 |
| 引用/参考文献未更新 | 没有 latexmk，直接引擎只运行一次 | 安装 latexmk，并确保 BibTeX/Biber 位于同一工具链 |
| 宏包缺失 | 离线环境没有该宏包 | 通过 TeX 发行版的受控管理工具预先安装；应用不会自动联网下载 |

## 5. Full 包

Full 包可带 Portable TeX，但仓库不会自动下载该载荷。准备好包含 `bin/windows/xelatex.exe` 的 Portable TeX Root 后执行：

```powershell
.\scripts\package-full.ps1 -PortableTexRoot 'D:\portable-texlive'
```

Lite 与 Full 共用同一 Build Application 和编译端口，差异只在打包内容与工具链发现来源。

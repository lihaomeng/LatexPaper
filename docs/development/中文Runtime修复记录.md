# 内置 MiKTeX 中文编译修复

日期：2026-09-15。

## 原因与修改

`ctexart.cls not found` 表明 pdfLaTeX 已启动，但发行包缺少 CTEX 文档类。只复制编译器或者生成 pdflatex.fmt 不足以支持中文。补齐后又实际检测到缺少 zhnumber，已一并处理。

- 新增 `scripts/prepare-chinese-runtime.ps1`，显式准备 ctex、cjk、arphic、cjkpunct、zhmetrics、zhnumber。
- 只有指定 `-AllowDownload` 的依赖准备阶段允许下载；部署、普通开发构建和应用内编译不会下载宏包。
- Runtime 源码构建的宏包安装阶段接入中文准备；跳过宏包安装时仍跳过该步骤。
- 开发部署与 Full 目录打包均刷新文件数据库、字体映射，并使用内置 pdfLaTeX 离线编译受控的中文文档；失败则终止并保留日志。
- 默认 CTEX 字体配置使用随 Runtime 分发的 Arphic 宋体和楷体，不依赖系统 SimSun，不复制 Windows 字体。黑体和仿宋使用替代映射，不承诺字体外观完全一致。文档显式指定的 fontset 仍优先。
- 不修改用户文档，不改变 RPC、编译器端口、Snapshot 或前端预览逻辑。

## 维护命令

仅在首次配置或补齐专用源 Runtime 时显式运行（此命令联网安装宏包）：

```powershell
.\scripts\prepare-chinese-runtime.ps1 -Root 'D:\CodeMyself\QTBest\thirdparty_install\miktex' -AllowDownload
```

日常编译、部署可运行：

```powershell
.\scripts\build.ps1 --dev
```

直接运行 `out/desktop-release/bin/Release/LightOverLeaf.exe`；必须保留其旁边的 Runtime 和其他依赖文件，不可只复制 EXE。

## 验证记录

最终 `.\scripts\build.ps1 --dev` 构建及部署通过，Runtime 校验了 25,760 个文件。中文检查生成 1 页 PDF。现有字体集对部分斜体字形会回退到默认字形并产生非致命警告，不代表所有中文字形完全匹配。

已使用部署后的内置 pdfLaTeX，在禁用安装器与 Shell Escape 的条件下，将 `resources/tex/chinese-check.tex` 编译为非空 PDF。内容覆盖中文标题、正文、粗体和楷体。

直接验证日志：`out/chinese-readiness.log`。每次部署检查的完整 TeX 日志和 PDF 保存在 Runtime 的 `lightoverleaf-deployment-logs/chinese-<唯一标识>/`。

未执行单元测试、CTest、正式打包或桌面预览交互验收；该中文编译属于 Runtime 部署就绪检查。未验证用户完整论文中的所有额外宏包、字体和参考文献需求。

MiKTeX 的“尚未检查更新”提示与缺少 CTEX 是不同问题；不通过全局更新或开放自动安装来掩盖缺包。

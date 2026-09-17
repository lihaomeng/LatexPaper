# 完整论文编译样例 paper1

这是中文合成实验论文，不是真实科研成果。含 120 条确定性观测、三种检测规则、四项指标、混淆矩阵、两张 PNG、跨页长表、目录、多文件章节、数学公式和三条正文引用。文献对应 notes 中的内部测试说明，不冒充已发表论文。

## 在编辑器中使用

1. 选择“打开项目”，打开本目录，不是原来的“应用内草稿”。
2. 将 main.tex 作为主文件并编译，保持 pdfLaTeX。
3. 正常情况下会生成目录、正文、参考文献及完整数据附录，三条引用均不应显示问号。
4. 修改正文、数据或图片后重新编译，检查预览刷新、页码、缩放及 SyncTeX。

生成好的图片、CSV 和表格已随项目保存，正常编译不需要先运行生成脚本。不要删除 data 或 figures。

## 复算与独立验证

在仓库根目录的 PowerShell 中执行：

```powershell
.\support\examples\paper1\generate-data.ps1
.\support\examples\paper1\compile.ps1
```

生成脚本仅使用 Windows PowerShell/.NET 的数学与绘图能力；重新生成会覆盖本目录 data 与 figures 中的生成文件，不覆盖手写正文。编译脚本默认使用 out/electron-dev/bin/Release/runtime/miktex，可通过 -RuntimeRoot 指定其他 Runtime；脚本，将输入复制到仓库 out 下的唯一目录后编译，不污染论文源码；成功时打印 PDF 位置。运行时禁用自动安装和 Shell Escape。

## 数据约定

- 全部数据由公式生成，不存在训练集、真实测量或统计显著性声明。
- CSV 是逐点记录；指标与图表均由相同数据计算，不手工编造结果。
- B、C 使用已知合成基线，不能与真实学习模型做公平性能比较。
- 引用使用 cite，不使用 nocite；仅打印实际被正文引用的三条测试文献。
- 无外部图片、网址或网络资源，无 TikZ、minted、Biber 等额外工具依赖。

## 文件结构

- main.tex：唯一编译入口。
- sections：引言、方法、实验、讨论、附录。
- references.bib / notes：三个内部测试文献及全文说明。
- data：120 行数据、指标 CSV 和四份生成表格。
- figures：观测曲线及指标对照 PNG。
- generate-data.ps1 / compile.ps1：复算和独立编译入口。

全部图表及文本仅供测试，可自由修改；故障测试请另存副本，不要破坏默认成功样例。

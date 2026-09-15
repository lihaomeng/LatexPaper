# 本地验证记录

## 实际执行

- 运行 generate-data.ps1：生成 120 条观测、12 个目标异常、指标 CSV、四份数据表和两张 1400×680 PNG。
- 运行 compile.ps1：内置 MiKTeX pdfLaTeX → BibTeX → pdfLaTeX → pdfLaTeX 全部返回 0。
- 输出：15 页，730186 字节；三条参考文献均存在 BBL 条目，没有未解析引用、重排提示、Overfull 或 Underfull 日志。
- PDF 位置：D:/CodeMyself/LightOverLeaf/out/paper1-3a84b390f3b24bc7aae112d2f0501f93/main.pdf。
- 使用 Poppler 渲染并查看全部 15 页；中文、公式、两图、文献和跨页表格均可见，无裁切或缺图。目录引用已收敛。渲染工具对其本地 Bulgarian/Greek/Thai nameToUnicode 配置路径给出提示，但生成了全部中文页面图像。

## 独立数值核对

| 规则 | TP | TN | FP | FN | Accuracy | Precision | Recall | F1 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| A | 7 | 92 | 16 | 5 | 0.8250 | 0.3043 | 0.5833 | 0.4000 |
| B | 7 | 105 | 3 | 5 | 0.9333 | 0.7000 | 0.5833 | 0.6364 |
| C | 8 | 104 | 4 | 4 | 0.9333 | 0.6667 | 0.6667 | 0.6667 |

每行总数为 120，TP+FN=12。数据为确定性合成测试，不可作为真实科研结论。

## 尚存提示与验证边界

内置中文字体存在斜体到直立体的回退警告，MiKTeX 存在未检查更新提示；不阻止本样例编译。未变更应用代码、未重编应用、未执行单元测试套件。未在 LightOverLeaf 可见界面逐项操作验收，PDF.js 页码、缩放和 SyncTeX 交互需要打开 paper1 后检查。

import type { DraftSnapshot } from "../features/session";
export const newProjectMain = `% LightOverLeaf 本地项目
\\documentclass[a4paper,12pt]{article}
\\usepackage[margin=2.5cm]{geometry}

\\title{My LightOverLeaf Project}
\\author{Author}
\\date{\\today}

\\begin{document}
\\maketitle

Start writing here.

\\end{document}
`;
const main = `% LightOverLeaf · 内存草稿示例
% 草稿缓存不等于保存到本地项目文件。
\\documentclass[UTF8, a4paper, 12pt]{ctexart}

\\usepackage{amsmath, amssymb}
\\usepackage{graphicx}
\\usepackage{booktabs}
\\usepackage{geometry}
\\geometry{margin=2.5cm}

\\title{让写作回归专注}
\\author{作者}
\\date{\\today}

\\begin{document}
\\maketitle

\\begin{abstract}
本文是一份可编辑的 LaTeX 起始草稿。左侧可以切换文件，
下方大纲会跟随章节更新。编辑内容会缓存到应用的草稿区，
但尚未写入任何本地项目文件。
\\end{abstract}

\\section{引言}
好的工具应该让复杂的工作变得清晰，让注意力留在内容本身。

LightOverLeaf 希望提供本地优先的写作体验：
从源码到文档，从一个想法到一篇完整的论文。

\\subsection{研究背景}
在这里介绍研究背景、已有工作和需要解决的问题。
你可以选中文字，使用工具栏插入粗体或斜体命令。

\\subsection{本文贡献}
\\begin{enumerate}
  \\item 清晰的文件组织与章节导航。
  \\item 多标签源码编辑和独立草稿缓存。
  \\item 为本地编译与 PDF 预览预留明确边界。
\\end{enumerate}

\\section{方法设计}
\\input{sections/method}

\\section{实验与讨论}
在这里补充实验设计、数据集和评价指标。
注意：右侧尚未接入 TeX 编译，不会生成示意性的 PDF。

\\subsection{评价指标}
一个简单的公式示例：
\\begin{equation}
  L = \\frac{1}{N} \\sum_{i=1}^{N} (y_i - \\hat{y}_i)^2
\\end{equation}

\\section{结论}
记录你的结论，以及值得继续探索的问题。

\\bibliographystyle{plain}
\\bibliography{references}
\\end{document}
`;
export const starter: DraftSnapshot = {
  version: 1,
  files: [
    { path: "main.tex", content: main },
    { path: "sections/method.tex", content: `% 独立章节文件
\\subsection{总体思路}
将一个复杂系统划分成职责清晰的模块，
通过稳定的接口连接，而不是互相依赖内部实现。

\\subsection{实现过程}
在这里描述你的方法，并补充必要的公式与图表。
` },
    { path: "references.bib", content: "@book{example,\n  title = {请替换为真实参考文献},\n  author = {作者},\n  year = {2026}\n}\n" },
    { path: "README.md", content: "# LightOverLeaf 草稿工作台\n\n这是应用内草稿，不是已打开的本地项目。\n\n- Ctrl+S：立即缓存草稿\n- Ctrl+F：查找当前文档\n- Ctrl+Z / Ctrl+Y：撤销 / 重做\n- 点击左侧大纲跳转章节\n\n关闭标签不会删除文件。缓存仅属于当前应用配置，清除应用缓存可能丢失草稿。\n本地文件保存、TeX 编译与 PDF 预览尚未接通。\n" },
  ],
  open: ["main.tex"], active: "main.tex",
};

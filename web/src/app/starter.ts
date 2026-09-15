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
// New drafts use the pdfLaTeX-compatible template; existing drafts are untouched.
const main = newProjectMain;
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
    { path: "README.md", content: "# LightOverLeaf 草稿工作台\n\n这是应用内草稿，不是已打开的本地项目。\n\n- Ctrl+S：立即缓存草稿\n- Ctrl+F：查找当前文档\n- Ctrl+Z / Ctrl+Y：撤销 / 重做\n- 点击左侧大纲跳转章节\n\n关闭标签不会删除文件。缓存仅属于当前应用配置，清除应用缓存可能丢失草稿。\n使用内置 pdfLaTeX 编译；中文需要自行准备兼容宏包与字体，暂不自动处理参考文献。\n" },
  ],
  open: ["main.tex"], active: "main.tex",
};

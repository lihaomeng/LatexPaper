# TEST-003：复现协议

这是内部编译测试记录，不是已发表研究。先执行 generate-data.ps1 产生 CSV、表格和 PNG；再以 main.tex 为主文件执行 pdfLaTeX、BibTeX、pdfLaTeX、pdfLaTeX。所有文件引用使用相对路径。不启用 shell escape，不联网下载。对照 README 的验收清单检查三条文献、两张图、公式和长表。

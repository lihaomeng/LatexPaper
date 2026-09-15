# pdfLaTeX 单引擎实现

日期：2026-09-15。

用户确认当前仅支持 pdfLaTeX；本决策覆盖早期多引擎及 latexmk 优先方案。

## 修改

- Windows Backend 只检测 pdfLaTeX，直接启动引擎，不调用 latexmk/Perl。拒绝其他引擎请求，保留端口隔离。
- 保持无 Shell Escape、MiKTeX 禁止自动安装、无控制台进程、隔离 Snapshot、取消和进程树超时。
- Build Application 最多执行五轮，各轮共享总编译超时。Adapter 比较主文件 aux/toc/out 并检查重编译提示，返回重编译信号；Application 决定是否继续。
- 每轮显示轮次和当前轮日志；达到上限提示引用可能未稳定。参考文献依赖只提示，不启动 BibTeX/Biber。
- RPC V2 Build/Preferences 引擎枚举收窄为 pdflatex，前后端必须配套更新；内部历史枚举保留，但产品入口拒绝其他引擎。
- SQLite 将已有 XeLaTeX/LuaLaTeX 设置迁移至 pdfLaTeX，设置页说明变更，不再显示引擎选择。
- 部署与打包仅要求 pdflatex 和 synctex 工具，仍复制完整 MiKTeX 资源，不裁剪宏包或字体。
- 新草稿使用英文 article 模板；不改写用户现存草稿。中文和依赖 fontspec/xeCJK 的旧项目需用户调整为兼容 pdfLaTeX 的宏包和字体方案。

## 验证边界

scripts/build.ps1 --dev 已通过（退出码 0），MiKTeX 部署核对 19,296 个文件，日志 out/dev-pdflatex-only.log。未执行测试、真实 TeX 编译或打包。

当前自动重编译覆盖主文件 aux/toc/out 与引擎重编译提示，不宣称具备 latexmk 全部依赖分析能力。没有自动参考文献、索引处理和任意中文模板兼容性保证。

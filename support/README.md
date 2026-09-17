# 项目配套内容

- `contracts/`：前后端共享 RPC Schema，供 C++ 与 TypeScript 协议生成工具消费。
- `examples/paper1/`：多文件中文论文示例及数据生成、编译脚本。
- `patches/`：独立第三方依赖构建所需补丁。
- `resources/`：TeX 支持资源与打包说明模板。

这些目录不改变业务模块依赖方向。测试夹具仍位于根目录 `tests/fixtures/`，生成代码与构建产物保持原输出位置。
Electron 分发目录中的 `resources/app/` 属于运行时结构，与此处源码资源目录无关。

日常在仓库根目录执行 `./scripts/build.ps1 --dev`。
论文示例移动后，应用中保存的旧项目路径需要通过“打开项目”重新选择 `support/examples/paper1/`。

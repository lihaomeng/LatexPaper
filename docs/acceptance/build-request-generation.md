# 重复编译 FILE_CONFLICT 修复

日期：2026-09-15。

## 原因与改动

- 前端此前使用文档 revision + 1 作为编译 generation；内容未变时重试，原生严格递增检查返回 build.staleGeneration。
- 新增独立请求序号分配器，每次编译请求分配安全整数，使用 sessionStorage 保留 Renderer 重载前的高水位；不依赖文档版本，也不放宽原生 Latest-wins 检查。
- RPC V2 错误枚举增加 BUILD_STALE_GENERATION、BUILD_JOB_EXISTS、BUILD_SNAPSHOT_EXISTS、BUILD_OVERLAY_CONFLICT、BUILD_DUPLICATE_OVERLAY。前端与原生通过 Schema 同步生成契约，需配套部署。
- 前端展示对应中文错误；提交请求时标记 snapshot，后续使用原生状态更新阶段，不再提前宣称 TeX 已开始 compile。
- 文件保存冲突仍使用 FILE_CONFLICT，不删除用户文件或编译缓存。

## 验证

scripts/build.ps1 --dev 已成功（退出码 0），MiKTeX 部署核对 19,296 个文件；日志位于 out/dev-build-generation.log。未执行测试或真实 PDF 预览验收。

人工验收：内容不变连续编译；失败后重试；自动编译后手动编译；Renderer 重载后重试。每次请求 generation 应递增，旧请求仍应被拒绝。

# ADR 0019: Electron desktop and C++ backend

Status: accepted, 2026-09-17, requested by the user.

Electron replaces the Qt/CEF desktop shell. React, TypeScript, Monaco and PDF.js
remain in `frontend/web/`. Electron main and sandboxed preload live in `frontend/electron/`.
The C++ application and adapters run in `LightOverLeafBackend.exe`; no Electron
or Node ABI enters the business modules. The existing V1/V2 RPC schemas remain
the source of truth.

Renderer -> narrow preload bridge -> validated Electron IPC -> bounded stdio
frames -> C++ RPC endpoint -> existing application ports.

Electron owns windows, native directory dialogs, local asset serving and child
process lifetime. C++ owns workspace authorization, revisions, atomic saves,
compilation, cancellation, artifact access, SyncTeX and persistence. Only main
may attach a directory selected by a native dialog to an internal transport
envelope; renderer RPC never accepts an absolute directory capability.

Pipe frames are a four-byte little-endian UTF-8 byte length followed by JSON,
limited to 32 MiB. Protocol stdout contains frames only. EOF closes the session,
cancels pending work and shuts down the backend. Renderer replacement gets a
new backend session; old replies must never reach its replacement.

`scripts/build.ps1 --dev` builds the Electron desktop and C++ backend without
test targets or test execution. The former Qt/CEF path remains available only
as an explicit legacy preset during migration. It is not linked into the new
backend. Rollback selects that legacy preset without changing RPC schemas or
user project formats. Removing legacy sources is a separate cleanup after
desktop behavior has been accepted.

This ADR supersedes the Qt/CEF shell, lifecycle and deployment requirements in
the V2 architecture baseline for the Electron build. Existing business,
ownership, filesystem and compiler safety requirements still apply.


Implementation references:

- https://www.electronjs.org/docs/latest/tutorial/security
- https://www.electronjs.org/docs/latest/tutorial/context-isolation
- https://www.electronjs.org/docs/latest/api/protocol

Electron version is pinned in frontend/electron/package-lock.json. The development
build uses the package's explicit install.js runtime installation command.

## 2026-09-17：开发分发入口收敛

默认 `scripts/package.ps1` 消费 Electron 开发构建，需要构建时只使用 `scripts/build.ps1 --dev`。它不自动执行测试或旧 CEF smoke，输出报告明确标记未运行项；开发分发不代表通过发布验收。旧 Qt/CEF 打包通过 `-LegacyCef` 显式选择。详细边界与验证见 [架构收敛记录](../../acceptance/architecture-cleanup.md)。

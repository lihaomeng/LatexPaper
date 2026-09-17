import assert from 'node:assert/strict';
import { test } from 'node:test';
import { SystemRpcClient } from '../src/native-api/system.ts';
import { WorkspaceRpcClient } from '../src/native-api/workspace.ts';
import type { SystemTransport } from '../src/native-api/system.ts';
test('file management forwards workspace identity and explicit operation through RPC', async () => {
  const calls: unknown[] = [];
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    calls.push(request.params);
    success(JSON.stringify({ version: 2, id: request.id, clientSequence: request.clientSequence, ok: true,
      method: 'workspace.manageFile', result: { workspaceId: 'workspace-1', displayName: '项目', revision: 'mutation-1', entries: [] } }));
    return () => {};
  } };
  const client = new WorkspaceRpcClient(new SystemRpcClient(transport));
  await client.manageFile('workspace-1', 'create', '新稿.tex');
  await client.manageFile('workspace-1', 'rename', '新稿.tex', '改名.tex');
  await client.manageFile('workspace-1', 'remove', '改名.tex');
  assert.deepEqual(calls, [
    { workspaceId: 'workspace-1', operation: 'create', fileId: '新稿.tex', destination: '' },
    { workspaceId: 'workspace-1', operation: 'rename', fileId: '新稿.tex', destination: '改名.tex' },
    { workspaceId: 'workspace-1', operation: 'remove', fileId: '改名.tex', destination: '' },
  ]);
});

test('directory management uses its own RPC contract', async () => {
  const calls: unknown[] = [];
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    calls.push({ method: request.method, params: request.params });
    success(JSON.stringify({ version: 2, id: request.id, clientSequence: request.clientSequence, ok: true,
      method: 'workspace.manageDirectory', result: { workspaceId: 'workspace-1', displayName: '项目',
        revision: 'mutation-directory', entries: [{ fileId: '正文', directory: true, sizeBytes: 0 }] } }));
    return () => {};
  } };
  const client = new WorkspaceRpcClient(new SystemRpcClient(transport));
  await client.manageDirectory('workspace-1', 'create', '章节');
  await client.manageDirectory('workspace-1', 'rename', '章节', '正文');
  await client.manageDirectory('workspace-1', 'remove', '正文');
  assert.deepEqual(calls, [
    { method: 'workspace.manageDirectory',
      params: { workspaceId: 'workspace-1', operation: 'create', directoryId: '章节', destination: '' } },
    { method: 'workspace.manageDirectory',
      params: { workspaceId: 'workspace-1', operation: 'rename', directoryId: '章节', destination: '正文' } },
    { method: 'workspace.manageDirectory',
      params: { workspaceId: 'workspace-1', operation: 'remove', directoryId: '正文', destination: '' } },
  ]);
});

test('trash listing and restore use explicit bounded contracts', async () => {
  const requests: { method: string }[] = [];
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire) as { id: string; clientSequence: number; method: string };
    requests.push(request);
    const base = { version: 2, id: request.id, clientSequence: request.clientSequence, ok: true };
    success(JSON.stringify(request.method === 'workspace.listTrash' ? { ...base,
      method: request.method, result: { entries: [{
        trashId: '0123456789abcdef0123456789abcdef', originalFileId: '旧稿.tex',
        directory: false, deletedAtUnixMs: 1770000000000 }] } } : { ...base,
      method: request.method, result: { workspaceId: 'workspace-1', displayName: '项目',
        revision: 'restored-1', entries: [] } }));
    return () => {};
  } };
  const client = new WorkspaceRpcClient(new SystemRpcClient(transport));
  const listed = await client.listTrash('workspace-1');
  assert.equal(listed.entries[0].originalFileId, '旧稿.tex');
  await client.restoreTrash('workspace-1', listed.entries[0].trashId);
  assert.deepEqual(requests.map(request => request.method), ['workspace.listTrash', 'workspace.restoreTrash']);
});

test('native change polling validates a typed response', async () => {
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    success(JSON.stringify({ version: 2, id: request.id, clientSequence: request.clientSequence,
      ok: true, method: 'workspace.pollChanges', result: { changed: true } }));
    return () => {};
  } };
  const client = new WorkspaceRpcClient(new SystemRpcClient(transport));
  assert.equal(await client.pollChanges('workspace-1'), true);
});

test('workspace client maps generated requests and validates unicode projections', async () => {
  const methods: string[] = [];
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire) as { id: string; clientSequence: number; method: string; params: Record<string, string> };
    methods.push(request.method);
    const base = { version: 2, id: request.id, clientSequence: request.clientSequence, ok: true } as const;
    if (request.method === 'workspace.open' || request.method === 'workspace.getState' || request.method === 'workspace.refresh')
      success(JSON.stringify({ ...base, method: request.method, result: { workspaceId: 'workspace-1',
        displayName: '论文', revision: 'revision-1', entries: [{ fileId: '章节/引言.tex', directory: false, sizeBytes: 12 }] } }));
    else if (request.method === 'workspace.close') success(JSON.stringify({ ...base, method: request.method, result: {} }));
    else if (request.method === 'document.open') success(JSON.stringify({ ...base, method: request.method,
      result: { fileId: request.params.fileId, content: '内容', revision: 'document-1', utf8Bom: false } }));
    else success(JSON.stringify({ ...base, method: request.method,
      result: { fileId: request.params.fileId, revision: 'document-2' } }));
    return () => {};
  } };
  const client = new WorkspaceRpcClient(new SystemRpcClient(transport));
  assert.equal((await client.open()).displayName, '论文');
  assert.equal((await client.getState()).entries[0].fileId, '章节/引言.tex');
  assert.equal((await client.refresh()).revision, 'revision-1');
  assert.equal((await client.openDocument('章节/引言.tex')).content, '内容');
  assert.equal((await client.saveDocument('章节/引言.tex', '修改', 'document-1')).revision, 'document-2');
  assert.equal((await client.saveDocumentAs('章节/副本.tex', '副本', true)).fileId, '章节/副本.tex');
  await client.close();
  assert.deepEqual(methods, ['workspace.open', 'workspace.getState', 'workspace.refresh',
    'document.open', 'document.save', 'document.saveAs', 'workspace.close']);
});

test('workspace client rejects a mismatched response method', async () => {
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire) as { id: string; clientSequence: number };
    success(JSON.stringify({ version: 2, id: request.id, clientSequence: request.clientSequence, ok: true,
      method: 'workspace.getState', result: { workspaceId: 'workspace-1', displayName: 'Project',
        revision: 'revision-1', entries: [] } }));
    return () => {};
  } };
  await assert.rejects(new WorkspaceRpcClient(new SystemRpcClient(transport)).open(), /INVALID_RESPONSE/);
});

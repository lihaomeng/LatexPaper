import assert from 'node:assert/strict';
import { test } from 'node:test';
import { ExportRpcClient } from '../src/native-api/export.ts';
import { SystemRpcClient, type SystemTransport } from '../src/native-api/system.ts';

test('export RPC exposes only an opaque destination token and immutable file snapshot', async () => {
  const requests: unknown[] = [];
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    requests.push(request);
    const base = { version: 2, id: request.id, clientSequence: request.clientSequence,
      ok: true, method: request.method };
    const result = request.method === 'export.selectDestination'
      ? { destinationToken: 'a'.repeat(64), displayName: '论文导出', expiresAtUnixMs: 2000000000000 }
      : { generation: request.params.generation, complete: true,
        writtenFiles: ['main.tex'], failures: [] };
    success(JSON.stringify({ ...base, result })); return () => {};
  } };
  const client = new ExportRpcClient(new SystemRpcClient(transport));
  const destination = await client.selectDestination();
  const result = await client.exportProject(destination.destinationToken, 7,
    [{ fileId: 'main.tex', content: '\\documentclass{article}' }]);
  assert.equal(result.complete, true);
  assert.equal(JSON.stringify(requests).includes('D:\\\\'), false);
  assert.deepEqual((requests[0] as { params: unknown }).params, {});
  assert.deepEqual((requests[1] as { params: { generation: number; files: unknown[] } }).params.files,
    [{ fileId: 'main.tex', content: '\\documentclass{article}' }]);
  assert.equal((requests[1] as { params: { generation: number } }).params.generation, 7);
});

test('export RPC rejects a cross-method response', async () => {
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    success(JSON.stringify({ version: 2, id: request.id, clientSequence: request.clientSequence,
      ok: true, method: 'export.project', result: { generation: 1, complete: true,
        writtenFiles: [], failures: [] } }));
    return () => {};
  } };
  await assert.rejects(new ExportRpcClient(new SystemRpcClient(transport)).selectDestination(),
    /INVALID_RESPONSE/);
});
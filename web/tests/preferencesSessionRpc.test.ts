import assert from 'node:assert/strict';
import { test } from 'node:test';
import { PreferencesSessionRpcClient } from '../src/native-api/preferencesSession.ts';
import { SystemRpcClient, type SystemTransport } from '../src/native-api/system.ts';

test('preferences and session RPC methods remain separate and validated', async () => {
  const methods: string[] = [];
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    methods.push(request.method);
    const base = { version: 2, id: request.id, clientSequence: request.clientSequence,
      ok: true, method: request.method };
    const result = request.method === 'preferences.get' || request.method === 'preferences.update'
      ? { texRoot: request.params.texRoot ?? '', engine: request.params.engine ?? 'xelatex',
          timeoutMs: request.params.timeoutMs ?? 120000, autoCompile: request.params.autoCompile ?? false }
      : request.method === 'session.restore'
        ? { found: true, state: { workspaceRoot: 'D:/paper', openFiles: ['main.tex'],
            activeFile: 'main.tex', sidebarWidth: 280, previewOpen: true } }
        : request.method === 'session.save' ? { saved: true } : { roots: ['D:/paper'] };
    success(JSON.stringify({ ...base, result })); return () => {};
  } };
  const client = new PreferencesSessionRpcClient(new SystemRpcClient(transport));
  assert.equal((await client.getPreferences()).engine, 'xelatex');
  assert.equal((await client.updatePreferences({ texRoot: 'D:/texlive', engine: 'lualatex',
    timeoutMs: 45000, autoCompile: true })).texRoot, 'D:/texlive');
  const restored = await client.restoreSession();
  assert.equal(restored.state.activeFile, 'main.tex');
  assert.equal(await client.saveSession(restored.state), true);
  assert.deepEqual(await client.history(), ['D:/paper']);
  assert.deepEqual(methods, ['preferences.get', 'preferences.update', 'session.restore',
    'session.save', 'session.history']);
});

test('preferences client rejects cross-method responses', async () => {
  const transport: SystemTransport = { send: (wire, success) => {
    const request = JSON.parse(wire);
    success(JSON.stringify({ version: 2, id: request.id, clientSequence: request.clientSequence,
      ok: true, method: 'session.history', result: { roots: [] } }));
    return () => {};
  } };
  await assert.rejects(new PreferencesSessionRpcClient(new SystemRpcClient(transport))
    .getPreferences(), /INVALID_RESPONSE/);
});

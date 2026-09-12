import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readFileSync } from 'node:fs';
import * as protocol from '../.generated/rpc/protocol.ts';
import { SystemRpcClient, FakeSystemTransport, CefSystemTransport } from '../src/native-api/index.ts';
import type { SystemTransport } from '../src/native-api/system.ts';

const fixtures = ['system-v2.json', 'lifecycle-v2.json', 'workspace-v2.json', 'document-v2.json']
  .flatMap(file => JSON.parse(readFileSync(new URL('../../tests/fixtures/' + file, import.meta.url), 'utf8'))) as {
  name: string; type: 'Request' | 'PingResponse' | 'CapabilitiesResponse' | 'ErrorResponse' |
    'Cancellation' | 'RequestEvent' | 'ConnectedEvent' | 'AcceptedEvent' | 'CancellationResponse' |
    'WorkspaceRequest' | 'WorkspaceManageRequest' | 'WorkspaceStateResponse' | 'WorkspaceClosedResponse' |
    'DocumentOpenRequest' | 'DocumentSaveRequest' | 'DocumentOpenResponse' | 'DocumentSaveResponse';
  value: unknown; accepted: boolean;
}[];
for (const fixture of fixtures) test('V2 fixture: ' + fixture.name, () => {
  assert.equal(protocol[`validate${fixture.type}`](fixture.value), fixture.accepted);
});
const request: protocol.Request = { version: 2, id: 'test', clientSequence: 4, method: 'system.ping', params: {} };
const response = { version: 2, id: 'test', clientSequence: 4, ok: true, method: 'system.ping', result: {} };

test('Fake system reports only unavailable native services and supports repeat read-only calls', async () => {
  const client = new SystemRpcClient(new FakeSystemTransport());
  await client.ping();
  assert.deepEqual(await client.getCapabilities(), { nativeFiles: false, build: false, pdf: false, syncTex: false });
  await client.request(request);
  await client.request(request);
  await assert.rejects(client.request({ ...request, method: 'unknown.method' }), /METHOD_NOT_FOUND/);
});

test('CEF adapter serializes request and shares protocol client with Fake', async () => {
  const transport = new CefSystemTransport({ query: options => {
    assert.deepEqual(JSON.parse(options.request), request);
    assert.equal(options.persistent, false);
    options.onSuccess(JSON.stringify(response));
    return 9;
  }, cancel: () => assert.fail('not cancelled') });
  assert.deepEqual(await new SystemRpcClient(transport).request(request), response);
});

for (const bad of ['invalid', 'null', JSON.stringify({ ...response, id: 'wrong' }),
  JSON.stringify({ ...response, clientSequence: 3 }), JSON.stringify({ ...response, extra: true }),
  JSON.stringify({ ...response, method: 'system.getCapabilities', result: { nativeFiles: false, build: false, pdf: false, syncTex: false } }),
  ' '.repeat(8193)]) test('V2 invalid reply ' + bad.slice(0, 100), async () => {
  const client = new SystemRpcClient({ send: (_wire, success) => { success(bad); return () => {}; } });
  await assert.rejects(client.request(request), /INVALID_RESPONSE/);
});

test('pending duplicate ID rejected; timeout releases transport and ignores late reply', async () => {
  let success: ((wire: string) => void) | undefined;
  let cancelled = 0;
  const client = new SystemRpcClient({ send: (_wire, callback) => { success = callback; return () => { cancelled++; }; } }, 15);
  const first = client.request(request);
  await assert.rejects(client.request(request), /DUPLICATE_REQUEST/);
  await assert.rejects(first, /RPC_TIMEOUT/);
  assert.equal(cancelled, 1);
  success?.(JSON.stringify(response));
});

test('abort releases transport, removes pending ID, and pre-abort sends nothing', async () => {
  let calls = 0;
  let cancels = 0;
  const client = new SystemRpcClient({ send: () => { calls++; return () => { cancels++; }; } });
  const controller = new AbortController();
  const pending = client.request(request, controller.signal);
  controller.abort();
  await assert.rejects(pending, /RPC_CANCELLED/);
  await assert.rejects(client.request(request, controller.signal), /RPC_CANCELLED/);
  assert.equal(calls, 1);
  assert.equal(cancels, 1);
});

test('synchronous abort during send still cancels returned query', async () => {
  const controller = new AbortController();
  let cancels = 0;
  const client = new SystemRpcClient({ send: () => { controller.abort(); return () => { cancels++; }; } });
  await assert.rejects(client.request(request, controller.signal), /RPC_CANCELLED/);
  assert.equal(cancels, 1);
});

test('caller mutation cannot alter expected response correlation', async () => {
  let success: ((wire: string) => void) | undefined;
  const client = new SystemRpcClient({ send: (_wire, callback) => { success = callback; return () => {}; } });
  const mutable = { ...request };
  const pending = client.request(mutable);
  mutable.id = 'mutated';
  mutable.clientSequence = 8;
  success?.(JSON.stringify(response));
  assert.deepEqual(await pending, response);
});

test('in-flight queue bounded at 64 and all cancelled requests settle', async () => {
  const controller = new AbortController();
  const client = new SystemRpcClient({ send: () => () => {} });
  const pending = Array.from({ length: 64 }, (_, index) => client.request({ ...request, id: 'id-' + index }, controller.signal).catch(error => error.message));
  await assert.rejects(client.request({ ...request, id: 'overflow' }), /RESOURCE_EXHAUSTED/);
  controller.abort();
  assert.ok((await Promise.all(pending)).every(value => value === 'RPC_CANCELLED'));
});

test('transport exceptions and empty failure codes reject instead of leaving promises pending', async () => {
  const throwing: SystemTransport = { send: () => { throw new Error('gone'); } };
  const failing: SystemTransport = { send: (_wire, _success, failure) => { failure(''); return () => {}; } };
  for (const transport of [throwing, failing]) await assert.rejects(new SystemRpcClient(transport).request(request), /TRANSPORT_UNAVAILABLE/);
  for (const timeout of [0, -1, NaN, Infinity, 305001]) assert.throws(() => new SystemRpcClient(throwing, timeout), /INVALID_TIMEOUT/);
});

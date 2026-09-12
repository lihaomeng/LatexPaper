import assert from 'node:assert/strict';
import { test } from 'node:test';
import { cancellationProbe } from '../src/native-api/smoke.ts';
import type { CefQueryOptions } from '../src/native-api/index.ts';

for (const mode of ['success', 'rejected-cancel', 'wrong-terminal', 'early-close'] as const) {
  test('cancellation probe: ' + mode, async () => {
    let subscription: CefQueryOptions | undefined;
    let original: CefQueryOptions | undefined;
    let nextId = 0;
    let released = 0;
    const probe = cancellationProbe({
      query: options => {
        const request = JSON.parse(options.request);
        if (options.persistent) {
          subscription = options;
          queueMicrotask(() => options.onSuccess(JSON.stringify({ version: 2, event: 'rpc.connected', sessionId: 'test' })));
        } else if (request.method === 'system.ping') {
          original = options;
          queueMicrotask(() => subscription?.onSuccess(JSON.stringify({ version: 2, event: 'rpc.accepted',
            sessionId: 'test', id: request.id, generation: 1, sequence: 1 })));
        } else if (request.method === 'rpc.cancel') {
          queueMicrotask(() => {
            options.onSuccess(JSON.stringify({ ...request, accepted: mode !== 'rejected-cancel' }));
            original?.onFailure(2, 'RPC_CANCELLED');
            subscription?.onSuccess(JSON.stringify({ version: 2, event: 'rpc.completed', sessionId: 'test',
              id: request.id, generation: 1, sequence: 2, outcome: mode === 'wrong-terminal' ? 'succeeded' : 'cancelled' }));
          });
        }
        return ++nextId;
      },
      cancel: () => { released++; },
    });
    if (mode === 'early-close') probe.close();
    if (mode === 'success') await probe.ready;
    else await assert.rejects(probe.ready, /RPC_/);
    assert.ok(released >= 1);
    probe.close();
  });
}

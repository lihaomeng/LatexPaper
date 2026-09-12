import assert from 'node:assert/strict';
import { test } from 'node:test';
import { CancellationRpcClient } from '../src/native-api/cancellation.ts';
import { NativeEventSubscription } from '../src/native-api/subscription.ts';
import type { CefQueryOptions } from '../src/native-api/index.ts';
import type { Cancellation } from '../.generated/rpc/protocol.ts';

const command: Cancellation = { version: 2, method: 'rpc.cancel', sessionId: 'session-a', id: 'request-a', generation: 1 };
for (const accepted of [true, false]) test('cancel acknowledgement: ' + accepted, async () => {
  const client = new CancellationRpcClient({ send: (wire, success) => {
    assert.deepEqual(JSON.parse(wire), command);
    success(JSON.stringify({ ...command, accepted }));
    return () => {};
  } });
  assert.equal(await client.cancel(command), accepted);
});
for (const bad of [{}, { ...command, accepted: true, generation: 2 },
  { ...command, accepted: true, sessionId: 'other' }, { ...command, accepted: 'true' },
  { ...command, accepted: true, extra: 1 }]) test('reject malformed or unrelated cancel response ' + JSON.stringify(bad), async () => {
  const client = new CancellationRpcClient({ send: (_wire, success) => { success(JSON.stringify(bad)); return () => {}; } });
  await assert.rejects(client.cancel(command), /INVALID_RESPONSE/);
});
test('cancel timeout releases query; late acknowledgement is ignored', async () => {
  let success!: (wire: string) => void;
  let released = 0;
  const client = new CancellationRpcClient({ send: (_wire, callback) => { success = callback; return () => released++; } }, 5);
  await assert.rejects(client.cancel(command), /RPC_TIMEOUT/);
  success(JSON.stringify({ ...command, accepted: true }));
  assert.equal(released, 1);
});
test('64 acknowledgement limit, close settles all and refuses further work', async () => {
  let released = 0;
  const client = new CancellationRpcClient({ send: () => () => released++ });
  const pending = Array.from({ length: 64 }, () => assert.rejects(client.cancel(command), /RPC_CANCELLED/));
  await assert.rejects(client.cancel(command), /RESOURCE_EXHAUSTED/);
  client.close();
  await Promise.all(pending);
  assert.equal(released, 64);
  await assert.rejects(client.cancel(command), /TRANSPORT_UNAVAILABLE/);
});
test('accepted notification supplies ticket for correlated wire cancellation', async () => {
  let subscriptionOptions!: CefQueryOptions;
  let acceptedCount = 0;
  let completedCount = 0;
  let cancelled = 0;
  const ticket = { version: 2 as const, event: 'rpc.accepted' as const, sessionId: 'session-a', id: 'request-a', generation: 1, sequence: 1 };
  const subscription = new NativeEventSubscription({
    query: options => {
      if (options.persistent) { subscriptionOptions = options; return 1; }
      assert.deepEqual(JSON.parse(options.request), command);
      options.onSuccess(JSON.stringify({ ...command, accepted: true }));
      return 2;
    },
    cancel: () => cancelled++,
  }, () => completedCount++, () => assert.fail('unexpected error'), 5000, () => acceptedCount++);
  subscriptionOptions.onSuccess(JSON.stringify({ version: 2, event: 'rpc.connected', sessionId: 'session-a' }));
  subscriptionOptions.onSuccess(JSON.stringify(ticket));
  subscriptionOptions.onSuccess(JSON.stringify(ticket));
  subscriptionOptions.onSuccess(JSON.stringify({ ...ticket, sessionId: 'other', generation: 2 }));
  assert.equal(acceptedCount, 1);
  const completed = { version: 2, event: 'rpc.completed', sessionId: 'session-a', id: 'request-a',
    generation: 1, sequence: 1, outcome: 'cancelled' };
  subscriptionOptions.onSuccess(JSON.stringify(completed));
  assert.equal(completedCount, 0);
  subscriptionOptions.onSuccess(JSON.stringify({ ...completed, sequence: 2 }));
  assert.equal(completedCount, 1);
  const result = subscription.cancelRequest(ticket);
  ticket.id = 'mutated';
  assert.equal(await result, true);
  await assert.rejects(subscription.cancelRequest({ ...ticket, sessionId: 'other' }), /INVALID_SESSION/);
  subscription.close();
  assert.equal(cancelled, 1);
});

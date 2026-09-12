import assert from 'node:assert/strict';
import { test } from 'node:test';
import { NativeEventSubscription } from '../src/native-api/subscription.ts';
import type { CefQueryOptions } from '../src/native-api/index.ts';

const connected = JSON.stringify({ version: 2, event: 'rpc.connected', sessionId: 'session-a' });
test('persistent subscription validates handshake, filters sessions and releases once', async () => {
  let options!: CefQueryOptions;
  let cancelled = 0;
  let delivered = 0;
  const subscription = new NativeEventSubscription({
    query: value => { options = value; return 9; },
    cancel: id => { assert.equal(id, 9); cancelled++; },
  }, () => delivered++, () => assert.fail('unexpected failure'));
  assert.equal(options.persistent, true);
  options.onSuccess(connected);
  await subscription.ready;
  const event = { version: 2, event: 'rpc.completed', sessionId: 'session-a', id: 'one', generation: 1, sequence: 1, outcome: 'succeeded' };
  options.onSuccess(JSON.stringify({ ...event, sessionId: 'old-session' }));
  options.onSuccess(JSON.stringify(event));
  options.onSuccess(JSON.stringify(event));
  assert.equal(delivered, 1);
  subscription.close(); subscription.close();
  options.onSuccess(JSON.stringify({ ...event, sequence: 2 }));
  assert.equal(delivered, 1);
  assert.equal(cancelled, 1);
});
test('invalid synchronous handshake cancels query after query returns', async () => {
  let cancelled = 0;
  const subscription = new NativeEventSubscription({
    query: options => { options.onSuccess('{}'); return 7; },
    cancel: () => cancelled++,
  }, () => {}, () => {});
  await assert.rejects(subscription.ready, /INVALID_EVENT/);
  assert.equal(cancelled, 1);
});
test('subscription timeout and early close release pending query', async () => {
  let cancelled = 0;
  const bridge = { query: () => 1, cancel: () => cancelled++ };
  const timeout = new NativeEventSubscription(bridge, () => {}, () => {}, 5);
  await assert.rejects(timeout.ready, /RPC_TIMEOUT/);
  const early = new NativeEventSubscription(bridge, () => {}, () => {});
  early.close();
  await assert.rejects(early.ready, /RPC_CANCELLED/);
  assert.equal(cancelled, 2);
});

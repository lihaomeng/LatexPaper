import { test } from 'node:test';
import assert from 'node:assert/strict';
import { RpcEventCursor } from '../src/native-api/index.ts';
const event = { version: 2, event: 'rpc.completed', sessionId: 'live', id: 'job', generation: 1, sequence: 1, outcome: 'succeeded' };
test('events reject old sessions, duplicates, late sequence and malformed envelopes', () => {
  const cursor = new RpcEventCursor('live');
  assert.equal(cursor.accept({ ...event, sessionId: 'old', sequence: 100 }), undefined);
  assert.equal(cursor.accept({ ...event, extra: true }), undefined);
  assert.deepEqual(cursor.accept(event), event);
  assert.equal(cursor.accept(event), undefined);
  assert.ok(cursor.accept({ ...event, sequence: 3 }));
  assert.equal(cursor.accept({ ...event, sequence: 2 }), undefined);
});
test('event output is a snapshot and cancellation is schema validated', () => {
  const cursor = new RpcEventCursor('live');
  const input = { ...event };
  const accepted = cursor.accept(input);
  input.id = 'mutated';
  assert.equal(accepted?.id, 'job');
  assert.deepEqual(cursor.cancellation('job', 2), { version: 2, method: 'rpc.cancel', sessionId: 'live', id: 'job', generation: 2 });
  assert.throws(() => cursor.cancellation('job', 0), /INVALID_ARGUMENT/);
  assert.throws(() => new RpcEventCursor('../invalid'), /INVALID_SESSION/);
});

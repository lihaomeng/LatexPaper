import assert from 'node:assert/strict';
import { test } from 'node:test';
import { reconcileWorkspaceRefresh } from '../src/app/refreshLocalWorkspace.ts';

function fixture(dirty = false) {
  const files = new Map([['main.tex', { content: 'local', version: 4, dirty }],
    ['removed.tex', { content: 'old', version: 2, dirty: false }]]);
  const session = {
    getView: () => ({ files: [...files].map(([path, value]) => ({ path, dirty: value.dirty })) }),
    recoverySnapshot: (path: string) => files.get(path) ?? null,
    replaceFromDisk: (path: string, content: string, expected: number) => {
      const value = files.get(path);
      if (!value || value.version !== expected) return false;
      files.set(path, { content, version: value.version + 1, dirty: false }); return true;
    },
    forget: (path: string) => { files.delete(path); },
  };
  const state = { workspaceId: 'workspace-1', displayName: '项目', revision: 'tree-2',
    entries: [{ fileId: 'main.tex', directory: false, sizeBytes: 8 }] };
  return { files, session, state, revisions: new Map([['main.tex', 'r1'], ['removed.tex', 'old']]) };
}

test('refresh reloads externally changed clean files and removes clean missing files', async () => {
  const f = fixture();
  const summary = await reconcileWorkspaceRefresh(f.session, f.revisions, f.state,
    { openDocument: async fileId => ({ fileId, content: 'disk', revision: 'r2' }) }, () => true);
  assert.deepEqual(summary, { reloaded: 1, removed: 1, deferred: 0 });
  assert.equal(f.files.get('main.tex')?.content, 'disk');
  assert.equal(f.files.has('removed.tex'), false);
  assert.equal(f.revisions.get('main.tex'), 'r2');
});
test('refresh never overwrites dirty or concurrently edited files', async () => {
  const dirty = fixture(true);
  const first = await reconcileWorkspaceRefresh(dirty.session, dirty.revisions, dirty.state,
    { openDocument: async () => assert.fail('dirty file must not be read') }, () => true);
  assert.equal(first.deferred, 1);
  assert.equal(dirty.files.get('main.tex')?.content, 'local');
  const concurrent = fixture();
  const second = await reconcileWorkspaceRefresh(concurrent.session, concurrent.revisions, concurrent.state,
    { openDocument: async fileId => {
      concurrent.files.set(fileId, { content: 'typing', version: 5, dirty: true });
      return { fileId, content: 'disk', revision: 'r2' };
    } }, () => true);
  assert.equal(second.deferred, 1);
  assert.equal(concurrent.files.get('main.tex')?.content, 'typing');
  assert.equal(concurrent.revisions.get('main.tex'), 'r1');
  concurrent.files.get('main.tex')!.dirty = false;
  const retry = await reconcileWorkspaceRefresh(concurrent.session, concurrent.revisions, concurrent.state,
    { openDocument: async fileId => ({ fileId, content: 'disk', revision: 'r2' }) }, () => true);
  assert.equal(retry.reloaded, 1);
  assert.equal(concurrent.files.get('main.tex')?.content, 'disk');
});
test('stale session, mismatched response and read errors cannot change models', async () => {
  const stale = fixture();
  await assert.rejects(reconcileWorkspaceRefresh(stale.session, stale.revisions, stale.state,
    { openDocument: async fileId => ({ fileId, content: 'disk', revision: 'r2' }) }, () => false), /STALE_WORKSPACE/);
  const mismatch = fixture();
  await assert.rejects(reconcileWorkspaceRefresh(mismatch.session, mismatch.revisions, mismatch.state,
    { openDocument: async () => ({ fileId: 'other.tex', content: 'disk', revision: 'r2' }) }, () => true), /INVALID_RESPONSE/);
  const failed = fixture();
  await assert.rejects(reconcileWorkspaceRefresh(failed.session, failed.revisions, failed.state,
    { openDocument: async () => { throw new Error('FILE_OPERATION_FAILED'); } }, () => true), /FILE_OPERATION_FAILED/);
  assert.equal(mismatch.files.get('main.tex')?.content, 'local');
  assert.equal(failed.files.get('main.tex')?.content, 'local');
});
test('file disappearing between scan and read is closed only while still clean', async () => {
  const clean = fixture();
  const summary = await reconcileWorkspaceRefresh(clean.session, clean.revisions, clean.state,
    { openDocument: async () => { throw new Error('FILE_NOT_FOUND'); } }, () => true);
  assert.equal(summary.removed, 2);
  const edited = fixture();
  const deferred = await reconcileWorkspaceRefresh(edited.session, edited.revisions, edited.state,
    { openDocument: async fileId => {
      edited.files.set(fileId, { content: 'typing', version: 5, dirty: true });
      throw new Error('FILE_NOT_FOUND');
    } }, () => true);
  assert.equal(deferred.deferred, 1);
  assert.equal(edited.files.get('main.tex')?.content, 'typing');
});

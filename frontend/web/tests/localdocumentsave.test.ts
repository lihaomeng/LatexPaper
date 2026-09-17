import assert from 'node:assert/strict';
import { test } from 'node:test';
import { saveLocalDocuments } from '../src/app/saveLocalDocuments.ts';

function fixture() {
  const files = [{ path: 'main.tex', content: '正文' }, { path: '章节.tex', content: '章节' }];
  const versions = new Map(files.map(file => [file.path, 2]));
  const saved = new Map(files.map(file => [file.path, 1]));
  const revisions = new Map(files.map(file => [file.path, 'r1']));
  const session = {
    getView: () => ({ files: files.map(file => ({ path: file.path, dirty: versions.get(file.path) !== saved.get(file.path) })) }),
    capture: () => ({ snapshot: { files: files.map(file => ({ ...file })) }, versions: new Map(versions) }),
    acknowledge: (ack: Map<string, number>) => { for (const [path, version] of ack) saved.set(path, version); },
  };
  return { session, files, versions, saved, revisions };
}

test('saves every dirty document even without an active tab; skips clean files', async () => {
  const f = fixture();
  const calls: string[] = [];
  const writer = { saveDocument: async (path: string) => { calls.push(path); return { revision: 'r2' }; } };
  await saveLocalDocuments(f.session, f.revisions, writer, () => true);
  assert.deepEqual(calls, ['main.tex', '章节.tex']);
  assert.equal(f.session.getView().files.some(file => file.dirty), false);
  await saveLocalDocuments(f.session, f.revisions, writer, () => true);
  assert.equal(calls.length, 2);
});

test('an edit during a pending save remains dirty and uses the new revision next time', async () => {
  const f = fixture();
  const calls: { content: string; revision: string }[] = [];
  await saveLocalDocuments(f.session, f.revisions, { saveDocument: async (path, content, revision) => {
    if (path === 'main.tex') {
      calls.push({ content, revision });
      f.files[0].content = '保存期间的修改'; f.versions.set(path, 3);
      await Promise.resolve();
    }
    return { revision: 'r2' };
  } }, () => true);
  assert.equal(f.session.getView().files[0].dirty, true);
  await saveLocalDocuments(f.session, f.revisions, { saveDocument: async (_path, content, revision) => {
    calls.push({ content, revision }); return { revision: 'r3' };
  } }, () => true);
  assert.deepEqual(calls, [{ content: '正文', revision: 'r1' }, { content: '保存期间的修改', revision: 'r2' }]);
  assert.equal(f.session.getView().files[0].dirty, false);
});

test('conflict keeps the failed document dirty without issuing later writes', async () => {
  const f = fixture(); let calls = 0;
  await assert.rejects(saveLocalDocuments(f.session, f.revisions, { saveDocument: async () => {
    calls++; throw new Error('FILE_CONFLICT');
  } }, () => true), /FILE_CONFLICT/);
  assert.equal(calls, 1);
  assert.equal(f.revisions.get('main.tex'), 'r1');
  assert.equal(f.session.getView().files.every(file => file.dirty), true);
});

test('late completion from a retired session cannot acknowledge or continue writing', async () => {
  const f = fixture(); let current = true; let calls = 0;
  await saveLocalDocuments(f.session, f.revisions, { saveDocument: async () => {
    calls++; current = false; return { revision: 'r2' };
  } }, () => current);
  assert.equal(calls, 1);
  assert.equal(f.revisions.get('main.tex'), 'r1');
  assert.equal(f.session.getView().files.every(file => file.dirty), true);
});

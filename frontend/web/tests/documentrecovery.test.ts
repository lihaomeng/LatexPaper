import assert from 'node:assert/strict';
import { test } from 'node:test';
import { adoptDiskVersion, compareDocument } from '../src/app/documentRecovery.ts';
import { LocalDocumentSaveError, saveLocalDocuments } from '../src/app/saveLocalDocuments.ts';

function fixture() {
  let content = '本地未保存内容', version = 4;
  const session = {
    recoverySnapshot: () => ({ content, baseContent: '上次保存内容', version }),
    replaceFromDisk: (_path: string, next: string, expected: number) => {
      if (version !== expected) return false;
      content = next; version++; return true;
    },
  };
  const reader = { openDocument: async (fileId: string) => ({
    fileId, content: '磁盘内容', revision: 'r2', utf8Bom: true,
  }) };
  return { session, reader, revisions: new Map([['正文.tex', 'r1']]),
    edit: () => { content = '稍后输入'; version++; } };
}

test('comparison is read-only until explicit adoption, which updates the revision', async () => {
  const f = fixture();
  const comparison = await compareDocument(f.session, '正文.tex', f.reader, () => true);
  assert.equal(comparison.diskUtf8Bom, true);
  assert.equal(comparison.baseContent, '上次保存内容');
  assert.equal(f.session.recoverySnapshot().content, '本地未保存内容');
  assert.equal(f.revisions.get('正文.tex'), 'r1');
  adoptDiskVersion(f.session, comparison, f.revisions, () => true);
  assert.equal(f.session.recoverySnapshot().content, '磁盘内容');
  assert.equal(f.revisions.get('正文.tex'), 'r2');
});
test('edits during disk read reject the comparison without changing editor content', async () => {
  const f = fixture();
  await assert.rejects(compareDocument(f.session, '正文.tex', { openDocument: async fileId => {
    f.edit(); return f.reader.openDocument(fileId);
  } }, () => true), /STALE_DOCUMENT/);
  assert.equal(f.session.recoverySnapshot().content, '稍后输入');
});
test('edits after comparison reject adoption and preserve the old revision', async () => {
  const f = fixture();
  const comparison = await compareDocument(f.session, '正文.tex', f.reader, () => true);
  f.edit();
  assert.throws(() => adoptDiskVersion(f.session, comparison, f.revisions, () => true), /STALE_DOCUMENT/);
  assert.equal(f.session.recoverySnapshot().content, '稍后输入');
  assert.equal(f.revisions.get('正文.tex'), 'r1');
});
test('retired session cannot compare or adopt', async () => {
  const f = fixture();
  const comparison = await compareDocument(f.session, '正文.tex', f.reader, () => true);
  await assert.rejects(compareDocument(f.session, '正文.tex', f.reader, () => false), /STALE_DOCUMENT/);
  assert.throws(() => adoptDiskVersion(f.session, comparison, f.revisions, () => false), /STALE_DOCUMENT/);
  assert.equal(f.session.recoverySnapshot().content, '本地未保存内容');
});
test('closing while reading ignores late completion', async () => {
  const f = fixture(); let current = true;
  await assert.rejects(compareDocument(f.session, '正文.tex', { openDocument: async fileId => {
    current = false; return f.reader.openDocument(fileId);
  } }, () => current), /STALE_DOCUMENT/);
});
test('missing file and mismatched response do not modify local data', async () => {
  const f = fixture();
  await assert.rejects(compareDocument(f.session, '正文.tex', { openDocument: async () => {
    throw new Error('FILE_NOT_FOUND');
  } }, () => true), /FILE_NOT_FOUND/);
  await assert.rejects(compareDocument(f.session, '正文.tex', { openDocument: () => f.reader.openDocument('other.tex') }, () => true), /INVALID_RESPONSE/);
  assert.equal(f.session.recoverySnapshot().content, '本地未保存内容');
});
test('save failure identifies the conflicting file without advancing its revision', async () => {
  const f = fixture();
  const session = {
    getView: () => ({ files: [{ path: '正文.tex', dirty: true }] }),
    capture: () => ({ snapshot: { files: [{ path: '正文.tex', content: '本地内容' }] }, versions: new Map([['正文.tex', 4]]) }),
    acknowledge: () => assert.fail('must not acknowledge failed save'),
  };
  await assert.rejects(saveLocalDocuments(session, f.revisions, { saveDocument: async () => {
    throw new Error('FILE_CONFLICT');
  } }, () => true), failure => failure instanceof LocalDocumentSaveError && failure.fileId === '正文.tex' && failure.message === 'FILE_CONFLICT');
  assert.equal(f.revisions.get('正文.tex'), 'r1');
});

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { saveConflictCopy } from '../src/app/saveConflictCopy.ts';
import type { DocumentComparison } from '../src/app/documentRecovery.ts';

const comparison: DocumentComparison = {
  fileId: 'main.tex', localContent: '编辑', localVersion: 3,
  diskContent: '磁盘', diskRevision: 'disk-2', diskUtf8Bom: true,
};

test('conflict copy performs one atomic request and may retire unchanged source', async () => {
  const calls: unknown[] = [];
  const session = { recoverySnapshot: () => ({ content: '编辑', version: 3 }) };
  const result = await saveConflictCopy(session, comparison, 'main-copy.tex', {
    saveDocumentAs: async (path, content, utf8Bom) => {
      calls.push({ path, content, utf8Bom });
      return { fileId: path, revision: 'copy-1' };
    },
  }, () => true);
  assert.equal(result.canRetireSource, true);
  assert.deepEqual(calls, [{ path: 'main-copy.tex', content: '编辑', utf8Bom: true }]);
});

test('committed copy never retires source edited while request is pending', async () => {
  let version = 3;
  const session = { recoverySnapshot: () => ({ content: version === 3 ? '编辑' : '继续编辑', version }) };
  const result = await saveConflictCopy(session, comparison, 'copy.tex', {
    saveDocumentAs: async path => {
      version = 4;
      return { fileId: path, revision: 'copy-2' };
    },
  }, () => true);
  assert.equal(result.canRetireSource, false);
  assert.equal(result.content, '编辑');
});

test('stale source sends no copy request and mismatched response is rejected', async () => {
  let calls = 0;
  await assert.rejects(saveConflictCopy(
    { recoverySnapshot: () => ({ content: 'new', version: 4 }) }, comparison, 'copy.tex',
    { saveDocumentAs: async path => { calls++; return { fileId: path, revision: 'unused' }; } },
    () => true), /STALE_DOCUMENT/);
  assert.equal(calls, 0);
  await assert.rejects(saveConflictCopy(
    { recoverySnapshot: () => ({ content: '编辑', version: 3 }) }, comparison, 'copy.tex',
    { saveDocumentAs: async () => ({ fileId: 'other.tex', revision: 'copy-3' }) },
    () => true), /UNCONFIRMED_COMMIT/);
});

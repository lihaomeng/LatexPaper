import assert from 'node:assert/strict';
import { test } from 'node:test';
import { containsMergeMarkers, mergeDocumentText } from '../src/app/threeWayMerge.ts';

test('three-way merge accepts one-sided changes', () => {
  assert.deepEqual(mergeDocumentText('a\nb', 'A\nb', 'a\nb'), { content: 'A\nb', conflicts: 0 });
  assert.deepEqual(mergeDocumentText('a\nb', 'a\nb', 'a\nB'), { content: 'a\nB', conflicts: 0 });
});

test('three-way merge combines independent same-shape edits', () => {
  const result = mergeDocumentText('a\nb\nc', 'A\nb\nc', 'a\nb\nC');
  assert.equal(result.content, 'A\nb\nC');
  assert.equal(result.conflicts, 0);
});

test('three-way merge exposes conflicts and detects unresolved markers', () => {
  const result = mergeDocumentText('base', 'local', 'disk');
  assert.equal(result.conflicts, 1);
  assert.equal(containsMergeMarkers(result.content), true);
  assert.equal(containsMergeMarkers(result.content.replace('<<<<<<< 本地编辑', 'resolved')), true);
  assert.equal(containsMergeMarkers('resolved content'), false);
});

test('shape-changing edits remain explicit conflicts instead of guessing', () => {
  const result = mergeDocumentText('a\nb', 'a\nnew\nb', 'a\nB');
  assert.equal(result.conflicts, 1);
  assert.match(result.content, /<<<<<<< 本地编辑/);
});

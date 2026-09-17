import assert from 'node:assert/strict';
import { test } from 'node:test';
import { validWorkspaceDirectoryId } from '../src/app/workspacePaths.ts';

test('workspace directory path accepts bounded unicode relative paths', () => {
  assert.equal(validWorkspaceDirectoryId('章节/实验结果'), true);
  assert.equal(validWorkspaceDirectoryId('figures.v2'), true);
});

for (const path of ['', '../outside', 'a\\b', '/root', 'a//b', 'a/..', 'a. ',
  'CON', 'aux.files', 'COM¹/data', '.lightoverleaf-trash/recover', 'wild*card']) {
  test('workspace directory path rejects ' + JSON.stringify(path), () => {
    assert.equal(validWorkspaceDirectoryId(path), false);
  });
}

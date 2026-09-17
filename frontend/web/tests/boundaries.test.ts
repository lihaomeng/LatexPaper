import { test } from "node:test";
import assert from "node:assert/strict";
import { ESLint } from "eslint";
test("ESLint rejects cross-feature internals and direct transport imports", async () => {
  const eslint = new ESLint();
  for (const dependency of ["../editor/internal", "../../features/editor/internal", "../../transport/cef"]) {
    const results = await eslint.lintText('import "' + dependency + '";', { filePath: "src/features/explorer/probe.ts" });
    assert.ok(results[0].messages.some(message => ["no-restricted-imports", "boundaries/no-feature-internals"].includes(message.ruleId ?? "")));
  }
});


test("ESLint allows NativeApi and public feature entry points", async () => {
  const eslint = new ESLint();
  for (const dependency of ["../../native-api", "../editor", "../editor/index"]) {
    const results = await eslint.lintText('import "' + dependency + '";', { filePath: "src/features/explorer/probe.ts" });
    assert.equal(results[0].errorCount, 0);
  }
});

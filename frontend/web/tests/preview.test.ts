import { test } from "node:test";
import assert from "node:assert/strict";
import { previewPresentation } from "../src/features/preview/presentation.ts";

test("draft compilation prepares an isolated snapshot without folder selection", () => {
  const view = previewPresentation("preparing");
  assert.deepEqual(view, {
    summary: "正在准备隔离编译快照",
    action: "准备编译",
    preparing: true,
    running: false,
  });
});

test("cancelled draft compilation remains a compile state", () => {
  const view = previewPresentation("cancelled");
  assert.equal(view.summary, "编译已取消");
  assert.equal(view.action, "编译");
  assert.equal(view.preparing, false);
  assert.equal(view.running, false);
});

test("native build states expose cancellation without an export path", () => {
  for (const state of ["detecting", "running"] as const) {
    const view = previewPresentation(state);
    assert.equal(view.action, "取消编译");
    assert.equal(view.running, true);
    assert.equal(view.preparing, false);
  }
});

test("PDF candidate has a non-cancellable rendering state before atomic commit", () => {
  const view = previewPresentation("rendering");
  assert.equal(view.summary, "正在验证并渲染 PDF");
  assert.equal(view.action, "编译");
  assert.equal(view.running, false);
});

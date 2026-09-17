import { test } from "node:test";
import assert from "node:assert/strict";
import { draftProjectDirectories } from "../src/app/draftProject.ts";

test("draft project directories are unique and parent-first", () => {
  assert.deepEqual(draftProjectDirectories([
    "main.tex", "sections/method.tex", "sections/deep/result.tex", "figures/readme.md",
  ]), ["figures", "sections", "sections/deep"]);
});

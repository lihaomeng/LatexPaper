import { test } from "node:test";
import assert from "node:assert/strict";
import { validateSnapshot, validDraftPath, CheckpointWriter, type DraftRepository, type DraftSnapshot } from "../src/features/session/checkpoint.ts";
import { extractOutline } from "../src/features/editor/outline.ts";
import { buildTree } from "../src/features/explorer/tree.ts";
import { starter } from "../src/app/starter.ts";
const make = (content = ""): DraftSnapshot => ({ version: 1, files: [{ path: "main.tex", content }], open: ["main.tex"], active: "main.tex" });
test("starter is a valid recoverable draft with real single-backslash LaTeX", () => {
  assert.equal(validateSnapshot(starter), true);
  assert.match(starter.files[0].content, /\\documentclass\[/);
  assert.equal(starter.files[0].content.includes("\\\\documentclass"), false);
});
test("Chinese relative draft filenames accepted", () => {
  assert.equal(validDraftPath("章节/引言.tex"), true);
  assert.equal(validateSnapshot({ version: 1, files: [{ path: "章节/引言.tex", content: "你好" }], open: [], active: null }), true);
});
for (const path of ["../a.tex", "/a.tex", "C:/a.tex", "a\\b.tex", "a//b.tex", "a.exe", "a\n.tex", "a /b.tex"]) {
  test("reject unsafe/unsupported virtual path " + JSON.stringify(path), () => assert.equal(validDraftPath(path), false));
}
for (const value of [
  { ...make(), version: 2 }, { ...make(), extra: 1 }, { ...make(), active: "missing.tex" },
  { ...make(), active: null }, { ...make(), open: ["main.tex", "main.tex"] },
  { ...make(), files: [{ path: "main.tex", content: "" }, { path: "MAIN.tex", content: "" }] },
  { ...make(), files: [{ path: "main.tex", content: "" }, { path: "main.tex/sub.tex", content: "" }] },
  make("x".repeat(1_000_001)),
]) {
  test("invalid cache rejected: " + JSON.stringify(value).slice(0, 100), () => assert.equal(validateSnapshot(value), false));
}
test("checkpoint writes are serialized and capture immutable snapshots", async () => {
  let release!: () => void;
  const gate = new Promise<void>(resolve => { release = resolve; });
  const started: string[] = [];
  const completed: string[] = [];
  const repository: DraftRepository = {
    load: async () => null,
    save: async snapshot => { const content = snapshot.files[0].content; started.push(content); if (content === "one") await gate; completed.push(content); },
  };
  const writer = new CheckpointWriter(repository);
  const first = writer.save(make("one"));
  await Promise.resolve();
  const mutable = make("two");
  const second = writer.save(mutable);
  mutable.files[0].content = "not captured";
  assert.deepEqual(started, ["one"]);
  release();
  await Promise.all([first, second]);
  assert.deepEqual(completed, ["one", "two"]);
});
test("a failed checkpoint does not poison the next manual retry", async () => {
  let writes = 0;
  const writer = new CheckpointWriter({ load: async () => null, save: async () => { if (++writes === 1) throw new Error("quota"); } });
  await assert.rejects(writer.save(make()), /quota/);
  await writer.save(make());
  assert.equal(writes, 2);
});
test("invalid checkpoint never reaches the repository", async () => {
  let calls = 0;
  const writer = new CheckpointWriter({ load: async () => null, save: async () => { calls++; } });
  await assert.rejects(writer.save({ ...make(), active: "missing.tex" }), /INVALID_DRAFT/);
  assert.equal(calls, 0);
});
test("outline parses nested titles, starred and short headings, ignoring comments", () => {
  const text = "% \\section{comment}\n\\section{引言}\n\\subsection*[short]{方法 {A}}\n\\subsubsection{细节}\n";
  assert.deepEqual(extractOutline(text), [
    { title: "引言", level: 1, line: 2 }, { title: "方法 {A}", level: 2, line: 3 }, { title: "细节", level: 3, line: 4 },
  ]);
});
test("outline ignores verbatim and malformed headings", () => {
  const text = "\\begin{verbatim}\n\\section{not heading}\n\\end{verbatim}\n\\section{missing\n\\section{real}";
  assert.deepEqual(extractOutline(text), [{ title: "real", level: 1, line: 5 }]);
});
test("outline is bounded and escaped percent does not start comment", () => {
  assert.equal(extractOutline("\\% \\section{yes}")[0].title, "yes");
  assert.equal(extractOutline("\\section{A}\n".repeat(1000)).length, 500);
  assert.equal(extractOutline("\\".repeat(10000)).length, 0);
});
test("file tree has deterministic directory-first grouping", () => {
  const tree = buildTree(["main.tex", "sections/method.tex", "sections/intro.tex", "references.bib"],
    ["empty", "sections"]);
  assert.equal(tree[0].path, "empty");
  assert.equal(tree[0].directory, true);
  assert.equal(tree[0].children.length, 0);
  assert.equal(tree[1].path, "sections");
  assert.deepEqual(tree[1].children.map(node => node.path), ["sections/intro.tex", "sections/method.tex"]);
  assert.equal(tree[2].directory, false);
});

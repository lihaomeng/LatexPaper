import * as monaco from "monaco-editor/editor/editor.api.js";
import "monaco-editor/editor/contrib/find/browser/findController.js";
import "monaco-editor/editor/contrib/wordOperations/browser/wordOperations.js";
import "monaco-editor/editor/contrib/clipboard/browser/clipboard.js";
import "monaco-editor/editor/contrib/contextmenu/browser/contextmenu.js";
import "monaco-editor/editor/contrib/hover/browser/hoverContribution.js";
import "monaco-editor/editor/contrib/bracketMatching/browser/bracketMatching.js";
import "monaco-editor/editor/contrib/linesOperations/browser/linesOperations.js";
import "monaco-editor/editor/contrib/suggest/browser/suggestController.js";
import EditorWorker from "monaco-editor/editor/editor.worker.js?worker";

self.MonacoEnvironment = { getWorker: () => new EditorWorker() };
monaco.languages.register({ id: "latex", extensions: [".tex", ".bib"] });
monaco.languages.setMonarchTokensProvider("latex", {
  tokenizer: {
    root: [
      [/%.*$/, "comment"],
      [/\\(?:begin|end|documentclass|usepackage|section|subsection|subsubsection|title|author|date|input|include|label|ref|cite)\b/, "keyword"],
      [/\\[a-zA-Z@]+\*?/, "tag"],
      [/\\[^a-zA-Z]/, "tag"],
      [/[{}[\]]/, "delimiter"],
      [/\$[^$]*\$/, "string"],
      [/\d+(?:\.\d+)?/, "number"],
    ],
  },
});
monaco.languages.setLanguageConfiguration("latex", {
  comments: { lineComment: "%" },
  brackets: [["{", "}"], ["[", "]"], ["(", ")"]],
  autoClosingPairs: [{ open: "{", close: "}" }, { open: "[", close: "]" }, { open: "(", close: ")" }],
});
monaco.editor.defineTheme("lightoverleaf-dark", {
  base: "vs-dark", inherit: true,
  rules: [
    { token: "comment", foreground: "718096" },
    { token: "keyword", foreground: "87CEE0" },
    { token: "tag", foreground: "D9B77A" },
    { token: "delimiter", foreground: "B9C7CE" },
    { token: "string", foreground: "B9CA90" },
    { token: "number", foreground: "C7ABDA" },
  ],
  colors: {
    "editor.background": "#1e242d", "editor.foreground": "#d0d7e0",
    "editorLineNumber.foreground": "#637080", "editorLineNumber.activeForeground": "#b9c7d7",
    "editor.lineHighlightBackground": "#252e39", "editor.selectionBackground": "#345545",
    "editorCursor.foreground": "#bee8cc", "editorWidget.background": "#252e39",
    "editorIndentGuide.background1": "#303947", "editorGutter.background": "#1e242d",
  },
});
export { monaco };

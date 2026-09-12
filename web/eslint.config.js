import js from "@eslint/js";
import ts from "typescript-eslint";
import path from "node:path";
const featureBoundary = {
  meta: { type: "problem", schema: [], messages: { internal: "Import another feature only through its public index." } },
  create(context) {
    const filename = context.filename.replaceAll("\\", "/");
    const owner = filename.match(/\/features\/([^/]+)\//)?.[1];
    const check = node => {
      const value = node.source?.value;
      if (typeof value !== "string" || !owner || !value.startsWith(".")) return;
      const resolved = path.posix.normalize(path.posix.join(path.posix.dirname(filename), value));
      const target = resolved.match(/\/features\/([^/]+)(.*)$/);
      if (target && target[1] !== owner && !["", "/index", "/index.ts", "/index.tsx"].includes(target[2])) {
        context.report({ node, messageId: "internal" });
      }
    };
    return { ImportDeclaration: check, ExportNamedDeclaration: check, ExportAllDeclaration: check, ImportExpression: check };
  }
};
export default [
  { ignores: ["dist/**", ".generated/**", "node_modules/**"] },
  js.configs.recommended,
  ...ts.configs.recommended,
  {
    files: ["src/features/**/*.{ts,tsx}"],
    plugins: { boundaries: { rules: { "no-feature-internals": featureBoundary } } },
    rules: {
      "boundaries/no-feature-internals": "error",
      "no-restricted-imports": ["error", { patterns: [
        { group: ["**/transport/**", "**/cef/**", "**/.generated/**"],
          message: "Features use NativeApi; compose across features in app/." }
      ] }]
    }
  },
  {
    files: ["src/native-api/**/*.{ts,tsx}"],
    rules: { "no-restricted-imports": ["error", { patterns: ["**/features/**", "**/app/**"] }] }
  }
];

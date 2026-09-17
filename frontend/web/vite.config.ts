import { defineConfig } from "vite";
import { randomBytes } from "node:crypto";
import { readFile } from "node:fs/promises";
const token = "__LIGHTOVERLEAF_STYLE_NONCE__";
const nonceTargets = ["/vs/base/browser/domStylesheets.js", "/vs/base/browser/ui/contextview/contextview.js"];
export default defineConfig({
  base: "/",
  html: { cspNonce: token },
  optimizeDeps: { exclude: ["monaco-editor"] },
  plugins: [{
    name: "monaco-authorized-style-nonce",
    enforce: "pre",
    transform(code, id) {
      if (!nonceTargets.some(target => id.replaceAll("\\", "/").endsWith(target))) return;
      const marker = "const style = document.createElement('style');";
      if (!code.includes(marker)) throw new Error("Pinned Monaco style factory changed; review CSP integration.");
      return { code: code.replace(marker, marker + "\n    style.nonce = document.querySelector('meta[name=lightoverleaf-style-nonce]')?.content ?? '';"), map: null };
    },
    transformIndexHtml: {
      order: "post",
      handler(html, context) { return context.server ? html.replaceAll(token, randomBytes(32).toString("hex")) : html; },
    },
    configurePreviewServer(server) {
      server.middlewares.use(async (request, response, next) => {
        if (request.url !== "/" && request.url !== "/index.html") { next(); return; }
        try {
          const html = await readFile(new URL("./dist/index.html", import.meta.url), "utf8");
          response.setHeader("Content-Type", "text/html; charset=utf-8");
          response.setHeader("Cache-Control", "no-store");
          response.end(html.replaceAll(token, randomBytes(32).toString("hex")));
        } catch (error) { next(error); }
      });
    },
  }],
  build: { outDir: "dist", emptyOutDir: true },
});


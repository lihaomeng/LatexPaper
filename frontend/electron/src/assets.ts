import { protocol } from 'electron';
import { readFile, realpath, stat } from 'node:fs/promises';
import path from 'node:path';
import { randomBytes } from 'node:crypto';

export const applicationUrl = 'lightoverleaf://app/index.html';
protocol.registerSchemesAsPrivileged([{
  scheme: 'lightoverleaf', privileges: { standard: true, secure: true, supportFetchAPI: true,
    corsEnabled: true, stream: true },
}]);

export async function registerAssets(root: string): Promise<void> {
  const resolvedRoot = await realpath(root);
  const mime: Record<string, string> = { '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
    '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png',
    '.woff': 'font/woff', '.woff2': 'font/woff2', '.ttf': 'font/ttf', '.wasm': 'application/wasm' };
  protocol.handle('lightoverleaf', async request => {
    try {
      const url = new URL(request.url);
      if (url.hostname !== 'app' || url.port || url.username || url.password ||
          request.method !== 'GET') return new Response(null, { status: 403 });
      const name = decodeURIComponent(url.pathname);
      if (name.includes('\\') || name.includes('\0')) return new Response(null, { status: 403 });
      const file = await realpath(path.resolve(resolvedRoot, '.' + (name === '/' ? '/index.html' : name)));
      const relative = path.relative(resolvedRoot, file);
      if (relative.startsWith('..') || path.isAbsolute(relative)) return new Response(null, { status: 403 });
      const info = await stat(file);
      if (!info.isFile() || info.size > 64 * 1024 * 1024) return new Response(null, { status: 404 });
      let data = await readFile(file);
      if (path.extname(file) === '.html') {
        data = Buffer.from(data.toString('utf8')
          .replaceAll('__LIGHTOVERLEAF_STYLE_NONCE__', randomBytes(32).toString('hex'))
          .replace(' ws://127.0.0.1:*', ''));
      }
      return new Response(new Uint8Array(data), { headers: {
        'Content-Type': mime[path.extname(file)] ?? 'application/octet-stream',
        'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff',
      } });
    } catch { return new Response(null, { status: 404 }); }
  });
}

import { build } from 'esbuild';
await build({
  entryPoints: ['src/main.ts', 'src/preload.ts'],
  outdir: '../../out/electron-dev/shell',
  bundle: true, platform: 'node', format: 'cjs', target: 'node22',
  external: ['electron'], sourcemap: true,
});

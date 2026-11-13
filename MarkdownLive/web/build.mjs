// Bundles the preview page into web/dist (copied next to the plugin DLL as "assets").
import { build } from 'esbuild';
import { cp, mkdir, readFile, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const outdir = fileURLToPath(new URL('./dist/', import.meta.url));
const katexPkg = JSON.parse(
  await readFile(new URL('./node_modules/katex/package.json', import.meta.url), 'utf8'),
);

await rm(outdir, { recursive: true, force: true });
await mkdir(outdir, { recursive: true });

await build({
  entryPoints: [fileURLToPath(new URL('./src/preview.js', import.meta.url))],
  outdir,
  bundle: true,
  format: 'esm',
  splitting: true,
  minify: true,
  target: ['chrome110'],
  legalComments: 'eof',
  chunkNames: 'chunks/[name]-[hash]',
  assetNames: 'fonts/[name]',
  loader: { '.woff2': 'file' },
  // Chromium always picks the woff2 fonts; the legacy formats are not shipped
  external: ['*.woff', '*.ttf'],
  define: { __KATEX_VERSION__: JSON.stringify(katexPkg.version) },
  logLevel: 'info',
});

await cp(fileURLToPath(new URL('./src/preview.html', import.meta.url)), `${outdir}preview.html`);

// Builds every page as ONE self-contained HTML file: Chromium refuses module
// scripts loaded from file:// URLs, and an inlined bundle sidesteps that with
// no custom scheme. Each page lands at dist/<page>/index.html, which is where
// WebHost::pageUrl() looks for it beside the executable.
import { build } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { renameSync, rmSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = dirname(fileURLToPath(import.meta.url));
const pages = ['overlay', 'hub', 'editor'];

// dist holds exactly these pages - nothing a retired page left behind, since
// the whole folder is what gets deployed.
rmSync(join(root, 'dist'), { recursive: true, force: true });

for (const page of pages) {
  const outDir = join(root, 'dist', page);
  await build({
    configFile: false,
    root,
    base: './',
    logLevel: 'warn',
    plugins: [svelte(), viteSingleFile()],
    build: { outDir, emptyOutDir: true, rollupOptions: { input: join(root, `${page}.html`) } },
  });
  renameSync(join(outDir, `${page}.html`), join(outDir, 'index.html'));
  console.log(`built ${page}`);
}

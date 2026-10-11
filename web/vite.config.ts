import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';

// One self-contained index.html: Chromium refuses module scripts loaded from
// file:// URLs, and an inlined bundle sidesteps that with no custom scheme.
export default defineConfig({
  plugins: [svelte(), viteSingleFile()],
  base: './',
  build: { outDir: 'dist', emptyOutDir: true },
  server: { port: 5173, strictPort: true },
});

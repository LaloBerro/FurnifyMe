import { defineConfig } from 'vitest/config';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// The dev server (npm run dev) serves every page at /<page>.html, which is
// what WebHost::pageUrl() asks for when FURNIFY_WEB_URL points here. The
// production build is build.mjs (one self-contained file per page); vitest
// reads the test block below.
export default defineConfig({
  plugins: [svelte()],
  server: { port: 5173, strictPort: true },
  resolve: process.env.VITEST ? { conditions: ['browser'] } : undefined,
  test: {
    environment: 'jsdom',
    include: ['test/**/*.test.ts'],
    setupFiles: ['test/setup.ts'],
  },
});

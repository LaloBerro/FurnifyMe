// Every theme's typeface ships with the pages (T-072, DESIGN.md: a page never
// fetches a font): each family a theme names, or that its page tokens use, has
// an @font-face in tokens.css whose file is in public/fonts.
import { describe, expect, it } from 'vitest';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const web = join(__dirname, '..');
const themesDir = join(web, '..', 'assets', 'themes');
const css = readFileSync(join(web, 'src', 'tokens.css'), 'utf8');

function faces(): Map<string, string[]> {
  const out = new Map<string, string[]>();
  for (const block of css.match(/@font-face\s*{[^}]*}/g) ?? []) {
    const family = /font-family:\s*'([^']+)'/.exec(block)?.[1];
    const url = /url\('([^']+)'\)/.exec(block)?.[1];
    if (family && url) out.set(family, [...(out.get(family) ?? []), url]);
  }
  return out;
}

function themeFamilies(): string[] {
  const families = new Set<string>();
  for (const file of readdirSync(themesDir).filter((f) => f.endsWith('.json'))) {
    const theme = JSON.parse(readFileSync(join(themesDir, file), 'utf8'));
    if (theme.fontFamily) families.add(theme.fontFamily);
    for (const value of Object.values(theme.page ?? {}) as string[]) {
      const quoted = /'([^']+)'/.exec(String(value));
      if (quoted) families.add(quoted[1]);
    }
  }
  return [...families];
}

describe('the typefaces', () => {
  it('every family a theme uses has a face, and its file ships', () => {
    const declared = faces();
    for (const family of themeFamilies()) {
      expect(declared.has(family), `@font-face for ${family}`).toBe(true);
      for (const url of declared.get(family)!) {
        const path = url.startsWith('/fonts/') ? join(web, 'public', url) : join(web, 'src', url);
        expect(existsSync(path), `${family}: ${url}`).toBe(true);
      }
    }
  });

  it('no face is fetched from the network', () => {
    expect(css).not.toMatch(/url\(['"]?https?:/);
    expect(css).not.toMatch(/@import/);
  });
});

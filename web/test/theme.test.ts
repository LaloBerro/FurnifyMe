// The pages wear the theme (T-072): the id the host sends goes on <html>, and
// themes.css carries each theme's whole style as the shape, surface and type
// variables every component reads (Graphite is those variables' fallbacks,
// so it has no block of its own). Pixels are judged in chrome_smoke's and
// hub_smoke's captures; this pins the contract the components read.
import { describe, expect, it } from 'vitest';
import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { applyTokens } from '../src/theme';

const web = join(__dirname, '..');
const css = readFileSync(join(web, 'src', 'themes.css'), 'utf8');
const ids = readdirSync(join(web, '..', 'assets', 'themes'))
  .filter((f) => f.endsWith('.json'))
  .map((f) => f.replace(/\.json$/, ''));

function block(id: string): Record<string, string> {
  const m = new RegExp(`:root\\[data-theme='${id}'\\]\\s*{([^}]*)}`).exec(css);
  if (!m) return {};
  const out: Record<string, string> = {};
  for (const decl of m[1].replace(/\/\*[\s\S]*?\*\//g, '').split(';')) {
    const at = decl.indexOf(':');
    if (at > 0) out[decl.slice(0, at).trim()] = decl.slice(at + 1).trim();
  }
  return out;
}

describe('the theme on the page', () => {
  it('puts the theme the host names on <html>, and Graphite when it names none', () => {
    applyTokens({ '--theme': 'nightshade-glass', '--accent': '#a57bff' });
    expect(document.documentElement.dataset.theme).toBe('nightshade-glass');
    applyTokens({ '--accent': '#6a00ff' });
    expect(document.documentElement.dataset.theme).toBe('graphite');
  });

  it("drops a theme's --shell- numbers when the next theme sends none (Phase 3i)", () => {
    applyTokens({ '--theme': 'violet-glass-float', '--shell-chip-size': '42px', '--accent': '#6a00ff' });
    const root = document.documentElement.style;
    expect(root.getPropertyValue('--shell-chip-size')).toBe('42px');
    applyTokens({ '--theme': 'graphite', '--accent': '#6a00ff' });
    expect(root.getPropertyValue('--shell-chip-size')).toBe('');
    expect(root.getPropertyValue('--accent')).toBe('#6a00ff');
  });

  it('every theme but Graphite has a block, and Graphite has none', () => {
    for (const id of ids) {
      if (id === 'graphite') expect(Object.keys(block(id))).toHaveLength(0);
      else expect(Object.keys(block(id)).length, id).toBeGreaterThan(5);
    }
  });

  it('Ultraviolet Grid squares every corner', () => {
    const uv = block('ultraviolet-grid');
    const radii = Object.entries(uv).filter(([k]) => k.startsWith('--r-'));
    expect(radii.length).toBeGreaterThanOrEqual(8);
    for (const [k, v] of radii) expect(v, k).toMatch(/^0(px)?$/);
    expect(uv['--label-transform']).toBe('uppercase');
    expect(uv['--label-family']).toMatch(/IBM Plex Mono/);
  });

  it('Violet Glass Float floats its cards at 26px with a rim', () => {
    const b = block('violet-glass-float');
    expect(b['--r-card']).toBe('26px');
    expect(b['--card-rim']).toMatch(/linear-gradient/);
  });

  it("Aurora's named extra: a 26px Sora title (DESIGN.md theme decisions)", () => {
    const c = block('aurora-mesh-gradient');
    expect(c['--title-size']).toBe('26px');
    expect(c['--title-family']).toMatch(/Sora/);
  });
});

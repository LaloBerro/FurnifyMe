import { copy } from './copy';

// WHAT A DELETE WOULD BREAK, fitted to the strip and never clipped - the Qt
// hub's rule: every name while they fit, then as many as fit with "and N
// more", then just the count. `measure` answers a string's width in the
// strip's own font.
export function fitUsedIn(names: string[], width: number, measure: (s: string) => number): string {
  if (names.length === 0) return '';
  const listed = (shown: string[]) =>
    shown.length === 1 ? shown[0] : copy.usedInPair(shown.slice(0, -1).join(', '), shown[shown.length - 1]);
  const all = copy.usedInAll(listed(names));
  if (measure(all) <= width) return all;
  for (let keep = names.length - 1; keep >= 1; --keep) {
    const some = copy.usedInMore(names.slice(0, keep).join(', '), names.length - keep);
    if (measure(some) <= width) return some;
  }
  return names.length === 1 ? copy.usedInOne : copy.usedInCount(names.length);
}

/** A width measurer in a CSS font, or a stand-in where there is no canvas. */
export function measurerFor(font: string): (s: string) => number {
  if (typeof OffscreenCanvas !== 'undefined') {
    const ctx = new OffscreenCanvas(1, 1).getContext('2d');
    if (ctx) {
      ctx.font = font;
      return (s) => ctx.measureText(s).width;
    }
  }
  return (s) => s.length * 7;
}

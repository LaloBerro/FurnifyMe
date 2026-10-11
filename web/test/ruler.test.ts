// The Joints card's ruler layout (migration Phase 3d): a port of the Qt
// drawer's layoutStrip(), tested with a measurer that makes every character
// 6 px wide - placement, staggering, the edge word and the written fallback.
import { describe, expect, it } from 'vitest';
import { kBarInset, kLabelGap, layoutStrip, type RulerPiece } from '../src/editor/joints/ruler';

const measure = (t: string) => t.length * 6;
const mark = (at: number, text: string) => ({ at, text });
const ticks = (marks: { at: number; text: string }[]): RulerPiece => ({
  band: false,
  marks,
  bandStart: mark(0, ''),
  bandEnd: mark(0, ''),
});

describe('the ruler', () => {
  it('puts marks that are well apart on the line above the bar', () => {
    const s = layoutStrip(ticks([mark(0.1, '30'), mark(0.5, '150'), mark(0.9, '270')]), 'front', 0, 212, measure)!;
    expect(s).not.toBeNull();
    expect(s.labels.map((l) => l.line)).toEqual([0, 0, 0]);
    expect(s.staggered).toBe(false);
    expect(s.barLeft).toBe(kBarInset);
    expect(s.barRight).toBe(212 - kBarInset);
    const span = s.barRight - s.barLeft;
    expect(s.ticks).toEqual([s.barLeft + 0.1 * span, s.barLeft + 0.5 * span, s.barLeft + 0.9 * span]);
    expect(s.band).toBeNull();
  });

  it('a label that would touch the last one on the top line takes the line below', () => {
    const s = layoutStrip(ticks([mark(0.5, '150'), mark(0.52, '156')]), 'front', 0, 212, measure)!;
    expect(s.labels.map((l) => l.line)).toEqual([0, 1]);
    expect(s.staggered).toBe(true);
  });

  it('labels that collide even on two lines give no ruler at all - the row writes its numbers', () => {
    expect(layoutStrip(ticks([mark(0.5, '150'), mark(0.51, '153'), mark(0.52, '156')]), 'front', 0, 212, measure)).toBeNull();
  });

  it('a band draws its two ends as ticks and the span between them', () => {
    const piece: RulerPiece = { band: true, marks: [], bandStart: mark(0.25, '75'), bandEnd: mark(0.75, '225') };
    const s = layoutStrip(piece, 'front', 0, 212, measure)!;
    const span = s.barRight - s.barLeft;
    expect(s.ticks).toEqual([s.barLeft + 0.25 * span, s.barLeft + 0.75 * span]);
    expect(s.band).toEqual([s.ticks[0], s.ticks[1]]);
    expect(s.labels.map((l) => l.text)).toEqual(['75', '225']);
  });

  it('the edge word sits at zero above the bar when it ends before the first label there', () => {
    const s = layoutStrip(ticks([mark(0.8, '240')]), 'front', 0, 212, measure)!;
    expect(s.edgeLine).toBe(0);
  });

  it('below the bar when only that line has room, and off the ruler when neither has', () => {
    // A first label near zero on the top line, nothing on the line below.
    const below = layoutStrip(ticks([mark(0.05, '15'), mark(0.8, '240')]), 'front', 0, 212, measure)!;
    expect(below.labels[0].line).toBe(0);
    expect(below.edgeLine).toBe(1);
    // Both lines taken near zero.
    const none = layoutStrip(ticks([mark(0.02, '6'), mark(0.04, '12'), mark(0.9, '270')]), 'front', 0, 212, measure)!;
    expect(none.labels.slice(0, 2).map((l) => l.line)).toEqual([0, 1]);
    expect(none.edgeLine).toBe(-1);
  });

  it('a label never leaves the strip, and labels on a line keep the least gap', () => {
    const s = layoutStrip(ticks([mark(0, '0'), mark(1, '600')]), 'front', 0, 212, measure)!;
    for (const l of s.labels) {
      expect(l.left).toBeGreaterThanOrEqual(0);
      expect(l.left + measure(l.text)).toBeLessThanOrEqual(212);
    }
    const top = s.labels.filter((l) => l.line === 0);
    for (let i = 1; i < top.length; i++)
      expect(top[i].left).toBeGreaterThanOrEqual(top[i - 1].left + measure(top[i - 1].text) + kLabelGap);
  });

  it('labels are placed left to right whatever order the marks came in', () => {
    const s = layoutStrip(ticks([mark(0.9, '270'), mark(0.1, '30')]), 'front', 0, 212, measure)!;
    expect(s.labels.map((l) => l.text)).toEqual(['30', '270']);
  });
});

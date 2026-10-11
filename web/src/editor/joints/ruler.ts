// One piece's ruler on the Joints card (migration Phase 3d): a port of the Qt
// drawer's layoutStrip() (src/ui/JointsPanel.cpp), placement for placement.
// TO SCALE: the named edge at zero, the joint's run as the bar's length; marks
// arrive as fractions of the run (JointRows), so this file never sees a
// millimetre. Pure - a measurer is passed in - so it is tested with a fake.
//
// TICK LABELS FIRST, above their ticks - the picked design. Greedy, left to
// right: a label takes the line above the bar when it clears the last label
// there, the line below when it clears the last one THERE, and otherwise the
// ruler cannot be drawn honestly at all (null: the row writes its numbers).
// THEN the edge word, where it is genuinely clear: at zero above the bar when
// it ends before the first label there, under zero below the bar when it ends
// before the first label on that line, and otherwise not on the ruler - the
// shared line already names the edge.
import type { JointMark } from './api';

export const kBarInset = 6; // zero sits this far in from the text edge
export const kLabelGap = 3; // the least room between two labels on a line
export const kBarHeight = 8;
export const kTickOverhang = 2; // an item tick past the bar, each side
export const kZeroOverhang = 3; // the zero tick, a little longer

export interface RulerPiece {
  band: boolean;
  marks: JointMark[];
  bandStart: JointMark;
  bandEnd: JointMark;
}

export interface RulerLabel {
  text: string;
  left: number;
  line: 0 | 1; // 0 above the bar, 1 below it
  width: number; // what the measurer said - the page checks it against the paint
  anchor: number; // the tick it labels; centred on it unless clamped to the strip
}

export interface StripLayout {
  barLeft: number;
  barRight: number;
  ticks: number[];
  band: [number, number] | null;
  labels: RulerLabel[]; // left to right
  edgeLine: -1 | 0 | 1; // -1: left off, the shared line names the edge
  staggered: boolean; // a tick label took the line below
}

const clamp = (v: number, lo: number, hi: number) => Math.min(Math.max(v, lo), hi);

export function layoutStrip(
  piece: RulerPiece,
  edge: string,
  x0: number,
  x1: number,
  measure: (text: string) => number,
): StripLayout | null {
  const barLeft = x0 + kBarInset;
  const barRight = x1 - kBarInset;
  const span = Math.max(1, barRight - barLeft);
  const xAt = (at: number) => barLeft + clamp(at, 0, 1) * span;

  const ends = piece.band ? [piece.bandStart, piece.bandEnd] : piece.marks;
  const ticks = ends.map((m) => xAt(m.at));
  const candidates = ends
    .map((m, i) => ({ text: m.text, anchor: ticks[i] }))
    .sort((a, b) => a.anchor - b.anchor); // stable in every engine this page runs in

  const lastRight = [-1e9, -1e9];
  const firstLeft = [1e9, 1e9];
  let staggered = false;
  const labels: RulerLabel[] = [];
  for (const c of candidates) {
    const w = measure(c.text);
    const left = clamp(c.anchor - w / 2, x0, Math.max(x0, x1 - w));
    let line: 0 | 1;
    if (left >= lastRight[0] + kLabelGap) line = 0;
    else if (left >= lastRight[1] + kLabelGap) line = 1;
    else return null;
    firstLeft[line] = Math.min(firstLeft[line], left);
    lastRight[line] = left + w;
    if (line === 1) staggered = true;
    labels.push({ text: c.text, left, line, width: w, anchor: c.anchor });
  }

  const edgeWidth = measure(edge);
  let edgeLine: -1 | 0 | 1 = -1;
  if (x0 + edgeWidth + kLabelGap <= firstLeft[0]) edgeLine = 0;
  else if (x0 + edgeWidth + kLabelGap <= firstLeft[1]) edgeLine = 1;

  return {
    barLeft,
    barRight,
    ticks,
    band: piece.band && ticks.length === 2 ? [ticks[0], ticks[1]] : null,
    labels,
    edgeLine,
    staggered,
  };
}

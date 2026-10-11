// The toast's placement - ToastHost::reposition()'s band solver, ported.
import { describe, expect, it } from 'vitest';
import { place } from '../src/editor/solver';

const host = { w: 1000, h: 700 };
const toast = { w: 360, h: 50 };

describe('placing the toast', () => {
  it('centres it 24 px above the bottom when nothing is in the way', () => {
    expect(place(host, toast, [])).toEqual({ x: 320, y: 700 - 50 - 24 });
  });

  it('steps right of an obstacle on the left half, with 8 px clearance', () => {
    // A bottom-left cluster reaching to x 400 in the toast's band.
    const p = place(host, toast, [{ x: 0, y: 600, w: 400, h: 100 }]);
    expect(p).toEqual({ x: 408, y: 626 });
  });

  it('steps left of an obstacle on the right half', () => {
    const p = place(host, toast, [{ x: 640, y: 560, w: 360, h: 140 }]);
    expect(p).toEqual({ x: 640 - 8 - 360, y: 626 });
  });

  it('ignores obstacles outside its band', () => {
    expect(place(host, toast, [{ x: 0, y: 0, w: 1000, h: 100 }])).toEqual({ x: 320, y: 626 });
  });

  it('goes up a band when the two sides leave no room, never sideways off it', () => {
    // Left reaches 400, right starts at 600: 200 px of room for a 360 px toast.
    const obstacles = [
      { x: 0, y: 500, w: 400, h: 200 },
      { x: 600, y: 520, w: 400, h: 180 },
    ];
    const p = place(host, toast, obstacles);
    // The band above the HIGHEST blocker (top 500): y = 500 - 8 - 50 = 442.
    expect(p).toEqual({ x: 320, y: 442 });
  });

  it('never leaves the top edge', () => {
    const wall = [
      { x: 0, y: 0, w: 450, h: 700 },
      { x: 550, y: 0, w: 450, h: 700 },
    ];
    expect(place(host, toast, wall).y).toBeGreaterThanOrEqual(0);
  });

  it('a toast wider than the host stays at x 0', () => {
    expect(place({ w: 300, h: 400 }, toast, []).x).toBe(0);
  });
});

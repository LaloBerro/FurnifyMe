// Where the toast stands - ToastHost::reposition()'s band solver, ported.
// Centred, 24 px above the bottom; then one horizontal band at a time: an
// obstacle left of centre raises the floor, one right of centre lowers the
// ceiling, and when the two meet there is no room at this height, so the
// search moves to the band above the HIGHEST thing blocking this one and asks
// again - never off the top edge. Rects are inclusive-exclusive (x + w is the
// first pixel past), which is Qt's right() + 1.

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export function place(
  host: { w: number; h: number },
  size: { w: number; h: number },
  obstacles: Rect[],
  bottomMargin = 24,
  clearance = 8,
): { x: number; y: number } {
  const centred = Math.floor((host.w - size.w) / 2);
  const limitX = Math.max(0, host.w - size.w);
  let x = Math.max(0, centred);
  let y = host.h - size.h - bottomMargin;

  for (let attempt = 0; attempt < 8; ++attempt) {
    let minX = 0;
    let maxX = limitX;
    let highestTop = -1;
    for (const o of obstacles) {
      // Only obstacles sharing this band matter.
      if (o.y + o.h - 1 < y || o.y > y + size.h - 1) continue;
      highestTop = highestTop < 0 ? o.y : Math.min(highestTop, o.y);
      if (o.x + o.w / 2 < host.w / 2) minX = Math.max(minX, o.x + o.w + clearance);
      else maxX = Math.min(maxX, o.x - clearance - size.w);
    }
    if (minX <= maxX) {
      x = Math.min(Math.max(centred, minX), maxX);
      return { x: Math.max(0, x), y };
    }
    const next = Math.max(0, highestTop - clearance - size.h);
    if (next >= y) return { x: Math.max(0, centred), y }; // no upward progress left
    y = next;
    x = centred;
  }
  return { x: Math.max(0, x), y };
}

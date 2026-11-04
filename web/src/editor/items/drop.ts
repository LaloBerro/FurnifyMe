// Where a drag over the Items card would land (migration Phase 3h): the Qt
// drawer's dropTargetAt() zones, ported, with its hysteresis. Pure - pixels
// in, a zone out - so it is tested on its own. Which parent and sibling a zone
// MEANS is the row's own (C++ works out `above` and `below`, ruling R4), and
// whether the move is refused is C++'s dry run.

export type Zone = 'above' | 'into' | 'below';

// How far the hand must travel past a zone's edge before the target flips
// back - a resting hand must not flicker between "above" and "into".
export const kZoneHysteresisPx = 4;

// `previous`: the zone this row already had, or null when the hand just
// arrived on it. Folder rows split 25 / 50 / 25 with the into band dominant;
// a body or an outline splits in halves.
export function zoneAt(isFolder: boolean, y: number, h: number, previous: Zone | null): Zone {
  if (isFolder) {
    let upper = Math.floor(h / 4);
    let lower = h - Math.floor(h / 4);
    if (previous === 'into') {
      upper -= kZoneHysteresisPx;
      lower += kZoneHysteresisPx;
    } else if (previous === 'below') {
      lower -= kZoneHysteresisPx;
    } else if (previous === 'above') {
      upper += kZoneHysteresisPx;
    }
    return y < upper ? 'above' : y < lower ? 'into' : 'below';
  }
  let middle = Math.floor(h / 2);
  if (previous === 'below') middle -= kZoneHysteresisPx;
  else if (previous === 'above') middle += kZoneHysteresisPx;
  return y < middle ? 'above' : 'below';
}

// The press that becomes a drag: a hand that travels this far (Manhattan)
// with the button held. Smaller than Qt's startDragDistance(), which is about
// dragging OUT of an application.
export function travelled(dx: number, dy: number, threshold: number): boolean {
  return Math.abs(dx) + Math.abs(dy) >= threshold;
}

// Auto-scroll: a hand within this band of the list's top or bottom edge
// scrolls it a step per tick.
export const kAutoScrollBandPx = 20;
export const kAutoScrollStepPx = 8;
export const kAutoScrollTickMs = 30;

export function autoScrollStep(y: number, top: number, bottom: number): number {
  if (y >= top && y < top + kAutoScrollBandPx) return -kAutoScrollStepPx;
  if (y < bottom && y >= bottom - kAutoScrollBandPx) return kAutoScrollStepPx;
  return 0;
}

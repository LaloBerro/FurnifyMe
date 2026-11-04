// Where a drag over the Items card would land (migration Phase 3h): the Qt
// drawer's zones, with their hysteresis, and the drag threshold.
import { describe, expect, it } from 'vitest';
import { autoScrollStep, travelled, zoneAt } from '../src/editor/items/drop';

describe('drop zones', () => {
  it('a folder row splits 25 / 50 / 25, the into band dominant', () => {
    expect(zoneAt(true, 2, 32, null)).toBe('above');
    expect(zoneAt(true, 9, 32, null)).toBe('into');
    expect(zoneAt(true, 22, 32, null)).toBe('into');
    expect(zoneAt(true, 25, 32, null)).toBe('below');
  });

  it('a body or an outline splits in halves - no into', () => {
    expect(zoneAt(false, 15, 32, null)).toBe('above');
    expect(zoneAt(false, 16, 32, null)).toBe('below');
    expect(zoneAt(false, 31, 32, null)).toBe('below');
  });

  it('a resting hand keeps its zone for 4 px past the edge', () => {
    // Into holds 4 px into either neighbour's band.
    expect(zoneAt(true, 5, 32, 'into')).toBe('into');
    expect(zoneAt(true, 3, 32, 'into')).toBe('above');
    expect(zoneAt(true, 27, 32, 'into')).toBe('into');
    expect(zoneAt(true, 28, 32, 'into')).toBe('below');
    // Above holds 4 px into the into band.
    expect(zoneAt(true, 11, 32, 'above')).toBe('above');
    expect(zoneAt(true, 12, 32, 'above')).toBe('into');
    // On a body, the line holds 4 px past the middle.
    expect(zoneAt(false, 19, 32, 'above')).toBe('above');
    expect(zoneAt(false, 20, 32, 'above')).toBe('below');
    expect(zoneAt(false, 12, 32, 'below')).toBe('below');
    expect(zoneAt(false, 11, 32, 'below')).toBe('above');
  });

  it('a press becomes a drag after 6 px of travel, measured Manhattan', () => {
    expect(travelled(3, 2, 6)).toBe(false);
    expect(travelled(3, 3, 6)).toBe(true);
    expect(travelled(-6, 0, 6)).toBe(true);
  });

  it('the list scrolls while the hand rests in its top or bottom band', () => {
    expect(autoScrollStep(105, 100, 400)).toBe(-8);
    expect(autoScrollStep(390, 100, 400)).toBe(8);
    expect(autoScrollStep(250, 100, 400)).toBe(0);
    expect(autoScrollStep(90, 100, 400)).toBe(0);
  });
});

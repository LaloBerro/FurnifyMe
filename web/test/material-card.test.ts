// The material card (migration Phase 7a) on the editor's chrome page, as a chip
// face: the name, the swatch in the material's colour, a slider per dial with
// the Qt card's readout - and every edit lands in the Qt card's own slider
// (chips.slide), every press on its own button (chips.press).
import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { mockChrome } from '../src/editor/mock';
import { mockTheme } from '../src/hub/mock';
import { mockChips } from '../src/editor/chips/mock';
import type { Face } from '../src/editor/chips/api';
import { bannedWordsIn } from './banned';

const kW = 260;
const kH = 220;
beforeAll(() => {
  const size = (axis: 'w' | 'h') =>
    function (this: HTMLElement) {
      if (!this.classList?.contains('chip-content')) return 0;
      return axis === 'w' ? kW : kH;
    };
  Object.defineProperty(HTMLElement.prototype, 'offsetWidth', { configurable: true, get: size('w') });
  Object.defineProperty(HTMLElement.prototype, 'offsetHeight', { configurable: true, get: size('h') });
});

afterEach(() => cleanup());

const slider = (id: string, label: string, value: number, min: number, max: number, valueText: string) => ({
  kind: 'slider' as const,
  id,
  label,
  valueText,
  value,
  min,
  max,
  tip: '',
  enabled: true,
});

const materialFace: Face = {
  rows: [
    [{ kind: 'text', text: 'Oak natural', tone: 'title' }],
    [
      {
        kind: 'button',
        id: 'swatch',
        text: '',
        tip: "Pick this material's colour",
        enabled: true,
        checked: false,
        swatch: '#a07850',
      },
      slider('brightness', '', 100, 25, 200, '100%'),
    ],
    [slider('surface', 'Surface', 45, 0, 100, '45')],
    [slider('grainSize', 'Grain size', 300, 50, 1000, '300 mm')],
    [slider('grainAngle', 'Grain angle', 0, 0, 359, '0°')],
    [{ kind: 'button', id: 'done', text: 'Done', tip: '', enabled: true, checked: false }],
  ],
  invalid: false,
  seed: 1,
  focus: '',
};

async function mount() {
  const chrome = mockChrome();
  const chips = mockChips({ material: materialFace });
  chrome.cards = { material: { x: 16, y: 500, w: kW, h: kH, shown: true, chip: true } };
  render(Editor, { props: { chrome, theme: mockTheme(), chips } });
  await tick();
  await tick();
  return { chrome, chips };
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;

describe('the material card', () => {
  it('draws the name, the swatch in the material colour and a slider per dial', async () => {
    await mount();
    expect(q('[data-gesture="material"]')).not.toBeNull();
    const swatch = q('[data-part="swatch"]')!;
    expect(swatch.classList.contains('swatch')).toBe(true);
    expect(swatch.style.background).toBe('rgb(160, 120, 80)');
    const ranges = [...document.querySelectorAll('[data-gesture="material"] input[type="range"]')] as HTMLInputElement[];
    expect(ranges.map((r) => r.dataset.part)).toEqual(['brightness', 'surface', 'grainSize', 'grainAngle']);
    expect(ranges[2].min).toBe('50');
    expect(ranges[2].max).toBe('1000');
    expect(ranges[2].value).toBe('300');
    const readouts = [...document.querySelectorAll('.slider .readout')].map((n) => n.textContent);
    expect(readouts).toEqual(['100%', '45', '300 mm', '0°']);
  });

  it("a slider's move lands in the Qt card's slider, and a press on its button", async () => {
    const { chips } = await mount();
    const surface = q('input[data-part="surface"]') as HTMLInputElement;
    surface.value = '70';
    await fireEvent.input(surface);
    expect(chips.calls.filter((c) => c.op === 'slide')).toEqual([{ op: 'slide', chip: 'material', args: ['surface', 70] }]);
    await fireEvent.click(q('[data-part="swatch"]')!);
    await fireEvent.click(q('[data-part="done"]')!);
    expect(chips.calls.filter((c) => c.op === 'press').map((c) => c.args[0])).toEqual(['swatch', 'done']);
  });

  it('paints none of the banned words', async () => {
    await mount();
    const texts = [...document.querySelectorAll('[data-gesture="material"] .label, [data-gesture="material"] button')].map(
      (n) => n.textContent ?? '',
    );
    expect(texts.length).toBeGreaterThan(3);
    expect(bannedWordsIn(texts)).toEqual([]);
  });
});

const shapesFace: Face = {
  rows: [
    [{ kind: 'text', text: 'Add a shape', tone: 'label' }],
    ['Box', 'Cylinder', 'Sphere'].map((name, i) => ({
      kind: 'button' as const,
      id: `shape${i}`,
      text: name,
      tip: '',
      enabled: true,
      checked: false,
      look: 'tile' as const,
      glyph: 'data:image/png;base64,AAAA',
    })),
  ],
  invalid: false,
  seed: 1,
  focus: '',
};

describe('the Add-shape flyout', () => {
  it('draws each shape as a tile, its glyph a mask over its name, and a press is the tile click', async () => {
    const chrome = mockChrome();
    const chips = mockChips({ shapes: shapesFace });
    chrome.cards = { shapes: { x: 80, y: 120, w: kW, h: kH, shown: true, chip: true } };
    render(Editor, { props: { chrome, theme: mockTheme(), chips } });
    await tick();
    await tick();
    const tiles = [...document.querySelectorAll('[data-gesture="shapes"] .tile')] as HTMLElement[];
    expect(tiles.map((t) => t.querySelector('.tile-name')!.textContent)).toEqual(['Box', 'Cylinder', 'Sphere']);
    expect((tiles[0].querySelector('.tile-glyph') as HTMLElement).getAttribute('style')).toContain('data:image/png;base64,AAAA');
    await fireEvent.click(tiles[1]);
    expect(chips.calls.filter((c) => c.op === 'press').map((c) => c.args[0])).toEqual(['shape1']);
  });
});

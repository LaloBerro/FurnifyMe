// The gesture chips (migration Phase 4a) on the editor's chrome page, against
// a mock ChipsBridge: each chip is drawn from its face where the Qt chip
// stands, once that rectangle carries the page's own size (R4); its fields
// are seeded on a new gesture and claim the keyboard (R5); edits are mirrored
// into the Qt fields, and Enter commits WITH what the fields hold (R3).
import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { mockChrome } from '../src/editor/mock';
import { mockTheme } from '../src/hub/mock';
import { booleanFace, mockChips, slatsFace } from '../src/editor/chips/mock';
import type { Face } from '../src/editor/chips/api';

// jsdom lays nothing out: a chip's content measures 300 x 56 here.
const kW = 300;
const kH = 56;
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

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const card = (name: string) => q(`[data-chip="${name}"]`);
const fieldNamed = (id: string) => q(`[data-chip] input[data-field="${id}"]`) as HTMLInputElement;

async function mount(faces: Record<string, Face>, rects: Record<string, { w: number; h: number }>) {
  const chrome = mockChrome();
  const chips = mockChips(faces);
  chrome.cards = Object.fromEntries(
    Object.entries(rects).map(([name, r]) => [name, { x: 100, y: 400, w: r.w, h: r.h, shown: true, chip: true }]),
  );
  render(Editor, { props: { chrome, theme: mockTheme(), chips } });
  await tick();
  await tick();
  return { chrome, chips };
}

async function moveCard(chrome: ReturnType<typeof mockChrome>, name: string, r: { w: number; h: number; shown?: boolean }) {
  chrome.cards = { ...chrome.cards, [name]: { x: 120, y: 410, w: r.w, h: r.h, shown: r.shown ?? true, chip: true } };
  chrome.cardsChanged.emit();
  await tick();
  await tick();
}

describe('a gesture chip', () => {
  it('reports the size it measures, and stays invisible until its rectangle carries it (R4)', async () => {
    const { chrome, chips } = await mount({ boolean: booleanFace() }, { boolean: { w: 240, h: 40 } });
    expect(chips.calls.filter((c) => c.op === 'reportSize')).toEqual([{ op: 'reportSize', chip: 'boolean', args: [kW, kH] }]);
    expect(card('boolean')!.style.visibility).toBe('hidden');
    expect(card('boolean')!.hasAttribute('data-hit')).toBe(false);
    // Rounded up to whole device pixels by the Qt chip: still its size.
    await moveCard(chrome, 'boolean', { w: kW + 2, h: kH + 1 });
    expect(card('boolean')!.style.visibility).toBe('');
    expect(card('boolean')!.hasAttribute('data-hit')).toBe(true);
    expect([card('boolean')!.style.left, card('boolean')!.style.top]).toEqual(['120px', '410px']);
    expect([card('boolean')!.style.width, card('boolean')!.style.height]).toEqual([`${kW + 2}px`, `${kH + 1}px`]);
  });

  it('reports its size again whenever the rectangle stops carrying it', async () => {
    const { chrome, chips } = await mount({ boolean: booleanFace() }, { boolean: { w: kW, h: kH } });
    // The Qt chip drew itself for a while and came back at its own size: the
    // page's measurement did not change, and must be sent again all the same.
    await moveCard(chrome, 'boolean', { w: 200, h: 40 });
    expect(chips.calls.filter((c) => c.op === 'reportSize').length).toBe(2);
    expect(card('boolean')!.style.visibility).toBe('hidden');
  });

  it('draws nothing while its Qt chip is hidden', async () => {
    const { chrome } = await mount({ boolean: booleanFace() }, { boolean: { w: kW, h: kH } });
    expect(card('boolean')).not.toBeNull();
    await moveCard(chrome, 'boolean', { w: kW, h: kH, shown: false });
    expect(card('boolean')).toBeNull();
  });

  it('draws the boolean chip from its face: the verb, the switch and the keys', async () => {
    const { chips } = await mount({ boolean: booleanFace(true) }, { boolean: { w: kW, h: kH } });
    const texts = [...card('boolean')!.querySelectorAll('.text')].map((n) => n.textContent);
    expect(texts).toEqual(['Subtract', 'Enter applies — Esc cancels']);
    expect(q('.text.title')!.textContent).toBe('Subtract');
    expect(q('.text.hint')!.textContent).toBe('Enter applies — Esc cancels');
    const toggle = q('[data-part="keep"]') as HTMLButtonElement;
    expect(toggle.textContent!.trim()).toBe('Keep the other bodies');
    expect(toggle.getAttribute('aria-pressed')).toBe('true');
    expect(toggle.title).toBe('Leave the bodies');
    // A control that never takes keyboard focus (F17).
    expect(toggle.tabIndex).toBe(-1);
    await fireEvent.click(toggle);
    expect(chips.calls.filter((c) => c.op === 'press')).toEqual([{ op: 'press', chip: 'boolean', args: ['keep'] }]);
  });

  it('follows a pushed face (the switch turned in C++)', async () => {
    const { chips } = await mount({ boolean: booleanFace(false) }, { boolean: { w: kW, h: kH } });
    expect(q('[data-part="keep"]')!.getAttribute('aria-pressed')).toBe('false');
    chips.push({ boolean: booleanFace(true) });
    await tick();
    expect(q('[data-part="keep"]')!.getAttribute('aria-pressed')).toBe('true');
  });
});

describe('the slats chip', () => {
  it('seeds its fields, focuses Width with its text selected, and takes the keyboard (R5)', async () => {
    const { chrome } = await mount({ slats: slatsFace(1, '40') }, { slats: { w: kW, h: kH } });
    await tick();
    expect(['width', 'gap', 'depth'].map((id) => fieldNamed(id).value)).toEqual(['40', '10', '18']);
    expect(document.activeElement).toBe(fieldNamed('width'));
    expect([fieldNamed('width').selectionStart, fieldNamed('width').selectionEnd]).toEqual([0, 2]);
    expect(chrome.calls.filter((c) => c.method === 'claimKeyboardFor').map((c) => c.args[0])).toContain('slats');
    const labels = [...card('slats')!.querySelectorAll('label .label')].map((n) => n.textContent);
    expect(labels).toEqual(['Width', 'Gap', 'Depth']);
    expect(fieldNamed('width').title).toBe('How wide each slat is, in mm');
  });

  it('focuses Width only once the card is drawn - a hidden input takes no focus', async () => {
    const { chrome } = await mount({ slats: slatsFace(1, '40') }, { slats: { w: 200, h: 40 } });
    await tick();
    expect(card('slats')!.style.visibility).toBe('hidden');
    expect(document.activeElement).not.toBe(fieldNamed('width'));
    await moveCard(chrome, 'slats', { w: kW, h: kH });
    await tick();
    expect(document.activeElement).toBe(fieldNamed('width'));
  });

  it('mirrors each edit into the Qt field', async () => {
    const { chips } = await mount({ slats: slatsFace() }, { slats: { w: kW, h: kH } });
    fieldNamed('width').value = '45';
    await fireEvent.input(fieldNamed('width'));
    expect(chips.calls.filter((c) => c.op === 'edit')).toEqual([{ op: 'edit', chip: 'slats', args: ['width', '45'] }]);
  });

  it('never retypes the field being typed in; the others follow C++', async () => {
    const { chips } = await mount({ slats: slatsFace(1, '40') }, { slats: { w: kW, h: kH } });
    await tick();
    fieldNamed('width').value = '4';
    await fireEvent.input(fieldNamed('width'));
    const pushed = slatsFace(1, '40');
    (pushed.rows[0][1] as { value: string }).value = '12';
    chips.push({ slats: pushed });
    await tick();
    expect(fieldNamed('width').value).toBe('4');
    expect(fieldNamed('gap').value).toBe('12');
  });

  it('commits on Enter WITH what its fields hold (R3), and cancels on Escape', async () => {
    const { chips } = await mount({ slats: slatsFace() }, { slats: { w: kW, h: kH } });
    fieldNamed('width').value = '45';
    await fireEvent.input(fieldNamed('width'));
    const enter = await fireEvent.keyDown(fieldNamed('width'), { key: 'Enter' });
    expect(enter).toBe(false); // default prevented
    expect(chips.calls.filter((c) => c.op === 'commit')).toEqual([
      { op: 'commit', chip: 'slats', args: [{ width: '45', gap: '10', depth: '18' }] },
    ]);
    await fireEvent.keyDown(fieldNamed('gap'), { key: 'Escape' });
    expect(chips.calls.filter((c) => c.op === 'cancel')).toEqual([{ op: 'cancel', chip: 'slats', args: [] }]);
  });

  it('a new gesture re-seeds even the field being typed in', async () => {
    const { chips } = await mount({ slats: slatsFace(1, '40') }, { slats: { w: kW, h: kH } });
    await tick();
    fieldNamed('width').value = '4';
    await fireEvent.input(fieldNamed('width'));
    chips.push({ slats: slatsFace(2, '30') });
    await tick();
    await tick();
    expect(fieldNamed('width').value).toBe('30');
  });

  it('presses Flip, and says why when the sizes lay nothing', async () => {
    const { chips } = await mount({ slats: slatsFace(1, '40', 'These sizes lay no slats here') }, { slats: { w: kW, h: kH } });
    await fireEvent.click(q('[data-part="flip"]')!);
    expect(chips.calls.filter((c) => c.op === 'press')).toEqual([{ op: 'press', chip: 'slats', args: ['flip'] }]);
    expect(card('slats')!.classList.contains('invalid')).toBe(true);
    expect(q('.text.danger')!.textContent).toBe('These sizes lay no slats here');
    expect(q('.text.muted')!.textContent).toBe('12 slats');
  });

  it('gives the keyboard back on a blur and when the chip goes', async () => {
    const { chrome } = await mount({ slats: slatsFace() }, { slats: { w: kW, h: kH } });
    await tick();
    await fireEvent.blur(fieldNamed('width'));
    expect(chrome.calls.filter((c) => c.method === 'releaseKeyboardFor').map((c) => c.args[0])).toEqual(['slats']);
    await fireEvent.focus(fieldNamed('gap'));
    await moveCard(chrome, 'slats', { w: kW, h: kH, shown: false });
    expect(chrome.calls.filter((c) => c.method === 'releaseKeyboardFor').map((c) => c.args[0])).toEqual(['slats', 'slats']);
  });
});

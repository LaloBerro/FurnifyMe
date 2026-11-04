// The Items card (migration Phase 3h) against a mock ItemsModel: the rows and
// their marks, names as text (Review Focus 2), clicks on release, the
// controls, rename, the menu, the keys and the list hold, the filter, and the
// drag (Review Focus 3, 4). The rules themselves are C++'s, tested headless.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Items from '../src/editor/items/Items.svelte';
import { itemRow, mockItems, sampleRows } from '../src/editor/items/mock';
import type { ItemRowData } from '../src/editor/items/api';
import { bannedWordsIn } from './banned';

const rect = { x: 60, y: 100, w: 200, h: 400, shown: true };

function mount(api = mockItems()) {
  const heights: number[] = [];
  const kb: string[] = [];
  const view = render(Items, {
    props: {
      items: api,
      keyboard: { claimKeyboard: () => kb.push('claim'), releaseKeyboard: () => kb.push('release') },
      listKeys: { hold: () => kb.push('hold'), release: () => kb.push('letGo') },
      rect,
      bottom: 800,
      report: (_n: string, h: number) => heights.push(h),
    },
  });
  return { api, view, heights, kb };
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const all = (sel: string) => [...document.querySelectorAll<HTMLElement>(sel)];
const calls = (api: ReturnType<typeof mockItems>, method: string) =>
  api.calls.filter((c) => c.method === method).map((c) => c.args);
const row = (id: number) => q(`[data-row="${id}"]`)!;

async function settle() {
  for (let i = 0; i < 3; i++) await tick();
}

function update(api: ReturnType<typeof mockItems>, change: () => void) {
  change();
  api.changed.emit();
  return settle();
}

// jsdom lays nothing out: give the card, its header, its list and its rows
// rectangles, top to bottom, so the drag's geometry has something to read.
function layOut() {
  const box = (el: Element, x: number, y: number, w: number, h: number) =>
    Object.defineProperty(el, 'getBoundingClientRect', {
      configurable: true,
      value: () => ({ left: x, top: y, right: x + w, bottom: y + h, width: w, height: h, x, y }) as DOMRect,
    });
  box(q('.items-card')!, 60, 100, 200, 400);
  box(q('.items-card header')!, 72, 112, 176, 24);
  box(q('.list')!, 72, 180, 176, 300);
  all('[data-row]').forEach((el, i) => box(el, 72, 180 + i * 34, 176, 32));
}

function press(el: Element, x: number, y: number, init: MouseEventInit = {}) {
  el.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, button: 0, buttons: 1, clientX: x, clientY: y, ...init }));
}
function move(x: number, y: number) {
  window.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, buttons: 1, clientX: x, clientY: y }));
}
function release(x: number, y: number) {
  window.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, button: 0, clientX: x, clientY: y }));
}

afterEach(() => cleanup());

describe('the Items card', () => {
  it('draws the rows C++ sends, in order, with their marks', () => {
    mount();
    expect(all('[data-row]').map((r) => r.dataset.row)).toEqual(['7', '1', '2', '8', '6', '4', '5']);
    expect(row(1).classList.contains('marked')).toBe(true);
    expect(row(5).classList.contains('quiet')).toBe(true);
    expect(row(1).getAttribute('style')).toContain('--depth: 1');
    expect(row(7).querySelector('.count')!.textContent).toBe('1/3');
    expect(row(7).querySelector('.count')!.getAttribute('title')).toBe('1 of 3 bodies in Carcass is selected');
    expect(row(6).classList.contains('pending')).toBe(true);
    expect(row(6).classList.contains('marked')).toBe(false);
    expect(row(6).getAttribute('title')).toBe('Outline 01, waiting to extrude');
    expect(row(1).getAttribute('aria-label')).toBe('Side left, in Carcass');
  });

  it('a name is text, never markup - highlighted or not (Review Focus 2)', async () => {
    const api = mockItems([itemRow(1, '<b>Top</b>'), itemRow(2, 'a & b <i>', { match: [2, 1] })]);
    mount(api);
    expect(row(1).querySelector('.name')!.textContent).toBe('<b>Top</b>');
    expect(row(1).querySelector('b')).toBeNull();
    expect(row(2).querySelector('.name')!.textContent).toBe('a & b <i>');
    expect(row(2).querySelector('i')).toBeNull();
    expect(row(2).querySelector('.hit')!.textContent).toBe('&');
  });

  it('a row selects on RELEASE, with its modifiers, and the press holds the list keys', () => {
    const { api, kb } = mount();
    press(row(4), 100, 300);
    expect(calls(api, 'activate')).toEqual([]);
    expect(kb).toContain('hold');
    release(100, 300);
    expect(calls(api, 'activate')).toEqual([[4, false, false]]);
    press(row(2), 100, 250, { ctrlKey: true });
    release(100, 250);
    press(row(5), 100, 400, { shiftKey: true });
    release(100, 400);
    expect(calls(api, 'activate')).toEqual([[4, false, false], [2, true, false], [5, false, true]]);
  });

  it('the eye, the grain mark and the twisty ask for themselves and never select', async () => {
    const { api } = mount();
    await fireEvent.click(row(4).querySelector('.eye')!);
    await fireEvent.click(row(4).querySelector('.grain')!);
    await fireEvent.click(row(7).querySelector('.twisty')!);
    expect(calls(api, 'toggleEye')).toEqual([[4]]);
    expect(calls(api, 'toggleGrain')).toEqual([[4]]);
    expect(calls(api, 'toggleFold')).toEqual([[7]]);
    expect(calls(api, 'activate')).toEqual([]);
    expect(row(4).querySelector('.eye')!.getAttribute('aria-label')).toBe('Hide Shelf');
    expect(row(5).querySelector('.eye')!.getAttribute('aria-label')).toBe('Show Back');
    expect(row(6).querySelector('.grain')).toBeNull();
    expect(row(7).querySelector('.grain')).toBeNull();
  });

  it("a folder the filter holds open has an inert twisty that says why", async () => {
    const api = mockItems([itemRow(7, 'Carcass', { kind: 'folder', open: true, held: true, muted: true, twistyTip: 'Shown open by the filter — clear the filter to fold it' })]);
    mount(api);
    await fireEvent.click(row(7).querySelector('.twisty')!);
    expect(calls(api, 'toggleFold')).toEqual([]);
    expect(row(7).querySelector('.twisty')!.getAttribute('title')).toContain('clear the filter');
  });

  it('a double-click asks for the rename; the field commits on Enter, cancels on Escape', async () => {
    const { api, kb } = mount();
    await fireEvent.dblClick(row(4));
    expect(calls(api, 'beginRename')).toEqual([[4]]);
    await update(api, () => (api.state.rename = { id: 4, name: 'Shelf', seq: 1 }));
    const field = row(4).querySelector('input.rename') as HTMLInputElement;
    expect(field).not.toBeNull();
    expect(field.value).toBe('Shelf');
    expect(kb).toContain('claim');
    await fireEvent.input(field, { target: { value: 'Top shelf' } });
    await fireEvent.keyDown(field, { key: 'Enter' });
    expect(calls(api, 'commitRename')).toEqual([[4, 'Top shelf']]);
    expect(kb.filter((k) => k === 'claim').length).toBe(kb.filter((k) => k === 'release').length);
    await update(api, () => (api.state.rename = { id: 2, name: 'Side right', seq: 2 }));
    await fireEvent.keyDown(row(2).querySelector('input.rename')!, { key: 'Escape' });
    expect(calls(api, 'cancelRename').length).toBe(1);
    expect(calls(api, 'commitRename').length).toBe(1);
  });

  it('a click away from the rename field commits what it holds', async () => {
    const { api } = mount();
    await update(api, () => (api.state.rename = { id: 4, name: 'Shelf', seq: 1 }));
    const field = row(4).querySelector('input.rename') as HTMLInputElement;
    await fireEvent.input(field, { target: { value: 'Board' } });
    await fireEvent.blur(field);
    expect(calls(api, 'commitRename')).toEqual([[4, 'Board']]);
  });

  it("the row menu: a right press opens it; it draws C++'s entries and reports a choice", async () => {
    const { api } = mount();
    await fireEvent.contextMenu(row(4), { clientX: 120, clientY: 300 });
    expect(calls(api, 'openMenu')).toEqual([[4]]);
    await update(api, () => {
      api.menu = {
        open: true,
        row: 4,
        entries: [
          { id: 'folder', label: 'New folder with these', enabled: true, tip: '', keys: '', depth: 0 },
          { id: 'up', label: 'Move up', enabled: false, tip: 'Clear the filter to move rows up or down', keys: 'Alt+Up', depth: 0 },
          { id: 'into', label: 'Move into', enabled: true, tip: '', keys: '', depth: 0, children: [
            { id: 'into:7', label: 'Carcass', enabled: true, tip: '', keys: '', depth: 0 },
          ] },
        ],
      };
    });
    expect(q('[data-entry="up"]')!.getAttribute('title')).toBe('Clear the filter to move rows up or down');
    expect(q('[data-entry="up"] .keys')!.textContent).toBe('Alt+Up');
    await fireEvent.click(q('[data-entry="up"]')!);
    expect(calls(api, 'chooseMenu')).toEqual([]);
    await fireEvent.click(q('[data-entry="into"]')!);
    await fireEvent.click(q('[data-entry="into:7"]')!);
    await fireEvent.click(q('[data-entry="folder"]')!);
    expect(calls(api, 'chooseMenu')).toEqual([['into:7'], ['folder']]);
    await fireEvent.mouseDown(q('.outside')!);
    expect(calls(api, 'closeMenu').length).toBe(1);
  });

  it("the list's keys send the table's commands; other keys send nothing (Review Focus 1)", async () => {
    const { api } = mount();
    const list = q('.list')!;
    await fireEvent.keyDown(list, { key: 'ArrowDown' });
    await fireEvent.keyDown(list, { key: 'ArrowUp', shiftKey: true });
    await fireEvent.keyDown(list, { key: 'ArrowUp', altKey: true });
    await fireEvent.keyDown(list, { key: 'End' });
    await fireEvent.keyDown(list, { key: 'F2' });
    await fireEvent.keyDown(list, { key: 'e' });
    await fireEvent.keyDown(list, { key: 'z', ctrlKey: true });
    expect(calls(api, 'key')).toEqual([
      ['step', true],
      ['extendStep', false],
      ['moveUp', false],
      ['firstOrLast', true],
      ['rename', false],
    ]);
  });

  it('the filter: typing filters, Escape clears, Down or Enter goes to the list', async () => {
    const { api, kb } = mount();
    const field = q('input.filter') as HTMLInputElement;
    expect(field.getAttribute('placeholder')).toBe('Filter by name');
    await fireEvent.mouseDown(field);
    expect(kb).toContain('claim');
    await fireEvent.input(field, { target: { value: 'side' } });
    expect(calls(api, 'setFilter')).toEqual([['side']]);
    await fireEvent.keyDown(field, { key: 'Escape' });
    expect(calls(api, 'clearFilter').length).toBe(1);
    await fireEvent.input(field, { target: { value: 'sh' } });
    await fireEvent.keyDown(field, { key: 'Enter' });
    expect(calls(api, 'key')).toEqual([['enterList', true]]);
    expect(kb[kb.length - 1]).toBe('hold');
  });

  it('a press that travels 6 px is a drag, not a click - and the release drops it', async () => {
    const { api } = mount();
    layOut();
    press(row(4), 100, 300);
    move(100, 303);
    expect(calls(api, 'beginDrag')).toEqual([]);
    move(100, 307);
    expect(calls(api, 'beginDrag')).toEqual([[4, true]]);
    await update(api, () => (api.drag = { ...api.drag, active: true, ghost: { kind: 'body', name: 'Shelf', more: 0 } }));
    layOut();
    // Over the Carcass folder row's middle: into it.
    move(100, 180 + 16);
    expect(calls(api, 'dragOver').at(-1)).toEqual(['into', 7, -1]);
    // The upper half of Side right: a line before it, as C++ worked it out.
    move(100, 180 + 34 * 2 + 4);
    expect(calls(api, 'dragOver').at(-1)).toEqual(['line', 7, 2]);
    // The title row: the end of the top level.
    move(100, 120);
    expect(calls(api, 'dragOver').at(-1)).toEqual(['top', 0, 0]);
    // Outside the card.
    move(400, 300);
    expect(calls(api, 'dragOver').at(-1)).toEqual(['outside', 0, 0]);
    await settle();
    expect(q('.ghost')!.textContent).toContain('Release to cancel');
    release(400, 300);
    expect(calls(api, 'endDrag')).toEqual([[true]]);
    expect(calls(api, 'activate')).toEqual([]);
  });

  it('a press on a row of a multiple selection with a modifier carries the row alone', () => {
    const { api } = mount();
    layOut();
    press(row(1), 100, 200, { ctrlKey: true });
    move(100, 210);
    expect(calls(api, 'beginDrag')).toEqual([[1, false]]);
  });

  it('Escape, or the window losing focus, cancels a drag (Review Focus 3)', async () => {
    const { api, kb } = mount();
    layOut();
    press(row(4), 100, 300);
    move(100, 310);
    expect(kb).toContain('claim');
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    expect(calls(api, 'endDrag')).toEqual([[false]]);
    expect(kb.filter((k) => k === 'claim').length).toBe(kb.filter((k) => k === 'release').length);
    press(row(4), 100, 300);
    move(100, 310);
    window.dispatchEvent(new Event('blur'));
    expect(calls(api, 'endDrag')).toEqual([[false], [false]]);
    // The release that finally arrives selects nothing.
    release(100, 310);
    expect(calls(api, 'activate')).toEqual([]);
  });

  it('while filtered, only a folder takes a drop (Review Focus 4)', async () => {
    const api = mockItems();
    api.state.filter = 'side';
    mount(api);
    layOut();
    press(row(4), 100, 300);
    move(100, 310);
    move(100, 180 + 34 + 8);   // over Side left, a body
    expect(calls(api, 'dragOver').at(-1)).toEqual(['none', 0, 0]);
    move(100, 180 + 4);   // the very top of the Carcass folder row
    expect(calls(api, 'dragOver').at(-1)).toEqual(['into', 7, -1]);
  });

  it('a refused target shows its cue in the danger ink, and only for that target', async () => {
    const { api } = mount();
    layOut();
    press(row(4), 100, 300);
    move(100, 310);
    move(100, 180 + 16);
    await update(api, () => (api.drag = { active: true, refused: true, status: 'x', target: { kind: 'into', parent: 7, before: -1 }, ghost: { kind: 'body', name: 'Shelf', more: 0 } }));
    layOut();
    expect(q('.indicator.ring')!.classList.contains('refused')).toBe(true);
    expect(q('.ghost')!.classList.contains('refused')).toBe(true);
    move(100, 180 + 34 * 5 + 4);
    await settle();
    expect(q('.indicator')!.classList.contains('refused')).toBe(false);
  });

  it('the ghost names the first row and counts the rest', async () => {
    const { api } = mount();
    layOut();
    press(row(1), 100, 200);
    move(100, 210);
    await update(api, () => (api.drag = { ...api.drag, active: true, ghost: { kind: 'body', name: 'Side left', more: 2 } }));
    expect(q('.ghost')!.textContent).toContain('Side left +2');
  });

  it('C++ ending the drag lets go of it here too', async () => {
    const { api, kb } = mount();
    layOut();
    press(row(4), 100, 300);
    move(100, 310);
    await update(api, () => (api.drag = { ...api.drag, active: true }));
    await update(api, () => (api.drag = { ...api.drag, active: false }));
    expect(q('.ghost')).toBeNull();
    expect(kb.filter((k) => k === 'claim').length).toBe(kb.filter((k) => k === 'release').length);
  });

  it('the empty state, the no-match line and the hidden-pick foot', async () => {
    const api = mockItems([]);
    mount(api);
    expect(q('.empty')!.textContent).toContain('Nothing to list yet');
    await update(api, () => {
      api.state = { ...api.state, empty: false, noMatch: 'Nothing matches "zz"', foot: 'Front is hidden by the filter —' };
    });
    expect(q('.no-match')!.textContent).toBe('Nothing matches "zz"');
    expect(q('.foot span')!.textContent).toBe('Front is hidden by the filter —');
    await fireEvent.click(q('.foot .clear')!);
    expect(calls(api, 'clearFilter').length).toBe(1);
  });

  it('New folder mirrors the action: its reason, and nothing while disabled', async () => {
    const { api } = mount();
    const button = q('.new-folder') as HTMLButtonElement;
    expect(button.getAttribute('title')).toBe('Put the selected bodies in a new folder (Ctrl+G)');
    await fireEvent.click(button);
    expect(calls(api, 'newFolder').length).toBe(1);
    await update(api, () => (api.state = { ...api.state, newFolder: { enabled: false, tip: 'Select bodies first' } }));
    expect((q('.new-folder') as HTMLButtonElement).disabled).toBe(true);
  });

  it('the card reports its height, at least 176', () => {
    const { heights } = mount();
    expect(heights.length).toBeGreaterThan(0);
    expect(Math.min(...heights)).toBeGreaterThanOrEqual(176);
  });

  it('a scroll request from C++ brings the row into view', async () => {
    const { api } = mount();
    let scrolled = 0;
    (row(5) as HTMLElement & { scrollIntoView: () => void }).scrollIntoView = () => scrolled++;
    await update(api, () => (api.state = { ...api.state, scroll: { id: 5, seq: 3 } }));
    expect(scrolled).toBe(1);
  });

  it('no banned word in its words or its rendered text', () => {
    const { api } = mount();
    const words = Object.values(api.words).filter((w): w is string => typeof w === 'string');
    expect(bannedWordsIn(words)).toEqual([]);
    const rows: ItemRowData[] = sampleRows();
    const own = rows.flatMap((r) => [r.eyeTip, r.twistyTip ?? '', r.grainTip ?? '']);
    expect(bannedWordsIn(own)).toEqual([]);
  });
});

// The scene window's two cards on the chrome page (migration Phase 5b),
// against mocks of their bridges: the Pieces card in its slot, sized by its
// rows, every gesture leaving as the Qt card's own request; and the add card,
// centred, claiming the keyboard so Escape closes it.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { mockChrome } from '../src/editor/mock';
import { mockTheme } from '../src/hub/mock';
import { addPieceState, mockAddPiece, mockPieces, piecesState } from '../src/editor/scene/mock';
import { bannedWordsIn } from './banned';

afterEach(() => cleanup());

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const qa = (sel: string) => [...document.querySelectorAll(sel)] as HTMLElement[];

async function mount(opts: { pieces?: ReturnType<typeof mockPieces>; addPiece?: ReturnType<typeof mockAddPiece> }) {
  const chrome = mockChrome();
  chrome.cards = { pieces: { x: 16, y: 72, w: 220, h: 120, shown: true } };
  render(Editor, { props: { chrome, theme: mockTheme(), ...opts } });
  await tick();
  await tick();
  return chrome;
}

describe('the Pieces card', () => {
  it('lists the pieces where its slot stands - the selected one marked, a hidden one with its eye shut, a broken one saying why', async () => {
    const pieces = mockPieces(
      piecesState([
        { id: 3, name: 'Chair', selected: true },
        { id: 7, name: 'Table', visible: false },
        { id: 9, name: 'Lamp', reason: 'This furniture is gone' },
      ]),
    );
    await mount({ pieces });
    const card = q('[data-scene="pieces"]')!;
    expect([card.style.left, card.style.top, card.style.width]).toEqual(['16px', '72px', '220px']);
    expect(q('[data-scene="pieces"] .title')!.textContent).toBe('Pieces');
    const rows = qa('[data-piece]');
    expect(rows.map((r) => r.dataset.piece)).toEqual(['3', '7', '9']);
    expect(rows[0].classList.contains('selected')).toBe(true);
    expect(q('[data-piece="7"] .eye')!.getAttribute('aria-pressed')).toBe('false');
    expect(q('[data-piece="9"] .reason')!.textContent).toBe('This furniture is gone');
    expect(q('[data-piece="3"] .name')!.textContent).toBe('Chair');
    expect(bannedWordsIn([(card.textContent ?? '').replace(/Chair|Table|Lamp/g, '')])).toEqual([]);
  });

  it('says how tall it is, so its slot can take the height', async () => {
    const pieces = mockPieces(piecesState([{ id: 3, name: 'Chair' }]));
    const chrome = await mount({ pieces });
    expect(chrome.calls.some((c) => c.method === 'reportCardHeight' && c.args[0] === 'pieces')).toBe(true);
  });

  it('selects on a click, toggles the eye, and takes a piece out in two clicks', async () => {
    const pieces = mockPieces(piecesState([{ id: 3, name: 'Chair' }]));
    await mount({ pieces });
    await fireEvent.click(q('[data-piece="3"] .name')!);
    await fireEvent.click(q('[data-piece="3"] .eye')!);
    await fireEvent.click(q('[data-piece="3"] .remove')!);
    expect(pieces.calls).toEqual([
      { op: 'select', args: [3] },
      { op: 'toggleVisible', args: [3, false] },
      { op: 'remove', args: [3] },
    ]);
    pieces.push(piecesState([{ id: 3, name: 'Chair', armed: true }]));
    await tick();
    const remove = q('[data-piece="3"] .remove')!;
    expect(remove.textContent!.trim()).toBe('Remove?');
    expect(remove.title).toBe('Click again to take it out of the scene');
    expect(remove.classList.contains('armed')).toBe(true);
  });

  it('renames in place on a double-click: Enter keeps the name, Escape keeps the old one', async () => {
    const pieces = mockPieces(piecesState([{ id: 3, name: 'Chair' }]));
    const chrome = await mount({ pieces });
    await fireEvent.dblClick(q('[data-piece="3"] .name')!);
    await tick();
    const field = q('[data-piece="3"] input') as HTMLInputElement;
    expect(field.value).toBe('Chair');
    expect(chrome.calls.filter((c) => c.method === 'claimKeyboardFor').map((c) => c.args[0])).toContain('pieces');
    field.value = 'Armchair';
    await fireEvent.input(field);
    await fireEvent.keyDown(field, { key: 'Enter' });
    expect(pieces.calls.filter((c) => c.op === 'rename')).toEqual([{ op: 'rename', args: [3, 'Armchair'] }]);
    await tick();
    expect(q('[data-piece="3"] input')).toBeNull();
    await fireEvent.dblClick(q('[data-piece="3"] .name')!);
    await tick();
    await fireEvent.keyDown(q('[data-piece="3"] input')!, { key: 'Escape' });
    expect(pieces.calls.filter((c) => c.op === 'rename').length).toBe(1);
  });

  it('says it is empty, and its Add piece row adds one', async () => {
    const pieces = mockPieces(piecesState([]));
    await mount({ pieces });
    expect(q('[data-scene="pieces"] .empty')!.textContent).toBe('No pieces yet — add a piece from the library');
    await fireEvent.click(q('[data-scene="pieces"] .add')!);
    expect(pieces.calls).toEqual([{ op: 'addPiece', args: [] }]);
  });
});

describe('the add card', () => {
  it('stands only while asked', async () => {
    const addPiece = mockAddPiece(addPieceState(false));
    await mount({ addPiece });
    expect(q('[data-scene="add"]')).toBeNull();
    addPiece.push(addPieceState(true));
    await tick();
    expect(q('[data-scene="add"]')).not.toBeNull();
  });

  it('asks which furniture, lists the library, and adds the one chosen', async () => {
    const addPiece = mockAddPiece(addPieceState(true, [{ id: 'f1', name: 'Bench' }, { id: 'f2', name: 'Shelf' }]));
    const chrome = await mount({ addPiece });
    expect(q('[data-scene="add"] .title')!.textContent).toBe('Which furniture?');
    expect(qa('[data-scene="add"] [data-entry]').map((b) => b.textContent!.trim())).toEqual(['Bench', 'Shelf']);
    expect(q('[data-scene="add"] .hint')!.textContent).toBe('Esc closes');
    expect(chrome.calls.filter((c) => c.method === 'claimKeyboardFor').map((c) => c.args[0])).toContain('addPiece');
    await fireEvent.click(q('[data-entry="f2"]')!);
    expect(addPiece.calls).toEqual([{ op: 'choose', args: ['f2'] }]);
  });

  it('over an empty library, says so and offers the way to it; Escape closes', async () => {
    const addPiece = mockAddPiece(addPieceState(true, []));
    await mount({ addPiece });
    expect(q('[data-scene="add"] .empty')!.textContent).toBe('The library is empty — make a furniture first');
    await fireEvent.click(q('[data-scene="add"] .library')!);
    await fireEvent.keyDown(window, { key: 'Escape' });
    expect(addPiece.calls).toEqual([
      { op: 'goToLibrary', args: [] },
      { op: 'close', args: [] },
    ]);
  });
});

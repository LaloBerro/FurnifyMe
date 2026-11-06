// The shortcut sheet (migration Phase 7a) on the editor's chrome page, against
// a mock EditorChrome: ShortcutSheet::words() draws the card where C++ placed
// it, each group at its own column place, each row its label and its keys -
// and the card is a hit region, since the page draws only inside its mask.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { mockChrome } from '../src/editor/mock';
import { mockTheme } from '../src/hub/mock';
import { bannedWordsIn } from './banned';

const sheet = {
  shown: true,
  title: 'Keyboard shortcuts',
  x: 120,
  y: 40,
  width: 640,
  height: 300,
  padding: 24,
  titleHeight: 44,
  groupHeight: 30,
  rowHeight: 26,
  columnWidth: 260,
  groups: [
    { title: 'File', x: 0, y: 0, rows: [{ label: 'Save', keys: 'Ctrl+S' }] },
    {
      title: 'Model',
      x: 308,
      y: 0,
      rows: [
        { label: 'Extrude', keys: 'E' },
        { label: 'Mirror', keys: 'S' },
      ],
    },
  ],
};

function mount() {
  const chrome = mockChrome();
  render(Editor, { props: { chrome, theme: mockTheme() } });
  return chrome;
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;

afterEach(() => cleanup());

describe('the shortcut sheet', () => {
  it('draws nothing until C++ says it is shown', async () => {
    mount();
    await tick();
    expect(q('.sheet')).toBeNull();
  });

  it('draws the card where the Qt sheet stands, as a hit region', async () => {
    const chrome = mount();
    chrome.sheetChanged.emit(sheet);
    await tick();
    const card = q('.sheet')!;
    expect(card.style.left).toBe('120px');
    expect(card.style.top).toBe('40px');
    expect(card.style.width).toBe('640px');
    expect(card.style.height).toBe('300px');
    expect(card.hasAttribute('data-hit')).toBe(true);
    expect(q('.sheet .title')!.textContent).toBe('Keyboard shortcuts');
  });

  it('stands each group at its own place, with its rows', async () => {
    const chrome = mount();
    chrome.sheetChanged.emit(sheet);
    await tick();
    const groups = [...document.querySelectorAll('.sheet .group')] as HTMLElement[];
    expect(groups.map((g) => g.querySelector('.heading')!.textContent)).toEqual(['File', 'Model']);
    expect(groups[1].style.left).toBe('332px');
    expect(groups[1].style.top).toBe('44px');
    const rows = [...groups[1].querySelectorAll('.row')].map(
      (r) => `${r.querySelector('.label')!.textContent}|${r.querySelector('.keys')!.textContent}`,
    );
    expect(rows).toEqual(['Extrude|E', 'Mirror|S']);
  });

  it('goes when the sheet is hidden', async () => {
    const chrome = mount();
    chrome.sheetChanged.emit(sheet);
    await tick();
    chrome.sheetChanged.emit({ shown: false });
    await tick();
    expect(q('.sheet')).toBeNull();
  });

  it('paints no banned word of its own', async () => {
    const chrome = mount();
    chrome.sheetChanged.emit(sheet);
    await tick();
    const texts = [...document.querySelectorAll('.sheet .title, .sheet .heading, .sheet .label')].map(
      (n) => n.textContent ?? '',
    );
    expect(texts.length).toBe(6);
    expect(bannedWordsIn(texts)).toEqual([]);
    // Non-vacuity: the sweep does see a banned word when one is painted.
    expect(bannedWordsIn(['Fuse the two bodies'])).not.toEqual([]);
  });
});

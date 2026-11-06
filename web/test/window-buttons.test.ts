// The editor's window buttons (migration Phase 7a) on its chrome page, against
// the hub's own window mock: drawn in the Qt buttons' place, minimize and
// close as the page's presses, maximize answering the native side (its hover
// and press come from there), and its rectangle reported for snap layouts.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { mockChrome } from '../src/editor/mock';
import { mockTheme, mockWindow } from '../src/hub/mock';

afterEach(() => cleanup());

async function mount() {
  const chrome = mockChrome();
  const win = mockWindow();
  chrome.cards = { buttons: { x: 1100, y: 16, w: 132, h: 40, shown: true } };
  render(Editor, { props: { chrome, theme: mockTheme(), win } });
  await tick();
  await tick();
  return { chrome, win };
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;

describe("the editor's window buttons", () => {
  it('draws the three buttons where the Qt buttons stand, as a hit region', async () => {
    await mount();
    const card = q('.window-buttons')!;
    expect(card.style.left).toBe('1100px');
    expect(card.hasAttribute('data-hit')).toBe(true);
    const labels = [...document.querySelectorAll('.window-buttons .win')].map((b) => b.getAttribute('aria-label'));
    expect(labels).toEqual(['Minimize', 'Maximize', 'Close']);
  });

  it('minimize, maximize and close answer through the window object', async () => {
    const { win } = await mount();
    const buttons = [...document.querySelectorAll('.window-buttons .win')] as HTMLElement[];
    for (const b of buttons) await fireEvent.click(b);
    expect(win.calls.map((c) => c.method)).toEqual(expect.arrayContaining(['minimize', 'toggleMaximize', 'close']));
  });

  it('reports where its maximize button is, and wears the native hover and press', async () => {
    const { win } = await mount();
    expect(win.calls.some((c) => c.method === 'reportChrome')).toBe(true);
    win.maxHovered.emit(true);
    await tick();
    expect(document.querySelectorAll('.window-buttons .win')[1].classList.contains('hovered')).toBe(true);
    win.maximizedChanged.emit(true);
    await tick();
    expect(document.querySelectorAll('.window-buttons .win')[1].getAttribute('aria-label')).toBe('Restore');
  });

  it('is not drawn without the window object', async () => {
    const chrome = mockChrome();
    chrome.cards = { buttons: { x: 1100, y: 16, w: 132, h: 40, shown: true } };
    render(Editor, { props: { chrome, theme: mockTheme() } });
    await tick();
    expect(q('.window-buttons')).toBeNull();
  });
});

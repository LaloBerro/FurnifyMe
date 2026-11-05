// The rail, the view cluster, the pill and the menus (migration Phase 3i)
// against mock ShellBridge and MenuModel objects: chips mirror their actions
// and never run a disabled one (Review Focus 3), names are text, the pill
// reports what it measured, the menu draws MenuModel's levels and closes on a
// press outside. QMenu's keyboard rules are C++'s, tested headless
// (menu_model_tests); the keys never reach this page.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Cluster from '../src/editor/shell/Cluster.svelte';
import Pill from '../src/editor/shell/Pill.svelte';
import Menu from '../src/editor/shell/Menu.svelte';
import { chipState, entry, mockMenu, mockShell, sampleMenus, separator } from '../src/editor/shell/mock';
import type { MenuOpen } from '../src/editor/shell/api';
import { bannedWordsIn } from './banned';

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const all = (sel: string) => [...document.querySelectorAll<HTMLElement>(sel)];

async function settle() {
  for (let i = 0; i < 4; i++) await tick();
  await new Promise((r) => setTimeout(r, 0));
  for (let i = 0; i < 2; i++) await tick();
}

afterEach(() => cleanup());

function mountRail(shell = mockShell()) {
  const triggers: string[] = [];
  const reports: Record<string, unknown>[] = [];
  render(Cluster, {
    props: {
      name: 'rail',
      entries: shell.rail,
      chips: shell.chips,
      glyphs: shell.glyphs,
      rect: { x: 16, y: 68, w: 50, h: 470 },
      animate: false,
      from: 'left',
      ontrigger: (id: string) => triggers.push(id),
      report: (rects: Record<string, unknown>) => reports.push(rects),
    },
  });
  return { shell, triggers, reports };
}

describe('the rail', () => {
  it('draws every chip in order with its separators, named by its action', async () => {
    const { shell } = mountRail();
    await settle();
    const chips = all('[data-cluster="rail"] [data-chip]');
    expect(chips.map((c) => c.dataset.chip)).toEqual(shell.rail.filter((e) => e.id).map((e) => e.id));
    expect(all('[data-cluster="rail"] [role="separator"]')).toHaveLength(4);
    const items = q('[data-chip="items"]')!;
    expect(items.getAttribute('aria-label')).toBe('Items');
    expect(items.getAttribute('title')).toBe('Items\nShow or hide the list of bodies');
    expect(items.getAttribute('aria-description')).toBe('Items\nShow or hide the list of bodies');
    expect(items.getAttribute('aria-pressed')).toBe('false');
    expect(items.tabIndex).toBe(-1);
  });

  it('a click asks for the action; a disabled chip asks for nothing (Review Focus 3)', async () => {
    const { triggers } = mountRail();
    await settle();
    await fireEvent.click(q('[data-chip="start-sketch"]')!);
    await fireEvent.click(q('[data-chip="extrude"]')!);
    await fireEvent.click(q('[data-chip="undo"]')!);
    expect(triggers).toEqual(['start-sketch']);
    expect(q('[data-chip="extrude"]')!.getAttribute('aria-disabled')).toBe('true');
    expect(q('[data-chip="extrude"]')!.classList.contains('off')).toBe(true);
  });

  it('a checked action shows its chip on', async () => {
    const shell = mockShell();
    shell.chips.items = chipState('Items', { checkable: true, checked: true });
    mountRail(shell);
    await settle();
    expect(q('[data-chip="items"]')!.classList.contains('on')).toBe(true);
    expect(q('[data-chip="items"]')!.getAttribute('aria-pressed')).toBe('true');
  });

  it('tints the glyph mask it was sent', async () => {
    mountRail();
    await settle();
    const glyph = q('[data-chip="undo"] .glyph')!;
    expect(glyph.getAttribute('style')).toMatch(/mask-image: url\("?data:image\/png;base64,/);
  });

  it("reports each chip's rectangle", async () => {
    const { reports } = mountRail();
    await settle();
    expect(reports.length).toBeGreaterThan(0);
    expect(Object.keys(reports[reports.length - 1])).toContain('add-shape');
  });

  it('takes the pointer (data-hit)', async () => {
    mountRail();
    await settle();
    expect(q('[data-cluster="rail"]')!.hasAttribute('data-hit')).toBe(true);
  });
});

function mountPill(menu = mockMenu()) {
  const reports: [number, number, unknown[]][] = [];
  const props = $stateLess(menu);
  const view = render(Pill, {
    props: {
      menu,
      titles: menu.titles,
      open: props.open,
      wordmark: 'FurnifyMe',
      rect: { x: 16, y: 16, w: 420, h: 44 },
      animate: false,
      report: (w: number, h: number, t: unknown[]) => reports.push([w, h, t]),
    },
  });
  return { menu, reports, view };
}
// The pill reads `open` from its parent; a plain object stands in here.
function $stateLess(menu: ReturnType<typeof mockMenu>) {
  return { open: menu.open as MenuOpen };
}

describe('the pill', () => {
  it('draws the wordmark and every title as text', async () => {
    mountPill();
    await settle();
    expect(q('[data-pill] .wordmark')!.textContent).toBe('FurnifyMe');
    expect(all('[data-title]').map((t) => t.textContent!.trim())).toEqual(['File', 'Sketch', 'Edit', 'Model', 'View', 'Help']);
    expect(q('[data-pill]')!.hasAttribute('data-hit')).toBe(true);
  });

  it('a press on a title opens its menu by mouse', async () => {
    const { menu } = mountPill();
    await settle();
    await fireEvent.mouseDown(q('[data-title="3"]')!, { button: 0 });
    expect(menu.calls.filter((c) => c.method === 'openMenu').map((c) => c.args)).toEqual([[3, false]]);
  });

  it('a press on the open title closes it, and the pointer on another switches', async () => {
    const menu = mockMenu();
    menu.openMenu(0, false);
    menu.calls.length = 0;
    const { view } = mountPill(menu);
    await view.rerender({ open: menu.open });
    await settle();
    await fireEvent.mouseEnter(q('[data-title="2"]')!);
    expect(menu.calls.map((c) => c.method)).toEqual(['openMenu']);
    expect(menu.calls[0].args).toEqual([2, false]);
    await view.rerender({ open: menu.open });
    await fireEvent.mouseDown(q('[data-title="2"]')!, { button: 0 });
    expect(menu.calls.map((c) => c.method)).toEqual(['openMenu', 'close']);
  });

  it('underlines the mnemonic only while the keyboard opened the menu', async () => {
    const menu = mockMenu();
    const { view } = mountPill(menu);
    await settle();
    expect(q('[data-title="0"] u')).toBeNull();
    menu.openMenu(0, true);
    await view.rerender({ open: menu.open });
    await settle();
    expect(q('[data-title="0"] u')!.textContent).toBe('F');
  });

  it('reports its size and its titles (R9, R10)', async () => {
    const { reports } = mountPill();
    await settle();
    expect(reports.length).toBeGreaterThan(0);
    expect(reports[reports.length - 1][2]).toHaveLength(6);
  });
});

function mountMenu(open: MenuOpen, menu = mockMenu()) {
  const view = render(Menu, {
    props: { menu, open, anchor: { x: 100, y: 16, w: 40, h: 30 }, bounds: { w: 1200, h: 800 } },
  });
  return { menu, view };
}

describe('the menu', () => {
  const sample = sampleMenus();
  const fileOpen = (current = -1, byKeyboard = false): MenuOpen => ({
    menu: 0,
    byKeyboard,
    levels: [{ entries: sample.levels[0], current }],
  });

  it("draws MenuModel's entries: text, shortcut, separator, submenu arrow, disabled", async () => {
    mountMenu(fileOpen());
    await settle();
    const entries = all('[data-menu-level="0"] .entry');
    expect(entries.map((e) => e.querySelector('.label')!.textContent)).toEqual([
      'New furniture...',
      'Save',
      'Autosave',
      'Export STEP...',
      'Close furniture',
    ]);
    expect(entries[0].querySelector('.keys')!.textContent).toBe('Ctrl+N');
    expect(entries[1].getAttribute('aria-disabled')).toBe('true');
    expect(entries[2].querySelector('.arrow')).not.toBeNull();
    expect(entries[2].getAttribute('aria-haspopup')).toBe('menu');
    expect(all('[data-menu-level="0"] [role="separator"]')).toHaveLength(1);
  });

  it('marks the current entry, and underlines mnemonics only when opened by keyboard', async () => {
    const { view } = mountMenu(fileOpen(0, false));
    await settle();
    expect(q('[data-menu-level="0"] .entry.current .label')!.textContent).toBe('New furniture...');
    expect(q('[data-menu-level="0"] u')).toBeNull();
    await view.rerender({ open: fileOpen(0, true) });
    await settle();
    expect(q('[data-entry="4"] u')!.textContent).toBe('x');
  });

  it('the pointer over an entry is a hover; a click chooses; a disabled entry chooses nothing', async () => {
    const { menu } = mountMenu(fileOpen());
    await settle();
    await fireEvent.mouseEnter(q('[data-menu-level="0"] [data-entry="4"]')!);
    await fireEvent.click(q('[data-menu-level="0"] [data-entry="4"]')!);
    await fireEvent.click(q('[data-menu-level="0"] [data-entry="1"]')!);
    expect(menu.calls.map((c) => [c.method, ...c.args])).toEqual([
      ['hover', 0, 4],
      ['choose', 0, 4],
    ]);
  });

  it('draws a submenu level with its radio marks', async () => {
    mountMenu({
      menu: 0,
      byKeyboard: false,
      levels: [
        { entries: sample.levels[0], current: 2 },
        { entries: sample.sub, current: -1 },
      ],
    });
    await settle();
    const sub = all('[data-menu-level="1"] .entry');
    expect(sub).toHaveLength(2);
    expect(sub[0].getAttribute('role')).toBe('menuitemradio');
    expect(sub[0].getAttribute('aria-checked')).toBe('true');
    expect(sub[0].querySelector('.mark')!.textContent).toBe('•');
    expect(sub[1].querySelector('.mark')!.textContent).toBe('');
  });

  it('a check entry shows a check', async () => {
    mountMenu({
      menu: 4,
      byKeyboard: false,
      levels: [{ entries: [entry('Show &Grid', { checkable: true, checked: true }), separator], current: -1 }],
    });
    await settle();
    const e = q('[data-menu-level="0"] .entry')!;
    expect(e.getAttribute('role')).toBe('menuitemcheckbox');
    expect(e.querySelector('.mark')!.textContent).toBe('✓');
  });

  it('a press outside closes the menu and goes no further', async () => {
    const { menu } = mountMenu(fileOpen());
    await settle();
    const outside = q('[data-menu-outside]')!;
    expect(outside.hasAttribute('data-hit')).toBe(true);
    const pressed = new MouseEvent('mousedown', { bubbles: true, cancelable: true });
    outside.dispatchEvent(pressed);
    expect(pressed.defaultPrevented).toBe(true);
    expect(menu.calls.map((c) => c.method)).toEqual(['close']);
  });

  it('names are text, never markup', async () => {
    mountMenu({
      menu: 0,
      byKeyboard: false,
      levels: [{ entries: [entry('<b>Bold</b> & co')], current: -1 }],
    });
    await settle();
    expect(q('[data-menu-level="0"] .entry .label')!.textContent).toBe('<b>Bold</b>  co');
    expect(q('[data-menu-level="0"] .entry b')).toBeNull();
  });

  it('every word the menu and the pill paint passes the vocabulary law', async () => {
    mountMenu(fileOpen());
    mountPill();
    await settle();
    expect(bannedWordsIn([document.body.textContent ?? ''])).toEqual([]);
  });
});

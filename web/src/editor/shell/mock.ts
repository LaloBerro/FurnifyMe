// ShellBridge and MenuModel stand-ins for the page tests and the dev server.
// They record what the page asks and decide almost nothing: the menu mock
// opens, hovers into submenus and closes so a test can watch the page draw
// it; QMenu's keyboard rules are MenuModel's, tested headless
// (menu_model_tests). Words are the app's own.
import { MockSignal, type Call } from '../../hub/mock';
import type { ChipState, MenuApi, MenuEntryData, MenuOpen, MenuTitle, Rect, ShellApi, ShellEntry } from './api';

// A 1x1 white PNG: a glyph mask the page can tint.
const dot =
  'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=';

export function chipState(label: string, over: Partial<ChipState> = {}): ChipState {
  return { label, tip: label, enabled: true, checkable: false, checked: false, ...over };
}

export function mockShell(): ShellApi & {
  calls: Call[];
  chipsChanged: MockSignal;
  chipRects: Record<string, Rect>;
  pill: { width: number; height: number; titles: Rect[] } | null;
} {
  const calls: Call[] = [];
  const rail: ShellEntry[] = [
    { id: 'items', glyph: 'items' },
    { separator: true },
    { id: 'start-sketch', glyph: 'sketch' },
    { id: 'extrude', glyph: 'extrude' },
    { id: 'add-shape', glyph: 'shapes' },
    { separator: true },
    { id: 'union', glyph: 'union' },
    { id: 'subtract', glyph: 'subtract' },
    { id: 'intersect', glyph: 'intersect' },
    { separator: true },
    { id: 'delete-selected', glyph: 'delete' },
    { separator: true },
    { id: 'undo', glyph: 'undo' },
    { id: 'redo', glyph: 'redo' },
  ];
  const view: ShellEntry[] = [
    { id: 'orthographic', glyph: 'projection' },
    { id: 'wireframe', glyph: 'wireframe' },
    { id: 'fit-all', glyph: 'fitAll' },
  ];
  const back: ShellEntry[] = [{ id: 'render-mode', glyph: 'back' }];
  const glyphs: Record<string, string> = {};
  for (const e of [...rail, ...view, ...back]) if (e.glyph) glyphs[e.glyph] = dot;
  const shell = {
    calls,
    rail,
    view,
    back,
    glyphs,
    chips: {
      items: chipState('Items', { checkable: true, tip: 'Items\nShow or hide the list of bodies' }),
      'start-sketch': chipState('Start Sketch'),
      extrude: chipState('Extrude', { enabled: false }),
      'add-shape': chipState('Add shape'),
      union: chipState('Union', { enabled: false }),
      subtract: chipState('Subtract', { enabled: false }),
      intersect: chipState('Intersect', { enabled: false }),
      'delete-selected': chipState('Delete Selected', { enabled: false }),
      undo: chipState('Undo', { enabled: false }),
      redo: chipState('Redo', { enabled: false }),
      orthographic: chipState('Orthographic', { checkable: true }),
      wireframe: chipState('Wireframe', { checkable: true }),
      'fit-all': chipState('Fit All'),
      'render-mode': chipState('Render mode', { checkable: true, checked: true }),
    } as Record<string, ChipState>,
    chipsChanged: new MockSignal(),
    wordmark: 'FurnifyMe',
    chipRects: {} as Record<string, Rect>,
    pill: null as { width: number; height: number; titles: Rect[] } | null,
    trigger(id: string) {
      calls.push({ method: 'trigger', args: [id] });
    },
    reportChipRects(rects: Record<string, Rect>) {
      calls.push({ method: 'reportChipRects', args: [rects] });
      shell.chipRects = { ...shell.chipRects, ...rects };
    },
    reportPill(width: number, height: number, titles: Rect[]) {
      calls.push({ method: 'reportPill', args: [width, height, titles] });
      shell.pill = { width, height, titles };
    },
  };
  return shell;
}

export function entry(text: string, over: Partial<MenuEntryData> = {}): MenuEntryData {
  const at = text.indexOf('&');
  return {
    separator: false,
    text: text.replace('&', ''),
    mnemonicAt: at,
    shortcut: '',
    enabled: true,
    checkable: false,
    checked: false,
    radio: false,
    submenu: false,
    ...over,
  };
}
export const separator: MenuEntryData = { separator: true };

export function sampleMenus(): { titles: MenuTitle[]; levels: MenuEntryData[][]; sub: MenuEntryData[] } {
  return {
    titles: [
      { title: 'File', mnemonicAt: 0, enabled: true },
      { title: 'Sketch', mnemonicAt: 0, enabled: true },
      { title: 'Edit', mnemonicAt: 0, enabled: true },
      { title: 'Model', mnemonicAt: 0, enabled: true },
      { title: 'View', mnemonicAt: 0, enabled: true },
      { title: 'Help', mnemonicAt: 0, enabled: true },
    ],
    levels: [
      [
        entry('&New furniture...', { shortcut: 'Ctrl+N' }),
        entry('&Save', { shortcut: 'Ctrl+S', enabled: false }),
        entry('&Autosave', { submenu: true }),
        separator,
        entry('E&xport STEP...'),
        entry('&Close furniture'),
      ],
    ],
    sub: [entry('&Off', { checkable: true, radio: true, checked: true }), entry('&After every change', { checkable: true, radio: true })],
  };
}

export function mockMenu(): MenuApi & { calls: Call[]; changed: MockSignal; set(open: MenuOpen): void } {
  const calls: Call[] = [];
  const sample = sampleMenus();
  const menu = {
    calls,
    titles: sample.titles,
    open: { menu: -1, byKeyboard: false, levels: [] } as MenuOpen,
    changed: new MockSignal(),
    set(open: MenuOpen) {
      menu.open = open;
      menu.changed.emit();
    },
    openMenu(index: number, byKeyboard: boolean) {
      calls.push({ method: 'openMenu', args: [index, byKeyboard] });
      menu.set({ menu: index, byKeyboard, levels: [{ entries: sample.levels[0], current: byKeyboard ? 0 : -1 }] });
    },
    hover(level: number, index: number) {
      calls.push({ method: 'hover', args: [level, index] });
      const levels = menu.open.levels.slice(0, level + 1).map((l) => ({ ...l }));
      if (!levels[level]) return;
      levels[level].current = index;
      const e = levels[level].entries[index];
      if (e?.submenu) levels.push({ entries: sample.sub, current: -1 });
      menu.set({ ...menu.open, levels });
    },
    key(name: string) {
      calls.push({ method: 'key', args: [name] });
    },
    choose(level: number, index: number) {
      calls.push({ method: 'choose', args: [level, index] });
    },
    close() {
      calls.push({ method: 'close', args: [] });
      menu.set({ menu: -1, byKeyboard: false, levels: [] });
    },
  };
  return menu;
}

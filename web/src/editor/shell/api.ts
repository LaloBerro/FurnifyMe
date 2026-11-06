// What the rail, the view cluster, the pill and the menus talk to (migration
// Phase 3i): ShellBridge as `shell`, MenuModel as `menu`, on the editor chrome
// page's channel. Every chip mirrors a QAction and every menu entry is one;
// the page draws, sends gestures and reports what it measured. It decides
// nothing - which entry is live, what a key does, whether an action may run.
import type { Signal } from '../api';

export interface ShellEntry {
  id?: string;
  glyph?: string;
  separator?: boolean;
}

export interface ChipState {
  label: string;
  tip: string;
  enabled: boolean;
  checkable: boolean;
  checked: boolean;
}

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface ShellApi {
  rail: ShellEntry[];
  view: ShellEntry[];
  // Render mode's way out (Phase 7a): one chip, in the Back chip's place.
  back?: ShellEntry[];
  glyphs: Record<string, string>;
  chips: Record<string, ChipState>;
  chipsChanged: Signal;
  wordmark: string;
  trigger(id: string): void;
  reportChipRects(rects: Record<string, Rect>): void;
  reportPill(width: number, height: number, titleRects: Rect[]): void;
}

export interface MenuTitle {
  title: string;
  mnemonicAt: number;
  enabled: boolean;
}

export interface MenuEntryData {
  separator: boolean;
  text?: string;
  mnemonicAt?: number;
  shortcut?: string;
  enabled?: boolean;
  checkable?: boolean;
  checked?: boolean;
  radio?: boolean;
  submenu?: boolean;
}

export interface MenuLevel {
  entries: MenuEntryData[];
  current: number;
}

export interface MenuOpen {
  menu: number; // -1: none
  byKeyboard: boolean;
  levels: MenuLevel[];
}

export interface MenuApi {
  titles: MenuTitle[];
  open: MenuOpen;
  changed: Signal;
  openMenu(index: number, byKeyboard: boolean): void;
  hover(level: number, entry: number): void;
  key(name: string): void;
  choose(level: number, entry: number): void;
  close(): void;
}

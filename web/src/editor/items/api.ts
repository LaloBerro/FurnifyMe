// What the Items card talks to: ItemsModel, the editor chrome page's `items`
// object (migration Phase 3h). `rows`, `state`, `menu` and `drag` are re-read
// on `changed`; `words` and `keys` are sent once. Every decision - a refusal,
// a sentence, which rows show - is C++'s; the page draws and gestures.
import type { Signal } from '../api';

export type RowKind = 'body' | 'outline' | 'folder';

export interface DropTargetData {
  parent: number;
  before: number;
  depth: number;
}

export interface ItemRowData {
  id: number;
  kind: RowKind;
  name: string;
  depth: number;
  parent: number;
  eyeOn: boolean;
  onScreen: boolean;
  muted: boolean;
  grain: '' | 'along' | 'across';
  grainTip?: string;
  grainName?: string;
  marked: boolean;
  current: boolean;
  pending: boolean;
  partial: string; // "2/5" or ''
  partialWords: string;
  open: boolean;
  held: boolean;
  match: [number, number] | null; // the filter's match, as a range - never markup
  carried: boolean;
  accessibleName: string;
  eyeName: string;
  eyeTip: string;
  twistyName?: string;
  twistyTip?: string;
  tip: string;
  above: DropTargetData;
  below: DropTargetData;
}

export interface ItemsState {
  live: boolean;
  filter: string;
  empty: boolean;
  noMatch: string;
  foot: string;
  current: number;
  newFolder: { enabled: boolean; tip: string };
  ledger: { bodies: string; selected: string };
  scroll: { id: number; seq: number };
  rename: { id: number; name: string; seq: number };
}

export interface MenuEntry {
  id: string;
  label: string;
  enabled: boolean;
  tip: string;
  keys: string;
  depth: number;
  children?: MenuEntry[];
}

export interface ItemsMenu {
  open: boolean;
  row?: number;
  entries?: MenuEntry[];
}

export type DropKind = 'none' | 'line' | 'into' | 'top' | 'outside';

export interface ItemsDrag {
  active: boolean;
  refused: boolean;
  status: string;
  target: { kind: DropKind; parent: number; before: number };
  ghost: { kind: string; name: string; more: number };
}

export interface ItemsWords {
  title: string;
  filter: string;
  clearFilter: string;
  empty: string;
  headName: string;
  headTools: string;
  ledgerKeys: string;
  ghostMore: string; // "%1 +%2"
  releaseToCancel: string;
  dragThreshold: number;
  springMs: number;
}

export interface KeyCombo {
  key: string;
  shift: boolean;
  alt: boolean;
  ctrl: boolean;
}

export interface KeyEntry {
  command: string;
  fromFilter: boolean;
  keys: KeyCombo[];
}

export interface ItemsApi {
  rows: ItemRowData[];
  state: ItemsState;
  menu: ItemsMenu;
  drag: ItemsDrag;
  words: ItemsWords;
  keys: KeyEntry[];
  changed: Signal;
  activate(id: number, ctrl: boolean, shift: boolean): void;
  toggleFold(id: number): void;
  openFolder(id: number): void;
  toggleEye(id: number): void;
  toggleGrain(id: number): void;
  setFilter(text: string): void;
  clearFilter(): void;
  beginRename(id: number): void;
  commitRename(id: number, text: string): void;
  cancelRename(): void;
  openMenu(id: number): void;
  chooseMenu(entry: string): void;
  closeMenu(): void;
  newFolder(): void;
  key(command: string, forward: boolean): void;
  beginDrag(id: number, plain: boolean): void;
  dragOver(kind: DropKind, parent: number, before: number): void;
  endDrag(commit: boolean): void;
}

// The list hold (ruling R3) - the chrome's, not the model's.
export interface ListKeys {
  hold: () => void;
  release: () => void;
}

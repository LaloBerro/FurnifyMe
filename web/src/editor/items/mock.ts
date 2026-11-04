// An ItemsModel stand-in for the page tests and the dev server. It records
// what the card asks and decides nothing - the rules are C++'s, tested headless
// (items_model_tests). Its words are the C++ strings verbatim.
import { MockSignal, type Call } from '../../hub/mock';
import type { ItemRowData, ItemsApi, ItemsDrag, ItemsMenu, ItemsState, KeyEntry, RowKind } from './api';

export function itemRow(id: number, name: string, over: Partial<ItemRowData> = {}): ItemRowData {
  const kind: RowKind = over.kind ?? 'body';
  return {
    id,
    kind,
    name,
    depth: 0,
    parent: 0,
    eyeOn: true,
    onScreen: true,
    muted: false,
    grain: kind === 'body' ? 'along' : '',
    grainTip: kind === 'body' ? 'The grain runs along this body — click to turn it' : undefined,
    grainName: kind === 'body' ? 'Grain along' : undefined,
    marked: false,
    current: false,
    pending: false,
    partial: '',
    partialWords: '',
    open: false,
    held: false,
    match: null,
    carried: false,
    accessibleName: name,
    eyeName: `Hide ${name}`,
    eyeTip: kind === 'folder' ? 'Show or hide everything in this folder' : 'Show or hide this body',
    twistyName: kind === 'folder' ? `Open ${name}` : undefined,
    twistyTip: kind === 'folder' ? 'Open this folder' : undefined,
    tip: '',
    above: { parent: 0, before: id, depth: 0 },
    below: { parent: 0, before: 0, depth: 0 },
    ...over,
  };
}

// The list's key table as ItemsModel sends it (session/ItemsKeys).
export const sampleKeys: KeyEntry[] = [
  { command: 'step', fromFilter: false, keys: [{ key: 'ArrowUp', shift: false, alt: false, ctrl: false }, { key: 'ArrowDown', shift: false, alt: false, ctrl: false }] },
  { command: 'extendStep', fromFilter: false, keys: [{ key: 'ArrowUp', shift: true, alt: false, ctrl: false }, { key: 'ArrowDown', shift: true, alt: false, ctrl: false }] },
  { command: 'firstOrLast', fromFilter: false, keys: [{ key: 'Home', shift: false, alt: false, ctrl: false }, { key: 'End', shift: false, alt: false, ctrl: false }] },
  { command: 'foldOrParent', fromFilter: false, keys: [{ key: 'ArrowLeft', shift: false, alt: false, ctrl: false }] },
  { command: 'unfoldOrChild', fromFilter: false, keys: [{ key: 'ArrowRight', shift: false, alt: false, ctrl: false }] },
  { command: 'rename', fromFilter: false, keys: [{ key: 'F2', shift: false, alt: false, ctrl: false }] },
  { command: 'moveUp', fromFilter: false, keys: [{ key: 'ArrowUp', shift: false, alt: true, ctrl: false }] },
  { command: 'moveDown', fromFilter: false, keys: [{ key: 'ArrowDown', shift: false, alt: true, ctrl: false }] },
  { command: 'moveOutOfFolder', fromFilter: false, keys: [{ key: 'ArrowLeft', shift: false, alt: true, ctrl: false }] },
  { command: 'moveIntoFolderAbove', fromFilter: false, keys: [{ key: 'ArrowRight', shift: false, alt: true, ctrl: false }] },
  { command: 'enterList', fromFilter: true, keys: [{ key: 'ArrowDown', shift: false, alt: false, ctrl: false }, { key: 'Enter', shift: false, alt: false, ctrl: false }] },
];

export function sampleRows(): ItemRowData[] {
  return [
    itemRow(7, 'Carcass', { kind: 'folder', open: true, partial: '1/3', partialWords: '1 of 3 bodies in Carcass is selected', twistyName: 'Close Carcass', twistyTip: 'Close this folder', below: { parent: 7, before: 1, depth: 1 } }),
    itemRow(1, 'Side left', { depth: 1, parent: 7, marked: true, current: true, accessibleName: 'Side left, in Carcass', above: { parent: 7, before: 1, depth: 1 }, below: { parent: 7, before: 2, depth: 1 } }),
    itemRow(2, 'Side right', { depth: 1, parent: 7, above: { parent: 7, before: 2, depth: 1 }, below: { parent: 7, before: 0, depth: 1 } }),
    itemRow(8, 'Drawer', { kind: 'folder', depth: 1, parent: 7, above: { parent: 7, before: 8, depth: 1 }, below: { parent: 7, before: 0, depth: 1 } }),
    itemRow(6, 'Outline 01', { kind: 'outline', pending: true, grain: '', tip: 'Outline 01, waiting to extrude', eyeTip: 'Show or hide this outline' }),
    itemRow(4, 'Shelf', { above: { parent: 0, before: 4, depth: 0 }, below: { parent: 0, before: 5, depth: 0 } }),
    itemRow(5, 'Back', { onScreen: false, eyeOn: false, eyeName: 'Show Back', above: { parent: 0, before: 5, depth: 0 } }),
  ];
}

export function mockItems(rows: ItemRowData[] = sampleRows()) {
  const calls: Call[] = [];
  const record = (method: string) => (...args: unknown[]) => {
    calls.push({ method, args });
  };
  const state: ItemsState = {
    live: true,
    filter: '',
    empty: rows.length === 0,
    noMatch: '',
    foot: '',
    current: rows.find((r) => r.current)?.id ?? 0,
    newFolder: { enabled: true, tip: 'Put the selected bodies in a new folder (Ctrl+G)' },
    ledger: { bodies: '5 bodies', selected: '1 selected' },
    scroll: { id: 0, seq: 0 },
    rename: { id: 0, name: '', seq: 0 },
  };
  const menu: ItemsMenu = { open: false };
  const drag: ItemsDrag = {
    active: false,
    refused: false,
    status: '',
    target: { kind: 'none', parent: 0, before: 0 },
    ghost: { kind: '', name: '', more: 0 },
  };
  const api: ItemsApi & { calls: Call[]; changed: MockSignal } = {
    calls,
    rows,
    state,
    menu,
    drag,
    words: {
      title: 'Items',
      filter: 'Filter by name',
      clearFilter: 'Clear filter',
      empty:
        'Nothing to list yet — bodies, outlines and folders you make show up here\n\nPress Ctrl+K and click points on the ground to draw your first outline.',
      headName: 'Name',
      headTools: 'Grain · Eye',
      ledgerKeys: 'Alt+↑↓ moves',
      ghostMore: '%1 +%2',
      releaseToCancel: 'Release to cancel',
      dragThreshold: 6,
      springMs: 700,
    },
    keys: sampleKeys,
    changed: new MockSignal(),
    activate: record('activate'),
    toggleFold: record('toggleFold'),
    openFolder: record('openFolder'),
    toggleEye: record('toggleEye'),
    toggleGrain: record('toggleGrain'),
    setFilter: record('setFilter'),
    clearFilter: record('clearFilter'),
    beginRename: record('beginRename'),
    commitRename: record('commitRename'),
    cancelRename: record('cancelRename'),
    openMenu: record('openMenu'),
    chooseMenu: record('chooseMenu'),
    closeMenu: record('closeMenu'),
    newFolder: record('newFolder'),
    key: record('key'),
    beginDrag: record('beginDrag'),
    dragOver: record('dragOver'),
    endDrag: record('endDrag'),
  };
  return api;
}

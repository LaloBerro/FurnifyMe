// Mocks of the scene's two cards with the bridges' shapes, for the tests and
// the dev server: every request recorded, every state pushable.
import type { AddPieceApi, AddPieceState, PieceRow, PiecesApi, PiecesState } from './api';

type Calls = { op: string; args: unknown[] }[];

function signal<T>() {
  const listeners: ((v: T) => void)[] = [];
  return { connect: (fn: (v: T) => void) => listeners.push(fn), emit: (v: T) => listeners.forEach((fn) => fn(v)) };
}

export function piecesState(rows: Partial<PieceRow>[] = []): PiecesState {
  return {
    title: 'Pieces',
    empty: 'No pieces yet — add a piece from the library',
    add: { text: 'Add piece', tip: 'Put a furniture from the library into this scene', enabled: true },
    words: {
      removeTip: 'Take this piece out of the scene',
      armed: 'Remove?',
      armedTip: 'Click again to take it out of the scene',
      eyeTip: 'Show or hide this piece',
    },
    rows: rows.map((r, i) => ({ id: i + 1, name: `Piece ${i + 1}`, visible: true, reason: '', selected: false, armed: false, ...r })),
  };
}

export function mockPieces(state: PiecesState = piecesState()) {
  const calls: Calls = [];
  const changed = signal<PiecesState>();
  const api: PiecesApi & { calls: Calls; push(s: PiecesState): void } = {
    state,
    stateChanged: { connect: (fn) => changed.connect(fn) },
    calls,
    select: (id) => calls.push({ op: 'select', args: [id] }),
    rename: (id, name) => calls.push({ op: 'rename', args: [id, name] }),
    toggleVisible: (id, v) => calls.push({ op: 'toggleVisible', args: [id, v] }),
    remove: (id) => calls.push({ op: 'remove', args: [id] }),
    disarm: () => calls.push({ op: 'disarm', args: [] }),
    addPiece: () => calls.push({ op: 'addPiece', args: [] }),
    push(s) {
      api.state = s;
      changed.emit(s);
    },
  };
  return api;
}

export function addPieceState(shown = true, entries = [{ id: 'f1', name: 'Bench' }], seed = 1): AddPieceState {
  return {
    shown,
    seed,
    title: 'Which furniture?',
    empty: 'The library is empty — make a furniture first',
    library: 'Go to the library',
    keyHint: 'Esc closes',
    entries,
  };
}

export function mockAddPiece(state: AddPieceState = addPieceState(false)) {
  const calls: Calls = [];
  const changed = signal<AddPieceState>();
  const api: AddPieceApi & { calls: Calls; push(s: AddPieceState): void } = {
    state,
    stateChanged: { connect: (fn) => changed.connect(fn) },
    calls,
    choose: (id) => calls.push({ op: 'choose', args: [id] }),
    goToLibrary: () => calls.push({ op: 'goToLibrary', args: [] }),
    close: () => calls.push({ op: 'close', args: [] }),
    push(s) {
      api.state = s;
      changed.emit(s);
    },
  };
  return api;
}

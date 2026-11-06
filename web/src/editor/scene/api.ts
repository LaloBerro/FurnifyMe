// The scene window's two cards (migration Phase 5b): ScenePiecesModel as
// `pieces` and AddPieceModel as `addPiece`, on the chrome page the scene
// window puts over its viewport. Each request leaves as the Qt card's own
// signal; the words are the Qt cards' own.
import type { Signal } from '../api';

export interface PieceRow {
  id: number;
  name: string; // the user's own
  visible: boolean;
  reason: string; // a piece whose furniture cannot be read: why, where the name goes
  selected: boolean;
  armed: boolean; // Remove was clicked once
}

export interface PiecesState {
  title: string;
  empty: string;
  add: { text: string; tip: string; enabled: boolean };
  words: { removeTip: string; armed: string; armedTip: string; eyeTip: string };
  rows: PieceRow[];
}

export interface PiecesApi {
  state: PiecesState;
  stateChanged: Signal<[PiecesState]>;
  select(id: number): void;
  rename(id: number, name: string): void;
  toggleVisible(id: number, visible: boolean): void;
  remove(id: number): void;
  disarm(): void;
  addPiece(): void;
}

export interface AddPieceState {
  shown: boolean;
  seed: number;
  title: string;
  empty: string;
  library: string;
  keyHint: string;
  entries: { id: string; name: string }[];
}

export interface AddPieceApi {
  state: AddPieceState;
  stateChanged: Signal<[AddPieceState]>;
  choose(id: string): void;
  goToLibrary(): void;
  close(): void;
}

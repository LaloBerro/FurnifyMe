// What the Versions card talks to: VersionsModel, the editor chrome page's
// `versions` object (migration Phase 3c). `versions`, `canSave` and
// `rowsEnabled` are re-read on `changed`; `words` is sent once.
import type { Signal } from '../api';

export interface VersionEntry {
  name: string;
  savedText: string;
  saved: number;
  thumbUrl: string; // '' when the version has no picture
}

export interface VersionsWords {
  title: string;
  add: string;
  empty: string;
  compare: string;
  restore: string;
  delete: string;
  deleteArmed: string;
  deleteMs: number;
  close: string;
}

export interface VersionsApi {
  versions: VersionEntry[];
  canSave: boolean;
  rowsEnabled: boolean;
  words: VersionsWords;
  changed: Signal;
  newRequested: Signal;
  defaultName(): Promise<string>;
  save(name: string): Promise<boolean>;
  restore(name: string): unknown;
  compare(name: string): unknown;
  remove(name: string): unknown;
  close(): void;
}

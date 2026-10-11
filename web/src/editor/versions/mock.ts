// A VersionsModel stand-in for the page tests and the dev server. Its words
// are the C++ strings verbatim (VersionsModel.cpp).
import { MockSignal, type Call } from '../../hub/mock';
import type { VersionEntry, VersionsApi } from './api';

export function mockVersions(list: VersionEntry[] = []) {
  const calls: Call[] = [];
  const api: VersionsApi & { calls: Call[]; changed: MockSignal; newRequested: MockSignal; saveAnswer: boolean } = {
    calls,
    saveAnswer: true,
    versions: list,
    canSave: true,
    rowsEnabled: true,
    words: {
      title: 'Versions',
      add: 'Save a new version',
      empty: 'No versions yet.\n\nPress + to keep a named snapshot you can come back to.',
      compare: 'Compare',
      restore: 'Restore',
      delete: 'Delete',
      deleteArmed: 'Delete — click again',
      deleteMs: 4000,
      close: 'Close this panel',
    },
    changed: new MockSignal(),
    newRequested: new MockSignal(),
    defaultName: async () => {
      calls.push({ method: 'defaultName', args: [] });
      return `Version ${api.versions.length + 1}`;
    },
    save: async (name: string) => {
      calls.push({ method: 'save', args: [name] });
      return api.saveAnswer;
    },
    restore: (name: string) => calls.push({ method: 'restore', args: [name] }),
    compare: (name: string) => calls.push({ method: 'compare', args: [name] }),
    remove: (name: string) => calls.push({ method: 'remove', args: [name] }),
    close: () => {
      calls.push({ method: 'close', args: [] });
    },
  };
  return api;
}

export const sampleVersions: VersionEntry[] = [
  { name: 'Version 1', savedText: '10/11/26, 9:12 AM', saved: 1, thumbUrl: '' },
  { name: 'Shelf added', savedText: '10/11/26, 11:40 AM', saved: 2, thumbUrl: '' },
];

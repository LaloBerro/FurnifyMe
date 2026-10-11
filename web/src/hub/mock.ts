// Mocks of the hub's three channel objects, for tests and the dev server.
// Every call is recorded so a test can assert what the page asked C++ for.

import type { HubApi, HubEntryData, Signal, ThemeApi, WindowApi } from './api';

export class MockSignal<A extends unknown[] = []> implements Signal<A> {
  private fns: ((...args: A) => void)[] = [];
  connect(fn: (...args: A) => void) {
    this.fns.push(fn);
  }
  emit(...args: A) {
    for (const fn of this.fns) fn(...args);
  }
}

export type Call = { method: string; args: unknown[] };

export function entry(id: string, name: string, edited = 0, scene = false): HubEntryData {
  const initial = [...name].find((c) => /[\p{L}\p{N}]/u.test(c))?.toUpperCase() ?? '';
  return {
    id,
    name,
    scene,
    thumbUrl: '',
    edited,
    editedText: edited ? 'Today, 9:05 AM' : 'Never saved',
    initial,
  };
}

export function mockHub(furniture: HubEntryData[] = [], scenes: HubEntryData[] = []) {
  const calls: Call[] = [];
  const record = (method: string, ...args: unknown[]) => calls.push({ method, args });
  const usedInNames: Record<string, string[]> = {};
  const hub: HubApi & {
    calls: Call[];
    usedInNames: Record<string, string[]>;
    changed: MockSignal;
    failure: MockSignal<[string]>;
    note: MockSignal<[string]>;
  } = {
    furniture,
    scenes,
    calls,
    usedInNames,
    changed: new MockSignal(),
    failure: new MockSignal<[string]>(),
    note: new MockSignal<[string]>(),
    refresh: () => record('refresh'),
    suggestedName: async () => {
      record('suggestedName');
      return 'Furniture 04';
    },
    usedIn: async (id) => {
      record('usedIn', id);
      return usedInNames[id] ?? [];
    },
    createFurniture: (name) => record('createFurniture', name),
    createScene: () => record('createScene'),
    rename: (id, scene, name) => record('rename', id, scene, name),
    remove: (id, scene) => record('remove', id, scene),
    choose: (id, scene) => record('choose', id, scene),
  };
  return hub;
}

export function mockWindow() {
  const calls: Call[] = [];
  const record = (method: string, ...args: unknown[]) => calls.push({ method, args });
  const win: WindowApi & {
    calls: Call[];
    maximizedChanged: MockSignal<[boolean]>;
    maxHovered: MockSignal<[boolean]>;
    maxPressed: MockSignal<[boolean]>;
  } = {
    calls,
    maximized: false,
    maximizedChanged: new MockSignal<[boolean]>(),
    maxHovered: new MockSignal<[boolean]>(),
    maxPressed: new MockSignal<[boolean]>(),
    minimize: () => record('minimize'),
    toggleMaximize: () => record('toggleMaximize'),
    close: () => record('close'),
    reportChrome: (chrome) => record('reportChrome', chrome),
  };
  return win;
}

export function mockTheme(): ThemeApi & { changed: MockSignal } {
  return { tokens: {}, changed: new MockSignal() };
}

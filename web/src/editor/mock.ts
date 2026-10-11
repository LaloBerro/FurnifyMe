import type { ChromeApi, StatusState, ToastKind } from './api';
import type { Rect } from './solver';
import { MockSignal } from '../hub/mock';

export function mockChrome() {
  const calls: { method: string; args: unknown[] }[] = [];
  const chrome: ChromeApi & {
    calls: typeof calls;
    toast: MockSignal<[string, ToastKind, boolean, number, number?, number?, boolean?]>;
    documentMoved: MockSignal<[number]>;
    undoEnabledChanged: MockSignal<[boolean]>;
    obstaclesChanged: MockSignal;
    statusChanged: MockSignal;
  } = {
    calls,
    toast: new MockSignal<[string, ToastKind, boolean, number, number?, number?, boolean?]>(),
    documentMoved: new MockSignal<[number]>(),
    undoEnabled: true,
    undoEnabledChanged: new MockSignal<[boolean]>(),
    obstacles: [] as Rect[],
    obstaclesChanged: new MockSignal(),
    status: { left: '', right: '', name: '', unsaved: false, shown: true } as StatusState,
    statusChanged: new MockSignal(),
    reportHitRects: (rects) => calls.push({ method: 'reportHitRects', args: [rects] }),
    undo: (stamp) => calls.push({ method: 'undo', args: [stamp] }),
    ready: () => calls.push({ method: 'ready', args: [] }),
  };
  return chrome;
}

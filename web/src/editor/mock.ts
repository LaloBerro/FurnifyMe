import type { QuestionState } from './Question.svelte';
import type { SheetState } from './Sheet.svelte';
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
    cardsChanged: MockSignal;
    questionChanged: MockSignal<[QuestionState]>;
    sheetChanged: MockSignal<[SheetState]>;
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
    cards: {},
    cardsChanged: new MockSignal(),
    question: { asking: false } as QuestionState,
    questionChanged: new MockSignal<[QuestionState]>(),
    sheet: { shown: false } as SheetState,
    sheetChanged: new MockSignal<[SheetState]>(),
    answerQuestion: (which) => calls.push({ method: 'answerQuestion', args: [which] }),
    answerNameQuestion: (which, name) => calls.push({ method: 'answerNameQuestion', args: [which, name] }),
    nameQuestionEdited: (name) => calls.push({ method: 'nameQuestionEdited', args: [name] }),
    reportHitRects: (rects) => calls.push({ method: 'reportHitRects', args: [rects] }),
    undo: (stamp) => calls.push({ method: 'undo', args: [stamp] }),
    ready: () => calls.push({ method: 'ready', args: [] }),
    claimKeyboard: () => calls.push({ method: 'claimKeyboard', args: [] }),
    releaseKeyboard: () => calls.push({ method: 'releaseKeyboard', args: [] }),
    claimKeyboardFor: (card) => calls.push({ method: 'claimKeyboardFor', args: [card] }),
    releaseKeyboardFor: (card) => calls.push({ method: 'releaseKeyboardFor', args: [card] }),
    holdListKeys: (card) => calls.push({ method: 'holdListKeys', args: [card] }),
    releaseListKeys: (card) => calls.push({ method: 'releaseListKeys', args: [card] }),
    reportCardHeight: (name, height) => calls.push({ method: 'reportCardHeight', args: [name, height] }),
  };
  return chrome;
}

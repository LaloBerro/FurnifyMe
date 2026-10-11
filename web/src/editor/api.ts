// What the editor's chrome page talks to: EditorChrome's `chrome` object on
// the web channel (ThemeBridge as `theme` beside it). A mock with the same
// shape serves the tests and the dev server (mock.ts).
import type { Rect } from './solver';

export interface Signal<A extends unknown[] = []> {
  connect(fn: (...args: A) => void): void;
}

export type ToastKind = 'note' | 'failure';

export interface StatusState {
  left: string; // the left channel's whole sentence
  right: string; // the mode and the next step
  name: string; // the open furniture's name ('' when none)
  unsaved: boolean;
  shown: boolean; // View -> Bottom bar, and not in render mode
}

export interface ChromeApi {
  // C++ -> page
  // pointerX/Y: where the pointer last was over the viewport (-1 when not
  // over it), so a toast landing under a resting pointer starts paused.
  // undoOpen: whether Undo is open as the message arrives - it rides with the
  // message because QWebChannel batches a property's NOTIFY on a timer.
  toast: Signal<[text: string, kind: ToastKind, offersUndo: boolean, stamp: number, pointerX?: number, pointerY?: number, undoOpen?: boolean]>;
  documentMoved: Signal<[stamp: number]>;
  undoEnabled: boolean;
  undoEnabledChanged: Signal<[enabled: boolean]>;
  obstacles: Rect[];
  obstaclesChanged: Signal;
  status: StatusState;
  statusChanged: Signal;
  // page -> C++
  reportHitRects(rects: Rect[]): void;
  // stamp: the change the toast names; C++ refuses it once the document has
  // moved past that change.
  undo(stamp: number): void;
  // The page is listening: until this arrives - and again after its render
  // process dies - C++ reports through the Qt toast and status bar, so no
  // Failure can land on a page that is not there.
  ready(): void;
}

import type { QuestionState } from './Question.svelte';
// What the editor's chrome page talks to: EditorChrome's `chrome` object on
// the web channel (ThemeBridge as `theme` beside it). A mock with the same
// shape serves the tests and the dev server (mock.ts).
import type { Rect } from './solver';
import type { CardRect } from './settings/api';

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
  // Where each HTML card's slot stands in the viewport (HtmlCardSlot), by
  // card name, and whether it is shown - the overlay's call, not the page's.
  cards: Record<string, CardRect>;
  cardsChanged: Signal;
  // page -> C++
  reportHitRects(rects: Rect[]): void;
  // stamp: the change the toast names; C++ refuses it once the document has
  // moved past that change.
  undo(stamp: number): void;
  // The page is listening: until this arrives - and again after its render
  // process dies - C++ reports through the Qt toast and status bar, so no
  // Failure can land on a page that is not there.
  ready(): void;
  // A typed field is being edited: the page takes the keyboard, and gives
  // it back after (ruling R5 - the view is otherwise Qt::NoFocus).
  claimKeyboard(): void;
  releaseKeyboard(): void;
  // The same, owned by one card (Phase 3c review I2): a card going away gives
  // back only its own claims.
  claimKeyboardFor(card: string): void;
  releaseKeyboardFor(card: string): void;
  // A card that sizes itself (the Versions drawer, Phase 3c) says how tall it
  // is; its slot takes the height and the overlay re-stacks around it.
  reportCardHeight(name: string, height: number): void;
  // The close question (Phase 3f): read once at load, then carried WITH each
  // change - a property's NOTIFY would arrive a batch late.
  question?: QuestionState;
  questionChanged?: Signal<[QuestionState]>;
  // "save", "discard" or "keep" - answered through the Qt card's own answer().
  answerQuestion?(which: 'save' | 'discard' | 'keep'): void;
  // The name question (Phase 3g, kind "name"): "create" carries the name the
  // field holds WITH the answer; "cancel" carries none. Every edit is
  // mirrored too, so the Qt card has the typed name if the page goes away.
  answerNameQuestion?(which: 'create' | 'cancel', name: string): void;
  nameQuestionEdited?(name: string): void;
}

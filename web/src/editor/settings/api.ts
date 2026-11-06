// What the Settings card talks to: SettingsBridge, the editor chrome page's
// `settings` object (migration Phase 3b). `layout` is sent once; `spec` and
// `actions` are re-read on their change signals, and nothing on the page
// caches either across one.
import type { Signal } from '../api';

export interface TabSpec {
  name: string;
  tip: string;
}

export interface NumberSpec {
  id: string;
  label: string;
  min: number;
  max: number;
  step: number;
  decimals: number;
  suffix: string;
  tip: string;
}

export interface TokenSpec {
  id: string;
  name: string;
  group: string; // the heading this token opens, '' for every other
}

export interface FamilySpec {
  shown: string;
  family: string;
}

export interface RowSpec {
  kind: 'toggle' | 'choice' | 'binary' | 'number';
  tab: number;
  label: string;
  actions: string[];
  options: string[];
  stacked: boolean;
  number: string;
}

export interface SettingsWords {
  title: string;
  close: string;
  save: string;
  saveTip: string;
  load: string;
  loadTip: string;
  reset: string;
  resetArmed: string;
  resetTip: string;
  pickTip: string; // "%1" is the token's name
  fontLabel: string;
  fontTip: string;
  resetMs: number;
}

export interface SettingsLayout {
  tabs: TabSpec[];
  numbers: NumberSpec[];
  tokens: TokenSpec[];
  families: FamilySpec[];
  rows: RowSpec[];
  words: SettingsWords;
}

export interface SettingsSpec {
  colours: Record<string, string>;
  numbers: Record<string, number>;
  fontFamily: string;
  fontShown: string;
}

export interface ActionState {
  id: string;
  text: string;
  tooltip: string;
  enabled: boolean;
  checkable: boolean;
  checked: boolean;
}

export interface SettingsApi {
  layout: SettingsLayout;
  spec: SettingsSpec;
  specChanged: Signal;
  actions: ActionState[];
  actionsChanged: Signal;
  setNumber(id: string, value: number): void;
  setColour(id: string, hex: string): void;
  setFontFamily(family: string): void;
  pickColour(id: string): void;
  reset(): void;
  saveColours(): void;
  loadColours(): void;
  trigger(actionId: string): unknown;
}

// The chrome's keyboard claim (EditorChrome): a typed field takes the keyboard
// while it is being edited and gives it back after - the view is otherwise
// Qt::NoFocus, so a click on a chip never takes keys from the viewport.
export interface Keyboard {
  claimKeyboard(): void;
  releaseKeyboard(): void;
}

export interface CardRect {
  x: number;
  y: number;
  w: number;
  h: number;
  shown?: boolean;
  // A gesture chip's own Qt widget, not a slot (Phase 4a, ruling R2).
  chip?: boolean;
  // A glued chip while the camera moves (Phase 4b): not drawn, no press.
  moving?: boolean;
}

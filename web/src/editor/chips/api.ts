// What the gesture chips talk to (migration Phase 4a): ChipsBridge as `chips`
// on the editor chrome page's channel. A face is what a Qt chip SAYS - rows of
// parts read off its real controls (ruling R1) - and every request lands in
// those controls, so the Qt chip decides everything: what a value means,
// whether Enter may apply, when the gesture ends.
import type { Signal } from '../api';

export type Tone = 'title' | 'label' | 'hint' | 'value' | 'muted' | 'danger';

export interface TextPart {
  kind: 'text';
  text: string;
  tone: Tone;
  user?: boolean; // the user's own words (a piece's name) - not this app's copy
}

export interface FieldPart {
  kind: 'field';
  id: string;
  label: string; // shown before the field; '' shows none
  name?: string; // what a screen reader calls it (the label by default)
  value: string;
  tip: string;
  enabled: boolean;
  stacked?: boolean; // the label above the field, the field the cell's width (the joint card)
  invalid?: boolean; // this field alone does not read (the joint card marks fields one by one)
}

export interface ButtonPart {
  kind: 'button' | 'toggle';
  id: string;
  text: string;
  tip: string;
  enabled: boolean;
  checked: boolean;
  // How it is drawn (Phase 4c): a segment (pressed while checked, the cell's
  // width, an optional label above it), a link, or a menu button.
  look?: 'segment' | 'link' | 'menu';
  label?: string;
  user?: boolean;
  reason?: string; // a menu entry the joint's contact refuses, and why
}

export interface RulePart {
  kind: 'rule';
}

// A menu drawn inside its card (the joint card's kinds, Phase 4c): groups of
// entries, the current one checked, a refused one disabled with its reason.
export interface MenuPart {
  kind: 'menu';
  id: string;
  note: string;
  groups: { title: string; entries: ButtonPart[] }[];
}

export type Part = TextPart | FieldPart | ButtonPart | RulePart | MenuPart;

export interface Face {
  rows: Part[][];
  // The input does not read: the card is outlined in danger.
  invalid: boolean;
  // Moves with each new gesture: the fields are seeded from the face again
  // and `focus` names the one to type in (ruling R5).
  seed: number;
  focus: string;
}

export interface ChipsApi {
  faces: Record<string, Face>;
  // Carries the faces WITH it - a property's NOTIFY arrives a batch late.
  facesChanged: Signal<[Record<string, Face>]>;
  edit(chip: string, id: string, text: string): void;
  press(chip: string, id: string): void;
  // Enter on the page: what its fields hold rides with it (ruling R3).
  commit(chip: string, values: Record<string, string>): void;
  cancel(chip: string): void;
  reportSize(chip: string, width: number, height: number): void;
}

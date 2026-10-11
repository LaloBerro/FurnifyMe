// What the Joints card talks to: JointsModel, the editor chrome page's
// `joints` object (migration Phase 3d). `rows`, `live` and `enabled` are
// re-read on `changed`; `words` is sent once. Every string a row says comes
// from C++ (JointRows) - the page decides only where it goes.
import type { Signal } from '../api';

export interface JointMark {
  at: number; // along the run, 0..1
  text: string; // the number, its unit left to the shared line
}

export interface JointPiece {
  name: string; // the user's own words
  depthText: string; // "drill 13.5 mm", "18 × 6 mm"
  band: boolean;
  marks: JointMark[];
  bandStart: JointMark;
  bandEnd: JointMark;
  writtenLine: string; // what the row writes when it cannot draw a ruler
}

export interface JointRowData {
  id: number;
  broken: boolean;
  selected: boolean;
  expanded: boolean;
  fastener: boolean;
  edgeNamed: boolean;
  nameA: string;
  nameB: string;
  kindLine: string; // or "Broken — <reason>"
  caveat: string;
  edge: string; // the edge's word, or the sentence saying none applies
  unit: string;
  sharedDrawn: string;
  sharedWritten: string;
  pieces: JointPiece[];
}

export interface JointsWords {
  title: string;
  empty: string;
  deleteTip: string;
  close: string;
}

export interface JointsApi {
  rows: JointRowData[];
  live: boolean;
  enabled: boolean;
  words: JointsWords;
  changed: Signal;
  activate(jointId: number): unknown;
  remove(jointId: number): unknown;
  close(): unknown;
}

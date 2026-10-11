// A JointsModel stand-in for the page tests and the dev server. Its words are
// the C++ strings verbatim (JointRows.cpp).
import { MockSignal, type Call } from '../../hub/mock';
import type { JointPiece, JointRowData, JointsApi } from './api';

const mark = (at: number, text: string) => ({ at, text });

export function dowelPiece(name: string, marks = [mark(0.12, '37'), mark(0.5, '150'), mark(0.88, '263')]): JointPiece {
  return {
    name,
    depthText: 'drill 13.5 mm',
    band: false,
    marks,
    bandStart: mark(0, ''),
    bandEnd: mark(0, ''),
    writtenLine: `from the front edge: ${marks.map((m) => m.text).join(' · ')} mm`,
  };
}

export function dowelRow(id: number, over: Partial<JointRowData> = {}): JointRowData {
  return {
    id,
    broken: false,
    selected: false,
    expanded: false,
    fastener: true,
    edgeNamed: true,
    nameA: 'Side left',
    nameB: 'Shelf',
    kindLine: 'Dowel, 3 × 8 mm',
    caveat: '',
    edge: 'front',
    unit: 'mm',
    sharedDrawn: 'mm from the front edge · inset 9 mm from the face',
    sharedWritten: 'inset 9 mm from the face',
    pieces: [dowelPiece('Side left'), dowelPiece('Shelf')],
    ...over,
  };
}

export function brokenRow(id: number): JointRowData {
  return dowelRow(id, {
    broken: true,
    kindLine: "Broken — these two pieces don't meet — move the pieces so they meet",
    edge: '',
    sharedDrawn: '',
    sharedWritten: '',
    pieces: [],
  });
}

export function mockJoints(rows: JointRowData[] = []) {
  const calls: Call[] = [];
  const api: JointsApi & { calls: Call[]; changed: MockSignal } = {
    calls,
    rows,
    live: true,
    enabled: true,
    words: {
      title: 'Joints',
      empty: 'No joints yet.\n\nSelect two pieces that meet and press J to plan one.',
      deleteTip: 'Delete this joint\nOne undo brings it back.',
      close: 'Close this panel',
    },
    changed: new MockSignal(),
    activate: (id: number) => calls.push({ method: 'activate', args: [id] }),
    remove: (id: number) => calls.push({ method: 'remove', args: [id] }),
    close: () => calls.push({ method: 'close', args: [] }),
  };
  return api;
}

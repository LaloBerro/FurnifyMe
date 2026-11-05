// A ChipsApi with the same shape as ChipsBridge, for the tests and the dev
// server: it records every request and lets a test push faces.
import type { ChipsApi, Face } from './api';

type Listener = (faces: Record<string, Face>) => void;

export interface MockChips extends ChipsApi {
  calls: { op: string; chip: string; args: unknown[] }[];
  push(faces: Record<string, Face>): void;
}

export function mockChips(faces: Record<string, Face> = {}): MockChips {
  const listeners: Listener[] = [];
  const api: MockChips = {
    faces,
    calls: [],
    facesChanged: { connect: (fn: Listener) => listeners.push(fn) },
    edit: (chip, id, text) => api.calls.push({ op: 'edit', chip, args: [id, text] }),
    press: (chip, id) => api.calls.push({ op: 'press', chip, args: [id] }),
    commit: (chip, values) => api.calls.push({ op: 'commit', chip, args: [values] }),
    cancel: (chip) => api.calls.push({ op: 'cancel', chip, args: [] }),
    reportSize: (chip, w, h) => api.calls.push({ op: 'reportSize', chip, args: [w, h] }),
    push(next) {
      api.faces = next;
      for (const fn of listeners) fn(next);
    },
  };
  return api;
}

export function booleanFace(keep = false): Face {
  return {
    rows: [
      [
        { kind: 'text', text: 'Subtract', tone: 'title' },
        { kind: 'toggle', id: 'keep', text: 'Keep the other bodies', tip: 'Leave the bodies', enabled: true, checked: keep },
        { kind: 'text', text: 'Enter applies — Esc cancels', tone: 'hint' },
      ],
    ],
    invalid: false,
    seed: 0,
    focus: '',
  };
}

export function slatsFace(seed = 1, width = '40', reason = ''): Face {
  const rows: Face['rows'] = [
    [
      { kind: 'field', id: 'width', label: 'Width', value: width, tip: 'How wide each slat is, in mm', enabled: true },
      { kind: 'field', id: 'gap', label: 'Gap', value: '10', tip: 'The smallest gap', enabled: true },
      { kind: 'field', id: 'depth', label: 'Depth', value: '18', tip: 'How far the slats stand out', enabled: true },
    ],
    [
      { kind: 'button', id: 'flip', text: 'Flip', tip: 'Turn the slats the other way', enabled: true, checked: false },
      { kind: 'text', text: '12 slats', tone: 'muted' },
      { kind: 'text', text: 'Enter applies — Esc cancels', tone: 'hint' },
    ],
  ];
  if (reason) rows.push([{ kind: 'text', text: reason, tone: 'danger' }]);
  return { rows, invalid: reason !== '', seed, focus: 'width' };
}

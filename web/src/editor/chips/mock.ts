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
    slide: (chip, id, value) => api.calls.push({ op: 'slide', chip, args: [id, value] }),
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

export function extrudeFace(seed = 1, height = '10'): Face {
  return {
    rows: [
      [{ kind: 'text', text: 'Extrude height (mm)', tone: 'label' }],
      [{ kind: 'field', id: 'height', label: '', name: 'Extrude height (mm)', value: height, tip: '', enabled: true }],
      [{ kind: 'text', text: 'Enter applies — Esc cancels', tone: 'hint' }],
    ],
    invalid: false,
    seed,
    focus: 'height',
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

export function jointFace(menuOpen = false, moreOpen = true): Face {
  const rows: Face['rows'] = [
    [
      { kind: 'button', id: 'kind', text: 'Dowel ▾', tip: 'Switch this joint to another kind', enabled: true, checked: false, look: 'menu' },
      { kind: 'text', text: 'Side ↔ Shelf', tone: 'muted', user: true },
    ],
  ];
  if (menuOpen)
    rows.push([
      {
        kind: 'menu',
        id: 'kinds',
        note: '',
        groups: [
          {
            title: 'Fasteners',
            entries: [
              { kind: 'button', id: 'kind:0', text: 'Dowel', tip: '', enabled: true, checked: true, reason: '' },
              { kind: 'button', id: 'kind:4', text: 'Screw', tip: '', enabled: true, checked: false, reason: '' },
            ],
          },
          {
            title: 'Interlocks',
            entries: [
              { kind: 'button', id: 'kind:9', text: 'Half-lap', tip: '', enabled: false, checked: false, reason: 'The pieces only touch at an end' },
            ],
          },
        ],
      },
    ]);
  rows.push([{ kind: 'field', id: 'count', label: 'Count', value: '2', tip: '', enabled: true, stacked: true, invalid: false }]);
  rows.push([{ kind: 'button', id: 'more', text: moreOpen ? 'Fewer ▴' : 'More ▾', tip: '', enabled: true, checked: false, look: 'link' }]);
  if (moreOpen) {
    rows.push([{ kind: 'rule' }]);
    rows.push([
      { kind: 'field', id: 'size', label: 'Size (mm)', value: '8', tip: '', enabled: true, stacked: true, invalid: true },
      { kind: 'button', id: 'stopped', text: 'Off', tip: '', enabled: true, checked: false, look: 'segment', label: 'Stopped' },
    ]);
    rows.push([
      { kind: 'button', id: 'host0', text: 'Side', tip: '', enabled: true, checked: true, look: 'segment', user: true },
      { kind: 'button', id: 'host1', text: 'Shelf', tip: '', enabled: true, checked: false, look: 'segment', user: true },
    ]);
  }
  rows.push([{ kind: 'text', text: 'Enter applies — Esc cancels', tone: 'hint' }]);
  return { rows, invalid: false, seed: 1, focus: '' };
}

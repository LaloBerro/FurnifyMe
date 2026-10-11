// The Joints card (migration Phase 3d) against a mock JointsModel: the rows,
// broken first; a row's click; the open row's rulers and its written fallback;
// delete; the empty state; and the words.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Joints from '../src/editor/joints/Joints.svelte';
import { brokenRow, dowelPiece, dowelRow, mockJoints } from '../src/editor/joints/mock';
import { bannedWordsIn } from './banned';

const rect = { x: 60, y: 300, w: 240, h: 200, shown: true };
const measure = (t: string) => t.length * 6;

function mount(api = mockJoints([brokenRow(2), dowelRow(1)])) {
  const heights: number[] = [];
  const view = render(Joints, {
    props: { joints: api, rect, report: (_n: string, h: number) => heights.push(h), measure, stripWidth: 212 },
  });
  return { api, view, heights };
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const all = (sel: string) => [...document.querySelectorAll<HTMLElement>(sel)];
const calls = (api: ReturnType<typeof mockJoints>, method: string) => api.calls.filter((c) => c.method === method);

async function settle() {
  for (let i = 0; i < 3; i++) await tick();
}

afterEach(() => cleanup());

describe('the Joints card', () => {
  it('lists the rows in the order given - broken first, as C++ sorts them', () => {
    mount();
    expect(all('.joint').map((r) => r.dataset.joint)).toEqual(['2', '1']);
    expect(q('.joints-card .count')!.textContent).toBe('2');
  });

  it('a broken row is marked, shows its reason, and carries no number', () => {
    mount(mockJoints([brokenRow(2)]));
    const row = q('.joint[data-joint="2"]')!;
    expect(row.classList.contains('broken')).toBe(true);
    expect(row.querySelector('.kind')!.textContent).toContain("these two pieces don't meet");
    expect(row.querySelector('.strip')).toBeNull();
    expect(/\d/.test(row.querySelector('.kind')!.textContent!)).toBe(false);
  });

  it('a click asks C++ to open the row and select its joint, once', async () => {
    const { api } = mount();
    await fireEvent.click(q('.joint[data-joint="1"]')!, { detail: 1 });
    expect(calls(api, 'activate')).toEqual([{ method: 'activate', args: [1] }]);
  });

  it("a double-click's second click asks nothing (Review Focus 4)", async () => {
    const { api } = mount();
    await fireEvent.click(q('.joint[data-joint="1"]')!, { detail: 1 });
    await fireEvent.click(q('.joint[data-joint="1"]')!, { detail: 2 });
    expect(calls(api, 'activate').length).toBe(1);
  });

  it('delete shows only on an open row, and its click never toggles the row', async () => {
    const api = mockJoints([dowelRow(1, { expanded: true, selected: true }), dowelRow(3)]);
    mount(api);
    expect(q('.joint[data-joint="3"] .delete')).toBeNull();
    const del = q('.joint[data-joint="1"] .delete')!;
    expect(del.getAttribute('title')).toBe('Delete this joint\nOne undo brings it back.');
    await fireEvent.click(del, { detail: 1 });
    expect(calls(api, 'remove')).toEqual([{ method: 'remove', args: [1] }]);
    expect(calls(api, 'activate')).toEqual([]);
  });

  it('delete is shut while the drawer is not enabled (sketching, render mode)', () => {
    const api = mockJoints([dowelRow(1, { expanded: true })]);
    api.enabled = false;
    mount(api);
    expect((q('.joint[data-joint="1"] .delete') as HTMLButtonElement).disabled).toBe(true);
  });

  it('an open fastener row draws one ruler per piece, with a tick per dowel', () => {
    mount(mockJoints([dowelRow(1, { expanded: true })]));
    const strips = all('.joint[data-joint="1"] .strip');
    expect(strips.length).toBe(2);
    for (const s of strips) {
      expect(s.querySelectorAll('.tick').length).toBe(3);
      expect(s.querySelectorAll('.tick-label').length).toBe(3);
      expect(s.querySelector('.bar')).not.toBeNull();
      expect(s.querySelector('.piece-name')!.textContent).toMatch(/Side left|Shelf/);
      expect(s.querySelector('.depth')!.textContent).toBe('drill 13.5 mm');
    }
    expect(q('.joint[data-joint="1"] .shared')!.textContent).toBe('mm from the front edge · inset 9 mm from the face');
    expect(q('.joint[data-joint="1"] .written')).toBeNull();
  });

  it('a housing row draws a band', () => {
    const band = {
      ...dowelPiece('Side left'),
      band: true,
      marks: [],
      bandStart: { at: 0.4, text: '120' },
      bandEnd: { at: 0.6, text: '180' },
      depthText: '18 × 6 mm',
    };
    mount(mockJoints([dowelRow(1, { expanded: true, fastener: false, pieces: [band] })]));
    expect(all('.joint .band').length).toBe(1);
    expect(all('.joint .tick').length).toBe(2);
  });

  it('labels that collide even staggered write the numbers instead of drawing them', () => {
    const crowded = dowelPiece('Side left', [
      { at: 0.5, text: '150' },
      { at: 0.51, text: '153' },
      { at: 0.52, text: '156' },
    ]);
    mount(mockJoints([dowelRow(1, { expanded: true, pieces: [crowded, dowelPiece('Shelf')] })]));
    expect(q('.joint .strip')).toBeNull();
    expect(all('.joint .written').map((w) => w.textContent)).toEqual([
      crowded.writtenLine,
      dowelPiece('Shelf').writtenLine,
    ]);
    expect(q('.joint .shared')!.textContent).toBe('inset 9 mm from the face');
  });

  it('no single edge: the sentence saying why comes first, then the written numbers', () => {
    const why = 'The board is angled, so no single edge is the zero';
    mount(mockJoints([dowelRow(1, { expanded: true, edgeNamed: false, edge: why })]));
    expect(q('.joint .strip')).toBeNull();
    expect(q('.joint .edge-sentence')!.textContent).toBe(why);
    // FIRST: every number below is read already knowing why no edge is named.
    const order = all('.joint .markout > *').map((e) => e.className.split(' ')[0]);
    expect(order[0]).toBe('edge-sentence');
    expect(order.indexOf('written')).toBeGreaterThan(0);
  });

  it('a caveat shows with its caution mark', () => {
    mount(mockJoints([dowelRow(1, { caveat: 'Part of this contact is not a full rectangle' })]));
    expect(q('.joint .caveat')!.textContent).toContain('not a full rectangle');
    expect(q('.joint .caveat svg')).not.toBeNull();
  });

  it('rows re-read on changed', async () => {
    const api = mockJoints([dowelRow(1)]);
    mount(api);
    api.rows = [brokenRow(4), dowelRow(1)];
    api.changed.emit();
    await settle();
    expect(all('.joint').map((r) => r.dataset.joint)).toEqual(['4', '1']);
  });

  it('the empty state, while a furniture is open', () => {
    mount(mockJoints([]));
    expect(q('.joints-card .empty')!.textContent).toContain('No joints yet.');
  });

  it('the close x asks the model; no row button takes keyboard focus', async () => {
    const { api } = mount(mockJoints([dowelRow(1, { expanded: true })]));
    for (const b of all('.joints-card button')) expect(b.tabIndex).toBe(-1);
    await fireEvent.click(q('.joints-card .close')!);
    expect(calls(api, 'close').length).toBe(1);
  });

  it('a different measurer lays the labels out again (Review Focus 1, review I1)', async () => {
    const api = mockJoints([dowelRow(1, { expanded: true })]);
    const view = render(Joints, { props: { joints: api, rect, report: () => {}, measure, stripWidth: 212 } });
    const lefts = () => all('.tick-label').map((l) => l.style.left);
    const widths = () => all('.tick-label').map((l) => l.dataset.w);
    const before = lefts();
    expect(widths()).toEqual(['12', '18', '18', '12', '18', '18']);
    await view.rerender({ joints: api, rect, report: () => {}, measure: (t: string) => t.length * 9, stripWidth: 212 });
    await settle();
    expect(lefts()).not.toEqual(before);
    expect(widths()).toEqual(['18', '27', '27', '18', '27', '27']);
  });

  it("a short name keeps its whole width beside a long one - the Qt rule (review M1)", () => {
    const long = 'Left side panel of the tall cabinet';
    mount(mockJoints([dowelRow(1, { nameA: 'Shelf', nameB: long })]));
    const names = all('.joint .name');
    // 212 wide, " ↔ " 18: 194 to share; "Shelf" (30) fits half and keeps it all.
    expect(names[0].style.maxWidth).toBe('30px');
    expect(names[1].style.maxWidth).toBe('164px');
  });

  it('two long names share the line by half', () => {
    const long = 'Left side panel of the tall cabinet';
    mount(mockJoints([dowelRow(1, { nameA: long, nameB: long })]));
    const names = all('.joint .name');
    expect(names[0].style.maxWidth).toBe('97px');
    expect(names[1].style.maxWidth).toBe('97px');
  });

  it("a click on the disabled delete's glyph never toggles the row (review M2)", async () => {
    const api = mockJoints([dowelRow(1, { expanded: true })]);
    api.enabled = false;
    mount(api);
    await fireEvent.click(q('.joint .delete svg')!, { detail: 1 });
    await fireEvent.click(q('.joint .delete path')!, { detail: 1 });
    expect(calls(api, 'activate')).toEqual([]);
  });

  it('a broken row that was open shows no number either (Review Focus 2, review M5)', () => {
    const broken = { ...brokenRow(2), expanded: true, pieces: [dowelPiece('Side left')] };
    mount(mockJoints([broken]));
    expect(q('.joint .strip')).toBeNull();
    expect(q('.joint .written')).toBeNull();
    expect(/\d/.test(q('.joint')!.querySelector('.kind')!.textContent!)).toBe(false);
  });

  it("every word of the app's own on a row is clean", () => {
    const api = mockJoints([brokenRow(2), dowelRow(1, { expanded: true })]);
    mount(api);
    const own = [
      ...Object.values(api.words),
      ...all('.joint .kind, .joint .depth, .joint .tick-label, .joint .shared, .joint .written').map(
        (e) => e.textContent ?? '',
      ),
    ];
    expect(bannedWordsIn(own)).toEqual([]);
  });
});

// The render page (migration Phase 3e) against a mock RenderBridge: every
// section in the Qt panel's order and words, the chips' current marks and the
// rule that a current chip sends nothing, the dials, the shots, the wood
// tiles, the footer, and the vocabulary law over all of it.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Render from '../src/render/Render.svelte';
import { mockRender, sampleLayout, sampleState, sampleThumb } from '../src/render/mock';
import { bannedWordsIn } from './banned';

const tiles = [
  { name: 'Oak natural', thumb: sampleThumb },
  { name: 'Walnut', thumb: sampleThumb },
];

function mount(api = mockRender(sampleLayout(tiles))) {
  const view = render(Render, { props: { render: api } });
  return { api, view };
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const qa = (sel: string) => [...document.querySelectorAll(sel)] as HTMLElement[];
const calls = (api: ReturnType<typeof mockRender>, method: string) => api.calls.filter((c) => c.method === method);
const texts = (sel: string) => qa(sel).map((n) => n.textContent?.trim());

afterEach(() => cleanup());

describe('the render page', () => {
  it('says it is listening once it is mounted', () => {
    const { api } = mount();
    expect(calls(api, 'ready').length).toBe(1);
  });

  it('shows the sections in the Qt panel order, with its words', () => {
    mount();
    expect(q('.title')!.textContent).toBe('Render settings');
    expect(texts('.scroll .heading')).toEqual(['Frame', 'Shots', 'Light', 'Material', 'Studio', 'Camera', 'Quality']);
    expect(q('.pinned .heading')!.textContent).toBe('Export');
  });

  it('marks the current chip and sends nothing when it is clicked again', async () => {
    const { api } = mount();
    const aspects = qa('.aspects .chip');
    expect(aspects.map((c) => c.textContent)).toEqual(['Free', '1:1', '4:5', '3:2', '16:9']);
    expect(aspects[0].classList.contains('current')).toBe(true);
    expect(aspects[0].getAttribute('title')).toBe("The window's own shape - nothing is masked off");
    await fireEvent.click(aspects[0]);
    expect(calls(api, 'aspect').length).toBe(0);
    await fireEvent.click(aspects[3]);
    expect(calls(api, 'aspect')).toEqual([{ method: 'aspect', args: [3] }]);
  });

  it('sends guides, quality and export size by index', async () => {
    const { api } = mount();
    await fireEvent.click(qa('.guides .chip')[2]);
    await fireEvent.click(qa('.qualities .chip')[0]);
    await fireEvent.click(qa('.exports .chip')[4]);
    expect(calls(api, 'guides')[0].args).toEqual([2]);
    expect(calls(api, 'quality')[0].args).toEqual([0]);
    expect(calls(api, 'exportSize')[0].args).toEqual([4]);
    expect(texts('.qualities .chip')).toEqual(['Simple', 'Balanced', 'Deep']);
  });

  it('toggles Cut out every click, and marks it while on', async () => {
    const { api } = mount();
    const cut = q('.cutout')!;
    expect(cut.classList.contains('current')).toBe(false);
    await fireEvent.click(cut);
    await fireEvent.click(cut);
    expect(calls(api, 'cutout').length).toBe(2);
    api.update({ cutout: true });
    await tick();
    expect(q('.cutout')!.classList.contains('current')).toBe(true);
  });

  it('follows a state change made anywhere else', async () => {
    const { api } = mount();
    api.update({ aspect: 4, quality: 0 });
    await tick();
    expect(qa('.aspects .chip')[4].classList.contains('current')).toBe(true);
    expect(qa('.qualities .chip')[0].classList.contains('current')).toBe(true);
  });

  it('shows each dial with its range, value and readout, and sends a drag live', async () => {
    const { api } = mount();
    const strength = q('.dial[data-dial="lightStrength"] input') as HTMLInputElement;
    expect(strength.min).toBe('20');
    expect(strength.max).toBe('400');
    expect(strength.value).toBe('200');
    expect(q('.dial[data-dial="lightStrength"] .readout')!.textContent).toBe('200%');
    expect(texts('.dial .label')).toEqual(['Angle', 'Strength', 'FOV']);
    await fireEvent.input(strength, { target: { value: '250' } });
    await fireEvent.input(strength, { target: { value: '260' } });
    expect(calls(api, 'setDial').map((c) => c.args)).toEqual([
      ['lightStrength', 250],
      ['lightStrength', 260],
    ]);
  });

  it('says there are no shots, then lists them as the user named them', async () => {
    const { api } = mount();
    expect(q('.no-shots')!.textContent).toBe('No shots saved yet');
    expect(q('.shot')).toBeNull();
    await fireEvent.click(q('.save-shot')!);
    expect(calls(api, 'saveShot').length).toBe(1);
    api.update({ shots: ['Front', '<b>Back</b>'] });
    await tick();
    expect(q('.no-shots')).toBeNull();
    expect(texts('.shot .apply')).toEqual(['Front', '<b>Back</b>']);
    expect(q('.shot b')).toBeNull();
    await fireEvent.click(qa('.shot .apply')[1]);
    await fireEvent.click(qa('.shot .drop')[0]);
    expect(calls(api, 'applyShot')[0].args).toEqual([1]);
    expect(calls(api, 'removeShot')[0].args).toEqual([0]);
    expect(qa('.shot .drop')[0].getAttribute('title')).toBe('Forget this shot');
  });

  it('shows the wood tiles, rings the one worn, chooses on a click and edits on a double-click', async () => {
    const { api } = mount();
    expect(texts('.tile .caption')).toEqual(['Oak natural', 'Walnut']);
    expect(qa('.tile')[0].classList.contains('current')).toBe(true);
    expect((qa('.tile img')[1] as HTMLImageElement).src).toBe(sampleThumb);
    await fireEvent.click(qa('.tile')[0]);
    expect(calls(api, 'wood').length).toBe(0);
    await fireEvent.click(qa('.tile')[1]);
    expect(calls(api, 'wood')).toEqual([{ method: 'wood', args: ['Walnut'] }]);
    await fireEvent.click(qa('.tile')[1], { detail: 2 });
    await fireEvent.dblClick(qa('.tile')[1]);
    expect(calls(api, 'wood').length).toBe(1);
    expect(calls(api, 'editMaterial')).toEqual([{ method: 'editMaterial', args: ['Walnut'] }]);
    expect(q('.material-note')!.textContent).toBe('Double-click a wood to set its colour, surface and grain');
  });

  it('has no Material section when there are no tiles', () => {
    mount(mockRender(sampleLayout([])));
    expect(texts('.scroll .heading')).not.toContain('Material');
    expect(q('.tile')).toBeNull();
    expect(q('.material-note')).toBeNull();
  });

  it('opens the picker from the swatch and asks for the studio tone back', async () => {
    const { api } = mount();
    const swatch = q('.swatch')!;
    expect(swatch.style.background).toBe('rgb(193, 191, 186)');
    await fireEvent.click(swatch);
    await fireEvent.click(q('.studio-reset')!);
    expect(calls(api, 'pickBackground').length).toBe(1);
    expect(calls(api, 'resetBackground').length).toBe(1);
  });

  it('pins the export note, the tier line, the polish bar and the shutter', async () => {
    const { api } = mount();
    expect(q('.pinned .export-note')!.textContent).toBe('2400 x 1552 px');
    expect(q('.pinned .tier')!.textContent).toBe('Path tracing');
    expect((q('.pinned .bar .fill') as HTMLElement).style.width).toBe('40%');
    expect(q('.pinned .shutter')!.textContent?.trim()).toBe('Save Screenshot');
    await fireEvent.click(q('.shutter')!);
    expect(calls(api, 'shoot').length).toBe(1);
    api.update({ tierProgress: -1, shutter: { label: 'Save Screenshot', tip: 'Save Screenshot', enabled: false } });
    await tick();
    expect(q('.bar')).toBeNull();
    expect((q('.shutter') as HTMLButtonElement).disabled).toBe(true);
    await fireEvent.click(q('.shutter')!);
    expect(calls(api, 'shoot').length).toBe(1);
  });

  it('takes no keyboard focus on any control', () => {
    mount();
    for (const control of qa('button')) expect(control.tabIndex).toBe(-1);
  });

  it('re-reads the tiles when the layout changes', async () => {
    const api = mockRender(sampleLayout(tiles));
    mount(api);
    api.layout = sampleLayout([...tiles, { name: 'Cherry', thumb: sampleThumb }]);
    api.layoutChanged.emit();
    await tick();
    expect(texts('.tile .caption')).toEqual(['Oak natural', 'Walnut', 'Cherry']);
  });

  it('says nothing the vocabulary law forbids', () => {
    mount();
    const layout = sampleLayout();
    const copy = [
      layout.title,
      ...Object.values(layout.sections),
      ...Object.values(layout.rows),
      ...[...layout.aspects, ...layout.guides, ...layout.qualities, ...layout.exportSizes].flatMap((c) => [c.label, c.tip]),
      layout.cutout.label,
      layout.cutout.tip,
      layout.saveShot.label,
      layout.saveShot.tip,
      layout.studioReset.label,
      layout.studioReset.tip,
      layout.applyShotTip,
      layout.removeShotTip,
      layout.noShots,
      layout.materialNote,
      layout.backgroundTip,
    ];
    expect(bannedWordsIn(copy)).toEqual([]);
    // The rendered page, less the user's own tile names.
    const shown = qa('body *:not(.caption)')
      .filter((n) => n.children.length === 0)
      .map((n) => n.textContent ?? '');
    expect(bannedWordsIn(shown)).toEqual([]);
    expect(sampleState().shots).toEqual([]);
  });
});

// The Versions card (migration Phase 3c) against a mock VersionsModel: the
// list, the two-click Delete, the pending version and its keyboard, and what
// happens when the card goes away or + shuts mid-edit.
import { afterEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Versions from '../src/editor/versions/Versions.svelte';
import { mockVersions, sampleVersions } from '../src/editor/versions/mock';
import { bannedWordsIn } from './banned';

const rect = { x: 60, y: 80, w: 240, h: 300, shown: true };

function keyboard() {
  const calls: string[] = [];
  return { calls, claimKeyboard: () => calls.push('claim'), releaseKeyboard: () => calls.push('release') };
}

function mount(api = mockVersions([...sampleVersions]), newRequest = 0) {
  const keys = keyboard();
  const heights: number[] = [];
  const report = (_name: string, h: number) => heights.push(h);
  const view = render(Versions, { props: { versions: api, keyboard: keys, rect, report, newRequest } });
  return { api, keys, view, heights, report };
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const field = () => q('.pending input') as HTMLInputElement | null;
const calls = (api: ReturnType<typeof mockVersions>, method: string) => api.calls.filter((c) => c.method === method);

async function settle() {
  for (let i = 0; i < 4; i++) await tick();
  await Promise.resolve();
  await tick();
}

async function type(text: string) {
  await fireEvent.input(field()!, { target: { value: text } });
}

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe('the Versions card', () => {
  it('lists every version with its date, in the order given', () => {
    mount();
    const names = [...document.querySelectorAll('.version .name')].map((n) => n.textContent);
    expect(names).toEqual(['Version 1', 'Shelf added']);
    expect(q('.version[data-version="Version 1"] .date')!.textContent).toBe('10/11/26, 9:12 AM');
  });

  it('shows the empty state when there is nothing', () => {
    mount(mockVersions([]));
    expect(q('.empty')!.textContent).toContain('No versions yet.');
    expect(q('.version')).toBeNull();
  });

  it('is drawn at the rectangle C++ gives it', () => {
    mount();
    const card = q('.versions-card')!;
    expect([card.style.left, card.style.top, card.style.width, card.style.height]).toEqual([
      '60px',
      '80px',
      '240px',
      '300px',
    ]);
  });

  it('a picture shows as an image, none as no image', () => {
    mount(mockVersions([{ ...sampleVersions[0], thumbUrl: 'file:///t.png?v=1' }, sampleVersions[1]]));
    expect(q('.version[data-version="Version 1"] img.thumb')!.getAttribute('src')).toBe('file:///t.png?v=1');
    expect(q('.version[data-version="Shelf added"] img.thumb')).toBeNull();
  });

  it('Compare and Restore ask once, by name', async () => {
    const { api } = mount();
    await fireEvent.click(q('.version[data-version="Shelf added"] .compare')!);
    await fireEvent.click(q('.version[data-version="Version 1"] .restore')!);
    expect(api.calls.filter((c) => c.method !== 'defaultName')).toEqual([
      { method: 'compare', args: ['Shelf added'] },
      { method: 'restore', args: ['Version 1'] },
    ]);
  });

  it('Delete takes two clicks inside its window, and the arm drops after it', async () => {
    vi.useFakeTimers();
    const { api } = mount();
    const del = () => q('.version[data-version="Version 1"] .delete')!;
    await fireEvent.click(del());
    expect(del().textContent).toBe('Delete — click again');
    expect(calls(api, 'remove')).toEqual([]);
    vi.advanceTimersByTime(4001);
    await tick();
    expect(del().textContent).toBe('Delete');
    await fireEvent.click(del());
    await fireEvent.click(del());
    expect(calls(api, 'remove')).toEqual([{ method: 'remove', args: ['Version 1'] }]);
  });

  it('an armed Delete survives a refresh that keeps its row (Review Focus 3)', async () => {
    const { api } = mount();
    await fireEvent.click(q('.version[data-version="Version 1"] .delete')!);
    api.versions = [{ name: 'Late', savedText: 'x', saved: 3, thumbUrl: '' }, ...sampleVersions];
    api.changed.emit();
    await settle();
    expect(q('.version[data-version="Version 1"] .delete')!.textContent).toBe('Delete — click again');
    expect(q('.version[data-version="Late"] .delete')!.textContent).toBe('Delete');
  });

  it('+ opens one pending version, seeded and selected, holding the keyboard', async () => {
    const { keys } = mount();
    await fireEvent.click(q('.add')!);
    await settle();
    expect(field()!.value).toBe('Version 3');
    expect(document.activeElement).toBe(field());
    expect(keys.calls).toEqual(['claim']);
    expect((q('.add') as HTMLButtonElement).disabled).toBe(true);
    await fireEvent.click(q('.add')!);
    await settle();
    expect(document.querySelectorAll('.pending').length).toBe(1);
  });

  it('Enter saves the typed name; a refusal keeps the card with it (Review Focus 2)', async () => {
    const { api, keys } = mount();
    await fireEvent.click(q('.add')!);
    await settle();
    api.saveAnswer = false;
    await type('Shelf added');
    await fireEvent.keyDown(field()!, { key: 'Enter' });
    await settle();
    expect(calls(api, 'save')).toEqual([{ method: 'save', args: ['Shelf added'] }]);
    expect(field()!.value).toBe('Shelf added');
    expect(keys.calls).toEqual(['claim']);
    api.saveAnswer = true;
    await type('Shelf moved');
    await fireEvent.keyDown(field()!, { key: 'Enter' });
    await settle();
    expect(calls(api, 'save').map((c) => c.args[0])).toEqual(['Shelf added', 'Shelf moved']);
    expect(q('.pending')).toBeNull();
    expect(keys.calls).toEqual(['claim', 'release']);
  });

  it('Escape discards, saving nothing; a blank name discards too', async () => {
    const { api, keys } = mount();
    await fireEvent.click(q('.add')!);
    await settle();
    await fireEvent.keyDown(field()!, { key: 'Escape' });
    await settle();
    expect(q('.pending')).toBeNull();
    await fireEvent.click(q('.add')!);
    await settle();
    await type('   ');
    await fireEvent.keyDown(field()!, { key: 'Enter' });
    await settle();
    expect(q('.pending')).toBeNull();
    expect(calls(api, 'save')).toEqual([]);
    expect(keys.calls).toEqual(['claim', 'release', 'claim', 'release']);
  });

  it('focus leaving the field saves, as the Qt drawer does (ruling R5)', async () => {
    const { api } = mount(mockVersions([]));
    await fireEvent.click(q('.add')!);
    await settle();
    await fireEvent.blur(field()!);
    await settle();
    expect(calls(api, 'save')).toEqual([{ method: 'save', args: ['Version 1'] }]);
  });

  it('+ shutting discards an open pending card and saves nothing', async () => {
    const { api, keys } = mount(mockVersions([]));
    await fireEvent.click(q('.add')!);
    await settle();
    api.canSave = false;
    api.changed.emit();
    await settle();
    expect(q('.pending')).toBeNull();
    expect(calls(api, 'save')).toEqual([]);
    expect(keys.calls).toEqual(['claim', 'release']);
    expect((q('.add') as HTMLButtonElement).disabled).toBe(true);
  });

  it('unmounting mid-edit gives the keyboard back and saves nothing (Review Focus 4)', async () => {
    const { api, keys, view } = mount(mockVersions([]));
    await fireEvent.click(q('.add')!);
    await settle();
    view.unmount();
    await settle();
    expect(keys.calls).toEqual(['claim', 'release']);
    expect(calls(api, 'save')).toEqual([]);
  });

  it('a request from the menu opens the pending card on mount, and again when it is repeated', async () => {
    const { view, api, report } = mount(mockVersions([]), 1);
    await settle();
    expect(field()!.value).toBe('Version 1');
    await fireEvent.keyDown(field()!, { key: 'Escape' });
    await settle();
    expect(field()).toBeNull();
    await view.rerender({ versions: api, keyboard: keyboard(), rect, report, newRequest: 2 });
    await settle();
    expect(field()?.value).toBe('Version 1');
  });

  it('rows shut while sketching: Compare, Restore and Delete are disabled', () => {
    const api = mockVersions([...sampleVersions]);
    api.rowsEnabled = false;
    mount(api);
    const buttons = [...document.querySelectorAll<HTMLButtonElement>('.version button')];
    expect(buttons.length).toBe(6);
    for (const b of buttons) expect(b.disabled).toBe(true);
  });

  it('no button takes keyboard focus; the name field does', async () => {
    mount();
    for (const b of document.querySelectorAll<HTMLButtonElement>('.versions-card button')) expect(b.tabIndex).toBe(-1);
    await fireEvent.click(q('.add')!);
    await settle();
    expect(field()!.tabIndex).toBe(0);
  });

  it('the close x asks the model, and every painted word is clean', async () => {
    const { api } = mount();
    await fireEvent.click(q('.close')!);
    expect(calls(api, 'close').length).toBe(1);
    const words = Object.values(api.words).filter((w): w is string => typeof w === 'string');
    expect(bannedWordsIn([...words, q('.versions-card')!.textContent ?? ''])).toEqual([]);
  });
});

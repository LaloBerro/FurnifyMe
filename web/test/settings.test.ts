// The Settings card (migration Phase 3b) against a mock SettingsBridge: the
// tabs, the colour rows, the number fields, the mirrored rows, the font list,
// the two-click reset, and what happens when the card goes away mid-edit.
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Settings, { forgetTab } from '../src/editor/settings/Settings.svelte';
import { mockSettings } from '../src/editor/settings/mock';
import { bannedWordsIn } from './banned';

function keyboard() {
  const calls: string[] = [];
  return {
    calls,
    claimKeyboard: () => calls.push('claim'),
    releaseKeyboard: () => calls.push('release'),
  };
}

function mount() {
  const settings = mockSettings();
  const keys = keyboard();
  const view = render(Settings, {
    props: { settings, keyboard: keys, rect: { x: 900, y: 120, w: 296, h: 380 } },
  });
  return { settings, keys, view };
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const calls = (s: ReturnType<typeof mockSettings>, method: string) =>
  s.calls.filter((c) => c.method === method);

async function settle() {
  await tick();
  await tick();
}

async function openTab(index: number) {
  await fireEvent.click(q(`[data-tab="${index}"]`)!);
  await settle();
}

beforeEach(() => {
  vi.useFakeTimers();
  forgetTab();
});
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe('the card', () => {
  it('stands at the rect it is given, and takes the pointer there', () => {
    mount();
    const card = q('.settings-card')!;
    expect(card.style.left).toBe('900px');
    expect(card.style.top).toBe('120px');
    expect(card.style.width).toBe('296px');
    expect(card.style.height).toBe('380px');
    expect(card.hasAttribute('data-hit')).toBe(true);
    expect(document.querySelectorAll('.settings-card [data-hit]')).toHaveLength(0);
  });

  it('is titled Settings', () => {
    mount();
    expect(q('.settings-card .title')!.textContent).toBe('Settings');
  });

  it('the close x appears on hover and triggers Settings', async () => {
    const { settings } = mount();
    const close = q('.settings-card .close')!;
    expect(close.classList.contains('shown')).toBe(false);
    await fireEvent.mouseEnter(q('.settings-card')!);
    expect(close.classList.contains('shown')).toBe(true);
    expect(close.getAttribute('aria-label')).toBe('Close this panel');
    await fireEvent.click(close);
    expect(calls(settings, 'trigger').map((c) => c.args[0])).toEqual(['settings']);
  });
});

describe('the tabs', () => {
  it('opens on Colours, and a tab swaps the rows', async () => {
    mount();
    expect(q('[data-tab="0"]')!.getAttribute('aria-selected')).toBe('true');
    expect(q('[data-swatch="accent"]')).not.toBeNull();
    await openTab(2);
    expect(q('[data-tab="2"]')!.getAttribute('aria-selected')).toBe('true');
    expect(q('[data-tab="0"]')!.getAttribute('aria-selected')).toBe('false');
    expect(q('[data-swatch="accent"]')).toBeNull();
    expect(q('[data-number="gridDensity"]')).not.toBeNull();
  });

  it('a tab carries its tooltip and takes no focus', () => {
    mount();
    const tab = q('[data-tab="3"]')!;
    expect(tab.textContent).toBe('Working');
    expect(tab.title).toBe('The unit sizes are written in, and how often this furniture is saved to disk');
    expect(tab.tabIndex).toBe(-1);
  });

  it('Text & lines holds its five rows in the drawer order', async () => {
    mount();
    await openTab(1);
    const labels = [...document.querySelectorAll('.page .row .label')].map((l) => l.textContent);
    expect(labels).toEqual(['Text size', 'Font', 'Edge lines', 'Outline lines', 'Button border']);
  });

  it('Viewport and Working hold the window rows in table order', async () => {
    mount();
    await openTab(2);
    const viewport = [...document.querySelectorAll('.page .row .label')].map((l) => l.textContent);
    expect(viewport).toEqual(['Show the grid', 'Grid detail', 'Projection', 'Gizmo size']);
    await openTab(3);
    const working = [...document.querySelectorAll('.page .row .label')].map((l) => l.textContent);
    expect(working).toEqual(['Sizes in', 'Autosave']);
  });
});

describe('the colours', () => {
  it('a swatch shows the live colour and asks C++ for the picker', async () => {
    const { settings } = mount();
    const swatch = q('[data-swatch="accent"]')!;
    expect(swatch.style.background).toBe('rgb(106, 0, 255)');
    expect(swatch.title).toBe('Pick a colour for Accent');
    expect(swatch.tabIndex).toBe(-1);
    await fireEvent.click(swatch);
    expect(calls(settings, 'pickColour').map((c) => c.args[0])).toEqual(['accent']);
  });

  it('a group heading opens its run', () => {
    mount();
    const items = [...document.querySelectorAll('.colours .heading, .colours .row .label')].map(
      (e) => e.textContent,
    );
    expect(items).toEqual(['Viewport', 'Viewport', 'Grid lines', 'Axes and gizmo', 'X axis', 'Accent']);
  });

  it('Save and Load ask C++, which opens the file dialogs', async () => {
    const { settings } = mount();
    await fireEvent.click(q('.settings-card .save')!);
    await fireEvent.click(q('.settings-card .load')!);
    expect(calls(settings, 'saveColours')).toHaveLength(1);
    expect(calls(settings, 'loadColours')).toHaveLength(1);
  });

  it('an edit made elsewhere re-dresses the open card (Review Focus 3)', async () => {
    const { settings } = mount();
    settings.spec = { ...settings.spec, colours: { ...settings.spec.colours, accent: '#c8285a' } };
    settings.specChanged.emit();
    await settle();
    expect(q('[data-swatch="accent"]')!.style.background).toBe('rgb(200, 40, 90)');
  });
});

describe('reset', () => {
  it('takes two clicks within its window', async () => {
    const { settings } = mount();
    const reset = q('.settings-card .reset')!;
    expect(reset.textContent).toContain('Reset colours and sizes');
    await fireEvent.click(reset);
    await settle();
    expect(q('.reset .now')!.textContent).toBe('Reset — click again');
    expect(calls(settings, 'reset')).toHaveLength(0);
    vi.advanceTimersByTime(4001);
    await settle();
    expect(q('.reset .now')!.textContent).toBe('Reset colours and sizes');
    await fireEvent.click(reset);
    await fireEvent.click(reset);
    expect(calls(settings, 'reset')).toHaveLength(1);
    await settle();
    expect(q('.reset .now')!.textContent).toBe('Reset colours and sizes');
  });
});

describe('the mirrored rows', () => {
  it('a switch mirrors its action and triggers it', async () => {
    const { settings } = mount();
    await openTab(2);
    const grid = q('[data-action="show-the-grid"]')!;
    expect(grid.getAttribute('role')).toBe('switch');
    expect(grid.getAttribute('aria-checked')).toBe('true');
    await fireEvent.click(grid);
    expect(calls(settings, 'trigger').map((c) => c.args[0])).toEqual(['show-the-grid']);
    // The click wrote nothing: the switch moves only when the action does.
    expect(grid.getAttribute('aria-checked')).toBe('true');
    settings.actions = settings.actions.map((a) => (a.id === 'show-the-grid' ? { ...a, checked: false } : a));
    settings.actionsChanged.emit();
    await settle();
    expect(grid.getAttribute('aria-checked')).toBe('false');
  });

  it('a current choice is not re-triggered (Review Focus 5)', async () => {
    const { settings } = mount();
    await openTab(3);
    await fireEvent.click(q('[data-action="millimetres"]')!);
    expect(calls(settings, 'trigger')).toHaveLength(0);
    await fireEvent.click(q('[data-action="centimetres"]')!);
    expect(calls(settings, 'trigger').map((c) => c.args[0])).toEqual(['centimetres']);
  });

  it("a disabled action's control triggers nothing (Review Focus 5)", async () => {
    const { settings } = mount();
    settings.actions = settings.actions.map((a) => (a.id === 'show-the-grid' ? { ...a, enabled: false } : a));
    settings.actionsChanged.emit();
    await openTab(2);
    const grid = q('[data-action="show-the-grid"]')!;
    expect(grid.getAttribute('aria-disabled')).toBe('true');
    await fireEvent.click(grid);
    expect(calls(settings, 'trigger')).toHaveLength(0);
  });

  it('a binary row triggers only from the side it is not', async () => {
    const { settings } = mount();
    await openTab(2);
    const off = q('[data-action="orthographic"][data-side="off"]')!;
    const on = q('[data-action="orthographic"][data-side="on"]')!;
    expect(off.getAttribute('aria-checked')).toBe('true');
    expect(on.getAttribute('aria-checked')).toBe('false');
    await fireEvent.click(off);
    expect(calls(settings, 'trigger')).toHaveLength(0);
    await fireEvent.click(on);
    expect(calls(settings, 'trigger').map((c) => c.args[0])).toEqual(['orthographic']);
  });

  it('a stacked choice lays out one option per line', async () => {
    mount();
    await openTab(3);
    expect(q('[data-row="Autosave"] .options')!.classList.contains('stacked')).toBe(true);
    expect(q('[data-row="Sizes in"] .options')!.classList.contains('stacked')).toBe(false);
  });
});

describe('a number field', () => {
  it('shows the value like the spin box, and steps and clamps', async () => {
    const { settings } = mount();
    await openTab(2);
    const field = q('[data-number="gridDensity"] input') as HTMLInputElement;
    expect(field.value).toBe('1.0x');
    await fireEvent.click(q('[data-number="gridDensity"] .up')!);
    expect(calls(settings, 'setNumber').at(-1)!.args).toEqual(['gridDensity', 1.1]);
    settings.spec = { ...settings.spec, numbers: { ...settings.spec.numbers, gridDensity: 2 } };
    settings.specChanged.emit();
    await settle();
    expect(field.value).toBe('2.0x');
    await fireEvent.click(q('[data-number="gridDensity"] .up')!);
    expect(calls(settings, 'setNumber').at(-1)!.args).toEqual(['gridDensity', 2]);
  });

  it('typing claims the keyboard, commits live, and Escape puts it back', async () => {
    const { settings, keys } = mount();
    await openTab(1);
    const field = q('[data-number="basePt"] input') as HTMLInputElement;
    await fireEvent.mouseDown(field);
    await fireEvent.focus(field);
    expect(keys.calls).toEqual(['claim']);
    field.value = '12';
    await fireEvent.input(field);
    expect(calls(settings, 'setNumber').at(-1)!.args).toEqual(['basePt', 12]);
    field.value = 'abc';
    await fireEvent.input(field);
    expect(calls(settings, 'setNumber')).toHaveLength(1);
    await fireEvent.keyDown(field, { key: 'Escape' });
    expect(calls(settings, 'setNumber').at(-1)!.args).toEqual(['basePt', 10]);
    expect(keys.calls).toEqual(['claim', 'release']);
  });

  it('a partial entry outside the range is not applied while typing (review I4)', async () => {
    const { settings } = mount();
    await openTab(1);
    const field = q('[data-number="basePt"] input') as HTMLInputElement;
    await fireEvent.focus(field);
    field.value = '1';
    await fireEvent.input(field);
    expect(calls(settings, 'setNumber')).toHaveLength(0);
    field.value = '12.5';
    await fireEvent.input(field);
    expect(calls(settings, 'setNumber')).toHaveLength(0);
    field.value = '99 pt';
    await fireEvent.input(field);
    expect(calls(settings, 'setNumber')).toHaveLength(0);
    await fireEvent.keyDown(field, { key: 'Enter' });
    expect(calls(settings, 'setNumber').at(-1)!.args).toEqual(['basePt', 14]);
  });

  it('a step while editing shows the new value at once (review I5)', async () => {
    const { settings } = mount();
    await openTab(1);
    const field = q('[data-number="basePt"] input') as HTMLInputElement;
    await fireEvent.focus(field);
    await fireEvent.keyDown(field, { key: 'ArrowUp' });
    await settle();
    expect(calls(settings, 'setNumber').at(-1)!.args).toEqual(['basePt', 11]);
    expect(field.value).toBe('11 pt');
  });

  it('Escape with nothing changed writes nothing', async () => {
    const { settings } = mount();
    await openTab(1);
    const field = q('[data-number="basePt"] input') as HTMLInputElement;
    await fireEvent.focus(field);
    await fireEvent.keyDown(field, { key: 'Escape' });
    expect(calls(settings, 'setNumber')).toHaveLength(0);
  });

  it('Enter commits and gives the keyboard back', async () => {
    const { keys } = mount();
    await openTab(1);
    const field = q('[data-number="basePt"] input') as HTMLInputElement;
    await fireEvent.focus(field);
    await fireEvent.keyDown(field, { key: 'Enter' });
    expect(keys.calls).toEqual(['claim', 'release']);
  });

  it('a wheel over a field steps it and goes no further', async () => {
    const { settings } = mount();
    await openTab(2);
    const field = q('[data-number="gizmoScale"] input')!;
    const wheel = new WheelEvent('wheel', { deltaY: -100, bubbles: true, cancelable: true });
    field.dispatchEvent(wheel);
    expect(wheel.defaultPrevented).toBe(true);
    expect(calls(settings, 'setNumber').at(-1)!.args).toEqual(['gizmoScale', 1.1]);
  });
});

describe('the font list', () => {
  it('previews on highlight, keeps on Enter', async () => {
    const { settings, keys } = mount();
    await openTab(1);
    await fireEvent.click(q('.font-picker .current')!);
    await settle();
    expect(keys.calls).toEqual(['claim']);
    const list = q('.font-picker [role="listbox"]')!;
    await fireEvent.keyDown(list, { key: 'ArrowDown' });
    expect(calls(settings, 'setFontFamily').at(-1)!.args).toEqual(['Arial']);
    await fireEvent.keyDown(list, { key: 'Enter' });
    await settle();
    expect(q('.font-picker [role="listbox"]')).toBeNull();
    expect(calls(settings, 'setFontFamily')).toHaveLength(1);
    expect(keys.calls).toEqual(['claim', 'release']);
  });

  it('Escape restores the family it opened over', async () => {
    const { settings } = mount();
    await openTab(1);
    await fireEvent.click(q('.font-picker .current')!);
    await settle();
    const list = q('.font-picker [role="listbox"]')!;
    await fireEvent.keyDown(list, { key: 'ArrowDown' });
    await fireEvent.keyDown(list, { key: 'ArrowDown' });
    await fireEvent.keyDown(list, { key: 'Escape' });
    expect(calls(settings, 'setFontFamily').map((c) => c.args[0])).toEqual(['Arial', 'Georgia', 'DM Sans 14pt']);
  });

  it('closes, keeping the preview, when focus leaves the page (review I6)', async () => {
    const { settings, keys } = mount();
    await openTab(1);
    await fireEvent.click(q('.font-picker .current')!);
    await settle();
    await fireEvent.keyDown(q('.font-picker [role="listbox"]')!, { key: 'ArrowDown' });
    window.dispatchEvent(new Event('blur'));
    await settle();
    expect(q('.font-picker [role="listbox"]')).toBeNull();
    expect(keys.calls).toEqual(['claim', 'release']);
    expect(calls(settings, 'setFontFamily').map((c) => c.args[0])).toEqual(['Arial']);
  });

  it('and when focus moves out of the list inside the page', async () => {
    const { keys } = mount();
    await openTab(1);
    await fireEvent.click(q('.font-picker .current')!);
    await settle();
    const list = q('.font-picker [role="listbox"]')!;
    list.dispatchEvent(new FocusEvent('focusout', { relatedTarget: document.body, bubbles: true }));
    await settle();
    expect(q('.font-picker [role="listbox"]')).toBeNull();
    expect(keys.calls).toEqual(['claim', 'release']);
  });

  it('shows the family by its shown name', async () => {
    mount();
    await openTab(1);
    expect(q('.font-picker .current')!.textContent).toBe('DM Sans');
  });
});

describe('reopening', () => {
  it('opens on the tab it was left on, for the session (as the Qt drawer does)', async () => {
    const first = mount();
    await openTab(2);
    first.view.unmount();
    mount();
    await settle();
    expect(q('[data-tab="2"]')!.getAttribute('aria-selected')).toBe('true');
    await openTab(0);
  });
});

describe('going away mid-edit (Review Focus 4)', () => {
  it('gives the keyboard back and restores an open font list', async () => {
    const { settings, keys, view } = mount();
    await openTab(1);
    await fireEvent.click(q('.font-picker .current')!);
    await settle();
    await fireEvent.keyDown(q('.font-picker [role="listbox"]')!, { key: 'ArrowDown' });
    view.unmount();
    expect(keys.calls).toEqual(['claim', 'release']);
    expect(calls(settings, 'setFontFamily').map((c) => c.args[0])).toEqual(['Arial', 'DM Sans 14pt']);
  });

  it('a typed field gives the keyboard back too', async () => {
    const { keys, view } = mount();
    await openTab(1);
    await fireEvent.focus(q('[data-number="basePt"] input')!);
    view.unmount();
    expect(keys.calls).toEqual(['claim', 'release']);
  });
});

describe('the vocabulary law', () => {
  it('no word the card paints is banned', async () => {
    const { settings } = mount();
    const words = [
      ...Object.values(settings.layout.words).filter((w): w is string => typeof w === 'string'),
      ...settings.layout.tabs.flatMap((t) => [t.name, t.tip]),
      ...settings.layout.numbers.flatMap((n) => [n.label, n.tip]),
      ...settings.layout.rows.flatMap((r) => [r.label, ...r.options]),
      ...settings.layout.tokens.flatMap((t) => [t.name, t.group]),
    ];
    for (let tab = 0; tab < 4; ++tab) {
      await openTab(tab);
      words.push(document.body.textContent ?? '');
    }
    expect(bannedWordsIn(words)).toEqual([]);
  });
});

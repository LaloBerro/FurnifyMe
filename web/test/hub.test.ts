// The HTML hub's behaviour, pinned against a mock bridge. Every rule here is
// the Qt hub's (src/ui/SelectorWindow.cpp) - the page is a port.
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, within } from '@testing-library/svelte';
import { tick } from 'svelte';

import Hub from '../src/hub/Hub.svelte';
import { copy, allCopy } from '../src/hub/copy';
import { fitUsedIn } from '../src/hub/usedIn';
import { entry, mockHub, mockTheme, mockWindow } from '../src/hub/mock';
import { bannedWordsIn } from './banned';

const T = Date.UTC(2026, 9, 11, 9, 0);

function library() {
  return mockHub(
    [entry('a', 'Bench', T - 3600e3), entry('b', 'Armchair', T), entry('c', 'Zebra stool', 0)],
    [entry('s1', 'Dining', T, true)],
  );
}

function mount(hub = library()) {
  const win = mockWindow();
  const theme = mockTheme();
  render(Hub, { props: { hub, win, theme } });
  return { hub, win, theme };
}

const cardNames = () =>
  [...document.querySelectorAll('[data-card-kind="furniture"] .name')].map((n) => n.textContent);

beforeEach(() => vi.useFakeTimers());
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe('the grid', () => {
  it('leads with the New card, then the furniture by most recent, never-saved last', async () => {
    mount();
    const cells = [...document.querySelectorAll('.grid.furniture > *')];
    expect(cells[0].textContent).toContain(copy.newFurniture);
    expect(cardNames()).toEqual(['Armchair', 'Bench', 'Zebra stool']);
  });

  it('sorts by name with the Name chip, and the two chips are one exclusive pair', async () => {
    mount();
    const recent = screen.getByRole('button', { name: copy.sortRecent });
    const name = screen.getByRole('button', { name: copy.sortName });
    expect(recent.getAttribute('aria-pressed')).toBe('true');
    await fireEvent.click(name);
    expect(cardNames()).toEqual(['Armchair', 'Bench', 'Zebra stool']);
    expect(name.getAttribute('aria-pressed')).toBe('true');
    expect(recent.getAttribute('aria-pressed')).toBe('false');
  });

  it('filters 150 ms after typing, keeps the New card, and says when nothing matches', async () => {
    mount();
    const search = screen.getByPlaceholderText(copy.search);
    await fireEvent.input(search, { target: { value: 'ben' } });
    expect(cardNames()).toHaveLength(3); // not yet - debounced
    vi.advanceTimersByTime(150);
    await tick();
    expect(cardNames()).toEqual(['Bench']);
    expect(screen.getByText(copy.newFurniture)).toBeTruthy();
    await fireEvent.input(search, { target: { value: 'zz' } });
    vi.advanceTimersByTime(150);
    await tick();
    expect(screen.getByText(copy.noMatch('zz'))).toBeTruthy();
  });

  it('lists scenes under their own heading, after their own New card', () => {
    mount();
    const scenes = document.querySelector('.grid.scenes')!;
    expect(screen.getByRole('heading', { name: copy.scenesHeading })).toBeTruthy();
    expect(scenes.firstElementChild!.textContent).toContain(copy.newScene);
    expect(within(scenes as HTMLElement).getByText('Dining')).toBeTruthy();
  });

  it('opens what is clicked, and a scene as a scene', async () => {
    const { hub } = mount();
    await fireEvent.click(screen.getByRole('button', { name: copy.openCard('Bench') }));
    await fireEvent.click(screen.getByRole('button', { name: copy.openCard('Dining') }));
    await fireEvent.click(screen.getByText(copy.newScene));
    expect(hub.calls.filter((c) => c.method !== 'usedIn')).toEqual([
      { method: 'choose', args: ['a', false] },
      { method: 'choose', args: ['s1', true] },
      { method: 'createScene', args: [] },
    ]);
  });

  it("renders a user's name as text, never as markup", () => {
    mount(mockHub([entry('x', '<b>bold</b> & "quoted"', T)]));
    expect(document.querySelector('.grid b')).toBeNull();
    expect(cardNames()).toEqual(['<b>bold</b> & "quoted"']);
  });
});

describe('a card under the pointer', () => {
  it('swaps its date for Rename and Delete', async () => {
    mount();
    const card = document.querySelector('[data-card-id="a"]') as HTMLElement;
    expect(within(card).getByText('Today, 9:05 AM')).toBeTruthy();
    expect(within(card).queryByRole('button', { name: copy.rename })).toBeNull();
    await fireEvent.mouseEnter(card);
    expect(within(card).queryByText('Today, 9:05 AM')).toBeNull();
    expect(within(card).getByRole('button', { name: copy.rename })).toBeTruthy();
    expect(within(card).getByRole('button', { name: copy.delete })).toBeTruthy();
  });

  it('arms Delete on the first click, names the scenes it breaks, disarms after 4 s', async () => {
    const hub = library();
    hub.usedInNames.a = ['Dining', 'Kitchen'];
    mount(hub);
    const card = document.querySelector('[data-card-id="a"]') as HTMLElement;
    await fireEvent.mouseEnter(card);
    await fireEvent.click(within(card).getByRole('button', { name: copy.delete }));
    await vi.runAllTicks();
    await tick();
    await tick();
    expect(within(card).getByRole('button', { name: copy.deleteArmed })).toBeTruthy();
    expect(within(card).getByText(/^Used in Dining and Kitchen$/)).toBeTruthy();
    expect(hub.calls.some((c) => c.method === 'remove')).toBe(false);
    vi.advanceTimersByTime(4000);
    await tick();
    expect(within(card).getByRole('button', { name: copy.delete })).toBeTruthy();
    expect(within(card).queryByText(/^Used in/)).toBeNull();
  });

  it('deletes on the second click inside the arm', async () => {
    const { hub } = mount();
    const card = document.querySelector('[data-card-id="b"]') as HTMLElement;
    await fireEvent.mouseEnter(card);
    await fireEvent.click(within(card).getByRole('button', { name: copy.delete }));
    await tick();
    await fireEvent.click(within(card).getByRole('button', { name: copy.deleteArmed }));
    expect(hub.calls.filter((c) => c.method === 'remove')).toEqual([{ method: 'remove', args: ['b', false] }]);
  });

  it('renames in place: Enter commits, Escape cancels, an unchanged name sends nothing', async () => {
    const { hub } = mount();
    const card = () => document.querySelector('[data-card-id="a"]') as HTMLElement;
    await fireEvent.mouseEnter(card());
    await fireEvent.click(within(card()).getByRole('button', { name: copy.rename }));
    let field = within(card()).getByRole('textbox') as HTMLInputElement;
    expect(field.value).toBe('Bench');
    await fireEvent.input(field, { target: { value: 'Hall bench' } });
    await fireEvent.keyDown(field, { key: 'Escape' });
    expect(within(card()).queryByRole('textbox')).toBeNull();

    await fireEvent.mouseEnter(card());
    await fireEvent.click(within(card()).getByRole('button', { name: copy.rename }));
    field = within(card()).getByRole('textbox') as HTMLInputElement;
    await fireEvent.keyDown(field, { key: 'Enter' }); // unchanged
    await fireEvent.mouseEnter(card());
    await fireEvent.click(within(card()).getByRole('button', { name: copy.rename }));
    field = within(card()).getByRole('textbox') as HTMLInputElement;
    await fireEvent.input(field, { target: { value: '  Hall bench  ' } });
    await fireEvent.keyDown(field, { key: 'Enter' });
    expect(hub.calls.filter((c) => c.method === 'rename')).toEqual([
      { method: 'rename', args: ['a', false, 'Hall bench'] },
    ]);
  });

  it('commits a rename when the field loses focus, as the Qt hub does', async () => {
    const { hub } = mount();
    const card = document.querySelector('[data-card-id="a"]') as HTMLElement;
    await fireEvent.mouseEnter(card);
    await fireEvent.click(within(card).getByRole('button', { name: copy.rename }));
    const field = within(card).getByRole('textbox') as HTMLInputElement;
    await fireEvent.input(field, { target: { value: 'Porch bench' } });
    await fireEvent.blur(field);
    expect(hub.calls.filter((c) => c.method === 'rename')).toEqual([
      { method: 'rename', args: ['a', false, 'Porch bench'] },
    ]);
  });
});

describe('the name question', () => {
  async function open() {
    const ctx = mount();
    await fireEvent.click(screen.getByText(copy.newFurniture));
    await vi.runAllTicks();
    await tick();
    await tick();
    return ctx;
  }

  it('asks with the suggested name, and Enter creates', async () => {
    const { hub } = await open();
    const dialog = screen.getByRole('dialog', { name: copy.nameTitle });
    const field = within(dialog).getByRole('textbox') as HTMLInputElement;
    expect(field.value).toBe('Furniture 04');
    expect(within(dialog).getByText(copy.nameBody)).toBeTruthy();
    await fireEvent.input(field, { target: { value: 'Shoe rack' } });
    await fireEvent.keyDown(field, { key: 'Enter' });
    expect(hub.calls.filter((c) => c.method === 'createFurniture')).toEqual([
      { method: 'createFurniture', args: ['Shoe rack'] },
    ]);
    expect(screen.queryByRole('dialog')).toBeNull();
  });

  it('will not create from an empty name', async () => {
    const { hub } = await open();
    const dialog = screen.getByRole('dialog');
    const field = within(dialog).getByRole('textbox') as HTMLInputElement;
    await fireEvent.input(field, { target: { value: '   ' } });
    const create = within(dialog).getByRole('button', { name: new RegExp(copy.create) }) as HTMLButtonElement;
    expect(create.disabled).toBe(true);
    await fireEvent.keyDown(field, { key: 'Enter' });
    expect(hub.calls.some((c) => c.method === 'createFurniture')).toBe(false);
  });

  it('Escape cancels the question and nothing else', async () => {
    const { hub, win } = await open();
    const field = within(screen.getByRole('dialog')).getByRole('textbox');
    await fireEvent.keyDown(field, { key: 'Escape' });
    expect(screen.queryByRole('dialog')).toBeNull();
    expect(hub.calls.some((c) => c.method === 'createFurniture')).toBe(false);
    expect(win.calls.some((c) => c.method === 'close')).toBe(false);
  });
});

describe('outcomes', () => {
  it('shows a refusal in the banner for 8 s', async () => {
    const { hub } = mount();
    hub.failure.emit("Couldn't create Nowhere — Check that the library folder still exists and isn't read-only");
    await tick();
    expect(screen.getByText(/^Couldn't create Nowhere/)).toBeTruthy();
    vi.advanceTimersByTime(8000);
    await tick();
    expect(screen.queryByText(/^Couldn't create Nowhere/)).toBeNull();
  });

  it('shows a Note as a toast', async () => {
    const { hub } = mount();
    hub.note.emit('Deleted Bench — 1 scene now shows it as missing');
    await tick();
    expect(screen.getByText('Deleted Bench — 1 scene now shows it as missing')).toBeTruthy();
  });

  it('re-reads the lists when the model changes', async () => {
    const { hub } = mount();
    hub.furniture = [...hub.furniture, entry('d', 'Desk', T + 1)];
    hub.changed.emit();
    await tick();
    expect(cardNames()[0]).toBe('Desk');
  });
});

describe('the window', () => {
  it('has the three window controls, and they reach the window', async () => {
    const { win } = mount();
    await fireEvent.click(screen.getByRole('button', { name: copy.minimize }));
    await fireEvent.click(screen.getByRole('button', { name: copy.maximize }));
    await fireEvent.click(screen.getByRole('button', { name: copy.close }));
    expect(win.calls.filter((c) => c.method !== 'reportChrome')).toEqual([
      { method: 'minimize', args: [] },
      { method: 'toggleMaximize', args: [] },
      { method: 'close', args: [] },
    ]);
  });

  it('reports where its title bar and maximize button are', async () => {
    const { win } = mount();
    await tick();
    expect(win.calls.some((c) => c.method === 'reportChrome')).toBe(true);
  });
});

describe('"Used in" fitting', () => {
  const measure = (s: string) => s.length * 7; // 7 px a character

  it('names every scene while they fit, then "and N more", then the count', () => {
    expect(fitUsedIn(['Dining'], 400, measure)).toBe('Used in Dining');
    expect(fitUsedIn(['Dining', 'Kitchen', 'Hall'], 400, measure)).toBe('Used in Dining, Kitchen and Hall');
    expect(fitUsedIn(['Dining', 'Kitchen', 'Hall'], 7 * 30, measure)).toBe('Used in Dining and 2 more');
    expect(fitUsedIn(['Dining', 'Kitchen', 'Hall'], 7 * 12, measure)).toBe('Used in 3 scenes');
    expect(fitUsedIn(['Dining room'], 7 * 12, measure)).toBe('Used in 1 scene');
    expect(fitUsedIn([], 400, measure)).toBe('');
  });
});

describe('the vocabulary law', () => {
  it('no copy the page can show uses a banned word', () => {
    expect(bannedWordsIn(allCopy())).toEqual([]);
  });

  it('nor does any text it renders, user names aside', async () => {
    const hub = library();
    hub.usedInNames.a = ['Dining'];
    mount(hub);
    const card = document.querySelector('[data-card-id="a"]') as HTMLElement;
    await fireEvent.mouseEnter(card);
    await fireEvent.click(within(card).getByRole('button', { name: copy.delete }));
    await vi.runAllTicks();
    await tick();
    const userNames = [...hub.furniture, ...hub.scenes].map((e) => e.name);
    let text = document.body.textContent ?? '';
    for (const n of userNames) text = text.split(n).join(' ');
    expect(bannedWordsIn([text])).toEqual([]);
  });

  it('the sweep itself catches a banned word, and lets Joint through', () => {
    expect(bannedWordsIn(['Fuse the two bodies'])).toEqual(['Fuse']);
    expect(bannedWordsIn(['a background swatch'])).toEqual([]); // a letter before 'round'
    expect(bannedWordsIn(['round the edge'])).toEqual(['round']);
    expect(bannedWordsIn(['Rounded corners'])).toEqual(['Round']); // gui_smoke's one-sided rule
    expect(bannedWordsIn(['Joints drawer'])).toEqual([]);
    expect(bannedWordsIn(['join them'])).toEqual(['join']);
  });
});

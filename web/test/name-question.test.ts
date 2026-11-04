// The name question (migration Phase 3g) on the editor's chrome page, against
// a mock EditorChrome: File -> New furniture's "Name this furniture", drawn
// when C++ says it stands. Its field is the page's: it is seeded with the
// suggestion selected, takes the keyboard, mirrors each edit, and Enter
// answers WITH the name it holds.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { mockChrome } from '../src/editor/mock';
import { mockTheme } from '../src/hub/mock';
import { bannedWordsIn } from './banned';

const words = {
  asking: true,
  kind: 'name' as const,
  title: 'Name this furniture',
  body: 'You can rename it later from the library.',
  name: 'Furniture 04',
  seed: 1,
  maxLength: 120,
  create: 'Create',
  createKey: 'Enter',
  cancel: 'Cancel',
  cancelKey: 'Esc',
};

function mount() {
  const chrome = mockChrome();
  render(Editor, { props: { chrome, theme: mockTheme() } });
  return chrome;
}

async function asked(chrome: ReturnType<typeof mockChrome>, extra = {}) {
  chrome.questionChanged.emit({ ...words, ...extra });
  await tick();
  await tick();
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const field = () => q('.name-question input') as HTMLInputElement;
const calls = (chrome: ReturnType<typeof mockChrome>, method: string) =>
  chrome.calls.filter((c) => c.method === method);

async function type(value: string) {
  field().value = value;
  await fireEvent.input(field());
}

afterEach(() => cleanup());

describe('the name question', () => {
  it('draws the name card, not the close question, for kind "name"', async () => {
    const chrome = mount();
    await asked(chrome);
    expect(q('.name-question')).not.toBeNull();
    expect(q('.answer.save')).toBeNull();
    expect(q('.wash .title')!.textContent).toBe('Name this furniture');
    expect(q('.wash .body')!.textContent).toBe('You can rename it later from the library.');
    const labels = [...document.querySelectorAll('.answer .label')].map((n) => n.textContent);
    expect(labels).toEqual(['Cancel', 'Create']);
    expect(q('.answer.cancel .key')!.textContent).toBe('Esc');
    expect(q('.answer.create .key')!.textContent).toBe('Enter');
  });

  it('seeds the field with the suggestion, focused and selected, so the first key replaces it', async () => {
    const chrome = mount();
    await asked(chrome);
    expect(field().value).toBe('Furniture 04');
    expect(document.activeElement).toBe(field());
    expect([field().selectionStart, field().selectionEnd]).toEqual([0, 'Furniture 04'.length]);
    expect(field().maxLength).toBe(120);
  });

  it('takes the keyboard for the question, and keeps it through a blur (review I1)', async () => {
    const chrome = mount();
    await asked(chrome);
    expect(calls(chrome, 'claimKeyboardFor').map((c) => c.args[0])).toEqual(['question']);
    // The window losing activation blurs the field: giving the keyboard back
    // there left it with no way to take it again, so it is kept until the
    // question ends.
    await fireEvent.blur(field());
    expect(calls(chrome, 'releaseKeyboardFor')).toEqual([]);
  });

  it('a press on the field claims the keyboard again, as every typed field does (R5)', async () => {
    const chrome = mount();
    await asked(chrome);
    // Whatever the page believes it holds: C++ counts claims and takes every
    // one of the question's back when it ends, so a press always asks.
    await fireEvent.mouseDown(field());
    expect(calls(chrome, 'claimKeyboardFor').map((c) => c.args[0])).toEqual(['question', 'question']);
  });

  it('gives the keyboard back when the question ends with the field still focused', async () => {
    const chrome = mount();
    await asked(chrome);
    chrome.questionChanged.emit({ asking: false });
    await tick();
    expect(q('.wash')).toBeNull();
    expect(calls(chrome, 'releaseKeyboardFor').map((c) => c.args[0])).toEqual(['question']);
  });

  it('mirrors every edit to C++', async () => {
    const chrome = mount();
    await asked(chrome);
    await type('Book');
    await type('Bookcase');
    expect(calls(chrome, 'nameQuestionEdited').map((c) => c.args[0])).toEqual(['Book', 'Bookcase']);
  });

  it('answers Enter WITH the name the field holds (Review Focus 1)', async () => {
    const chrome = mount();
    await asked(chrome);
    await type('Bookcase');
    await fireEvent.keyDown(field(), { key: 'Enter' });
    expect(calls(chrome, 'answerNameQuestion').map((c) => c.args)).toEqual([['create', 'Bookcase']]);
  });

  it('Create answers the same way', async () => {
    const chrome = mount();
    await asked(chrome);
    await type('Hall bench');
    await fireEvent.click(q('.answer.create')!);
    expect(calls(chrome, 'answerNameQuestion').map((c) => c.args)).toEqual([['create', 'Hall bench']]);
  });

  it('creates nothing from a blank name - Create dims and Enter does nothing (Review Focus 3)', async () => {
    const chrome = mount();
    await asked(chrome);
    await type('   ');
    expect((q('.answer.create') as HTMLButtonElement).disabled).toBe(true);
    await fireEvent.keyDown(field(), { key: 'Enter' });
    await fireEvent.click(q('.answer.create')!);
    expect(calls(chrome, 'answerNameQuestion')).toEqual([]);
    await type('Shelf');
    expect((q('.answer.create') as HTMLButtonElement).disabled).toBe(false);
  });

  it('Escape and Cancel cancel', async () => {
    const chrome = mount();
    await asked(chrome);
    await fireEvent.keyDown(field(), { key: 'Escape' });
    await fireEvent.click(q('.answer.cancel')!);
    expect(calls(chrome, 'answerNameQuestion').map((c) => c.args[0])).toEqual(['cancel', 'cancel']);
  });

  it('leaves a key mid-composition to the input method', async () => {
    const chrome = mount();
    await asked(chrome);
    await fireEvent.keyDown(field(), { key: 'Enter', isComposing: true });
    expect(calls(chrome, 'answerNameQuestion')).toEqual([]);
  });

  it('does not retype the field for a mirrored edit, and re-seeds for a new ask', async () => {
    const chrome = mount();
    await asked(chrome);
    await type('Bookcase');
    // C++ re-sends the map with the mirrored name (a relayout): same seed.
    await asked(chrome, { name: 'Bookcase' });
    await type('Bookcase, tall');
    await asked(chrome, { name: 'Bookcase' });
    expect(field().value).toBe('Bookcase, tall');
    // Asked again: a new seed, a new suggestion.
    await asked(chrome, { name: 'Furniture 05', seed: 2 });
    expect(field().value).toBe('Furniture 05');
  });

  it('stands on the wash: every press over the viewport, the rest inert, the window buttons clear', async () => {
    const chrome = mount();
    await asked(chrome, { keepClear: { x: 1130, y: 16, w: 130, h: 36 } });
    expect(q('.wash')!.hasAttribute('data-hit')).toBe(true);
    expect((q('.behind') as HTMLElement & { inert: boolean }).inert).toBe(true);
    expect(q('.wash')!.style.height).toBe(`${window.innerHeight - 24}px`);
    expect(q('.wash')!.style.clipPath).toContain('1130px 16px');
  });

  it('a press on the card keeps focus in the field', async () => {
    const chrome = mount();
    await asked(chrome);
    const press = new MouseEvent('mousedown', { bubbles: true, cancelable: true });
    q('.wash .title')!.dispatchEvent(press);
    expect(press.defaultPrevented).toBe(true);
  });

  it('takes no keyboard focus with its buttons, and says nothing the vocabulary law forbids', async () => {
    const chrome = mount();
    await asked(chrome);
    for (const b of document.querySelectorAll('.answer')) expect((b as HTMLElement).tabIndex).toBe(-1);
    expect(bannedWordsIn([words.title, words.body, words.create, words.createKey, words.cancel, words.cancelKey])).toEqual([]);
  });
});

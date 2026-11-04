// The close question (migration Phase 3f) on the editor's chrome page,
// against a mock EditorChrome: it appears when C++ says the question stands,
// with the Qt card's words, takes every press over the viewport, leaves the
// status strip readable, answers through the bridge and goes when C++ says.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { mockChrome } from '../src/editor/mock';
import { mockTheme } from '../src/hub/mock';
import { bannedWordsIn } from './banned';

const words = {
  asking: true,
  title: 'Save changes to Bench before closing?',
  body: 'You have changes since the last save.',
  save: 'Save and close',
  saveKey: 'Enter',
  discard: 'Close without saving',
  keep: 'Keep editing',
  keepKey: 'Esc',
};

function mount() {
  const chrome = mockChrome();
  render(Editor, { props: { chrome, theme: mockTheme() } });
  return chrome;
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const calls = (chrome: ReturnType<typeof mockChrome>, method: string) =>
  chrome.calls.filter((c) => c.method === method);

afterEach(() => cleanup());

describe('the close question', () => {
  it('is not there until C++ asks', () => {
    mount();
    expect(q('.wash')).toBeNull();
  });

  it('shows the Qt card words the moment it is asked', async () => {
    const chrome = mount();
    chrome.questionChanged.emit(words);
    await tick();
    expect(q('.wash .title')!.textContent).toBe('Save changes to Bench before closing?');
    expect(q('.wash .body')!.textContent).toBe('You have changes since the last save.');
    const labels = [...document.querySelectorAll('.answer .label')].map((n) => n.textContent);
    expect(labels).toEqual(['Save and close', 'Close without saving', 'Keep editing']);
    expect(q('.answer.save .key')!.textContent).toBe('Enter');
    expect(q('.answer.keep .key')!.textContent).toBe('Esc');
    expect(q('.answer.discard .key')).toBeNull();
    expect(q('.card')!.getAttribute('role')).toBe('alertdialog');
  });

  it('takes every press over the viewport, and makes the rest of the page inert', async () => {
    const chrome = mount();
    chrome.questionChanged.emit(words);
    await tick();
    expect(q('.wash')!.hasAttribute('data-hit')).toBe(true);
    expect((q('.behind') as HTMLElement & { inert: boolean }).inert).toBe(true);
  });

  it('stops above the status strip, so its unsaved dot stays readable', async () => {
    const chrome = mount();
    chrome.questionChanged.emit(words);
    await tick();
    expect(q('.wash')!.style.height).toBe(`${window.innerHeight - 24}px`);
  });

  it('leaves the window buttons clear of its wash', async () => {
    const chrome = mount();
    chrome.questionChanged.emit({ ...words, keepClear: { x: 1130, y: 16, w: 130, h: 36 } });
    await tick();
    const clip = q('.wash')!.style.clipPath;
    expect(clip).toContain('evenodd');
    expect(clip).toContain('1130px 16px');
    expect(clip).toContain('1260px 52px');
  });

  it('answers through the bridge, each button its own word', async () => {
    const chrome = mount();
    chrome.questionChanged.emit(words);
    await tick();
    await fireEvent.click(q('.answer.discard')!);
    await fireEvent.click(q('.answer.keep')!);
    await fireEvent.click(q('.answer.save')!);
    expect(calls(chrome, 'answerQuestion').map((c) => c.args[0])).toEqual(['discard', 'keep', 'save']);
  });

  it('goes when C++ says the question ended', async () => {
    const chrome = mount();
    chrome.questionChanged.emit(words);
    await tick();
    chrome.questionChanged.emit({ asking: false });
    await tick();
    expect(q('.wash')).toBeNull();
    expect((q('.behind') as HTMLElement & { inert: boolean }).inert).toBe(false);
  });

  it('reads a question already standing when the page arrives', async () => {
    const chrome = mockChrome();
    chrome.question = words;
    render(Editor, { props: { chrome, theme: mockTheme() } });
    await tick();
    expect(q('.wash .title')!.textContent).toContain('Bench');
  });

  it('takes no keyboard focus, and says nothing the vocabulary law forbids', async () => {
    const chrome = mount();
    chrome.questionChanged.emit(words);
    await tick();
    for (const b of document.querySelectorAll('.answer')) expect((b as HTMLElement).tabIndex).toBe(-1);
    const copy = [words.body, words.save, words.saveKey, words.discard, words.keep, words.keepKey,
      'Save changes to', 'before closing?'];
    expect(bannedWordsIn(copy)).toEqual([]);
  });
});

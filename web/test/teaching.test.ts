// The first-run guide and the hint balloon (migration Phase 7a) on the
// editor's chrome page, against a mock EditorChrome: each is drawn at the
// place C++ gives it, as a hit region, and answers through C++ - the guide's
// skip and the balloon's "got it" - which is what retires them.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { mockChrome } from '../src/editor/mock';
import { mockTheme } from '../src/hub/mock';
import { bannedWordsIn } from './banned';

const guide = {
  shown: true,
  title: 'Your first build',
  skip: 'skip',
  steps: [
    { text: 'Start a sketch — K', done: true },
    { text: 'Click points on the ground', done: false },
    { text: 'Close the outline', done: false },
    { text: 'Extrude it — E', done: false },
  ],
  skipRect: { x: 212, y: 8, w: 34, h: 18 },
  x: 900,
  y: 560,
  w: 260,
  h: 148,
};

const hint = { shown: true, text: 'Two bodies selected — Union, Subtract or Intersect them', gotIt: 'got it', x: 400, y: 600, w: 250, h: 70 };

function mount() {
  const chrome = mockChrome();
  render(Editor, { props: { chrome, theme: mockTheme() } });
  return chrome;
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
const calls = (chrome: ReturnType<typeof mockChrome>, method: string) =>
  chrome.calls.filter((c) => c.method === method);

afterEach(() => cleanup());

describe('the first-run guide', () => {
  it('draws the title and the four steps, marking the done ones', async () => {
    const chrome = mount();
    chrome.guideChanged.emit(guide);
    await tick();
    const card = q('.guide')!;
    expect(card.style.left).toBe('900px');
    expect(card.style.width).toBe('260px');
    expect(card.hasAttribute('data-hit')).toBe(true);
    expect(q('.guide .title')!.textContent).toBe('Your first build');
    const steps = [...document.querySelectorAll('.guide .steps li')];
    expect(steps.length).toBe(4);
    expect(steps.map((s) => s.classList.contains('done'))).toEqual([true, false, false, false]);
    expect(steps[0].textContent).toBe('✓  Start a sketch — K');
  });

  it('puts skip where the Qt panel puts it, and skipping answers through C++', async () => {
    const chrome = mount();
    chrome.guideChanged.emit(guide);
    await tick();
    const skip = q('.guide .skip')!;
    expect(skip.style.left).toBe('212px');
    expect(skip.textContent).toBe('skip');
    await fireEvent.click(skip);
    expect(calls(chrome, 'skipGuide').length).toBe(1);
    // C++ hides the guide, and the card goes with it.
    chrome.guideChanged.emit({ shown: false });
    await tick();
    expect(q('.guide')).toBeNull();
  });
});

describe('the hint balloon', () => {
  it('draws the hint and its got it, at its own place, as a hit region', async () => {
    const chrome = mount();
    chrome.hintChanged.emit(hint);
    await tick();
    const card = q('.hint')!;
    expect(card.style.top).toBe('600px');
    expect(card.hasAttribute('data-hit')).toBe(true);
    expect(q('.hint .text')!.textContent).toBe(hint.text);
    expect(q('.hint .got-it')!.textContent).toBe('got it');
  });

  it('a press anywhere on it is its got it', async () => {
    const chrome = mount();
    chrome.hintChanged.emit(hint);
    await tick();
    await fireEvent.click(q('.hint .text')!);
    expect(calls(chrome, 'dismissHint').length).toBe(1);
  });

  it('paints none of the banned words, guide included', async () => {
    const chrome = mount();
    chrome.guideChanged.emit(guide);
    chrome.hintChanged.emit(hint);
    await tick();
    const texts = [...document.querySelectorAll('.guide .title, .guide li, .guide .skip, .hint .text, .hint .got-it')].map(
      (n) => n.textContent ?? '',
    );
    expect(texts.length).toBe(8);
    expect(bannedWordsIn(texts)).toEqual([]);
  });
});

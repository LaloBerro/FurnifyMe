// The loading card (migration Phase 7a) on the editor's chrome page, against a
// mock EditorChrome: LoadingCard's words on the question channel draw the
// mark, the furniture's name, a bar filled to done/total and the step - and
// nothing to press, since the Qt card swallows every press itself.
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, render } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { mockChrome } from '../src/editor/mock';
import { mockTheme } from '../src/hub/mock';

function mount() {
  const chrome = mockChrome();
  render(Editor, { props: { chrome, theme: mockTheme() } });
  return chrome;
}

async function loading(chrome: ReturnType<typeof mockChrome>, extra = {}) {
  chrome.questionChanged.emit({
    asking: true,
    kind: 'loading',
    title: 'Kitchen dresser',
    step: 'Putting the bodies on screen',
    done: 3,
    total: 12,
    ...extra,
  });
  await tick();
  await tick();
}

const q = (sel: string) => document.querySelector(sel) as HTMLElement | null;
// Svelte sets the inert PROPERTY; jsdom keeps just that (hub.test.ts).
const inert = (el: Element) => (el as HTMLElement & { inert?: boolean }).inert === true || el.hasAttribute('inert');

afterEach(() => cleanup());

describe('the loading card', () => {
  it('draws the name and the step on the wash, and no question', async () => {
    const chrome = mount();
    await loading(chrome);
    expect(q('.loading-card')).not.toBeNull();
    expect(q('.loading-card .title')!.textContent).toBe('Kitchen dresser');
    expect(q('.loading-card .step')!.textContent).toBe('Putting the bodies on screen');
    expect(q('.wash')).not.toBeNull();
    expect(q('.answer')).toBeNull();
    expect(document.querySelectorAll('.loading-card button').length).toBe(0);
  });

  it('fills the bar to done over total, and shows a stub before the total is known', async () => {
    const chrome = mount();
    await loading(chrome);
    expect(q('.loading-card .fill')!.style.width).toBe('25%');
    await loading(chrome, { done: 0, total: 0, step: 'Reading the file' });
    expect(q('.loading-card .fill')!.style.width).toBe('12%');
    await loading(chrome, { done: 30, total: 12 });
    expect(q('.loading-card .fill')!.style.width).toBe('100%');
  });

  it('goes when the card ends, and puts nothing inert behind it then', async () => {
    const chrome = mount();
    await loading(chrome);
    expect(inert(q('.behind')!)).toBe(true);
    chrome.questionChanged.emit({ asking: false });
    await tick();
    expect(q('.loading-card')).toBeNull();
    expect(inert(q('.behind')!)).toBe(false);
  });
});

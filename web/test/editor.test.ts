// The editor's chrome page: the toast (Toast.cpp, ported) and the status
// strip (the Qt status bar, ported), against a mock of EditorChrome.
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/svelte';
import { tick } from 'svelte';

import Editor from '../src/editor/Editor.svelte';
import { allCopy, copy } from '../src/editor/copy';
import { mockChrome } from '../src/editor/mock';
import { mockTheme } from '../src/hub/mock';
import { bannedWordsIn } from './banned';

function mount() {
  const chrome = mockChrome();
  render(Editor, { props: { chrome, theme: mockTheme() } });
  return chrome;
}

const toastEl = () => document.querySelector('.toast') as HTMLElement | null;

async function settle() {
  await tick();
  await tick();
}

beforeEach(() => vi.useFakeTimers());
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe('the toast', () => {
  it('shows a Note for 4 s, then fades out', async () => {
    const chrome = mount();
    chrome.toast.emit('Deleted Body 02', 'note', true, 7);
    await settle();
    expect(toastEl()!.textContent).toContain('Deleted Body 02');
    expect(toastEl()!.classList.contains('failure')).toBe(false);
    expect(document.querySelector('.toast .glyph')).toBeNull();
    vi.advanceTimersByTime(3990);
    await settle();
    expect(toastEl()).not.toBeNull();
    vi.advanceTimersByTime(10 + 200);
    await settle();
    expect(toastEl()).toBeNull();
  });

  it('shows a Failure for 8 s, with the warning mark and the danger border', async () => {
    const chrome = mount();
    chrome.toast.emit('Pull refused — the carve is deeper than the body', 'failure', false, -1);
    await settle();
    expect(toastEl()!.classList.contains('failure')).toBe(true);
    expect(document.querySelector('.toast .glyph')).not.toBeNull();
    vi.advanceTimersByTime(7990);
    await settle();
    expect(toastEl()).not.toBeNull();
    vi.advanceTimersByTime(210);
    await settle();
    expect(toastEl()).toBeNull();
  });

  it('a second message replaces the first and restarts the countdown', async () => {
    const chrome = mount();
    chrome.toast.emit('First', 'note', false, -1);
    await settle();
    vi.advanceTimersByTime(3000);
    chrome.toast.emit('Second', 'note', false, -1);
    await settle();
    expect(document.querySelectorAll('.toast')).toHaveLength(1);
    expect(toastEl()!.textContent).toContain('Second');
    vi.advanceTimersByTime(3000);
    await settle();
    expect(toastEl()).not.toBeNull();
  });

  it('offers Undo, which asks C++, and not while Undo is closed', async () => {
    const chrome = mount();
    chrome.toast.emit('Deleted Body 02', 'note', true, 7);
    await settle();
    const pill = screen.getByRole('button', { name: copy.undo });
    await fireEvent.click(pill);
    expect(chrome.calls.filter((c) => c.method === 'undo')).toHaveLength(1);
    chrome.undoEnabled = false;
    chrome.undoEnabledChanged.emit(false);
    await settle();
    expect(pill.getAttribute('aria-disabled')).toBe('true');
    await fireEvent.click(pill);
    expect(chrome.calls.filter((c) => c.method === 'undo')).toHaveLength(1);
  });

  it("takes Undo's state from the message itself, not from a later property update", async () => {
    const chrome = mount();
    chrome.undoEnabled = false;
    chrome.undoEnabledChanged.emit(false);
    await settle();
    chrome.toast.emit('Deleted Body 02', 'note', true, 7, -1, -1, true);
    await settle();
    expect(screen.getByRole('button', { name: copy.undo }).getAttribute('aria-disabled')).toBe('false');
  });

  it('is dismissed when the document moves past the change it names', async () => {
    const chrome = mount();
    chrome.toast.emit('Deleted Body 02', 'note', true, 7);
    await settle();
    chrome.documentMoved.emit(7);
    await settle();
    expect(toastEl()).not.toBeNull();
    chrome.documentMoved.emit(8);
    vi.advanceTimersByTime(200);
    await settle();
    expect(toastEl()).toBeNull();
  });

  it('a message with no stamp outlives document changes', async () => {
    const chrome = mount();
    chrome.toast.emit('Saved', 'note', false, -1);
    await settle();
    chrome.documentMoved.emit(9);
    vi.advanceTimersByTime(200);
    await settle();
    expect(toastEl()).not.toBeNull();
  });

  it('waits while the pointer rests on it, and resumes with at least 1.5 s', async () => {
    const chrome = mount();
    chrome.toast.emit('Deleted Body 02', 'note', true, 7);
    await settle();
    vi.advanceTimersByTime(3900);
    await fireEvent.mouseEnter(toastEl()!);
    vi.advanceTimersByTime(10000);
    await settle();
    expect(toastEl()).not.toBeNull();
    await fireEvent.mouseLeave(toastEl()!);
    vi.advanceTimersByTime(1400);
    await settle();
    expect(toastEl()).not.toBeNull();
    vi.advanceTimersByTime(100 + 200);
    await settle();
    expect(toastEl()).toBeNull();
  });

  it('is announced: politely for a Note, assertively for a Failure, never as an alert', async () => {
    const chrome = mount();
    chrome.toast.emit('Saved', 'note', false, -1);
    await settle();
    expect(toastEl()!.closest('[aria-live]')!.getAttribute('aria-live')).toBe('polite');
    chrome.toast.emit('Refused', 'failure', false, -1);
    await settle();
    expect(toastEl()!.closest('[aria-live]')!.getAttribute('aria-live')).toBe('assertive');
    expect(document.querySelector('[role=alert]')).toBeNull();
  });

  it('a message arriving during a fade is not swept away by it (review C1)', async () => {
    const chrome = mount();
    chrome.toast.emit('Deleted Body 01', 'note', true, 7);
    await settle();
    // Every commit moves the document BEFORE it reports, so the old toast
    // starts fading and the new one lands inside that fade.
    chrome.documentMoved.emit(8);
    await settle();
    chrome.toast.emit('Deleted Body 02', 'note', true, 8);
    await settle();
    vi.advanceTimersByTime(200);
    await settle();
    expect(toastEl()).not.toBeNull();
    expect(toastEl()!.textContent).toContain('Deleted Body 02');
    vi.advanceTimersByTime(3000);
    await settle();
    expect(toastEl()).not.toBeNull();
  });

  it('Undo is taken once, and carries the change it names (review I1)', async () => {
    const chrome = mount();
    chrome.toast.emit('Deleted Body 02', 'note', true, 7);
    await settle();
    const pill = screen.getByRole('button', { name: copy.undo });
    await fireEvent.click(pill);
    await fireEvent.click(pill);
    const undos = chrome.calls.filter((c) => c.method === 'undo');
    expect(undos).toHaveLength(1);
    expect(undos[0].args).toEqual([7]);
  });

  it('a fading toast offers no Undo (review I1)', async () => {
    const chrome = mount();
    chrome.toast.emit('Deleted Body 02', 'note', true, 7);
    await settle();
    chrome.documentMoved.emit(9);
    await settle();
    const pill = screen.getByRole('button', { name: copy.undo });
    expect(pill.getAttribute('aria-disabled')).toBe('true');
    expect(toastEl()!.hasAttribute('data-hit')).toBe(false);
    await fireEvent.click(pill);
    expect(chrome.calls.filter((c) => c.method === 'undo')).toHaveLength(0);
  });

  it('a replacement under a resting pointer waits too (review I2)', async () => {
    const chrome = mount();
    chrome.toast.emit('First', 'note', false, -1);
    await settle();
    await fireEvent.mouseEnter(toastEl()!);
    chrome.toast.emit('Second', 'note', false, -1);
    await settle();
    vi.advanceTimersByTime(10000);
    await settle();
    expect(toastEl()).not.toBeNull();
    expect(toastEl()!.textContent).toContain('Second');
  });

  it('takes the pointer (for Undo and the hover pause)', async () => {
    const chrome = mount();
    chrome.toast.emit('Saved', 'note', false, -1);
    await settle();
    expect(toastEl()!.hasAttribute('data-hit')).toBe(true);
  });
});

describe('the status strip', () => {
  it('shows the left sentence, the name with its unsaved mark, and the state', async () => {
    const chrome = mount();
    chrome.status = {
      left: '1 body selected — 600 × 300 × 18 mm',
      right: 'Pull a face',
      name: 'Bench',
      unsaved: true,
      shown: true,
    };
    chrome.statusChanged.emit();
    await settle();
    const strip = document.querySelector('.strip') as HTMLElement;
    expect(strip.querySelector('.left')!.textContent).toBe('1 body selected — 600 × 300 × 18 mm');
    expect(strip.querySelector('.name')!.textContent).toBe(copy.nameWithUnsaved('Bench'));
    expect(strip.querySelector('.dot')).not.toBeNull();
    expect(strip.querySelector('.right')!.textContent).toBe('Pull a face');
  });

  it('carries no dot when nothing is unsaved, and no mark with no furniture', async () => {
    const chrome = mount();
    chrome.status = { left: '', right: '', name: 'Bench', unsaved: false, shown: true };
    chrome.statusChanged.emit();
    await settle();
    expect(document.querySelector('.strip .dot')).toBeNull();
    expect(document.querySelector('.strip .name')!.textContent).toBe('Bench');
    chrome.status = { left: '', right: '', name: '', unsaved: false, shown: true };
    chrome.statusChanged.emit();
    await settle();
    expect(document.querySelector('.strip .name')).toBeNull();
  });

  it('the right channel carries its whole sentence as its tooltip (review I4)', async () => {
    const chrome = mount();
    const long = 'Drag to move Side left into Carcass — release to drop, Esc cancels';
    chrome.status = { left: '', right: long, name: '', unsaved: false, shown: true };
    chrome.statusChanged.emit();
    await settle();
    expect(document.querySelector('.strip .right')!.getAttribute('title')).toBe(long);
  });

  it('follows the Bottom bar switch', async () => {
    const chrome = mount();
    chrome.status = { ...chrome.status, shown: false };
    chrome.statusChanged.emit();
    await settle();
    expect(document.querySelector('.strip')).toBeNull();
  });
});

describe('the handshake', () => {
  it('tells C++ it is ready once it is listening (review I3)', async () => {
    const chrome = mount();
    await settle();
    expect(chrome.calls.filter((c) => c.method === 'ready')).toHaveLength(1);
  });
});

describe('the vocabulary law', () => {
  it('no copy the page can show uses a banned word', () => {
    expect(bannedWordsIn(allCopy())).toEqual([]);
  });
});

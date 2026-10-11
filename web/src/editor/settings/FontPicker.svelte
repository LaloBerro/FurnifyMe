<script lang="ts">
  // The Font row's list (ruling R4: a listbox of our own, because a native
  // select cannot preview). The Qt combo applies a family on HIGHLIGHT, so
  // arrowing down the open list re-dresses the app one family at a time, and
  // Escape puts back the family the list opened over; Enter or a click keeps
  // what is showing, and so does a click outside. The list holds the keyboard
  // while it is open. Each entry shows the family's shown name and carries the
  // real family, as the combo's item data does.
  import { onDestroy, tick } from 'svelte';
  import { chip } from '../../actions';
  import type { FamilySpec, Keyboard } from './api';

  let {
    families,
    current,
    shown,
    tip,
    keyboard,
    onpreview,
  }: {
    families: FamilySpec[];
    current: string;
    shown: string;
    tip: string;
    keyboard: Keyboard;
    onpreview: (family: string) => void;
  } = $props();

  let open = $state(false);
  let highlight = $state(0);
  let list: HTMLElement | undefined = $state();
  let root: HTMLElement | undefined = $state();
  let before = '';

  function indexOfCurrent() {
    const exact = families.findIndex((f) => f.family === current);
    if (exact >= 0) return exact;
    return Math.max(0, families.findIndex((f) => f.shown === shown));
  }

  async function openList() {
    if (open) return;
    before = current;
    highlight = indexOfCurrent();
    open = true;
    keyboard.claimKeyboard();
    window.addEventListener('mousedown', outside, true);
    window.addEventListener('blur', left);
    await tick();
    list?.focus();
    list?.querySelector<HTMLElement>('[aria-selected="true"]')?.scrollIntoView?.({ block: 'nearest' });
  }

  function close(keep: boolean) {
    if (!open) return;
    open = false;
    window.removeEventListener('mousedown', outside, true);
    window.removeEventListener('blur', left);
    if (!keep && before) onpreview(before);
    before = '';
    keyboard.releaseKeyboard();
  }

  function preview(index: number) {
    if (index < 0 || index >= families.length) return;
    highlight = index;
    onpreview(families[index].family);
  }

  function key(event: KeyboardEvent) {
    switch (event.key) {
      case 'ArrowDown':
        event.preventDefault();
        preview(Math.min(families.length - 1, highlight + 1));
        break;
      case 'ArrowUp':
        event.preventDefault();
        preview(Math.max(0, highlight - 1));
        break;
      case 'Enter':
        event.preventDefault();
        close(true);
        break;
      case 'Escape':
        event.preventDefault();
        close(false);
        break;
    }
  }

  function outside(event: MouseEvent) {
    if (root && !root.contains(event.target as Node)) close(true);
  }

  // Focus leaving the page - a click on the viewport or any Qt widget - closes
  // the list keeping what it shows, as the Qt combo's popup closes on any
  // outside click (review I6); so does focus moving elsewhere on the page.
  function left() {
    close(true);
  }

  function focusOut(event: FocusEvent) {
    const next = event.relatedTarget as Node | null;
    if (!root || !next || !root.contains(next)) close(true);
  }

  onDestroy(() => close(false));
</script>

<div class="font-picker" bind:this={root}>
  <button use:chip class="current" title={tip} aria-haspopup="listbox" aria-expanded={open} onclick={openList}
    >{shown}</button
  >
  {#if open}
    <div class="list" role="listbox" tabindex="-1" bind:this={list} onkeydown={key} onfocusout={focusOut}>
      {#each families as family, i (family.family)}
        <div
          class="family"
          role="option"
          tabindex="-1"
          aria-selected={i === highlight}
          onmouseenter={() => i !== highlight && preview(i)}
          onmousedown={(e) => e.preventDefault()}
          onclick={() => {
            if (i !== highlight) preview(i);
            close(true);
          }}
          onkeydown={key}
        >
          {family.shown}
        </div>
      {/each}
    </div>
  {/if}
</div>

<style>
  .font-picker {
    position: relative;
    flex: 1;
    min-width: 0;
  }
  .current {
    width: 100%;
    height: 22px;
    padding: 0 22px 0 8px;
    border: 1px solid var(--border);
    border-radius: 5px;
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-label);
    text-align: left;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    cursor: pointer;
    position: relative;
  }
  .current::after {
    content: '';
    position: absolute;
    right: 8px;
    top: 50%;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid var(--text-muted);
    transform: translateY(-30%);
  }
  .current:hover {
    background: var(--chip-hover);
  }
  .list {
    position: absolute;
    z-index: 2;
    left: 0;
    right: 0;
    top: 24px;
    max-height: 200px;
    overflow-y: auto;
    padding: 3px;
    border: 1px solid var(--border);
    border-radius: 6px;
    background: var(--panel);
    outline: none;
  }
  .family {
    padding: 3px 6px;
    border-radius: 4px;
    font-size: var(--font-label);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    cursor: pointer;
  }
  .family[aria-selected='true'] {
    background: var(--chip-active);
    color: var(--text);
  }
</style>

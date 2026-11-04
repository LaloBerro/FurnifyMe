<script lang="ts">
  // THE ROW MENU (ruling R5 of the plan): ItemsModel's description, drawn.
  // Entries carry their own enabled state, tooltip and key text; a choice is
  // reported by id and C++ acts (and refuses past a disabled entry). A press
  // anywhere outside closes it and is swallowed, as a QMenu's is; Escape
  // closes it too.
  import { onDestroy, onMount } from 'svelte';
  import { chip } from '../../actions';
  import type { MenuEntry } from './api';

  let {
    entries,
    x,
    y,
    bounds,
    onchoose,
    onclose,
    keyboard,
  }: {
    entries: MenuEntry[];
    x: number;
    y: number;
    bounds: { w: number; h: number };
    onchoose: (id: string) => void;
    onclose: () => void;
    keyboard: { claimKeyboard: () => void; releaseKeyboard: () => void };
  } = $props();

  let box: HTMLElement | undefined = $state();
  let width = $state(220);
  let height = $state(240);
  let openSub = $state('');

  const left = $derived(Math.max(8, Math.min(x, bounds.w - width - 8)));
  const top = $derived(Math.max(8, Math.min(y, bounds.h - height - 8)));
  // The submenu opens to the right, or to the left where there is no room.
  const subLeft = $derived(left + width + 4 + 200 > bounds.w);

  function choose(entry: MenuEntry) {
    if (!entry.enabled || entry.children) return;
    onchoose(entry.id);
  }

  function onKey(event: KeyboardEvent) {
    if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      onclose();
    }
  }

  onMount(() => {
    // Escape is the menu's while it stands, whatever holds focus.
    keyboard.claimKeyboard();
    window.addEventListener('keydown', onKey, true);
    if (box) {
      const r = box.getBoundingClientRect();
      if (r.width > 0) width = r.width;
      if (r.height > 0) height = r.height;
    }
  });
  onDestroy(() => {
    window.removeEventListener('keydown', onKey, true);
    keyboard.releaseKeyboard();
  });
</script>

<!-- The layer that takes a press outside the menu: it closes the menu and
     the press goes no further (data-hit, so the viewport never sees it). -->
<div
  class="outside"
  data-hit
  role="presentation"
  onmousedown={(e) => {
    e.preventDefault();
    onclose();
  }}
  oncontextmenu={(e) => e.preventDefault()}
></div>
<div
  class="menu themed-rim"
  data-hit
  role="menu"
  tabindex="-1"
  bind:this={box}
  style="left: {left}px; top: {top}px;"
  oncontextmenu={(e) => e.preventDefault()}
>
  {#each entries as entry (entry.id)}
    <div class="entry-wrap" role="none" onmouseenter={() => (openSub = entry.children ? entry.id : '')}>
      <button
        use:chip
        class="entry"
        class:sub={!!entry.children}
        role="menuitem"
        data-entry={entry.id}
        disabled={!entry.enabled}
        title={entry.tip || undefined}
        aria-haspopup={entry.children ? 'menu' : undefined}
        onclick={() => (entry.children ? (openSub = openSub === entry.id ? '' : entry.id) : choose(entry))}
      >
        <span class="label">{entry.label}</span>
        {#if entry.keys}<span class="keys">{entry.keys}</span>{/if}
        {#if entry.children}<span class="arrow">›</span>{/if}
      </button>
      {#if entry.children && openSub === entry.id && entry.enabled}
        <div class="menu submenu themed-rim" data-hit role="menu" class:left={subLeft}>
          {#each entry.children as child (child.id)}
            <button
              use:chip
              class="entry"
              role="menuitem"
              data-entry={child.id}
              disabled={!child.enabled}
              style="padding-left: {12 + child.depth * 12}px;"
              onclick={() => choose(child)}
            >
              <span class="label">{child.label}</span>
              {#if child.keys}<span class="keys">{child.keys}</span>{/if}
            </button>
          {/each}
        </div>
      {/if}
    </div>
  {/each}
</div>

<style>
  .outside {
    position: fixed;
    inset: 0;
    background: transparent;
  }
  .menu {
    position: fixed;
    box-sizing: border-box;
    min-width: 200px;
    padding: 4px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-small, 6px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, 0 6px 18px rgba(0, 0, 0, 0.35));
    color: var(--text);
    font-size: var(--font-body);
    outline: none;
  }
  .entry-wrap {
    position: relative;
  }
  .submenu {
    position: absolute;
    left: calc(100% + 4px);
    top: -5px;
  }
  .submenu.left {
    left: auto;
    right: calc(100% + 4px);
  }
  .entry {
    display: flex;
    align-items: center;
    gap: 16px;
    width: 100%;
    padding: 6px 12px;
    border: none;
    border-radius: var(--r-tiny, 4px);
    background: transparent;
    color: inherit;
    font: inherit;
    text-align: left;
    cursor: pointer;
    white-space: nowrap;
  }
  .entry:hover:not(:disabled) {
    background: var(--chip-hover);
  }
  .entry:disabled {
    color: var(--text-disabled);
    cursor: default;
  }
  .label {
    flex: 1;
  }
  .keys,
  .arrow {
    color: var(--text-muted);
    font-size: var(--font-label);
  }
</style>

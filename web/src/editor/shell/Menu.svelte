<script lang="ts">
  // The open menu (Phase 3i): MenuModel's levels, drawn - the menu under its
  // title, each submenu beside the entry that opened it (to the left where
  // the viewport has no room). The pointer over an entry makes it current
  // and opens a submenu; a click chooses. Every decision - which entries are
  // live, what a key does, whether the action may run - is C++'s: the keys
  // never reach this page (an open menu takes them through an
  // application-wide filter), and a choice of a disabled entry is refused
  // there. A press outside closes the menu and goes no further (ruling R7).
  import { tick } from 'svelte';
  import { chip } from '../../actions';
  import type { MenuApi, MenuEntryData, MenuOpen } from './api';

  let {
    menu,
    open,
    anchor,
    bounds,
  }: {
    menu: MenuApi;
    open: MenuOpen;
    // Where the open menu's title stands, in the viewport.
    anchor: { x: number; y: number; w: number; h: number } | null;
    bounds: { w: number; h: number };
  } = $props();

  let boxes: (HTMLElement | undefined)[] = $state([]);
  let places: { left: number; top: number }[] = $state([]);

  async function place() {
    await tick();
    const next: { left: number; top: number }[] = [];
    for (let level = 0; level < open.levels.length; level++) {
      const box = boxes[level];
      const w = box?.offsetWidth ?? 200;
      const h = box?.offsetHeight ?? 100;
      let left = 0;
      let top = 0;
      if (level === 0) {
        // Not placed until its title's rectangle is known: a menu drawn at
        // the corner for a frame flashed over the pill on every open.
        if (!anchor) {
          places = [];
          return;
        }
        left = anchor.x;
        top = anchor.y + anchor.h + 4;
      } else {
        const parent = boxes[level - 1];
        const row = parent?.querySelector<HTMLElement>(`[data-entry="${open.levels[level - 1].current}"]`);
        const r = row?.getBoundingClientRect();
        const p = next[level - 1];
        const parentW = parent?.offsetWidth ?? 200;
        left = p.left + parentW + 2;
        if (left + w > bounds.w - 4) left = p.left - w - 2;
        top = r ? r.top - 5 : p.top;
      }
      left = Math.max(4, Math.min(left, bounds.w - w - 4));
      top = Math.max(4, Math.min(top, bounds.h - h - 4));
      next.push({ left, top });
    }
    places = next;
  }

  $effect(() => {
    void open;
    void anchor;
    void bounds.w;
    void bounds.h;
    place();
  });

  function label(entry: MenuEntryData, byKeyboard: boolean) {
    const text = entry.text ?? '';
    const at = byKeyboard ? (entry.mnemonicAt ?? -1) : -1;
    return { before: at >= 0 ? text.slice(0, at) : text, key: at >= 0 ? text.charAt(at) : '', after: at >= 0 ? text.slice(at + 1) : '' };
  }
</script>

<!-- The layer that takes a press outside the menu: it closes the menu and
     the press goes no further (data-hit, so the viewport never sees it). The
     pill stands over it, so a press on another title switches menus. -->
<div
  class="outside"
  data-hit
  data-menu-outside
  role="presentation"
  onmousedown={(e) => {
    e.preventDefault();
    menu.close();
  }}
  oncontextmenu={(e) => e.preventDefault()}
></div>
{#each open.levels as level, li (li)}
  <div
    class="menu themed-rim"
    data-hit
    data-menu-level={li}
    tabindex="-1"
    role="menu"
    bind:this={boxes[li]}
    style="left: {places[li]?.left ?? -9999}px; top: {places[li]?.top ?? -9999}px;"
    oncontextmenu={(e) => e.preventDefault()}
  >
    {#each level.entries as entry, ei (ei)}
      {#if entry.separator}
        <div class="separator" role="separator"></div>
      {:else}
        {@const parts = label(entry, open.byKeyboard)}
        <button
          use:chip
          class="entry"
          class:current={level.current === ei}
          data-entry={ei}
          role={entry.radio ? 'menuitemradio' : entry.checkable ? 'menuitemcheckbox' : 'menuitem'}
          aria-checked={entry.checkable ? !!entry.checked : undefined}
          aria-haspopup={entry.submenu ? 'menu' : undefined}
          aria-disabled={!entry.enabled}
          class:disabled={!entry.enabled}
          onmouseenter={() => menu.hover(li, ei)}
          onmousedown={(e) => e.preventDefault()}
          onclick={() => {
            if (entry.enabled) menu.choose(li, ei);
          }}
        >
          <span class="mark" aria-hidden="true">{entry.checked ? (entry.radio ? '•' : '✓') : ''}</span>
          <span class="label">{parts.before}{#if parts.key}<u>{parts.key}</u>{/if}{parts.after}</span>
          {#if entry.shortcut}<span class="keys">{entry.shortcut}</span>{/if}
          {#if entry.submenu}<span class="arrow" aria-hidden="true">›</span>{/if}
        </button>
      {/if}
    {/each}
  </div>
{/each}

<style>
  .outside {
    position: fixed;
    inset: 0;
    background: transparent;
    z-index: 2;
  }
  /* QMenu's Graphite rule: the panel with its border, 4 px of padding, items
     at 6 px 24 px 6 px 12 px, the current one in chip-hover, disabled in
     text-disabled, a 1 px separator 4 px / 8 px in - on the page's card. */
  .menu {
    position: fixed;
    box-sizing: border-box;
    min-width: 200px;
    padding: 4px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-small, 6px);
    /* Opaque under the theme's glass: a menu stands over cards, and a
       nearly-opaque fill let a card's words show through its entries. */
    background: var(--panel);
    box-shadow: var(--card-shadow, 0 6px 18px rgba(0, 0, 0, 0.35));
    color: var(--text);
    font-size: var(--font-body);
    z-index: 4;
    isolation: isolate;
  }
  .menu::before {
    content: '';
    position: absolute;
    inset: 0;
    z-index: -1;
    border-radius: inherit;
    background: var(--card-fill, var(--panel));
    pointer-events: none;
  }
  .entry {
    display: flex;
    align-items: center;
    gap: 8px;
    width: 100%;
    padding: 6px 24px 6px 6px;
    border: none;
    border-radius: var(--r-tiny, 4px);
    background: transparent;
    color: inherit;
    font: inherit;
    text-align: left;
    white-space: nowrap;
    cursor: pointer;
  }
  .entry.current {
    background: var(--chip-hover);
  }
  .entry.disabled {
    color: var(--text-disabled);
    cursor: default;
  }
  .mark {
    flex: none;
    width: 12px;
    text-align: center;
    color: var(--accent-text);
  }
  .label {
    flex: 1;
  }
  .keys {
    margin-left: 24px;
    color: var(--text-muted);
    font-size: var(--font-label);
  }
  .arrow {
    color: var(--text-muted);
  }
  .separator {
    height: 1px;
    margin: 4px 8px;
    background: var(--border);
  }
</style>

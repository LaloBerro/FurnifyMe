<script lang="ts">
  // The pill (Phase 3i): the app mark, the wordmark and the menu titles -
  // AppBar, drawn. Fully rounded ends (radius = half its height), its
  // horizontal padding the radius, so nothing inside meets the curve. It
  // measures its own row and reports it (ruling R9): the slot takes the size,
  // and the window's caption hit test reads the titles' rectangles, so the
  // ground drags the window and a title does not (R10). A press on a title
  // opens its menu, a second press closes it, and while one is open the
  // pointer moving onto another title switches to it - QMenuBar's behaviour.
  import { onDestroy, tick } from 'svelte';
  import { fly } from 'svelte/transition';
  import { chip } from '../../actions';
  import mark from '../../assets/app-mark.png';
  import type { MenuApi, MenuOpen, MenuTitle, Rect } from './api';

  let {
    menu,
    titles,
    open,
    wordmark,
    rect,
    animate,
    report,
  }: {
    menu: MenuApi;
    titles: MenuTitle[];
    open: MenuOpen;
    wordmark: string;
    rect: { x: number; y: number; w: number; h: number };
    animate: boolean;
    report: (width: number, height: number, titles: Rect[]) => void;
  } = $props();

  let pill: HTMLElement | undefined = $state();
  let lastSent = '';

  async function measure() {
    await tick();
    if (!pill) return;
    const box = pill.getBoundingClientRect();
    const rects: Rect[] = [];
    for (const el of pill.querySelectorAll<HTMLElement>('[data-title]')) {
      const r = el.getBoundingClientRect();
      rects.push({ x: Math.floor(r.left), y: Math.floor(r.top), w: Math.ceil(r.width), h: Math.ceil(r.height) });
    }
    const width = Math.ceil(box.width);
    const height = Math.ceil(box.height);
    const key = JSON.stringify([width, height, rects]);
    if (key === lastSent) return;
    lastSent = key;
    report(width, height, rects);
  }

  $effect(() => {
    void rect.x;
    void rect.y;
    void titles;
    measure();
  });
  let observer: ResizeObserver | undefined;
  $effect(() => {
    if (!pill) return;
    observer = new ResizeObserver(() => measure());
    observer.observe(pill);
    return () => observer?.disconnect();
  });
  onDestroy(() => observer?.disconnect());

  function press(index: number) {
    if (open.menu === index) menu.close();
    else menu.openMenu(index, false);
  }

  const slide = (node: Element) => fly(node, { y: -(rect.y + rect.h + 8), duration: animate ? 160 : 0 });
</script>

<div
  class="pill themed-rim"
  data-hit
  data-pill
  role="menubar"
  bind:this={pill}
  style="left: {rect.x}px; top: {rect.y}px;"
  transition:slide
>
  <img class="mark" src={mark} alt="" width="20" height="20" />
  <span class="wordmark">{wordmark}</span>
  {#each titles as title, i (i)}
    <button
      use:chip
      class="title"
      class:active={open.menu === i}
      data-title={i}
      role="menuitem"
      aria-haspopup="menu"
      aria-expanded={open.menu === i}
      disabled={!title.enabled}
      onmousedown={(e) => {
        if (e.button === 0) press(i);
      }}
      onmouseenter={() => {
        if (open.menu >= 0 && open.menu !== i) menu.openMenu(i, false);
      }}
    >
      {#if open.byKeyboard && title.mnemonicAt >= 0}
        {title.title.slice(0, title.mnemonicAt)}<u>{title.title.charAt(title.mnemonicAt)}</u>{title.title.slice(title.mnemonicAt + 1)}
      {:else}
        {title.title}
      {/if}
    </button>
  {/each}
</div>

<style>
  /* AppBar: a pill of 6 px top and bottom, its ends' radius half its height
     and its sides padded by that radius; the mark 20 px; 8 px between mark,
     wordmark and the titles; QMenuBar's items at 6 px 12 px. A theme's words
     arrive as --shell-menu-*. */
  .pill {
    --pill-h: calc(max(20px, 1.25em + 12px) + 12px);
    position: fixed;
    box-sizing: border-box;
    display: flex;
    align-items: center;
    height: var(--pill-h);
    padding: 6px calc(var(--pill-h) / 2);
    border: 1px solid var(--card-edge, var(--border));
    /* Half its height (AppBar), or the theme's card radius where it is
       smaller - Ultraviolet's pill is square, as its Qt one is. */
    border-radius: var(--shell-card-radius, 999px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    color: var(--shell-menu-ink, var(--text));
    font-size: var(--shell-menu-text, var(--font-body));
    white-space: nowrap;
    z-index: 3;
  }
  .mark {
    flex: none;
    display: block;
    width: 20px;
    height: 20px;
  }
  .wordmark {
    margin: 0 8px;
    color: var(--text);
    font-size: var(--shell-menu-wordmark-size, var(--font-body));
    font-weight: var(--shell-menu-wordmark-weight, 700);
  }
  .title {
    padding: 6px var(--shell-menu-item-pad-x, 12px);
    border: none;
    border-radius: var(--shell-menu-item-radius, 4px);
    background: transparent;
    color: inherit;
    font: inherit;
    font-weight: var(--shell-menu-weight, 400);
    line-height: 1.25;
    cursor: pointer;
  }
  .title:hover:not(:disabled),
  .title.active {
    background: var(--shell-menu-hover, var(--chip-hover));
  }
  .title:disabled {
    color: var(--text-disabled);
    cursor: default;
  }
</style>

<script lang="ts">
  // A column of chips on a card (Phase 3i): the rail, or the view cluster
  // under the axis card - ToolCluster, drawn. WHERE it stands is the overlay's
  // (its slot's rectangle); the rail's slot is as tall as the viewport in a
  // theme whose rail runs full height, the chips staying at the top.
  //
  // It measures itself (review I1): a theme's chips are their own size, so
  // the card reports its natural size - the column plus the card's padding -
  // and the slot takes it, the way the pill does (ruling R9). Each chip's
  // rectangle is reported too, so C++ can stand the Add-shape flyout beside
  // its chip. Both are read from LAYOUT (offsets from the slot's rectangle),
  // never from getBoundingClientRect(): the card flies in under a transform,
  // and a rectangle read mid-flight is where the card was, not where it lands
  // (review C1). The rail flies in from the left as its slot shows and out
  // as it hides, on the page's own clock (ruling R8).
  import { onDestroy, tick } from 'svelte';
  import { fly } from 'svelte/transition';
  import Chip from './Chip.svelte';
  import type { ChipState, Rect, ShellEntry } from './api';

  let {
    name,
    entries,
    chips,
    glyphs,
    rect,
    animate,
    from = 'none',
    ontrigger,
    report,
    reportSize,
  }: {
    name: string;
    entries: ShellEntry[];
    chips: Record<string, ChipState>;
    glyphs: Record<string, string>;
    rect: { x: number; y: number; w: number; h: number };
    animate: boolean;
    from?: 'left' | 'none';
    ontrigger: (id: string) => void;
    report: (rects: Record<string, Rect>) => void;
    reportSize?: (width: number, height: number) => void;
  } = $props();

  // ToolCluster's content margin: the card's border is painted inside it.
  const kPad = 8;

  let card: HTMLElement | undefined = $state();
  let column: HTMLElement | undefined = $state();
  let lastRects = '';
  let lastSize = '';

  async function send() {
    await tick();
    if (!card || !column) return;
    const rects: Record<string, Rect> = {};
    for (const el of column.querySelectorAll<HTMLElement>('[data-chip]')) {
      rects[el.dataset.chip!] = {
        x: Math.round(rect.x + column.offsetLeft + el.offsetLeft),
        y: Math.round(rect.y + column.offsetTop + el.offsetTop),
        w: el.offsetWidth,
        h: el.offsetHeight,
      };
    }
    const key = JSON.stringify(rects);
    if (key !== lastRects) {
      lastRects = key;
      report(rects);
    }
    const w = Math.ceil(column.offsetWidth + 2 * kPad);
    const h = Math.ceil(column.offsetHeight + 2 * kPad);
    const size = `${w}x${h}`;
    if (reportSize && w > 2 * kPad && size !== lastSize) {
      lastSize = size;
      reportSize(w, h);
    }
  }

  // Re-sent whenever the card moves or the chips change size (a theme).
  $effect(() => {
    void rect.x;
    void rect.y;
    void rect.w;
    void rect.h;
    void entries;
    send();
  });
  let observer: ResizeObserver | undefined;
  $effect(() => {
    if (!column) return;
    observer = new ResizeObserver(() => send());
    observer.observe(column);
    for (const el of column.querySelectorAll('[data-chip]')) observer.observe(el);
    return () => observer?.disconnect();
  });
  onDestroy(() => observer?.disconnect());

  const slide = (node: Element) =>
    fly(node, { x: from === 'left' ? -(rect.x + rect.w) : 0, duration: animate && from !== 'none' ? 160 : 0 });
</script>

<div
  class="cluster themed-rim"
  data-hit
  data-cluster={name}
  role="toolbar"
  aria-orientation="vertical"
  bind:this={card}
  style="left: {rect.x}px; top: {rect.y}px; width: {rect.w}px; height: {rect.h}px;"
  transition:slide
>
  <div class="column" bind:this={column}>
    {#each entries as entry, i (entry.id ?? `separator-${i}`)}
      {#if entry.separator}
        <div class="separator" role="separator"></div>
      {:else if entry.id}
        <Chip id={entry.id} info={chips[entry.id]} glyph={entry.glyph ? glyphs[entry.glyph] : undefined} {ontrigger} />
      {/if}
    {/each}
  </div>
</div>

<style>
  /* ToolCluster: 8 px of content margin (the border painted inside it), 3 px
     between chips, a 7 px band around each separator's 1 px rule (inset 7),
     a 10 px corner - or the theme's card. */
  .cluster {
    position: fixed;
    box-sizing: border-box;
    display: flex;
    justify-content: center;
    align-items: flex-start;
    padding: 7px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--shell-card-radius, 10px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
  }
  .column {
    /* The chips' offsetParent, so their offsets are the column's. */
    position: relative;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 3px;
  }
  .separator {
    flex: none;
    position: relative;
    align-self: stretch;
    height: 7px;
  }
  .separator::after {
    content: '';
    position: absolute;
    left: 7px;
    right: 7px;
    top: 3px;
    height: 1px;
    background: var(--border);
  }
</style>

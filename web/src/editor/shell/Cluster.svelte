<script lang="ts">
  // A column of chips on a card (Phase 3i): the rail, or the view cluster
  // under the axis card - ToolCluster, drawn. WHERE it stands is the overlay's
  // (its slot's rectangle); the rail's slot is as tall as the viewport in a
  // theme whose rail runs full height, the chips staying at the top. Each
  // chip's rectangle is reported, so C++ can stand the Add-shape flyout
  // beside its chip. The rail flies in from the left as its slot shows and
  // out as it hides, on the page's own clock (ruling R8).
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
  } = $props();

  let card: HTMLElement | undefined = $state();
  let lastSent = '';

  async function sendRects() {
    await tick();
    if (!card) return;
    const rects: Record<string, Rect> = {};
    for (const el of card.querySelectorAll<HTMLElement>('[data-chip]')) {
      const r = el.getBoundingClientRect();
      rects[el.dataset.chip!] = { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height) };
    }
    const key = JSON.stringify(rects);
    if (key === lastSent) return;
    lastSent = key;
    report(rects);
  }

  // Re-sent whenever the card moves or the chips change size (a theme).
  $effect(() => {
    void rect.x;
    void rect.y;
    void rect.w;
    void rect.h;
    void entries;
    sendRects();
  });
  let observer: ResizeObserver | undefined;
  $effect(() => {
    if (!card) return;
    observer = new ResizeObserver(() => sendRects());
    observer.observe(card);
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
  {#each entries as entry, i (entry.id ?? `separator-${i}`)}
    {#if entry.separator}
      <div class="separator" role="separator"></div>
    {:else if entry.id}
      <Chip id={entry.id} info={chips[entry.id]} glyph={entry.glyph ? glyphs[entry.glyph] : undefined} {ontrigger} />
    {/if}
  {/each}
</div>

<style>
  /* ToolCluster: 8 px of padding, 3 px between chips, a 7 px band around
     each separator's 1 px rule (inset 7), a 10 px corner - or the theme's
     card. */
  .cluster {
    position: fixed;
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 3px;
    padding: 8px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--shell-card-radius, 10px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
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

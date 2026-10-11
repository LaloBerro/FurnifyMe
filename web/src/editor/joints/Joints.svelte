<script lang="ts">
  // THE JOINTS DRAWER as a card on the editor's chrome page (migration Phase
  // 3d) - JointsPanel, ported. Its place is the overlay's (an HtmlCardSlot in
  // the Qt drawer's TopLeft anchor) and its height is ours, reported as the
  // Versions card reports it. Which rows are open is C++'s (JointsModel, by
  // joint id): a click asks, and the row comes back open.
  //
  // The rulers are laid out against the badge font as the page paints it: a
  // hidden probe set in that font changes width whenever the theme or the type
  // scale does, and every row lays out again (Review Focus 1).
  import { onDestroy, onMount, untrack } from 'svelte';
  import { chip } from '../../actions';
  import { measurerFor } from '../../hub/usedIn';
  import type { CardRect } from '../settings/api';
  import type { JointRowData, JointsApi } from './api';
  import JointRow from './JointRow.svelte';

  let {
    joints,
    rect,
    report,
    measure: fixedMeasure,
    stripWidth,
  }: {
    joints: JointsApi;
    rect: CardRect;
    report: (name: string, height: number) => void;
    measure?: (text: string) => number; // a test's fixed measurer
    stripWidth?: number; // a test's fixed strip width
  } = $props();

  // One JointsModel for the card's whole life - read once, on purpose.
  const api: JointsApi = untrack(() => joints);
  const words = api.words;

  let rows: JointRowData[] = $state([...api.rows]);
  let live = $state(api.live);
  let enabled = $state(api.enabled);
  let body: HTMLElement | undefined = $state();
  let probe: HTMLElement | undefined = $state();
  let probeWidth = $state(0);
  let alive = true;

  api.changed.connect(() => {
    if (!alive) return;
    rows = [...api.rows];
    live = api.live;
    enabled = api.enabled;
  });

  // The badge font's measurer, rebuilt whenever the probe's width moves - a
  // theme, a font or a type-scale change.
  const measure = $derived.by(() => {
    if (fixedMeasure) return fixedMeasure;
    void probeWidth;
    if (!probe) return (s: string) => s.length * 6;
    const style = getComputedStyle(probe);
    return measurerFor(`${style.fontWeight} ${style.fontSize} ${style.fontFamily}`);
  });

  onMount(() => {
    const measureCard = () => {
      if (!body) return;
      const card = body.parentElement;
      const style = card ? getComputedStyle(card) : null;
      const border = style ? parseFloat(style.borderTopWidth) + parseFloat(style.borderBottomWidth) : 0;
      const h = Math.ceil(body.getBoundingClientRect().height + (border || 0));
      if (h <= 0) return;
      (window as unknown as { __jointsHeight?: number }).__jointsHeight = h;
      report('joints', h);
    };
    const observer = new ResizeObserver(measureCard);
    if (body) observer.observe(body);
    measureCard();
    return () => observer.disconnect();
  });
  onDestroy(() => (alive = false));
</script>

<section
  class="joints-card themed-rim"
  data-hit
  aria-label={words.title}
  style="left: {rect.x}px; top: {rect.y}px; width: {rect.w}px; height: {rect.h}px;"
>
  <span class="probe" aria-hidden="true" bind:this={probe} bind:clientWidth={probeWidth}>0123456789 front</span>
  <div class="body" bind:this={body}>
    <header>
      <span class="title">{words.title}</span>
      <span class="count">{rows.length}</span>
      <button use:chip class="close" title={words.close} aria-label={words.close} onclick={() => api.close()}>×</button>
    </header>
    <div class="list">
      {#each rows as row (row.id)}
        <JointRow
          {row}
          {words}
          {enabled}
          {measure}
          {stripWidth}
          onactivate={() => api.activate(row.id)}
          onremove={() => api.remove(row.id)}
        />
      {/each}
      {#if rows.length === 0 && live}
        <p class="empty">{words.empty}</p>
      {/if}
    </div>
  </div>
</section>

<style>
  .joints-card {
    position: fixed;
    box-sizing: border-box;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-card, 10px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    color: var(--text);
    font-size: var(--font-body);
    overflow: hidden;
  }
  .probe {
    position: absolute;
    visibility: hidden;
    white-space: nowrap;
    font-size: var(--font-badge);
    pointer-events: none;
  }
  .body {
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    min-height: 60px;
    padding: 10px 1px 8px;
  }
  header {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 0 11px 8px;
  }
  .title {
    flex: 1;
    color: var(--text);
    font-family: var(--title-family, inherit);
    font-size: var(--title-size, var(--font-title));
    font-weight: var(--title-weight, 600);
    letter-spacing: var(--title-tracking, normal);
  }
  .count {
    color: var(--text-muted);
    font-size: var(--font-label);
  }
  .close {
    width: 20px;
    height: 20px;
    padding: 0;
    border: none;
    border-radius: var(--r-tiny, 4px);
    background: transparent;
    color: var(--text-muted);
    font: inherit;
    cursor: pointer;
  }
  .close:hover {
    background: var(--chip-hover);
    color: var(--text);
  }
  .list {
    max-height: 420px;
    overflow-y: auto;
  }
  /* The app's one scrollbar description (Theme::install()). */
  .list::-webkit-scrollbar {
    width: 8px;
  }
  .list::-webkit-scrollbar-track {
    background: transparent;
  }
  .list::-webkit-scrollbar-thumb {
    background: var(--chip-active);
    border-radius: var(--r-tiny, 4px);
  }
  .list::-webkit-scrollbar-button {
    display: none;
  }
  .empty {
    margin: 0;
    padding: 4px 11px;
    color: var(--text-muted);
    white-space: pre-line;
  }
</style>

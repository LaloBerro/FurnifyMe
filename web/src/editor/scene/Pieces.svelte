<script lang="ts">
  // THE PIECES CARD (migration Phase 5b): the scene's pieces, listed, in the Qt
  // card's TopLeft place (`rect`, its slot) - the card measures its content
  // and reports the height, so the slot takes it. Every gesture is a request
  // to ScenePiecesModel, which sends the Qt card's own signals: a click
  // selects, the eye shows or hides, Remove takes two clicks, a double-click
  // renames in place, and the Add piece row adds one.
  import { onDestroy, onMount, tick } from 'svelte';
  import { chip } from '../../actions';
  import type { CardRect } from '../settings/api';
  import type { PiecesApi, PiecesState } from './api';

  let {
    pieces,
    rect,
    report,
    keyboard,
  }: {
    pieces: PiecesApi;
    rect: CardRect;
    report: (name: string, height: number) => void;
    keyboard: { claimKeyboard(): void; releaseKeyboard(): void };
  } = $props();

  let view: PiecesState = $state(pieces.state);
  let body: HTMLElement | undefined = $state();

  // The row being renamed, and what its field holds.
  let renaming = $state(0);
  let draft = $state('');
  let field: HTMLInputElement | undefined = $state();
  let holding = false;

  function claim() {
    if (holding) return;
    holding = true;
    keyboard.claimKeyboard();
  }
  function release() {
    if (!holding) return;
    holding = false;
    keyboard.releaseKeyboard();
  }

  async function beginRename(id: number, name: string) {
    renaming = id;
    draft = name;
    await tick();
    claim();
    field?.focus();
    field?.select();
  }
  function endRename(keep: boolean) {
    const id = renaming;
    renaming = 0;
    release();
    if (keep && draft.trim() !== '') pieces.rename(id, draft);
  }
  function onKey(event: KeyboardEvent) {
    if (event.isComposing) return;
    if (event.key === 'Enter') {
      event.preventDefault();
      event.stopPropagation();
      endRename(true);
    } else if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      endRename(false);
    }
  }

  function selectRow(id: number) {
    // A click anywhere else drops an armed Remove, as the Qt card's re-arm does.
    if (view.rows.some((r) => r.armed && r.id !== id)) pieces.disarm();
    pieces.select(id);
  }

  onMount(() => {
    pieces.stateChanged.connect((next) => (view = next ?? pieces.state));
    let last = 0;
    const measure = () => {
      if (!body) return;
      const card = body.parentElement;
      const style = card ? getComputedStyle(card) : null;
      const border = style ? parseFloat(style.borderTopWidth) + parseFloat(style.borderBottomWidth) : 0;
      const h = Math.ceil(body.offsetHeight + (border || 0));
      if (h <= 0 || h === last) return;
      last = h;
      report('pieces', h);
    };
    const observer = new ResizeObserver(measure);
    if (body) observer.observe(body);
    measure();
    // jsdom lays nothing out: report what the rows ask for there too.
    if (last === 0) report('pieces', 40 + 30 * view.rows.length);
    return () => observer.disconnect();
  });
  onDestroy(release);
</script>

<section
  class="pieces-card"
  data-scene="pieces"
  data-hit=""
  style:left="{rect.x}px"
  style:top="{rect.y}px"
  style:width="{rect.w}px"
>
  <div class="body" bind:this={body}>
    <div class="title">{view.title}</div>
    {#if view.rows.length === 0}
      <p class="empty">{view.empty}</p>
    {:else}
      <div class="list">
        {#each view.rows as row (row.id)}
          <div class="row" class:selected={row.selected} data-piece={row.id}>
            {#if renaming === row.id}
              <input
                type="text"
                spellcheck="false"
                autocomplete="off"
                aria-label={row.name}
                bind:this={field}
                bind:value={draft}
                onkeydown={onKey}
                onblur={() => renaming === row.id && endRename(true)}
              />
            {:else if row.reason}
              <button type="button" use:chip class="name reason" title={row.name} onclick={() => selectRow(row.id)}
                >{row.reason}</button
              >
            {:else}
              <button
                type="button"
                use:chip
                class="name"
                onclick={() => selectRow(row.id)}
                ondblclick={(e) => {
                  if (e.detail >= 3) return;
                  beginRename(row.id, row.name);
                }}>{row.name}</button
              >
            {/if}
            <button
              type="button"
              use:chip
              class="eye"
              class:shut={!row.visible}
              title={view.words.eyeTip}
              aria-label={view.words.eyeTip}
              aria-pressed={row.visible ? 'true' : 'false'}
              onclick={() => pieces.toggleVisible(row.id, !row.visible)}
            >
              <svg viewBox="0 0 16 16" aria-hidden="true"
                ><path d="M1 8s2.6-4.5 7-4.5S15 8 15 8s-2.6 4.5-7 4.5S1 8 1 8z" /><circle cx="8" cy="8" r="2" />{#if !row.visible}<line
                    x1="2"
                    y1="14"
                    x2="14"
                    y2="2"
                  />{/if}</svg
              >
            </button>
            <button
              type="button"
              use:chip
              class="remove"
              class:armed={row.armed}
              title={row.armed ? view.words.armedTip : view.words.removeTip}
              aria-label={row.armed ? view.words.armedTip : view.words.removeTip}
              onclick={() => pieces.remove(row.id)}
            >
              {#if row.armed}{view.words.armed}{:else}<svg viewBox="0 0 16 16" aria-hidden="true"
                  ><path d="M3 4.5h10M6.5 4.5V3h3v1.5M4.5 4.5l.7 8.5h5.6l.7-8.5" /></svg
                >{/if}
            </button>
          </div>
        {/each}
      </div>
    {/if}
    <button
      type="button"
      use:chip
      class="add"
      title={view.add.tip}
      disabled={!view.add.enabled}
      onclick={() => pieces.addPiece()}><span aria-hidden="true">+</span> {view.add.text}</button
    >
  </div>
</section>

<style>
  .pieces-card {
    position: fixed;
    box-sizing: border-box;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-card, 10px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    color: var(--text);
    font-size: var(--font-label);
    overflow: hidden;
  }
  .body {
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    gap: 6px;
    padding: 10px;
  }
  .title {
    color: var(--text);
    font-family: var(--title-family, inherit);
    font-size: var(--font-body);
    font-weight: var(--title-weight, 600);
  }
  .empty {
    margin: 0;
    color: var(--text-muted);
    white-space: normal;
  }
  .list {
    display: flex;
    flex-direction: column;
    gap: 2px;
    max-height: 360px;
    overflow-y: auto;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 4px;
    border-radius: var(--r-tiny, 4px);
  }
  .row.selected {
    background: var(--chip-active);
  }
  button {
    border: none;
    background: transparent;
    color: inherit;
    font: inherit;
    cursor: pointer;
  }
  .name {
    flex: 1;
    min-width: 0;
    padding: 4px 6px;
    overflow: hidden;
    text-align: left;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .name.reason {
    color: var(--danger-text);
  }
  input {
    flex: 1;
    min-width: 0;
    box-sizing: border-box;
    height: 24px;
    padding: 2px 6px;
    border: 1px solid var(--accent);
    border-radius: var(--r-tiny, 4px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    outline: none;
  }
  .eye,
  .remove {
    display: inline-flex;
    flex-shrink: 0;
    align-items: center;
    justify-content: center;
    min-width: 24px;
    height: 24px;
    padding: 0 4px;
    border-radius: var(--r-tiny, 4px);
    color: var(--text-muted);
  }
  .eye:hover,
  .remove:hover {
    background: var(--chip-hover);
    color: var(--text);
  }
  .eye.shut {
    color: var(--text-disabled);
  }
  .remove.armed {
    color: var(--danger-text);
    font-size: var(--font-badge);
  }
  svg {
    width: 14px;
    height: 14px;
    fill: none;
    stroke: currentColor;
    stroke-width: 1.3;
    stroke-linecap: round;
    stroke-linejoin: round;
  }
  .add {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 6px 10px;
    border: 1px solid var(--border);
    border-radius: var(--r-control, 6px);
    background: var(--chip);
  }
  .add:hover:not(:disabled) {
    background: var(--chip-hover);
  }
  .add:disabled {
    color: var(--text-disabled);
    cursor: default;
  }
</style>

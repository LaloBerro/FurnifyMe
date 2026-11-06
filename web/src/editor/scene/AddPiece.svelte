<script lang="ts">
  // THE ADD CARD (migration Phase 5b): "Which furniture?" - the library's
  // furniture as a list, centred over the scene's viewport, never a dialog.
  // It claims the keyboard while it stands so its one key, Escape, reaches it;
  // a choice, Go to the library and Escape are requests to AddPieceModel.
  import { onDestroy, onMount } from 'svelte';
  import { chip } from '../../actions';
  import type { AddPieceApi, AddPieceState } from './api';

  let {
    addPiece,
    host,
    keyboard,
  }: {
    addPiece: AddPieceApi;
    host: { w: number; h: number };
    keyboard: { claimKeyboard(): void; releaseKeyboard(): void };
  } = $props();

  let card: AddPieceState = $state(addPiece.state);
  let cardW = $state(0);
  let cardH = $state(0);
  let holding = false;

  const left = $derived(Math.max(8, Math.round((host.w - cardW) / 2)));
  const top = $derived(Math.max(8, Math.round((host.h - cardH) / 2)));

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

  // The keyboard goes with the card: claimed while it stands, given back as
  // it closes (whoever closes it).
  $effect(() => {
    if (card.shown) claim();
    else release();
  });
  onDestroy(release);

  function onKey(event: KeyboardEvent) {
    if (!card.shown || event.key !== 'Escape') return;
    event.preventDefault();
    addPiece.close();
  }

  onMount(() => {
    addPiece.stateChanged.connect((next) => (card = next ?? addPiece.state));
  });
</script>

<svelte:window onkeydown={onKey} />

{#if card.shown}
  <div
    class="add-card"
    data-scene="add"
    data-hit=""
    role="dialog"
    aria-labelledby="add-piece-title"
    style:left="{left}px"
    style:top="{top}px"
    bind:clientWidth={cardW}
    bind:clientHeight={cardH}
  >
    <div class="title" id="add-piece-title">{card.title}</div>
    {#if card.entries.length === 0}
      <p class="empty">{card.empty}</p>
      <button type="button" use:chip class="library" onclick={() => addPiece.goToLibrary()}>{card.library}</button>
    {:else}
      <div class="list">
        {#each card.entries as entry (entry.id)}
          <button type="button" use:chip data-entry={entry.id} onclick={() => addPiece.choose(entry.id)}>
            {entry.name}
          </button>
        {/each}
      </div>
    {/if}
    <div class="hint">{card.keyHint}</div>
  </div>
{/if}

<style>
  .add-card {
    position: fixed;
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    gap: 8px;
    width: 300px;
    max-height: 70vh;
    padding: 14px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-card, 10px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    color: var(--text);
    font-size: var(--font-label);
  }
  .title {
    font-family: var(--title-family, inherit);
    font-size: var(--font-body);
    font-weight: var(--title-weight, 600);
  }
  .empty {
    margin: 0;
    color: var(--text-muted);
  }
  .list {
    display: flex;
    flex-direction: column;
    gap: 2px;
    overflow-y: auto;
  }
  button {
    padding: 6px 10px;
    border: none;
    border-radius: var(--r-tiny, 4px);
    background: transparent;
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  button:hover {
    background: var(--chip-hover);
  }
  .library {
    border: 1px solid var(--accent);
    background: var(--chip);
    text-align: center;
  }
  .hint {
    color: var(--text-muted);
    font-size: var(--font-badge);
  }
</style>

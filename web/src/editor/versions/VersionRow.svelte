<script lang="ts">
  // One saved version (VersionsPanel::buildRealRow, ported): the picture at
  // 16:10, the name over its date, and an actions row that is always there at
  // a fixed height - its buttons appear on hover, so revealing them never
  // moves the rows below. Delete takes two clicks inside its window; the arm
  // lives in this row, which the list keys by name, so a refresh that keeps
  // the row keeps the arm (Review Focus 3). While it is armed the row is
  // Delete's: Compare and Restore step aside and the sentence widens LEFTWARD
  // from the right edge, so the second click lands where the first did (the
  // scene window's Remove rule) - beside them it does not fit the card.
  import { onDestroy } from 'svelte';
  import { chip } from '../../actions';
  import type { VersionEntry, VersionsWords } from './api';

  let {
    entry,
    words,
    enabled,
    oncompare,
    onrestore,
    ondelete,
  }: {
    entry: VersionEntry;
    words: VersionsWords;
    enabled: boolean;
    oncompare: () => void;
    onrestore: () => void;
    ondelete: () => void;
  } = $props();

  let armed = $state(false);
  let timer = 0;

  function disarm() {
    armed = false;
    clearTimeout(timer);
  }

  function onDelete() {
    if (armed) {
      // The second click inside the arm: delete. The list refreshes without
      // this row, so nothing below may touch it afterwards.
      disarm();
      ondelete();
      return;
    }
    armed = true;
    clearTimeout(timer);
    timer = window.setTimeout(disarm, words.deleteMs);
  }

  onDestroy(() => clearTimeout(timer));
</script>

<div class="version" class:armed data-version={entry.name}>
  {#if entry.thumbUrl}
    <img class="thumb" src={entry.thumbUrl} alt="" draggable="false" />
  {:else}
    <div class="thumb blank"></div>
  {/if}
  <div class="bar">
    <span class="name">{entry.name}</span>
    <span class="date">{entry.savedText}</span>
  </div>
  <div class="actions">
    {#if !armed}
      <button use:chip class="compare" disabled={!enabled} onclick={oncompare}>{words.compare}</button>
      <button use:chip class="restore" disabled={!enabled} onclick={onrestore}>{words.restore}</button>
    {/if}
    <button use:chip class="delete" disabled={!enabled} onclick={onDelete}
      >{armed ? words.deleteArmed : words.delete}</button
    >
  </div>
</div>

<style>
  .version {
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    flex-shrink: 0;
    padding: 3px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-small, 6px);
    background: var(--strip-fill, var(--chip));
  }
  .thumb {
    display: block;
    width: 100%;
    aspect-ratio: 16 / 10;
    object-fit: cover;
    border-radius: max(0px, calc(var(--r-small, 6px) - 3px));
  }
  .thumb.blank {
    background: transparent;
  }
  .bar {
    display: flex;
    flex-direction: column;
    min-width: 0;
    padding: 8px 10px;
  }
  .name {
    color: var(--text);
    font-weight: 500;
    font-size: var(--font-body);
    overflow-wrap: anywhere;
  }
  .date {
    color: var(--text-muted);
    font-size: var(--font-label);
  }
  .actions {
    box-sizing: border-box;
    display: flex;
    justify-content: flex-end;
    gap: 4px;
    height: 34px;
    padding: 6px 10px;
  }
  .actions button {
    visibility: hidden;
    height: 22px;
    padding: 2px 8px;
    border: 1px solid var(--border);
    border-radius: var(--r-tiny, 4px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-label);
    white-space: nowrap;
    cursor: pointer;
  }
  .version:hover .actions button,
  .version.armed .actions button {
    visibility: visible;
  }
  .actions button:hover:not(:disabled) {
    background: var(--chip-hover);
  }
  .actions button:disabled {
    color: var(--text-disabled);
    border-color: var(--text-disabled);
    cursor: default;
  }
  .version.armed .delete {
    color: var(--danger-text);
    border-color: var(--danger-text);
  }
</style>

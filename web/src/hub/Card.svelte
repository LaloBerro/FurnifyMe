<script lang="ts">
  // One cell: the 260 x 163 thumbnail above, the under-row below - the name
  // on the left and, at rest, the last-edited date on the right, swapped on
  // hover for Rename and Delete. Hovering the CELL is what swaps it, so an
  // entry reads as one unit. The same card serves a furniture and a scene;
  // only what a click opens differs, which the section heading already says.
  import { tick } from 'svelte';
  import type { HubApi, HubEntryData } from './api';
  import { copy } from './copy';
  import { fitUsedIn, measurerFor } from './usedIn';

  let { entry, hub }: { entry: HubEntryData; hub: HubApi } = $props();

  const kDeleteConfirmMs = 4000;

  let hovered = $state(false);
  let armed = $state(false);
  let usedIn = $state('');
  let renaming = $state(false);
  let draft = $state('');
  let field: HTMLInputElement | undefined = $state();
  let strip: HTMLElement | undefined = $state();
  let armTimer = 0;
  let settled = false;

  function open() {
    hub.choose(entry.id, entry.scene);
  }

  async function onDelete() {
    if (armed) {
      // The second click inside the arm: delete. The model refreshes the grid,
      // so nothing below may touch this card afterwards.
      disarm();
      hub.remove(entry.id, entry.scene);
      return;
    }
    armed = true;
    clearTimeout(armTimer);
    armTimer = window.setTimeout(disarm, kDeleteConfirmMs);
    // Asked here, once per arming - never per paint: it reads every scene.
    // A scene card has no "used in": nothing references a scene.
    if (!entry.scene) {
      const names = await hub.usedIn(entry.id);
      if (!armed) return;
      await tick();
      const style = strip ? getComputedStyle(strip) : null;
      const font = style ? `${style.fontSize} ${style.fontFamily}` : '12px DM Sans';
      // The strip's text column: its width less 8 px padding and a 1 px
      // border each side - the Qt hub's own arithmetic.
      usedIn = fitUsedIn(names, 260 - 2 * 8 - 2 * 8 - 2, measurerFor(font));
    }
  }

  function disarm() {
    clearTimeout(armTimer);
    armed = false;
    usedIn = '';
  }

  async function beginRename() {
    draft = entry.name;
    settled = false;
    renaming = true;
    await tick();
    field?.focus();
    field?.select();
  }

  // InlineRename's rules: Enter and losing focus commit, Escape cancels
  // outright, and an empty or unchanged name is no change at all.
  function commit() {
    if (settled) return;
    settled = true;
    renaming = false;
    const name = draft.trim();
    if (name !== '' && name !== entry.name) hub.rename(entry.id, entry.scene, name);
  }

  function cancel() {
    settled = true;
    renaming = false;
  }

  function onFieldKey(event: KeyboardEvent) {
    if (event.key === 'Enter') {
      event.preventDefault();
      commit();
    } else if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      cancel();
    }
  }
</script>

<div
  class="cell"
  role="group"
  aria-label={entry.name}
  data-card-id={entry.id}
  data-card-kind={entry.scene ? 'scene' : 'furniture'}
  onmouseenter={() => (hovered = true)}
  onmouseleave={() => (hovered = false)}
>
  <button class="thumb" class:hovered aria-label={copy.openCard(entry.name)} onclick={open}>
    {#if entry.thumbUrl}
      <img src={entry.thumbUrl} alt="" draggable="false" />
    {:else}
      <span class="placeholder" aria-hidden="true"><span class="disc">{entry.initial}</span></span>
    {/if}
    {#if armed && usedIn}
      <span class="used-in" bind:this={strip}>{usedIn}</span>
    {:else if armed}
      <span class="used-in measuring" bind:this={strip} aria-hidden="true"></span>
    {/if}
  </button>

  <div class="under">
    {#if renaming}
      <input
        class="rename"
        type="text"
        aria-label={copy.renameField(entry.name)}
        bind:this={field}
        bind:value={draft}
        onkeydown={onFieldKey}
        onblur={commit}
      />
    {:else}
      <span class="name">{entry.name}</span>
    {/if}
    {#if hovered && !renaming}
      <button class="action" onclick={beginRename}>{copy.rename}</button>
      <button class="action" onclick={onDelete}>{armed ? copy.deleteArmed : copy.delete}</button>
    {:else if !renaming}
      <span class="date">{entry.editedText}</span>
    {/if}
  </div>
</div>

<style>
  .cell {
    width: 260px;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .thumb {
    position: relative;
    width: 260px;
    height: 163px;
    padding: 0;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--panel);
    overflow: hidden;
    cursor: pointer;
    display: block;
  }
  .thumb.hovered {
    border-color: var(--accent);
  }
  .thumb img {
    width: 100%;
    height: 100%;
    object-fit: cover;
    display: block;
  }
  .placeholder {
    position: absolute;
    inset: 0;
    display: grid;
    place-items: center;
  }
  .disc {
    width: 56px;
    height: 56px;
    border-radius: 50%;
    background: var(--chip);
    color: var(--text-muted);
    font-size: var(--font-title);
    font-weight: 600;
    display: grid;
    place-items: center;
  }
  .used-in {
    position: absolute;
    left: 8px;
    right: 8px;
    bottom: 8px;
    height: 26px;
    box-sizing: border-box;
    padding: 0 8px;
    display: flex;
    align-items: center;
    background: var(--panel);
    color: var(--danger-text);
    border: 1px solid var(--danger);
    border-radius: 6px;
    font-size: var(--font-label);
    white-space: nowrap;
    overflow: hidden;
  }
  .used-in.measuring {
    visibility: hidden;
  }
  .under {
    height: 26px;
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .name {
    flex: 1;
    min-width: 0;
    font-weight: 500;
    color: var(--text);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .date {
    color: var(--text-muted);
    font-size: var(--font-label);
    white-space: nowrap;
  }
  .action {
    height: 22px;
    padding: 2px 8px;
    border: 1px solid var(--border);
    border-radius: 4px;
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-label);
    cursor: pointer;
    white-space: nowrap;
  }
  .action:hover {
    background: var(--chip-hover);
  }
  .rename {
    flex: 1;
    min-width: 0;
    height: 24px;
    box-sizing: border-box;
    padding: 0 6px;
    border: 1px solid var(--accent);
    border-radius: 4px;
    background: var(--chip);
    color: var(--text);
    font: inherit;
  }
</style>

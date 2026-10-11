<script lang="ts">
  // THE VERSIONS DRAWER as a card on the editor's chrome page (migration
  // Phase 3c) - VersionsPanel, ported. Where it stands is the overlay's call
  // (an HtmlCardSlot in the Qt drawer's TopLeft anchor); how TALL it is, is
  // ours: the list grows, so the card measures itself and reports the height,
  // and the slot follows (ruling R2). The card is drawn at the slot's rect;
  // the measured wrapper inside does not depend on that rect, so the two
  // cannot feed back.
  //
  // A version is file data (CLAUDE.md "Files, versions and the library"): +
  // opens ONE pending card, named nothing in the store until it is saved.
  // Enter or a click away saves (InlineRename's contract), Escape or a blank
  // name discards, and a refused save keeps the card with what was typed.
  import { onDestroy, onMount, tick, untrack } from 'svelte';
  import { chip } from '../../actions';
  import type { CardRect, Keyboard } from '../settings/api';
  import type { VersionEntry, VersionsApi } from './api';
  import VersionRow from './VersionRow.svelte';

  let {
    versions,
    keyboard,
    rect,
    report,
    newRequest = 0,
    onrequesthandled,
  }: {
    versions: VersionsApi;
    keyboard: Keyboard;
    rect: CardRect;
    report: (name: string, height: number) => void;
    // File -> Save version... asked for a pending card (ruling R4): above
    // zero until this card takes the request and says so.
    newRequest?: number;
    onrequesthandled?: () => void;
  } = $props();

  // One VersionsModel for the card's whole life - read once, on purpose.
  const api: VersionsApi = untrack(() => versions);
  const words = api.words;

  let list: VersionEntry[] = $state([...api.versions]);
  let canSave = $state(api.canSave);
  let rowsEnabled = $state(api.rowsEnabled);
  let pending = $state(false);
  let draft = $state('');
  let field: HTMLInputElement | undefined = $state();
  let body: HTMLElement | undefined = $state();
  let holding = false;
  let saving = false;
  let alive = true;

  api.changed.connect(() => {
    if (!alive) return;
    list = [...api.versions];
    canSave = api.canSave;
    rowsEnabled = api.rowsEnabled;
    // VersionsPanel::refresh()'s auto-cancel: a competing claim (a sketch, a
    // pull, render mode) shuts +, and a pending card shuts with it.
    if (!canSave && pending) discard();
  });

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

  async function begin() {
    if (pending || !canSave) return;
    pending = true;
    draft = await api.defaultName();
    if (!alive || !pending) return;
    await tick();
    claim();
    field?.focus();
    field?.select();
  }

  function discard() {
    pending = false;
    draft = '';
    release();
  }

  async function commit() {
    if (!pending || saving) return;
    const name = draft.trim();
    if (!name) {
      discard();
      return;
    }
    saving = true;
    const ok = await api.save(name);
    saving = false;
    if (!alive || !pending) return;
    if (ok) {
      discard();
      return;
    }
    // Refused (a duplicate name): MainWindow already said why. The card
    // stays, holding what was typed, for another try.
    await tick();
    field?.focus();
    field?.select();
  }

  function onKey(event: KeyboardEvent) {
    if (event.key === 'Enter') {
      event.preventDefault();
      commit();
    } else if (event.key === 'Escape') {
      event.preventDefault();
      discard();
    }
  }

  function onBlur() {
    // A click away saves, as InlineRename does (ruling R5). Not while a save
    // is in flight, and not once the card is going away.
    if (alive && pending && !saving) commit();
  }

  $effect(() => {
    if (newRequest > 0)
      untrack(() => {
        begin();
        onrequesthandled?.();
      });
  });

  onMount(() => {
    const measure = () => {
      if (!body) return;
      // The body plus the card's own border, which the slot also holds
      // (review M3).
      const card = body.parentElement;
      const style = card ? getComputedStyle(card) : null;
      const border = style ? parseFloat(style.borderTopWidth) + parseFloat(style.borderBottomWidth) : 0;
      const h = Math.ceil(body.getBoundingClientRect().height + (border || 0));
      if (h <= 0) return;
      // The harness reads the last height the card reported.
      (window as unknown as { __versionsHeight?: number }).__versionsHeight = h;
      report('versions', h);
    };
    const observer = new ResizeObserver(measure);
    if (body) observer.observe(body);
    measure();
    return () => observer.disconnect();
  });

  onDestroy(() => {
    alive = false;
    pending = false;
    release();
  });
</script>

<section
  class="versions-card themed-rim"
  data-hit
  aria-label={words.title}
  style="left: {rect.x}px; top: {rect.y}px; width: {rect.w}px; height: {rect.h}px;"
>
  <div class="body" bind:this={body}>
    <header>
      <span class="title">{words.title}</span>
      <button
        use:chip
        class="add"
        title={words.add}
        aria-label={words.add}
        disabled={pending || !canSave}
        onclick={begin}>+</button
      >
      <button use:chip class="close" title={words.close} aria-label={words.close} onclick={() => api.close()}
        >×</button
      >
    </header>
    <div class="list">
      {#if pending}
        <div class="pending">
          <div class="thumb"></div>
          <input
            bind:this={field}
            bind:value={draft}
            aria-label={words.add}
            spellcheck="false"
            autocomplete="off"
            onkeydown={onKey}
            onblur={onBlur}
          />
        </div>
      {/if}
      {#each list as entry (entry.name)}
        <VersionRow
          {entry}
          {words}
          enabled={rowsEnabled}
          oncompare={() => api.compare(entry.name)}
          onrestore={() => api.restore(entry.name)}
          ondelete={() => api.remove(entry.name)}
        />
      {/each}
      {#if list.length === 0 && !pending}
        <p class="empty">{words.empty}</p>
      {/if}
    </div>
  </div>
</section>

<style>
  .versions-card {
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
  .body {
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    gap: 10px;
    min-height: 160px;
    padding: 12px;
  }
  header {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .title {
    flex: 1;
    color: var(--text-muted);
    font-family: var(--title-family, inherit);
    font-size: var(--title-size, var(--font-title));
    font-weight: var(--title-weight, 600);
    letter-spacing: var(--title-tracking, normal);
  }
  .add {
    width: 26px;
    height: 26px;
    border: 1.5px solid var(--accent);
    border-radius: var(--r-control, 6px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-weight: 600;
    cursor: pointer;
  }
  .add:hover:not(:disabled) {
    background: var(--chip-hover);
  }
  .add:disabled {
    color: var(--text-disabled);
    border-color: var(--text-disabled);
    cursor: default;
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
    display: flex;
    flex-direction: column;
    gap: 10px;
    max-height: 384px;
    overflow-y: auto;
  }
  /* The app's one scrollbar description (Theme::install()): a quiet chipActive
     handle on a transparent track, no arrows. */
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
  .pending {
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    flex-shrink: 0;
    padding: 3px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-small, 6px);
    background: var(--strip-fill, var(--chip));
  }
  .pending .thumb {
    width: 100%;
    aspect-ratio: 16 / 10;
  }
  .pending input {
    margin: 8px 10px;
    padding: 2px 6px;
    border: 1px solid var(--accent);
    border-radius: var(--r-tiny, 4px);
    background: var(--panel);
    color: var(--text);
    font: inherit;
    font-weight: 500;
    outline: none;
  }
  .empty {
    margin: 0;
    color: var(--text-muted);
    white-space: pre-line;
  }
</style>

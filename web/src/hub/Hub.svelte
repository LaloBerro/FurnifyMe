<script lang="ts">
  import { chip } from '../actions';
  // The library hub as a page - a port of the Qt hub (SelectorWindow.cpp),
  // rule for rule. HubModel (C++) lists and does the work; this page owns the
  // view state the Qt hub owned: the sort, the search, which card is hovered,
  // armed or being renamed, and the name question.
  import { onMount } from 'svelte';
  import type { HubApi, HubEntryData, ThemeApi, WindowApi } from './api';
  import Card from './Card.svelte';
  import NameCard from './NameCard.svelte';
  import TitleBar from './TitleBar.svelte';
  import { copy } from './copy';
  import { matches, ordered } from './order';
  import { applyTokens } from '../theme';

  let { hub, win, theme }: { hub: HubApi; win: WindowApi; theme: ThemeApi } = $props();

  const kSearchDebounceMs = 150;
  const kFailureMs = 8000;
  const kNoteMs = 4000;

  let furniture: HubEntryData[] = $state([]);
  let scenes: HubEntryData[] = $state([]);
  let byName = $state(false);
  let typed = $state('');
  let query = $state('');
  let failure = $state('');
  let note = $state('');
  let naming: string | null = $state(null);
  let searchTimer = 0;
  let failureTimer = 0;
  let noteTimer = 0;

  const shownFurniture = $derived(ordered(furniture, byName).filter((e) => matches(e.name, query)));
  const shownScenes = $derived(ordered(scenes, byName).filter((e) => matches(e.name, query)));
  const nothingMatches = $derived(
    query.trim() !== '' && shownFurniture.length === 0 && shownScenes.length === 0,
  );

  function reread() {
    furniture = [...hub.furniture];
    scenes = [...hub.scenes];
  }

  function onSearch(value: string) {
    typed = value;
    clearTimeout(searchTimer);
    searchTimer = window.setTimeout(() => (query = typed), kSearchDebounceMs);
  }

  function showFailure(text: string) {
    failure = text;
    clearTimeout(failureTimer);
    failureTimer = window.setTimeout(() => (failure = ''), kFailureMs);
  }

  function showNote(text: string) {
    note = text;
    clearTimeout(noteTimer);
    noteTimer = window.setTimeout(() => (note = ''), kNoteMs);
  }

  async function askName() {
    naming = await hub.suggestedName();
  }

  onMount(() => {
    reread();
    hub.changed.connect(reread);
    hub.failure.connect(showFailure);
    hub.note.connect(showNote);
    applyTokens(theme.tokens);
    theme.changed.connect(() => applyTokens(theme.tokens));
  });
</script>

<div class="window">
  <!-- While the name question stands nothing behind it can be reached: no tab
       stop, no click, no key - the Qt card claimed every key app-wide. -->
  <div class="titlebar-slot" inert={naming !== null}><TitleBar {win} /></div>

  <main inert={naming !== null}>
    <div class="header">
      <h1>{copy.title}</h1>
      <div class="search">
        <input
          type="search"
          placeholder={copy.search}
          aria-label={copy.search}
          value={typed}
          oninput={(e) => onSearch((e.target as HTMLInputElement).value)}
        />
      </div>
      <button use:chip class="sort" aria-pressed={!byName} onclick={() => (byName = false)}>{copy.sortRecent}</button>
      <button use:chip class="sort" aria-pressed={byName} onclick={() => (byName = true)}>{copy.sortName}</button>
    </div>

    <!-- Assertive but NOT role=alert: Qt answers an Alert with a system sound. -->
    <div class="banner-slot" aria-live="assertive">
      {#if failure}<div class="banner">{failure}</div>{/if}
    </div>
    {#if nothingMatches}<p class="no-match">{copy.noMatch(query.trim())}</p>{/if}

    <div class="scroll">
      <div class="grid furniture">
        <button use:chip class="new" onclick={askName}>{copy.newFurniture}</button>
        {#each shownFurniture as entry (entry.id)}
          <Card {entry} {hub} />
        {/each}
      </div>

      <h2 class="section">{copy.scenesHeading}</h2>
      <div class="grid scenes">
        <button use:chip class="new" onclick={() => hub.createScene()}>{copy.newScene}</button>
        {#each shownScenes as entry (entry.id)}
          <Card {entry} {hub} />
        {/each}
      </div>
    </div>
  </main>

  <div class="toast-slot" aria-live="polite">
    {#if note}<div class="toast">{note}</div>{/if}
  </div>

  {#if naming !== null}
    <NameCard
      suggestion={naming}
      oncreate={(name) => {
        naming = null;
        hub.createFurniture(name);
      }}
      oncancel={() => (naming = null)}
    />
  {/if}
</div>

<style>
  /* #app too: without it the layout's 100% heights resolve against content,
     the scroll area grows to fit everything and NOTHING scrolls - the Scenes
     section was simply cut off below the window (found by hub_smoke). */
  :global(html),
  :global(body),
  :global(#app) {
    height: 100%;
    background: var(--chrome);
    overflow: hidden;
    user-select: none;
  }
  .window {
    height: 100%;
    display: flex;
    flex-direction: column;
    /* A theme's light at the window's edges (themes.css), behind everything. */
    background: var(--hub-glow, none);
  }
  main {
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
    gap: 16px;
    padding: 16px 32px 32px;
  }
  .header {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  h1 {
    flex: 1;
    margin: 0;
    font-family: var(--title-family, inherit);
    font-size: var(--title-size, var(--font-title));
    font-weight: var(--title-weight, 600);
    letter-spacing: var(--title-tracking, normal);
  }
  .search input {
    width: 180px;
    height: 28px;
    box-sizing: border-box;
    padding: 2px 9px;
    border: 1px solid var(--border);
    border-radius: var(--r-float, 8px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-label);
  }
  .search input::placeholder {
    color: var(--text-muted);
  }
  .sort {
    height: 28px;
    padding: 2px 12px;
    border: 1px solid var(--border);
    border-radius: var(--r-float, 8px);
    background: var(--chip);
    color: var(--text-muted);
    font: inherit;
    font-size: var(--font-label);
    font-family: var(--label-family, inherit);
    text-transform: var(--label-transform, none);
    letter-spacing: var(--label-tracking, normal);
    cursor: pointer;
  }
  .sort[aria-pressed='true'] {
    background: var(--chip-active);
    color: var(--text);
    border-color: var(--accent);
    font-weight: 600;
  }
  .sort:hover {
    background: var(--chip-hover);
  }
  .banner-slot:empty {
    display: none;
  }
  .banner {
    padding: 8px 12px;
    border: 1px solid var(--danger-text);
    border-radius: var(--r-control, 6px);
    background: var(--chip);
    color: var(--danger-text);
  }
  .no-match {
    margin: 0;
    color: var(--text-muted);
  }
  .scroll {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    overflow-x: hidden;
  }
  /* The app's one scrollbar: a quiet handle on a transparent track. */
  .scroll::-webkit-scrollbar {
    width: 10px;
  }
  .scroll::-webkit-scrollbar-track {
    background: transparent;
  }
  .scroll::-webkit-scrollbar-thumb {
    background: var(--chip-active);
    border-radius: var(--r-small, 5px);
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(3, 260px);
    gap: 24px;
    align-items: start;
  }
  .section {
    margin: 24px 0 12px;
    font-size: var(--font-body);
    font-weight: 400;
  }
  .new {
    width: 260px;
    height: 197px;
    border: 1px dashed var(--border);
    border-radius: var(--r-card, var(--radius));
    background: transparent;
    color: var(--accent-text);
    font: inherit;
    cursor: pointer;
  }
  .new:hover {
    background: var(--chip-hover);
    border-color: var(--accent);
  }
  .toast-slot {
    position: fixed;
    left: 50%;
    bottom: 24px;
    translate: -50% 0;
  }
  .toast {
    padding: 10px 16px;
    background: var(--chip-active);
    border: 1px solid var(--border);
    border-left: 6px solid var(--accent);
    border-radius: var(--r-card, var(--radius));
    color: var(--text);
    white-space: nowrap;
  }
</style>

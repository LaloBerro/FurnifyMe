<script lang="ts">
  // THE NAME QUESTION (migration Phase 3g) - File -> New furniture's "Name
  // this furniture", a faithful port of NameFurnitureCard's paint over the
  // close question's wash. The Qt card still stands (invisible) and keeps the
  // claim: no key reaches a window shortcut. What is this page's own is the
  // FIELD - every key aimed at the page goes through the claim to it, Enter
  // and Escape too, and Enter answers with the name the field holds right
  // now, which no mirrored edit can lag behind (ruling R3).
  import { onDestroy, tick } from 'svelte';
  import { chip } from '../actions';
  import Wash from './Wash.svelte';
  import type { QuestionState } from './Question.svelte';

  let {
    question,
    host,
    keyboard,
    oncreate,
    oncancel,
    onedit,
  }: {
    question: QuestionState;
    host: { w: number; h: number };
    keyboard: { claimKeyboard(): void; releaseKeyboard(): void };
    oncreate: (name: string) => void;
    oncancel: () => void;
    onedit: (name: string) => void;
  } = $props();

  const kCardWidth = 380;
  const kGutter = 16;
  let cardHeight = $state(0);
  const width = $derived(Math.min(kCardWidth, Math.max(80, host.w - kGutter * 2)));
  const left = $derived(Math.round((host.w - width) / 2));
  const top = $derived(Math.max(kGutter, Math.round(host.h * 0.44 - cardHeight / 2)));

  let name = $state('');
  let field: HTMLInputElement | undefined = $state();
  let holding = false;
  const empty = $derived(name.trim() === '');

  // Seeded on every ask and every hand-over from Qt - never from a mirrored
  // edit, which would retype the field under the user's hands.
  let seededWith = -1;
  $effect(() => {
    const seed = question.seed ?? 0;
    if (seed === seededWith) return;
    seededWith = seed;
    name = question.name ?? '';
    // The suggestion is a suggestion: selected, so the first key replaces it.
    // The keyboard is claimed outright, as the Versions field does: a focus()
    // in a page whose window is not active fires no focus event.
    tick().then(() => {
      claim();
      field?.focus();
      field?.select();
    });
  });

  function claim() {
    if (holding || !field) return;
    holding = true;
    keyboard.claimKeyboard();
  }
  function release() {
    if (!holding) return;
    holding = false;
    keyboard.releaseKeyboard();
  }
  onDestroy(release);

  function create() {
    if (empty) return;
    oncreate(name);
  }

  function onKey(event: KeyboardEvent) {
    if (event.isComposing) return;
    if (event.key === 'Enter') {
      event.preventDefault();
      event.stopPropagation();
      create();
    } else if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      oncancel();
    }
  }
</script>

<svelte:window onkeydown={onKey} />

<Wash {host} keepClear={question.keepClear}>
  <!-- A press on the wash or the card keeps focus in the field. -->
  <div
    class="card name-question"
    role="dialog"
    tabindex="-1"
    aria-modal="true"
    aria-labelledby="name-question-title"
    aria-describedby="name-question-body"
    style:left="{left}px"
    style:top="{top}px"
    style:width="{width}px"
    bind:clientHeight={cardHeight}
    onmousedown={(e) => e.target !== field && e.preventDefault()}
  >
    <div class="title" id="name-question-title">{question.title}</div>
    <input
      type="text"
      aria-labelledby="name-question-title"
      maxlength={question.maxLength ?? 120}
      spellcheck="false"
      autocomplete="off"
      bind:this={field}
      bind:value={name}
      oninput={() => onedit(name)}
      onfocus={claim}
      onblur={release}
    />
    <div class="body" id="name-question-body">{question.body}</div>
    <div class="answers">
      <button use:chip class="answer cancel" onclick={oncancel}>
        <span class="label">{question.cancel}</span><span class="key">{question.cancelKey}</span>
      </button>
      <button use:chip class="answer create" disabled={empty} onclick={create}>
        <span class="label">{question.create}</span><span class="key">{question.createKey}</span>
      </button>
    </div>
  </div>
</Wash>

<style>
  /* NameFurnitureCard's numbers: 380 wide, 20 padding (16 at the foot), a
     12 px gap, a 36 px field and buttons, radius 14, a little above centre. */
  .card {
    position: absolute;
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    gap: 12px;
    padding: 20px 20px 16px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-dialog, 14px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
  }
  .title {
    font-size: var(--font-title);
    font-weight: 700;
    color: var(--text);
    overflow-wrap: anywhere;
  }
  input {
    box-sizing: border-box;
    width: 100%;
    height: 36px;
    padding: 0 10px;
    border: 1px solid var(--border);
    border-radius: var(--r-control, 8px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-body);
    outline: none;
  }
  /* A typed field marks itself with the accent while it is being edited. */
  input:focus {
    border-color: var(--accent);
  }
  .body {
    font-size: var(--font-body);
    color: var(--text-muted);
  }
  .answers {
    display: flex;
    gap: 8px;
  }
  .answer {
    flex: 1 1 0;
    min-width: 0;
    height: 36px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    padding: 0 12px;
    border: max(1px, var(--chip-stroke, 1px)) solid var(--border);
    border-radius: var(--r-control, 8px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-body);
    cursor: pointer;
  }
  .answer:hover {
    background: var(--chip-hover);
  }
  .answer:active {
    background: var(--chip-active);
  }
  .label {
    font-weight: 500;
  }
  .key {
    font-size: var(--font-badge);
    color: var(--text-muted);
  }
  .answer.create {
    border-color: var(--accent);
    background: var(--accent);
  }
  .answer.create:hover {
    filter: brightness(1.15);
  }
  .answer.create:active {
    filter: brightness(0.87);
  }
  .answer.create .key {
    color: var(--text);
  }
  /* A name with nothing in it cannot be created, and says so by looking it. */
  .answer.create:disabled {
    border-color: var(--border);
    background: var(--chip);
    color: var(--text-disabled);
    filter: none;
    cursor: default;
  }
  .answer.create:disabled .key {
    color: var(--text-disabled);
  }
</style>

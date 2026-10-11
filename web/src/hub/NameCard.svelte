<script lang="ts">
  // "What should this be called" - asked before a furniture exists, so Escape
  // leaves no orphan in the library. A scrim over the hub and a card on it;
  // Enter creates (never from an empty name), Escape cancels, and both keys
  // are claimed here so neither reaches anything behind the question.
  import { onMount, tick } from 'svelte';
  import { copy } from './copy';
  import { chip } from '../actions';

  let {
    suggestion,
    oncreate,
    oncancel,
  }: { suggestion: string; oncreate: (name: string) => void; oncancel: () => void } = $props();

  let name = $state('');
  let field: HTMLInputElement | undefined = $state();
  const empty = $derived(name.trim() === '');

  onMount(async () => {
    name = suggestion;
    await tick();
    field?.focus();
    field?.select();
  });

  function create() {
    if (empty) return;
    oncreate(name.trim());
  }

  function onKey(event: KeyboardEvent) {
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

<!-- A press on the scrim or the card keeps focus in the field. -->
<div class="scrim" role="presentation" onmousedown={(e) => e.target !== field && e.preventDefault()}>
  <div class="card" role="dialog" aria-modal="true" aria-labelledby="name-title" aria-describedby="name-body">
    <h2 id="name-title">{copy.nameTitle}</h2>
    <input type="text" maxlength="120" bind:this={field} bind:value={name} />
    <p id="name-body">{copy.nameBody}</p>
    <div class="buttons">
      <button use:chip class="plain" onclick={oncancel}>
        <span>{copy.cancel}</span><span class="key">{copy.escKey}</span>
      </button>
      <button use:chip class="primary" disabled={empty} onclick={create}>
        <span>{copy.create}</span><span class="key">{copy.enterKey}</span>
      </button>
    </div>
  </div>
</div>

<style>
  /* NameFurnitureCard.cpp's numbers: 380 wide, 20 padding (16 at the foot),
     a 12 px gap, 36 px field and buttons, radius 14, the viewport at 80%
     behind it, the card a little above centre. No shadow - borders carry the
     separation everywhere in this app. */
  .scrim {
    position: fixed;
    inset: 0;
    background: color-mix(in srgb, var(--viewport) 80%, transparent);
    display: flex;
    justify-content: center;
    align-items: flex-start;
    padding-top: max(16px, calc(44vh - 90px));
    box-sizing: border-box;
    z-index: 10;
  }
  .card {
    width: min(380px, calc(100vw - 32px));
    box-sizing: border-box;
    padding: 20px 20px 16px;
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 14px;
    display: flex;
    flex-direction: column;
    gap: 12px;
  }
  h2 {
    margin: 0;
    font-size: var(--font-title);
    font-weight: 600;
  }
  p {
    margin: 0;
    color: var(--text-muted);
  }
  input {
    width: 100%;
    box-sizing: border-box;
    height: 36px;
    padding: 0 10px;
    border: 1px solid var(--accent);
    border-radius: 8px;
    background: var(--chip);
    color: var(--text);
    font: inherit;
  }
  .buttons {
    display: flex;
    justify-content: space-between;
    gap: 8px;
    margin-top: 4px;
  }
  button {
    width: calc(50% - 4px);
    height: 36px;
    display: flex;
    justify-content: center;
    align-items: center;
    gap: 8px;
    border-radius: 8px;
    font: inherit;
    cursor: pointer;
  }
  .plain {
    border: 1px solid var(--border);
    background: var(--chip);
    color: var(--text);
  }
  .plain:hover {
    background: var(--chip-hover);
  }
  .plain:active {
    background: var(--chip-active);
  }
  .primary {
    border: 1px solid var(--accent);
    background: var(--accent);
    color: var(--text);
  }
  .primary:hover {
    background: color-mix(in srgb, var(--accent) 87%, white);
  }
  .primary:disabled {
    background: var(--chip);
    border-color: var(--border);
    color: var(--text-disabled);
    cursor: default;
  }
  .key {
    font-size: var(--font-badge);
    opacity: 0.7;
  }
</style>

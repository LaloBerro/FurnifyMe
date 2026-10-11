<script lang="ts">
  // "What should this be called" - asked before a furniture exists, so Escape
  // leaves no orphan in the library. A scrim over the hub and a card on it;
  // Enter creates (never from an empty name), Escape cancels, and both keys
  // are claimed here so neither reaches anything behind the question.
  import { onMount, tick } from 'svelte';
  import { copy } from './copy';

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

<div class="scrim" role="presentation" onkeydown={onKey}>
  <div class="card" role="dialog" aria-modal="true" aria-labelledby="name-title" aria-describedby="name-body">
    <h2 id="name-title">{copy.nameTitle}</h2>
    <p id="name-body">{copy.nameBody}</p>
    <input type="text" bind:this={field} bind:value={name} onkeydown={onKey} />
    <div class="buttons">
      <button class="plain" onclick={oncancel}>
        <span>{copy.cancel}</span><span class="key">{copy.escKey}</span>
      </button>
      <button class="primary" disabled={empty} onclick={create}>
        <span>{copy.create}</span><span class="key">{copy.enterKey}</span>
      </button>
    </div>
  </div>
</div>

<style>
  .scrim {
    position: fixed;
    inset: 0;
    background: color-mix(in srgb, var(--chrome) 60%, transparent);
    display: grid;
    place-items: center;
    z-index: 10;
  }
  .card {
    width: 360px;
    box-sizing: border-box;
    padding: 20px;
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    box-shadow: 0 16px 40px rgba(0, 0, 0, 0.45);
  }
  h2 {
    margin: 0 0 4px;
    font-size: var(--font-title);
    font-weight: 600;
  }
  p {
    margin: 0 0 14px;
    color: var(--text-muted);
  }
  input {
    width: 100%;
    box-sizing: border-box;
    height: 32px;
    padding: 0 10px;
    border: 1px solid var(--accent);
    border-radius: 8px;
    background: var(--chip);
    color: var(--text);
    font: inherit;
    margin-bottom: 16px;
  }
  .buttons {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
  }
  button {
    display: flex;
    align-items: baseline;
    gap: 8px;
    height: 32px;
    padding: 0 12px;
    border-radius: 8px;
    border: 1px solid var(--border);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    cursor: pointer;
    align-items: center;
  }
  button:hover {
    background: var(--chip-hover);
  }
  .primary {
    background: var(--accent);
    border-color: var(--accent);
    color: #ffffff;
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

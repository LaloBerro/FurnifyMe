<script lang="ts">
  // ONE ROW of the Items card: a body, an outline or a folder. It draws what
  // ItemsModel says the row is and reports its controls; every press on the
  // row itself (select, drag, menu, rename) is the card's to interpret, so the
  // row only names itself with data-row.
  import { tick } from 'svelte';
  import { chip } from '../../actions';
  import Glyph from './Glyph.svelte';
  import type { ItemRowData } from './api';

  let {
    row,
    renaming = false,
    renameSeed = '',
    oneye,
    ongrain,
    ontwisty,
    oncommit,
    oncancel,
    onfieldclaim,
    onfieldrelease,
  }: {
    row: ItemRowData;
    renaming?: boolean;
    renameSeed?: string;
    oneye: () => void;
    ongrain: () => void;
    ontwisty: () => void;
    // `byKey`: Enter or Escape in the field, not a click away - only then do
    // the list's keys come back to the page (review I1).
    oncommit: (text: string, byKey: boolean) => void;
    oncancel: (byKey: boolean) => void;
    onfieldclaim: () => void;
    onfieldrelease: () => void;
  } = $props();

  // The name as text nodes around the filter's match - never markup, so
  // "<b>Top</b>" and "a & b" read as typed (Review Focus 2).
  const parts = $derived.by(() => {
    if (!row.match) return null;
    const [at, length] = row.match;
    return [row.name.slice(0, at), row.name.slice(at, at + length), row.name.slice(at + length)];
  });

  let field: HTMLInputElement | undefined = $state();
  let draft = $state('');
  let done = false;

  $effect(() => {
    if (!renaming) return;
    draft = renameSeed;
    done = false;
    tick().then(() => {
      if (done) return;
      onfieldclaim();
      field?.focus();
      field?.select();
    });
    // C++ closing the rename (a document swap, the row folding away) removes
    // the field with no blur to count on: its claim goes back here (review I3).
    return () => {
      if (done) return;
      done = true;
      onfieldrelease();
    };
  });

  function finish(commit: boolean, byKey: boolean) {
    if (done) return;
    done = true;
    onfieldrelease();
    if (commit) oncommit(draft, byKey);
    else oncancel(byKey);
  }

  function onKey(event: KeyboardEvent) {
    // The field's keys are its own: nothing here reaches the list.
    event.stopPropagation();
    if (event.key === 'Enter') {
      event.preventDefault();
      finish(true, true);
    } else if (event.key === 'Escape') {
      event.preventDefault();
      finish(false, true);
    }
  }

  const stop = (event: Event) => event.stopPropagation();
</script>

<div
  class="row"
  class:folder={row.kind === 'folder'}
  class:outline={row.kind === 'outline'}
  class:marked={row.marked}
  class:current={row.current}
  class:pending={row.pending}
  class:quiet={row.muted || !row.onScreen}
  class:carried={row.carried}
  id="item-row-{row.id}"
  data-row={row.id}
  data-kind={row.kind}
  role="treeitem"
  aria-level={row.depth + 1}
  aria-selected={row.marked}
  aria-expanded={row.kind === 'folder' ? row.open : undefined}
  aria-label={row.accessibleName}
  title={row.tip || undefined}
  style="--depth: {row.depth};"
>
  {#if row.kind === 'folder'}
    <button
      use:chip
      class="twisty"
      class:held={row.held}
      title={row.twistyTip}
      aria-label={row.twistyName}
      onmousedown={stop}
      ondblclick={stop}
      onclick={(e) => {
        e.stopPropagation();
        if (!row.held) ontwisty();
      }}
    >
      <Glyph name={row.open ? 'chevronDown' : 'chevronRight'} size={14} />
    </button>
  {/if}
  {#if renaming}
    <input
      bind:this={field}
      bind:value={draft}
      class="rename"
      aria-label={row.name}
      spellcheck="false"
      autocomplete="off"
      onmousedown={stop}
      onclick={stop}
      ondblclick={stop}
      onkeydown={onKey}
      onblur={() => finish(true, false)}
    />
  {:else}
    <span class="name"
      >{#if parts}{parts[0]}<span class="hit">{parts[1]}</span>{parts[2]}{:else}{row.name}{/if}</span
    >
  {/if}
  {#if row.partial}
    <span class="count" title={row.partialWords}>{row.partial}</span>
  {/if}
  {#if row.grain}
    <button
      use:chip
      class="grain"
      title={row.grainTip}
      aria-label={row.grainName}
      disabled={row.carried}
      onmousedown={stop}
      ondblclick={stop}
      onclick={(e) => {
        e.stopPropagation();
        ongrain();
      }}
    >
      <Glyph name={row.grain === 'across' ? 'grainAcross' : 'grainAlong'} />
    </button>
  {/if}
  <button
    use:chip
    class="eye"
    class:off={!row.eyeOn}
    title={row.eyeTip}
    aria-label={row.eyeName}
    aria-pressed={row.eyeOn}
    disabled={row.carried}
    onmousedown={stop}
    ondblclick={stop}
    onclick={(e) => {
      e.stopPropagation();
      oneye();
    }}
  >
    <Glyph name={row.eyeOn ? 'eye' : 'eyeHidden'} />
  </button>
</div>

<style>
  .row {
    position: relative;
    box-sizing: border-box;
    display: flex;
    align-items: center;
    gap: 6px;
    min-height: 32px;
    padding: 4px 6px 4px calc(6px + var(--depth) * 12px);
    border: 1px solid transparent;
    border-radius: var(--list-row-radius, 4px);
    border-bottom: var(--list-hairline-rule, 1px solid transparent);
    color: var(--text);
    cursor: default;
    user-select: none;
  }
  .row.marked {
    background: var(--list-sel-fill, var(--chip-active));
    border-color: var(--list-sel-edge, transparent);
  }
  /* The fill's partner (ruling D3): a bar on the row's left edge. */
  .row.marked::before {
    content: '';
    position: absolute;
    left: var(--list-bar-x, 0px);
    top: var(--list-bar-inset, 4px);
    bottom: var(--list-bar-inset, 4px);
    width: var(--list-bar-width, 3px);
    background: var(--list-bar, var(--accent-text));
  }
  .name {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: pre;
    font-weight: var(--list-name-weight, 400);
  }
  .row.folder .name {
    font-weight: var(--list-folder-weight, 400);
  }
  .row.marked .name {
    font-weight: var(--list-sel-weight, 400);
  }
  .row.quiet .name {
    color: var(--text-muted);
  }
  /* What a drag carries: its ink at 45%, its buttons out of reach. */
  .row.carried .name {
    opacity: 0.45;
  }
  .hit {
    color: var(--accent-text);
  }
  .count {
    flex-shrink: 0;
    padding: var(--list-badge-pad, 0);
    border: var(--list-badge-border, none);
    border-radius: var(--list-badge-radius, 0);
    background: var(--list-badge-fill, transparent);
    color: var(--list-badge-ink, var(--accent-text));
    font-size: var(--font-label);
    font-weight: var(--list-badge-weight, 400);
  }
  button {
    display: grid;
    place-items: center;
    flex-shrink: 0;
    width: 24px;
    height: 24px;
    padding: 0;
    border: none;
    border-radius: var(--r-tiny, 4px);
    background: transparent;
    color: var(--text-muted);
    cursor: pointer;
  }
  button:hover:not(:disabled) {
    background: var(--chip-hover);
    color: var(--text);
  }
  button:disabled {
    opacity: 0.45;
    cursor: default;
  }
  .eye.off {
    color: var(--text-disabled);
  }
  .twisty {
    width: 16px;
    height: 16px;
  }
  .twisty.held {
    cursor: default;
  }
  /* Every mockup puts the grain mark before the eye; Graphite keeps the eye first. */
  .grain {
    order: var(--list-grain-order, 1);
  }
  .eye {
    order: 0;
  }
  .rename {
    flex: 1;
    min-width: 0;
    box-sizing: border-box;
    height: 22px;
    padding: 0 4px;
    border: 1px solid var(--accent);
    border-radius: var(--r-tiny, 4px);
    background: var(--panel);
    color: var(--text);
    font: inherit;
    outline: none;
  }
</style>

<script lang="ts">
  // The Colours tab's scrolling list: a heading above the token that opens
  // each run - near-identical darks read as three rows of one colour without
  // one - then a row per token, the user's word for it and a swatch in the
  // live colour. A swatch asks C++ for the picker (ruling R2: the modeless
  // QColorDialog, live preview and Cancel-restores, as the Qt drawer opens).
  import { chip } from '../../actions';
  import type { TokenSpec } from './api';

  let {
    tokens,
    colours,
    pickTip,
    onpick,
  }: {
    tokens: TokenSpec[];
    colours: Record<string, string>;
    pickTip: string;
    onpick: (id: string) => void;
  } = $props();
</script>

<div class="colours">
  {#each tokens as token (token.id)}
    {#if token.group}
      <div class="heading">{token.group}</div>
    {/if}
    <div class="row">
      <span class="label">{token.name}</span>
      <button
        use:chip
        class="swatch"
        data-swatch={token.id}
        title={pickTip.replace('%1', token.name)}
        aria-label={pickTip.replace('%1', token.name)}
        style="background: {colours[token.id] ?? 'transparent'};"
        onclick={() => onpick(token.id)}
      ></button>
    </div>
  {/each}
</div>

<style>
  .colours {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 4px;
    padding-right: 6px;
  }
  .heading {
    font-size: var(--font-label);
    font-weight: 700;
    color: var(--text-muted);
  }
  .row {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .label {
    flex: 1;
    min-width: 0;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .swatch {
    flex: none;
    width: 46px;
    height: 18px;
    padding: 0;
    border: 1px solid var(--border);
    border-radius: 4px;
    cursor: pointer;
  }
  /* The app's one scrollbar description (Theme::install()): a quiet chipActive
     handle on a transparent track, no arrows. */
  .colours::-webkit-scrollbar {
    width: 8px;
  }
  .colours::-webkit-scrollbar-track {
    background: transparent;
  }
  .colours::-webkit-scrollbar-thumb {
    background: var(--chip-active);
    border-radius: 4px;
  }
  .colours::-webkit-scrollbar-button {
    display: none;
  }
</style>

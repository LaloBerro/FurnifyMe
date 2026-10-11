<script lang="ts" module>
  export interface Segment {
    action: string;
    side?: 'off' | 'on'; // a binary row's half
    label: string;
    current: boolean;
    enabled: boolean;
    tip: string;
  }
</script>

<script lang="ts">
  // A set of exclusive choices - AppearancePanel's Segment chips. Each chip is
  // a view of an action; the row decides what a click means (a choice never
  // re-triggers its current option, a binary half triggers only from the
  // side it is not), so this only reports which chip was pressed.
  import { chip } from '../../actions';

  let {
    segments,
    stacked = false,
    onpick,
  }: { segments: Segment[]; stacked?: boolean; onpick: (index: number) => void } = $props();
</script>

<div class="options" class:stacked role="radiogroup">
  {#each segments as segment, i (segment.action + (segment.side ?? '') + i)}
    <button
      use:chip
      class="segment"
      class:current={segment.current}
      role="radio"
      aria-checked={segment.current}
      aria-disabled={!segment.enabled}
      data-action={segment.action}
      data-side={segment.side}
      title={segment.tip || undefined}
      onclick={() => segment.enabled && onpick(i)}>{segment.label}</button
    >
  {/each}
</div>

<style>
  .options {
    display: flex;
    gap: 4px;
  }
  .options.stacked {
    flex-direction: column;
    flex: 1;
  }
  .segment {
    height: 22px;
    padding: 0 9px;
    border: 1px solid var(--border);
    border-radius: 6px;
    background: var(--chip);
    color: var(--text-muted);
    font: inherit;
    font-size: var(--font-label);
    white-space: nowrap;
    cursor: pointer;
  }
  .segment:hover:not(.current):not([aria-disabled='true']) {
    background: var(--chip-hover);
  }
  .segment.current {
    background: var(--chip-active);
    color: var(--text);
    border: 1.6px solid var(--accent);
  }
  .segment[aria-disabled='true'] {
    color: var(--text-disabled);
    border-color: var(--border);
    cursor: default;
  }
</style>

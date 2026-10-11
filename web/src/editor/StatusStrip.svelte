<script lang="ts">
  // The status bar, ported (MainWindow's QStatusBar + StatusMessageLine +
  // FurnitureNameMark + the state label). Two channels, one job each (F9):
  // the LEFT is the selection and the outcome of the last thing done, elided
  // to what fits with the whole sentence as its tooltip (F20); the RIGHT is
  // the mode and the next step. The name mark between them is the identity,
  // with a caution dot while there are unsaved changes. C++ still writes the
  // Qt bar - which stays the state, hidden - and this strip draws it.
  import type { StatusState } from './api';
  import { copy } from './copy';

  let { status }: { status: StatusState } = $props();

  let left: HTMLElement | undefined = $state();
  let elided = $state(false);

  $effect(() => {
    void status.left;
    elided = !!left && left.scrollWidth > left.clientWidth;
  });
</script>

<footer class="strip" data-hit>
  <span class="left" bind:this={left} title={elided ? status.left : undefined}>{status.left}</span>
  {#if status.name}
    <span class="rule" aria-hidden="true"></span>
    <span class="mark">
      {#if status.unsaved}<span class="dot" aria-hidden="true"></span>{/if}
      <span class="name">{status.unsaved ? copy.nameWithUnsaved(status.name) : status.name}</span>
    </span>
  {/if}
  {#if status.right}
    <span class="rule" aria-hidden="true"></span>
    <span class="right">{status.right}</span>
  {/if}
</footer>

<style>
  .strip {
    position: fixed;
    left: 0;
    right: 0;
    bottom: 0;
    height: 24px;
    box-sizing: border-box;
    padding: 0 8px 0 6px;
    display: flex;
    align-items: center;
    gap: 8px;
    background: var(--chrome);
    color: var(--text-muted);
    font-size: var(--font-label);
    white-space: nowrap;
  }
  .left {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .rule {
    width: 1px;
    align-self: stretch;
    margin: 4px 0;
    background: var(--border);
  }
  .mark {
    display: flex;
    align-items: center;
    gap: 6px;
    color: var(--text);
  }
  .dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--caution);
  }
  .right {
    flex: none;
  }
</style>

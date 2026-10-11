<script lang="ts">
  // Reset, demoted and guarded (AppearancePanel::onResetClicked): it puts the
  // whole Theme::Spec back with no Undo, so the first click only ARMS it -
  // its label says so - and a second click inside the window resets. The arm
  // drops on its timer, and when the card goes away (this component with it).
  // Its width is the wider of its two labels, so arming moves nothing.
  import { onDestroy } from 'svelte';
  import { chip } from '../../actions';

  let {
    label,
    armedLabel,
    tip,
    windowMs,
    onreset,
  }: { label: string; armedLabel: string; tip: string; windowMs: number; onreset: () => void } = $props();

  let armed = $state(false);
  let timer = 0;

  function disarm() {
    clearTimeout(timer);
    armed = false;
  }

  function press() {
    if (armed) {
      disarm();
      onreset();
      return;
    }
    armed = true;
    timer = window.setTimeout(disarm, windowMs);
  }

  onDestroy(() => clearTimeout(timer));
</script>

<button use:chip class="reset" class:armed title={tip} onclick={press}>
  <span class="sizer" aria-hidden="true">{label}</span>
  <span class="sizer" aria-hidden="true">{armedLabel}</span>
  <span class="now">{armed ? armedLabel : label}</span>
</button>

<style>
  .reset {
    display: grid;
    height: 26px;
    padding: 0 12px;
    border: 1px solid var(--border);
    border-radius: var(--r-control, 6px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-label);
    cursor: pointer;
  }
  .reset:hover {
    background: var(--chip-hover);
  }
  .reset > span {
    grid-area: 1 / 1;
    line-height: 24px;
    white-space: nowrap;
    text-align: center;
  }
  .sizer {
    visibility: hidden;
  }
</style>

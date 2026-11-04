<script lang="ts">
  // One slider row (RenderSettingsPanel's addRow): the label, the slider at
  // the panel's 120 px and the readout C++ formats. A drag sends every step,
  // live, as the QSlider's valueChanged did; while the hand is on it the
  // slider keeps what the hand says rather than jumping back to a state the
  // round trip has not caught up with yet.
  import type { DialId, DialRange, DialState } from './api';

  let {
    id,
    label,
    range,
    dial,
    onmove,
  }: { id: DialId; label: string; range: DialRange; dial: DialState; onmove: (id: DialId, value: number) => void } =
    $props();

  let held = $state(false);
  let local = $state(0);
  const shown = $derived(held ? local : dial.value);

  function input(event: Event) {
    const value = Number((event.currentTarget as HTMLInputElement).value);
    local = value;
    onmove(id, value);
  }
</script>

<div class="dial" data-dial={id}>
  <span class="label">{label}</span>
  <input
    type="range"
    tabindex="-1"
    min={range.min}
    max={range.max}
    step="1"
    value={shown}
    oninput={input}
    onpointerdown={() => {
      local = dial.value;
      held = true;
    }}
    onpointerup={() => (held = false)}
    onpointercancel={() => (held = false)}
  />
  <span class="readout">{dial.readout}</span>
</div>

<style>
  .dial {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .label {
    flex: 1;
    font-size: var(--font-label);
    color: var(--text);
  }
  input {
    width: 120px;
    margin: 0;
    accent-color: var(--accent);
    cursor: pointer;
  }
  .readout {
    min-width: 34px;
    text-align: right;
    font-size: var(--font-badge);
    color: var(--text-muted);
    font-variant-numeric: tabular-nums;
  }
</style>

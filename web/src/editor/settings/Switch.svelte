<script lang="ts">
  // One checkable action as an on/off pill - AppearancePanel's OptionChip in
  // its Switch style. A MIRROR: `checked` and `enabled` are the action's, read
  // fresh on every actionsChanged, and a click asks for the action to be
  // triggered rather than moving the knob - the knob moves only when the
  // action does, so the pill cannot show a state the action refused to take.
  import { chip } from '../../actions';

  let {
    id,
    checked,
    enabled,
    tip,
    onpress,
  }: { id: string; checked: boolean; enabled: boolean; tip: string; onpress: () => void } = $props();

  function press() {
    if (enabled) onpress();
  }
</script>

<button
  use:chip
  class="switch"
  class:on={checked}
  role="switch"
  aria-checked={checked}
  aria-disabled={!enabled}
  data-action={id}
  title={tip || undefined}
  onclick={press}
>
  <span class="knob"></span>
</button>

<style>
  .switch {
    position: relative;
    flex: none;
    width: 38px;
    height: 20px;
    padding: 0;
    border: 1px solid var(--border);
    border-radius: var(--r-pill, 10px);
    background: var(--chip);
    cursor: pointer;
  }
  .switch:hover:not(.on):not([aria-disabled='true']) {
    background: var(--chip-hover);
  }
  .switch.on {
    background: var(--accent);
  }
  .switch[aria-disabled='true'] {
    background: var(--chip);
    cursor: default;
  }
  .knob {
    position: absolute;
    top: 2px;
    left: 2px;
    width: 14px;
    height: 14px;
    border-radius: var(--r-dot, 50%);
    background: var(--text);
    transition: left var(--motion, 160ms) ease-out;
  }
  .switch.on .knob {
    left: 20px;
  }
  .switch[aria-disabled='true'] .knob {
    background: var(--text-disabled);
  }
</style>

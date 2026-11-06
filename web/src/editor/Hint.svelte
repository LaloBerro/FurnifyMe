<script lang="ts" module>
  // The hint balloon (migration Phase 7a): HintBalloon::words() at the
  // balloon's own place. Which hint stands, and when it retires, stay in C++;
  // a press anywhere on the balloon is its "got it", as on the Qt balloon.
  export interface HintState {
    shown: boolean;
    text?: string;
    gotIt?: string;
    x?: number;
    y?: number;
    w?: number;
    h?: number;
  }
</script>

<script lang="ts">
  import { chip } from '../actions';

  let { hint, ondismiss }: { hint: HintState; ondismiss: () => void } = $props();
</script>

<button
  use:chip
  class="hint"
  data-hit
  aria-label={`${hint.text} — ${hint.gotIt}`}
  style:left="{hint.x ?? 0}px"
  style:top="{hint.y ?? 0}px"
  style:width="{hint.w ?? 0}px"
  style:height="{hint.h ?? 0}px"
  onclick={ondismiss}
>
  <span class="text">{hint.text}</span>
  <span class="got-it">{hint.gotIt}</span>
</button>

<style>
  .hint {
    position: absolute;
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    padding: 12px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: 8px;
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .text {
    font-size: var(--font-body);
    color: var(--text);
    overflow-wrap: anywhere;
  }
  .got-it {
    align-self: flex-end;
    font-size: var(--font-label);
    color: var(--accent-text);
  }
</style>

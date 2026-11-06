<script lang="ts">
  // THE LOADING CARD (migration Phase 7a) - LoadingCard's paint, ported: the
  // viewport washed, the app mark, the furniture's name, a bar filling as the
  // bodies arrive and the step underneath. The Qt card still stands (painting
  // nothing) and swallows every press, so this only draws. With no total yet
  // the bar shows a short stub, as the Qt card did: nothing animates while
  // the load blocks the thread that feeds this page.
  import mark from '../assets/app-mark.png';
  import Wash from './Wash.svelte';
  import type { QuestionState } from './Question.svelte';

  let { question, host }: { question: QuestionState; host: { w: number; h: number } } = $props();

  const kCardWidth = 380;
  const kGutter = 16;
  let cardHeight = $state(0);
  const width = $derived(Math.min(kCardWidth, Math.max(80, host.w - kGutter * 2)));
  const left = $derived(Math.round((host.w - width) / 2));
  const top = $derived(Math.max(kGutter, Math.round(host.h * 0.46 - cardHeight / 2)));
  const fraction = $derived(
    (question.total ?? 0) > 0 ? Math.min(1, Math.max(0, (question.done ?? 0) / (question.total ?? 1))) : 0.12,
  );
</script>

<Wash {host} keepClear={question.keepClear}>
  <div
    class="card loading-card"
    role="status"
    aria-live="polite"
    style:left="{left}px"
    style:top="{top}px"
    style:width="{width}px"
    bind:clientHeight={cardHeight}
  >
    <div class="head">
      <img class="mark" src={mark} alt="" width="28" height="28" />
      <div class="title">{question.title}</div>
    </div>
    <div class="track"><div class="fill" style:width="{fraction * 100}%"></div></div>
    <div class="step">{question.step}</div>
  </div>
</Wash>

<style>
  .card {
    position: absolute;
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 20px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-dialog, 14px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
  }
  .head {
    display: flex;
    align-items: center;
    gap: 8px;
    min-width: 0;
  }
  .mark {
    flex: none;
  }
  .title {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-size: var(--font-title);
    font-weight: 700;
    color: var(--text);
  }
  .track {
    height: 6px;
    border-radius: 3px;
    background: var(--chip);
    overflow: hidden;
  }
  .fill {
    height: 100%;
    border-radius: 3px;
    background: var(--accent);
  }
  .step {
    font-size: var(--font-body);
    color: var(--text-muted);
  }
</style>

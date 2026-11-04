<script lang="ts" module>
  export interface QuestionState {
    asking: boolean;
    title?: string;
    body?: string;
    save?: string;
    saveKey?: string;
    discard?: string;
    keep?: string;
    keepKey?: string;
    // The window buttons' rectangle in viewport pixels: the wash leaves it
    // clear, as the Qt card's raise did.
    keepClear?: { x: number; y: number; w: number; h: number };
  }
</script>

<script lang="ts">
  // THE CLOSE QUESTION (migration Phase 3f) - a faithful port of
  // UnsavedCloseCard's paint: the viewport washed in its own token, most of
  // the way opaque, and the card over it. The Qt card still stands (invisible)
  // and keeps the key claim - Enter saves, Esc keeps editing, no other key
  // reaches a shortcut - so the page only draws and answers clicks. The wash
  // takes every press over the viewport (data-hit), stopping above the status
  // strip so the unsaved dot stays readable, as the Qt bar did.
  import { chip } from '../actions';

  let {
    question,
    host,
    onanswer,
  }: {
    question: QuestionState;
    host: { w: number; h: number };
    onanswer: (which: 'save' | 'discard' | 'keep') => void;
  } = $props();

  const kCardWidth = 380;
  const kGutter = 16;
  let cardHeight = $state(0);
  const width = $derived(Math.min(kCardWidth, Math.max(80, host.w - kGutter * 2)));
  const left = $derived(Math.round((host.w - width) / 2));
  const top = $derived(Math.max(kGutter, Math.round(host.h * 0.48 - cardHeight / 2)));
  // An even-odd hole where the window buttons stand: Qt composites this page
  // over plain widgets whatever their stacking, so they would read dimmed.
  const hole = $derived.by(() => {
    const k = question.keepClear;
    if (!k || k.w <= 0 || k.h <= 0) return undefined;
    const x2 = k.x + k.w;
    const y2 = k.y + k.h;
    return `polygon(evenodd, 0 0, 100% 0, 100% 100%, 0 100%, 0 0, ${k.x}px ${k.y}px, ${k.x}px ${y2}px, ${x2}px ${y2}px, ${x2}px ${k.y}px, ${k.x}px ${k.y}px)`;
  });
</script>

<div class="wash" data-hit style:height="{host.h}px" style:clip-path={hole} role="presentation">
  <div
    class="card"
    role="alertdialog"
    aria-modal="true"
    aria-labelledby="close-question-title"
    aria-describedby="close-question-body"
    style:left="{left}px"
    style:top="{top}px"
    style:width="{width}px"
    bind:clientHeight={cardHeight}
  >
    <div class="title" id="close-question-title">{question.title}</div>
    <div class="body" id="close-question-body">{question.body}</div>
    <div class="answers">
      <button use:chip class="answer save" onclick={() => onanswer('save')}>
        <span class="label">{question.save}</span><span class="key">{question.saveKey}</span>
      </button>
      <button use:chip class="answer discard" onclick={() => onanswer('discard')}>
        <span class="label">{question.discard}</span>
      </button>
      <button use:chip class="answer keep" onclick={() => onanswer('keep')}>
        <span class="label">{question.keep}</span><span class="key">{question.keepKey}</span>
      </button>
    </div>
  </div>
</div>

<style>
  .wash {
    position: fixed;
    left: 0;
    top: 0;
    width: 100%;
    /* The Qt card's #0a0a0dcc: the viewport's own token at 204 of 255. */
    background: color-mix(in srgb, var(--viewport) 80%, transparent);
  }
  .card {
    position: absolute;
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    gap: 12px;
    padding: 20px 20px 16px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-dialog, 14px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
  }
  .title {
    font-size: var(--font-title);
    font-weight: 700;
    color: var(--text);
    overflow-wrap: anywhere;
  }
  .body {
    font-size: var(--font-body);
    color: var(--text-muted);
  }
  .answers {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .answer {
    height: 36px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 12px;
    border: max(1px, var(--chip-stroke, 1px)) solid var(--border);
    border-radius: var(--r-control, 8px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-body);
    cursor: pointer;
  }
  .answer:hover {
    background: var(--chip-hover);
  }
  .answer:active {
    background: var(--chip-active);
  }
  .label {
    font-weight: 500;
  }
  .key {
    font-size: var(--font-badge);
    color: var(--text-muted);
  }
  .answer.save {
    border-color: var(--accent);
    background: var(--accent);
  }
  .answer.save:hover {
    filter: brightness(1.15);
  }
  .answer.save:active {
    filter: brightness(0.87);
  }
  .answer.save .key {
    color: var(--text);
  }
  .answer.discard {
    color: var(--danger-text);
  }
</style>

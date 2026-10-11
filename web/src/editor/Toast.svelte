<script lang="ts" module>
  export interface ToastMessage {
    id: number;
    text: string;
    kind: 'note' | 'failure';
    offersUndo: boolean;
    stamp: number;
  }
</script>

<script lang="ts">
  // ONE toast at a time - Toast.cpp/ToastHost, ported. A Note lives 4 s and
  // a Failure 8 s; the countdown waits while the pointer rests on it and
  // resumes with at least 1.5 s left, so the hand that was reading it does not
  // make it vanish. It fades out over 160 ms and rises 6 px only on arrival -
  // a replacement does not jump back down, the card never left. A Failure is
  // a SHAPE as well as a colour (F6): a warning mark and its whole border in
  // danger. Announced politely for a Note, assertively for a Failure - and
  // never as an alert, which Qt answers with a system sound.
  import { chip } from '../actions';
  import { copy } from './copy';
  import { place, type Rect } from './solver';

  let {
    message,
    undoEnabled,
    obstacles,
    host,
    pointer,
    onundo,
    ondismissed,
  }: {
    message: ToastMessage | null;
    undoEnabled: boolean;
    obstacles: Rect[];
    host: { w: number; h: number };
    pointer: { x: number; y: number } | null;
    onundo: (stamp: number) => void;
    ondismissed: () => void;
  } = $props();

  const kNoteMs = 4000;
  const kFailureMs = 8000;
  const kResumeFloorMs = 1500;
  const kFadeMs = 160;

  let shown: ToastMessage | null = $state(null);
  let arriving = $state(false);
  let leaving = $state(false);
  let width = $state(0);
  let height = $state(0);
  let timer = 0;
  // The fade's own timer: a message arriving mid-fade cancels it, or the
  // fade that was ending the OLD toast would sweep the new one away (review
  // C1 - Qt's fadeTo() stops a running fade the same way).
  let fadeTimer = 0;
  // Undo is taken once per message: a second click inside the fade, or one
  // landing before documentMoved has arrived, would otherwise pop the
  // checkpoint BEFORE the one this toast names (review I1 - Toast.cpp's
  // myDismissing). C++ also refuses a stamp the document has moved past.
  let undoTaken = $state(false);
  let deadline = 0;
  let remaining = 0;
  let paused = false;
  let over = false;
  let lastId = -1;
  let card: HTMLElement | undefined = $state();

  const position = $derived(place(host, { w: width, h: height }, obstacles));

  function arm(ms: number) {
    clearTimeout(timer);
    deadline = Date.now() + ms;
    timer = window.setTimeout(dismiss, ms);
  }

  function pause() {
    if (paused || !shown || leaving) return;
    paused = true;
    remaining = Math.max(0, deadline - Date.now());
    clearTimeout(timer);
  }

  function resume() {
    if (!paused) return;
    paused = false;
    arm(Math.max(remaining, kResumeFloorMs));
  }

  export function dismiss() {
    if (!shown || leaving) return;
    clearTimeout(timer);
    leaving = true;
    fadeTimer = window.setTimeout(() => {
      fadeTimer = 0;
      shown = null;
      leaving = false;
      paused = false;
      ondismissed();
    }, kFadeMs);
  }

  $effect(() => {
    const m = message;
    if (!m || m.id === lastId) return;
    lastId = m.id;
    const wasShown = shown !== null && !leaving;
    clearTimeout(fadeTimer);
    fadeTimer = 0;
    shown = m;
    leaving = false;
    paused = false;
    undoTaken = false;
    arriving = !wasShown;
    if (arriving) window.setTimeout(() => (arriving = false), 0);
    arm(m.kind === 'failure' ? kFailureMs : kNoteMs);
    // A replacement under a pointer that is ALREADY resting on the card is
    // being read too (review I2 - ToastHost::show() ends in syncHoverPause()).
    if (over) pause();
    // A message landing under a pointer that already rests there is being
    // read from its first frame - decided once it is laid out, against the
    // box it actually landed in.
    window.setTimeout(() => {
      const p = pointer;
      if (!p || over || !card) return;
      const r = card.getBoundingClientRect();
      if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) pause();
    }, 50);
  });

  function enter() {
    over = true;
    pause();
  }

  function leave() {
    over = false;
    resume();
  }

  const undoOpen = $derived(undoEnabled && !leaving && !undoTaken);

  function undo() {
    if (!undoOpen || !shown) return;
    undoTaken = true;
    onundo(shown.stamp);
  }
</script>

<div class="lane" aria-live={shown?.kind === 'failure' ? 'assertive' : 'polite'}>
  {#if shown}
    <div
      class="toast"
      class:failure={shown.kind === 'failure'}
      class:arriving
      class:leaving
      data-hit={leaving ? undefined : ''}
      role="status"
      aria-label={shown.text}
      aria-describedby="toast-kind"
      style="left: {position.x}px; top: {position.y}px;"
      bind:this={card}
      bind:clientWidth={width}
      bind:clientHeight={height}
      onmouseenter={enter}
      onmouseleave={leave}
    >
      {#if shown.kind === 'failure'}
        <svg class="glyph" viewBox="0 0 24 24" aria-hidden="true">
          <path d="M12 3.5 L21.5 20 L2.5 20 Z" />
          <line x1="12" y1="9.5" x2="12" y2="14" />
          <line x1="12" y1="17" x2="12" y2="17.01" />
        </svg>
      {/if}
      <span id="toast-kind" class="hidden">{shown.kind === 'failure' ? copy.failure : copy.note}</span>
      <span class="text">{shown.text}</span>
      {#if shown.offersUndo}
        <button use:chip class="undo" aria-disabled={!undoOpen} onclick={undo}>{copy.undo}</button>
      {/if}
    </div>
  {/if}
</div>

<style>
  .toast {
    position: fixed;
    width: 360px;
    box-sizing: border-box;
    min-height: 44px;
    padding: 12px 14px 12px 20px; /* 14 + the 6 px stripe */
    display: flex;
    align-items: center;
    gap: 8px;
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 8px;
    overflow: hidden;
    color: var(--text);
    font-size: var(--font-body);
    opacity: 1;
    transform: translateY(0);
    transition:
      opacity var(--motion) ease-out,
      transform var(--motion) ease-out;
  }
  .toast::before {
    content: '';
    position: absolute;
    left: 0;
    top: 0;
    bottom: 0;
    width: 6px;
    background: var(--accent);
  }
  .toast.failure {
    border-color: var(--danger);
  }
  .toast.failure::before {
    background: var(--danger);
  }
  .toast.arriving {
    opacity: 0;
    transform: translateY(6px);
  }
  .toast.leaving {
    opacity: 0;
  }
  .glyph {
    flex: none;
    width: 16px;
    height: 16px;
    fill: none;
    stroke: var(--danger);
    stroke-width: 2;
    stroke-linecap: round;
    stroke-linejoin: round;
  }
  .hidden {
    position: absolute;
    width: 1px;
    height: 1px;
    overflow: hidden;
    clip-path: inset(50%);
  }
  .text {
    flex: 1;
    min-width: 0;
    overflow-wrap: anywhere;
  }
  .undo {
    flex: none;
    width: 64px;
    height: 26px;
    border: none;
    border-radius: 5px;
    background: var(--chip-hover);
    color: var(--accent-text);
    font: inherit;
    font-size: var(--font-label);
    cursor: pointer;
  }
  .undo[aria-disabled='true'] {
    color: var(--text-disabled);
    cursor: default;
  }
</style>

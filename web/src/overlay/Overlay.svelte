<script lang="ts">
  import { onMount, tick } from 'svelte';
  import { connect, type Bridge } from './bridge';
  import { reportHitsOnChange } from './hits';

  let bridge: Bridge | null = null;

  // The glued chip's anchor, in viewport pixels, as C++ projected it.
  let anchor = $state({ rev: 0, x: 0, y: 0, visible: false });
  // True while the right button orbits: the chip fades rather than trailing.
  let orbiting = $state(false);
  // Any camera gesture. The chip leaves the click mask while it lasts: a mask
  // that changes every frame makes Qt repaint the viewport behind it every
  // frame - one extra 3D redraw per step (measured, gate 2's pan).
  let cameraMoving = $state(false);
  let animating = $state(false);
  let toast = $state<{ text: string; kind: string; id: number } | null>(null);
  let width = $state(40);
  let toastTimer = 0;
  let toastSeq = 0;

  function showToast(text: string, kind: string) {
    toast = { text, kind, id: ++toastSeq };
    clearTimeout(toastTimer);
    toastTimer = window.setTimeout(() => (toast = null), kind === 'failure' ? 8000 : 4000);
  }

  onMount(async () => {
    const b = await connect();
    bridge = b;
    b.anchorMoved.connect((rev, x, y, visible) => {
      anchor = { rev, x, y, visible };
      // Svelte has written the DOM by the next frame callback; that frame is
      // the one the chip is painted in, so its revision is what was shown.
      requestAnimationFrame(() => b.reportChipShown(rev, x, y));
    });
    b.orbitActive.connect((on) => (orbiting = on));
    // While the camera moves, every page animation holds still: a running
    // animation makes Chromium produce frames Qt then composites, which
    // costs the viewport about a millisecond per frame (measured, gate 2).
    b.cameraMoving.connect((on) => {
      cameraMoving = on;
      document.documentElement.classList.toggle('camera-moving', on);
    });
    b.animatingChanged.connect((on) => (animating = on));
    b.toastRequested.connect(showToast);
    reportHitsOnChange(b);
    await tick();
    b.reportReady();
  });

  const chipShown = $derived(anchor.visible && !orbiting);
</script>

<section class="card" data-hit aria-label="Overlay card">
  <h2>Overlay card</h2>
  <p class="muted">Drawn by HTML over the viewport</p>
  <label class="row">
    <span>Width</span>
    <input type="range" min="10" max="120" bind:value={width} aria-label="Width" />
    <output>{width} mm</output>
  </label>
  <div class="actions">
    <button class="secondary" onclick={() => showToast(`Width is ${width} mm`, 'note')}>Show note</button>
    <button class="primary" onclick={() => bridge?.runCommand('apply')}>Apply</button>
  </div>
</section>

<div
  class="chip"
  class:shown={chipShown}
  data-hit={chipShown && !cameraMoving ? '' : 'off'}
  style="transform: translate3d({anchor.x}px, {anchor.y}px, 0) translate(-50%, calc(-100% - 14px));"
  aria-hidden={!chipShown}
>
  <span class="chip-label">Pull</span>
  <span class="chip-value">25 mm</span>
</div>

<div class="toast-lane">
  {#if animating}
    <div class="busy" data-hit><span class="dot"></span>Animating</div>
  {/if}
  <div class="toast-live" role="status" aria-live="polite">
    {#if toast}
      {#key toast.id}
        <div class="toast" class:failure={toast.kind === 'failure'} data-hit>
          {toast.text}
        </div>
      {/key}
    {/if}
  </div>
</div>

<style>
  .card {
    position: fixed;
    right: 24px;
    top: 50%;
    translate: 0 -50%;
    width: 248px;
    box-sizing: border-box;
    padding: 16px;
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    box-shadow: 0 10px 28px rgba(0, 0, 0, 0.35);
  }
  h2 {
    margin: 0 0 2px;
    font-size: 15px;
    font-weight: 600;
  }
  .muted {
    margin: 0 0 14px;
    color: var(--text-muted);
  }
  .row {
    display: grid;
    grid-template-columns: auto 1fr auto;
    align-items: center;
    gap: 10px;
    margin-bottom: 16px;
  }
  .row output {
    min-width: 48px;
    text-align: right;
    font-variant-numeric: tabular-nums;
  }
  input[type='range'] {
    accent-color: var(--accent);
    width: 100%;
  }
  .actions {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
  }
  button {
    font: inherit;
    color: var(--text);
    border-radius: 8px;
    padding: 6px 12px;
    border: 1px solid var(--border);
    background: var(--chip);
    cursor: pointer;
    transition: background var(--motion);
  }
  button:hover {
    background: var(--chip-hover);
  }
  button:focus {
    outline: none;
  }
  .primary {
    background: var(--accent);
    border-color: var(--accent);
  }
  .primary:hover {
    background: color-mix(in srgb, var(--accent) 85%, white);
  }

  .chip {
    position: fixed;
    left: 0;
    top: 0;
    display: flex;
    gap: 6px;
    align-items: baseline;
    padding: 5px 10px;
    background: var(--panel);
    border: 2px solid var(--accent);
    border-radius: 999px;
    white-space: nowrap;
    opacity: 0;
    transition: opacity 120ms ease-out;
    will-change: transform;
  }
  .chip.shown {
    opacity: 1;
  }
  .chip-label {
    color: var(--text-muted);
  }
  .chip-value {
    color: var(--accent-text);
    font-weight: 600;
  }

  .toast-lane {
    position: fixed;
    left: 50%;
    bottom: 56px;
    translate: -50% 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 8px;
  }
  .toast {
    padding: 10px 16px;
    background: var(--chip-active);
    border: 1px solid var(--border);
    border-left: 6px solid var(--accent);
    border-radius: var(--radius);
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.35);
    animation: rise var(--motion) ease-out;
  }
  .toast.failure {
    border-color: var(--danger-text);
  }
  .busy {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 6px 12px;
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    color: var(--text-muted);
  }
  .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--accent);
    animation: pulse 900ms ease-in-out infinite;
  }
  @keyframes rise {
    from {
      opacity: 0;
      transform: translateY(8px);
    }
  }
  @keyframes pulse {
    50% {
      transform: scale(1.6);
      opacity: 0.4;
    }
  }
</style>

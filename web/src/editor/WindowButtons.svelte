<script lang="ts">
  // THE WINDOW BUTTONS over the editor's viewport (migration Phase 7a) - the Qt
  // WindowButtons' Card look: a small card holding minimize, maximize and
  // close. They talk to the hub's own controls object: minimize and close are
  // this page's presses; maximize answers Windows' hit test as HTMAXBUTTON
  // from the rectangle reported here (snap layouts), so its hover and press
  // arrive from the native side and its click never reaches the page.
  import { onMount } from 'svelte';
  import { chip } from '../actions';
  import type { Rect, WindowApi } from '../hub/api';
  import { copy } from '../hub/copy';

  let { win, rect }: { win: WindowApi; rect: { x: number; y: number; w: number; h: number } } = $props();

  let card: HTMLElement;
  let maxButton: HTMLButtonElement;
  let maximized = $state(false);
  let maxHovered = $state(false);
  let maxPressed = $state(false);

  const rectOf = (el: Element): Rect => {
    const r = el.getBoundingClientRect();
    return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height) };
  };
  const report = () => {
    if (!card || !maxButton) return;
    win.reportChrome({ titleBar: { x: 0, y: 0, w: 0, h: 0 }, maxButton: rectOf(maxButton), buttons: rectOf(card) });
  };

  onMount(() => {
    maximized = win.maximized;
    win.maximizedChanged.connect((m) => (maximized = m));
    win.maxHovered.connect((h) => (maxHovered = h));
    win.maxPressed.connect((p) => (maxPressed = p));
    report();
    const observer = new ResizeObserver(report);
    observer.observe(card);
    return () => observer.disconnect();
  });

  // Moved by the overlay (a resize, a theme): the native side needs the new place.
  $effect(() => {
    void rect.x;
    void rect.y;
    queueMicrotask(report);
  });
</script>

<div
  class="window-buttons"
  data-hit
  bind:this={card}
  style:left="{rect.x}px"
  style:top="{rect.y}px"
  style:width="{rect.w}px"
  style:height="{rect.h}px"
>
  <button use:chip class="win" aria-label={copy.minimize} onclick={() => win.minimize()}>
    <svg viewBox="0 0 24 24" aria-hidden="true"><line x1="5" y1="12" x2="19" y2="12" /></svg>
  </button>
  <button
    use:chip
    class="win"
    class:hovered={maxHovered}
    class:pressed={maxPressed}
    aria-label={maximized ? copy.restore : copy.maximize}
    bind:this={maxButton}
    onclick={() => win.toggleMaximize()}
  >
    <svg viewBox="0 0 24 24" aria-hidden="true">
      {#if maximized}
        <polyline points="8,5 19,5 19,16" />
        <rect x="5" y="8" width="11" height="11" />
      {:else}
        <rect x="5" y="5" width="14" height="14" />
      {/if}
    </svg>
  </button>
  <button use:chip class="win close" aria-label={copy.close} onclick={() => win.close()}>
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <line x1="5" y1="5" x2="19" y2="19" />
      <line x1="5" y1="19" x2="19" y2="5" />
    </svg>
  </button>
</div>

<style>
  .window-buttons {
    position: absolute;
    box-sizing: border-box;
    display: flex;
    gap: 4px;
    padding: 6px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: 10px;
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
  }
  .win {
    width: 36px;
    height: 26px;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 0;
    border: 0;
    border-radius: 6px;
    background: none;
    color: var(--text);
    cursor: pointer;
  }
  .win svg {
    width: 14px;
    height: 14px;
    fill: none;
    stroke: currentColor;
    stroke-width: 1.6;
  }
  .win:hover,
  .win.hovered {
    background: var(--chip-hover);
  }
  .win:active,
  .win.pressed {
    background: var(--chip-active);
  }
  /* Close wears the platform's red on hover, white ink over it - the Qt
     buttons' rule. */
  .win.close:hover {
    background: var(--danger);
    color: #fff;
  }
</style>

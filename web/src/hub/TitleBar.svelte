<script lang="ts">
  // The hub's own title strip (the native caption is gone - WindowChrome on
  // the C++ side answers Windows' hit tests from the rects this reports):
  // the app mark, the wordmark, and the three Flat window buttons, 44 x 32
  // each with no gaps. The maximize button's hover and press arrive from the
  // native side, because Windows routes that button's traffic as NON-client
  // (so snap layouts appear over it) and the page never sees it.
  import { onMount } from 'svelte';
  import type { Rect, WindowApi } from './api';
  import { copy } from './copy';
  import mark from '../assets/app-mark.png';

  let { win }: { win: WindowApi } = $props();

  let bar: HTMLElement;
  let buttons: HTMLElement;
  let maxButton: HTMLButtonElement;
  let maximized = $state(false);
  let maxHovered = $state(false);
  let maxPressed = $state(false);

  const rectOf = (el: Element): Rect => {
    const r = el.getBoundingClientRect();
    return { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height) };
  };
  const report = () =>
    win.reportChrome({ titleBar: rectOf(bar), maxButton: rectOf(maxButton), buttons: rectOf(buttons) });

  onMount(() => {
    maximized = win.maximized;
    win.maximizedChanged.connect((m) => (maximized = m));
    win.maxHovered.connect((h) => (maxHovered = h));
    win.maxPressed.connect((p) => (maxPressed = p));
    report();
    const observer = new ResizeObserver(report);
    observer.observe(bar);
    return () => observer.disconnect();
  });
</script>

<header class="bar" bind:this={bar}>
  <img class="mark" src={mark} alt="" width="18" height="18" />
  <span class="wordmark">{copy.wordmark}</span>
  <div class="buttons" bind:this={buttons}>
    <button class="win" aria-label={copy.minimize} onclick={() => win.minimize()}>
      <svg viewBox="0 0 24 24" aria-hidden="true"><line x1="5" y1="12" x2="19" y2="12" /></svg>
    </button>
    <button
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
    <button class="win close" aria-label={copy.close} onclick={() => win.close()}>
      <svg viewBox="0 0 24 24" aria-hidden="true">
        <line x1="5" y1="5" x2="19" y2="19" />
        <line x1="5" y1="19" x2="19" y2="5" />
      </svg>
    </button>
  </div>
</header>

<style>
  .bar {
    display: flex;
    align-items: center;
    gap: 8px;
    height: 32px;
    padding-left: 12px;
    flex: none;
    user-select: none;
  }
  .mark {
    width: 18px;
    height: 18px;
  }
  .wordmark {
    font-size: var(--font-label);
    font-weight: 600;
    color: var(--text);
    flex: 1;
  }
  .buttons {
    display: flex;
    align-self: stretch;
  }
  .win {
    width: 44px;
    height: 32px;
    border: none;
    background: transparent;
    color: var(--text-muted);
    padding: 0;
    display: grid;
    place-items: center;
  }
  .win svg {
    width: 12px;
    height: 12px;
    fill: none;
    stroke: currentColor;
    stroke-width: 2.4; /* 1.2 px at the glyph's half scale */
  }
  .win:hover,
  .win.hovered {
    background: var(--chip-hover);
    color: var(--text);
  }
  .win:active,
  .win.pressed {
    background: var(--chip-active);
  }
  .win.close:hover {
    background: var(--danger);
    color: #ffffff;
  }
  .win.close:active {
    background: color-mix(in srgb, var(--danger) 83%, black);
    color: #ffffff;
  }
</style>

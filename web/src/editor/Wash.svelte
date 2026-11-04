<script lang="ts" module>
  export interface KeepClear {
    x: number;
    y: number;
    w: number;
    h: number;
  }
</script>

<script lang="ts">
  // THE WASH a question stands on (Phases 3f, 3g): the viewport washed in its
  // own token, most of the way opaque, taking every press over the viewport
  // (data-hit) and stopping above the status strip, so the unsaved dot stays
  // readable as the Qt bar's did. It leaves the window buttons clear - Qt
  // composites this page over plain widgets whatever their stacking, so they
  // would read dimmed under it.
  import type { Snippet } from 'svelte';

  let {
    host,
    keepClear,
    children,
  }: { host: { w: number; h: number }; keepClear?: KeepClear; children: Snippet } = $props();

  // An even-odd hole where the window buttons stand.
  const hole = $derived.by(() => {
    const k = keepClear;
    if (!k || k.w <= 0 || k.h <= 0) return undefined;
    const x2 = k.x + k.w;
    const y2 = k.y + k.h;
    return `polygon(evenodd, 0 0, 100% 0, 100% 100%, 0 100%, 0 0, ${k.x}px ${k.y}px, ${k.x}px ${y2}px, ${x2}px ${y2}px, ${x2}px ${k.y}px, ${k.x}px ${k.y}px)`;
  });
</script>

<div class="wash" data-hit style:height="{host.h}px" style:clip-path={hole} role="presentation">
  {@render children()}
</div>

<style>
  .wash {
    position: fixed;
    left: 0;
    top: 0;
    width: 100%;
    /* The Qt cards' #0a0a0dcc: the viewport's own token at 204 of 255. */
    background: color-mix(in srgb, var(--viewport) 80%, transparent);
  }
</style>

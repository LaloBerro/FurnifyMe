<script lang="ts">
  // The Items card's glyphs, drawn as the Qt drawer's IconSet draws them: a
  // 1.5 px stroke in the current colour on a 16-unit grid. Inline SVG - no
  // asset to load, and the colour is the CSS token around it.
  let { name, size = 16 }: { name: string; size?: number } = $props();
</script>

<svg
  class="glyph"
  width={size}
  height={size}
  viewBox="0 0 16 16"
  fill="none"
  stroke="currentColor"
  stroke-width="1.5"
  stroke-linecap="round"
  stroke-linejoin="round"
  aria-hidden="true"
>
  {#if name === 'eye'}
    <path d="M1.5 8c1.6-3 4-4.5 6.5-4.5S12.9 5 14.5 8c-1.6 3-4 4.5-6.5 4.5S3.1 11 1.5 8z" />
    <circle cx="8" cy="8" r="2" />
  {:else if name === 'eyeHidden'}
    <path d="M1.5 8c1.6-3 4-4.5 6.5-4.5S12.9 5 14.5 8c-1.6 3-4 4.5-6.5 4.5S3.1 11 1.5 8z" />
    <path d="M2.5 13.5l11-11" />
  {:else if name === 'grainAlong'}
    <rect x="2" y="4" width="12" height="8" rx="1" />
    <path d="M4 6.5c2-.8 4 .8 8 0M4 9.5c2-.8 4 .8 8 0" />
  {:else if name === 'grainAcross'}
    <rect x="2" y="4" width="12" height="8" rx="1" />
    <path d="M6 5.5c-.8 2 .8 3 0 5M10 5.5c-.8 2 .8 3 0 5" />
  {:else if name === 'chevronRight'}
    <path d="M6 4l4 4-4 4" />
  {:else if name === 'chevronDown'}
    <path d="M4 6l4 4 4-4" />
  {:else if name === 'folder'}
    <path d="M2 4.5h4l1.5 1.5H14v6.5H2z" />
    <path d="M8 7.5v3.5M6.25 9.25h3.5" />
  {:else if name === 'plus'}
    <path d="M8 3.5v9M3.5 8h9" />
  {:else if name === 'body'}
    <path d="M8 2l5.5 3v6L8 14l-5.5-3V5z" />
    <path d="M2.5 5L8 8l5.5-3M8 8v6" />
  {:else if name === 'outline'}
    <path d="M3 12.5l2-8 6.5-1.5 1.5 7-6 3.5z" />
  {:else if name === 'warning'}
    <path d="M8 2l6.5 11.5h-13z" />
    <path d="M8 6.5v3.5M8 12v.01" />
  {/if}
</svg>

<style>
  .glyph {
    display: block;
    flex-shrink: 0;
  }
</style>

<script lang="ts">
  // One tool chip (Phase 3i): ToolChip's icon-only form, drawn. It mirrors
  // its QAction - enabled, checked, label, tooltip all come from C++ on every
  // change - and a click asks C++ to trigger the action, which refuses a
  // disabled one. The glyph is IconSet's own painting, sent as an alpha mask
  // and filled with the state's ink here (ruling R2).
  import { chip } from '../../actions';
  import type { ChipState } from './api';

  let {
    id,
    info,
    glyph,
    ontrigger,
  }: {
    id: string;
    info: ChipState | undefined;
    glyph: string | undefined;
    ontrigger: (id: string) => void;
  } = $props();

  const enabled = $derived(!!info?.enabled);
  const on = $derived(!!info?.checkable && !!info?.checked);
  let down = $state(false);
  // What a screen reader is told beyond the name: the composed tooltip, as
  // ToolChip's accessibleDescription (F26).
  const described = $derived<Record<string, string>>(info?.tip ? { 'aria-description': info.tip } : {});
</script>

<button
  use:chip
  class="chip"
  class:on
  class:down={down && enabled && !on}
  class:off={!enabled}
  data-chip={id}
  aria-label={info?.label ?? id}
  {...described}
  aria-pressed={info?.checkable ? on : undefined}
  aria-disabled={!enabled}
  title={info?.tip || undefined}
  onmousedown={() => (down = true)}
  onmouseup={() => (down = false)}
  onmouseleave={() => (down = false)}
  onclick={() => {
    if (enabled) ontrigger(id);
  }}
>
  <span class="glyph" style={glyph ? `-webkit-mask-image: url(${glyph}); mask-image: url(${glyph});` : ''}></span>
</button>

<style>
  /* Graphite is the fallbacks: ToolChip's own paint (chip fill, border at the
     chip stroke, the accent inset ring when checked, a darker fill when
     disabled). A theme's chip arrives as --shell-chip-* (ruling R3). */
  .chip {
    box-sizing: border-box;
    display: grid;
    place-items: center;
    width: var(--shell-chip-size, 34px);
    height: var(--shell-chip-size, 34px);
    padding: 0;
    border: var(--shell-chip-edge-width, var(--chip-stroke, 1px)) solid var(--shell-chip-idle-edge, var(--border));
    border-radius: var(--shell-chip-radius, 6px);
    background: var(--shell-chip-idle-fill, var(--chip));
    color: var(--shell-chip-idle-ink, var(--text));
    cursor: pointer;
  }
  .chip:hover:not(.off):not(.on) {
    border-color: var(--shell-chip-hover-edge, var(--border));
    background: var(--shell-chip-hover-fill, var(--chip-hover));
    color: var(--shell-chip-hover-ink, var(--text));
  }
  .chip.on,
  .chip.down {
    border-color: var(--shell-chip-on-edge, var(--border));
    background: linear-gradient(
      var(--shell-chip-on-top, var(--chip-active)),
      var(--shell-chip-on-bottom, var(--chip-active))
    );
    color: var(--shell-chip-on-ink, var(--text));
    box-shadow: inset 0 1px 0 var(--shell-chip-on-inner-top, transparent);
    /* Graphite's checked chip: "bordered, plus marked" - a second ring one
       pixel inside the border, at the chip stroke's width (ToolChip). */
    outline: var(--chip-stroke, 1px) solid var(--shell-chip-on-ring, var(--accent));
    outline-offset: calc(-1px - 2 * var(--chip-stroke, 1px));
  }
  /* Pressed: the checked look's fill, never its ring (ToolChip: isDown()). */
  .chip.down {
    outline: none;
  }
  .chip.off {
    border-color: var(--shell-chip-off-edge, var(--border));
    background: var(--shell-chip-off-fill, color-mix(in srgb, var(--chip) 87%, black));
    color: var(--shell-chip-off-ink, var(--text-disabled));
    cursor: default;
  }
  .glyph {
    display: block;
    width: var(--shell-chip-glyph, 16px);
    height: var(--shell-chip-glyph, 16px);
    background: currentColor;
    -webkit-mask-size: 100% 100%;
    mask-size: 100% 100%;
    -webkit-mask-repeat: no-repeat;
    mask-repeat: no-repeat;
  }
</style>

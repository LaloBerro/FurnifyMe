<script lang="ts">
  // One joint (JointsPanel's row, ported - migration Phase 3d). Closed: the two
  // pieces' names, the kind line (or the reason, for a broken joint) and the
  // caveat. Open: each cut piece's name and depth over a TO-SCALE ruler - ticks
  // for a fastener, a band for a housing or interlock - and the one line the
  // pieces share; or, when no single edge applies or the labels collide even
  // staggered, the numbers WRITTEN instead (ruler.ts decides). Every string is
  // C++'s (JointRows); this file decides only where it goes.
  //
  // A broken joint carries no number anywhere: C++ sends it no pieces, and its
  // names wear dangerText. A click toggles the row and selects its joint - a
  // double-click's second click is ignored (detail >= 2), as the Qt row
  // swallowed the trailing release.
  import { chip } from '../../actions';
  import type { JointRowData, JointsWords } from './api';
  import { kBarHeight, kTickOverhang, kZeroOverhang, layoutStrip, type StripLayout } from './ruler';

  let {
    row,
    words,
    enabled,
    measure,
    stripWidth,
    onactivate,
    onremove,
  }: {
    row: JointRowData;
    words: JointsWords;
    enabled: boolean;
    measure: (text: string) => number;
    stripWidth?: number; // a test's fixed width; the measured one otherwise
    onactivate: () => void;
    onremove: () => void;
  } = $props();

  let measured = $state(0);
  const width = $derived(stripWidth ?? measured);
  const open = $derived(row.expanded && !row.broken && row.pieces.length > 0);

  // Every ruler, or null: no single edge to put at zero, or a strip whose
  // labels collide even on two lines - then every piece is written instead.
  const strips: StripLayout[] | null = $derived.by(() => {
    if (!open || !row.edgeNamed || width <= 0) return null;
    const out: StripLayout[] = [];
    for (const piece of row.pieces) {
      const s = layoutStrip(piece, row.edge, 0, width, measure);
      if (!s) return null;
      out.push(s);
    }
    return out;
  });
  // Until the row has a width there is nothing to decide; written is the
  // honest answer only once a measured strip refused.
  const written = $derived(open && (!row.edgeNamed || (width > 0 && strips === null)));
  const shared = $derived(written ? row.sharedWritten : row.sharedDrawn);

  function click(event: MouseEvent) {
    if (event.detail >= 2) return;
    onactivate();
  }
</script>

<!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
<div
  class="joint"
  class:broken={row.broken}
  class:selected={row.selected && !row.broken}
  class:open={row.expanded}
  data-joint={row.id}
  onclick={click}
>
  <div class="names">
    <span class="name">{row.nameA}</span>
    <span class="arrow">↔</span>
    <span class="name">{row.nameB}</span>
    {#if row.expanded}
      <svg class="chevron" viewBox="0 0 8 5" aria-hidden="true"><polyline points="1,1 4,4 7,1" /></svg>
      <button
        use:chip
        class="delete"
        title={words.deleteTip}
        aria-label={words.deleteTip.split('\n')[0]}
        disabled={!enabled}
        onclick={(e) => {
          e.stopPropagation();
          if (e.detail < 2) onremove();
        }}
      >
        <svg viewBox="0 0 14 14" aria-hidden="true">
          <path d="M2.5 4h9M5.5 4V2.5h3V4M4 4l.6 7.5h4.8L10 4M6 6.5v3M8 6.5v3" />
        </svg>
      </button>
    {/if}
  </div>
  <div class="kind">{row.kindLine}</div>
  {#if row.caveat}
    <div class="caveat">
      <svg viewBox="0 0 12 12" aria-hidden="true">
        <path d="M6 1L11 11H1Z" />
        <path d="M6 4.8v3.2M6 9.9v.1" />
      </svg>
      <span>{row.caveat}</span>
    </div>
  {/if}
  {#if open}
    <div class="markout" bind:clientWidth={measured}>
      {#if strips}
        {#each row.pieces as piece, i (i)}
          {@const s = strips[i]}
          {@const below = s.staggered || s.edgeLine === 1}
          <div class="strip">
            <div class="who"><span class="piece-name">{piece.name}</span><span class="depth">{piece.depthText}</span></div>
            <div class="ruler" class:below>
              {#each s.labels as label, j (j)}
                <span class="tick-label" class:low={label.line === 1} style="left: {label.left}px">{label.text}</span>
              {/each}
              {#if s.edgeLine >= 0}
                <span class="edge-word" class:low={s.edgeLine === 1} style="left: 0px">{row.edge}</span>
              {/if}
              <div class="bar" style="left: {s.barLeft}px; width: {s.barRight - s.barLeft}px; height: {kBarHeight}px"></div>
              {#if s.band}
                <div class="band" style="left: {s.band[0]}px; width: {s.band[1] - s.band[0]}px; height: {kBarHeight}px"></div>
              {/if}
              <div
                class="zero"
                style="left: {s.barLeft - 0.7}px; height: {kBarHeight + 2 * kZeroOverhang}px; margin-top: -{kZeroOverhang}px"
              ></div>
              {#each s.ticks as x, j (j)}
                <div
                  class="tick"
                  style="left: {x - 0.8}px; height: {kBarHeight + 2 * kTickOverhang}px; margin-top: -{kTickOverhang}px"
                ></div>
              {/each}
            </div>
          </div>
        {/each}
      {:else if written}
        {#if !row.edgeNamed}
          <div class="edge-sentence">{row.edge}</div>
        {/if}
        {#each row.pieces as piece, i (i)}
          <div class="who"><span class="piece-name">{piece.name}</span><span class="depth">{piece.depthText}</span></div>
          <div class="written">{piece.writtenLine}</div>
        {/each}
      {/if}
      {#if shared && (strips || written)}
        <div class="shared">{shared}</div>
      {/if}
    </div>
  {/if}
</div>

<style>
  .joint {
    position: relative;
    box-sizing: border-box;
    padding: 6px 11px 7px 13px;
    border-top: 1px solid var(--border);
    cursor: pointer;
  }
  .joint.selected {
    background: color-mix(in srgb, var(--accent) 22%, transparent);
  }
  .joint.broken {
    background: color-mix(in srgb, var(--danger) 13%, transparent);
  }
  .joint.selected::before,
  .joint.broken::before {
    content: '';
    position: absolute;
    left: 0;
    top: 6px;
    bottom: 6px;
    width: 3px;
    background: color-mix(in srgb, var(--accent) 60%, white);
  }
  .joint.broken::before {
    background: var(--danger);
  }
  .names {
    display: flex;
    align-items: center;
    gap: 4px;
    min-width: 0;
    font-size: var(--font-body);
    color: var(--text);
  }
  .joint.broken .name {
    color: var(--danger-text);
  }
  .name {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    flex: 0 1 auto;
  }
  .arrow {
    flex: none;
    color: var(--text-muted);
  }
  .chevron {
    flex: none;
    width: 8px;
    height: 5px;
    margin-left: auto;
    fill: none;
    stroke: var(--text-muted);
    stroke-width: 1.4;
    stroke-linecap: round;
    stroke-linejoin: round;
  }
  .delete {
    flex: none;
    width: 20px;
    height: 20px;
    padding: 0;
    margin-left: 6px;
    border: none;
    border-radius: var(--r-tiny, 4px);
    background: transparent;
    cursor: pointer;
    display: grid;
    place-items: center;
  }
  .delete svg {
    width: 14px;
    height: 14px;
    fill: none;
    stroke: var(--text);
    stroke-width: 1.2;
    stroke-linecap: round;
    stroke-linejoin: round;
  }
  .delete:hover:not(:disabled) {
    background: var(--chip-hover);
  }
  .delete:disabled svg {
    stroke: var(--text-disabled);
  }
  .kind {
    font-size: var(--font-label);
    color: var(--text-muted);
    overflow-wrap: anywhere;
  }
  .joint.broken .kind {
    color: var(--text);
  }
  .caveat {
    display: flex;
    gap: 6px;
    margin-top: 4px;
    font-size: var(--font-badge);
    color: var(--text-muted);
  }
  .caveat svg {
    flex: none;
    width: 12px;
    height: 12px;
    margin-top: 1px;
    fill: none;
    stroke: var(--caution);
    stroke-width: 1.4;
    stroke-linecap: round;
    stroke-linejoin: round;
  }
  .markout {
    font-size: var(--font-label);
  }
  .strip {
    margin-top: 6px;
  }
  .who {
    display: flex;
    justify-content: space-between;
    gap: 8px;
    margin-top: 6px;
    margin-bottom: 1px;
  }
  .strip .who {
    margin-top: 0;
  }
  .piece-name {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-weight: 600;
    color: var(--text);
  }
  .depth {
    flex: none;
    color: var(--text-muted);
  }
  /* The ruler: a label line above the bar, the bar, and - only when a label
     or the edge word needs it - a label line below. Lines are a badge line
     tall; everything along them is placed by ruler.ts, in px. */
  .ruler {
    --lh: calc(var(--font-badge) * 1.3);
    position: relative;
    height: calc(var(--lh) + 3px + 8px + 3px);
  }
  .ruler.below {
    height: calc(var(--lh) + 3px + 8px + 4px + var(--lh));
  }
  .tick-label,
  .edge-word {
    position: absolute;
    top: 0;
    font-size: var(--font-badge);
    line-height: 1.3;
    white-space: nowrap;
    color: var(--text);
  }
  .edge-word {
    color: var(--text-muted);
  }
  .tick-label.low,
  .edge-word.low {
    top: calc(var(--lh) + 3px + 8px + 4px);
  }
  .bar,
  .band,
  .zero,
  .tick {
    position: absolute;
    top: calc(var(--lh) + 3px);
    box-sizing: border-box;
  }
  .bar {
    border: 1px solid var(--border);
    border-radius: 1.5px;
    background: var(--chip-hover);
  }
  .band {
    background: color-mix(in srgb, var(--accent) 55%, transparent);
  }
  .zero {
    width: 1.4px;
    background: var(--text-muted);
  }
  .tick {
    width: 1.6px;
    background: color-mix(in srgb, var(--accent) 60%, white);
  }
  .written {
    color: var(--text);
    overflow-wrap: anywhere;
  }
  .edge-sentence {
    margin-top: 5px;
    color: var(--text-muted);
  }
  .shared {
    margin-top: 5px;
    color: var(--text-muted);
  }
</style>

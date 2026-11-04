<script lang="ts">
  // THE RENDER SETTINGS PANEL as a page (migration Phase 3e) - a faithful
  // port of RenderSettingsPanel, docked beside the viewport by RenderStudio
  // in the Qt panel's place. Everything shown is read off the bridge; every
  // click is one of its slots, which reach the Qt panel's own edit path. A
  // chip that is already current sends nothing, as the Qt chips do.
  //
  // The sections scroll; the export chips and their note, the tier line, the
  // polish bar and the shutter stay pinned below them - a setting the pinned
  // action governs belongs in the pinned part of the card.
  import { onMount, untrack } from 'svelte';
  import { chip } from '../actions';
  import Dial from './Dial.svelte';
  import Tile from './Tile.svelte';
  import type { Choice, DialId, RenderApi, RenderLayout, RenderState } from './api';

  let { render }: { render: RenderApi } = $props();

  // The bridge's first values; its signals carry every later one.
  let layout = $state<RenderLayout>(untrack(() => render.layout));
  let now = $state<RenderState>(untrack(() => render.state));

  onMount(() => {
    render.stateChanged.connect(() => (now = render.state));
    render.layoutChanged.connect(() => (layout = render.layout));
    render.ready();
  });

  const dials: { id: DialId; label: () => string; section: 'light' | 'camera' }[] = [
    { id: 'lightAngle', label: () => layout.rows.angle, section: 'light' },
    { id: 'lightStrength', label: () => layout.rows.strength, section: 'light' },
    { id: 'fov', label: () => layout.rows.fov, section: 'camera' },
  ];
  const move = (id: DialId, value: number) => render.setDial(id, value);

  // The Qt panel's gutter: while - and only while - the sections scroll, a
  // 6 px gap opens between the rows and the bar, so a readout or the Studio
  // chip never stands flush against the track (the 2026-10 review's F18).
  let scroller = $state<HTMLElement>();
  let content = $state<HTMLElement>();
  let scrolls = $state(false);
  $effect(() => {
    if (!scroller || !content) return;
    const measure = () => (scrolls = scroller!.scrollHeight > scroller!.clientHeight + 1);
    const observer = new ResizeObserver(measure);
    observer.observe(scroller);
    observer.observe(content);
    measure();
    return () => observer.disconnect();
  });
  const pick = (current: number, index: number, send: (i: number) => void) => {
    if (index !== current) send(index);
  };
</script>

{#snippet chips(list: Choice[], current: number, send: (i: number) => void, grid: boolean, cls: string)}
  <div class="chips {cls}" class:grid role="radiogroup">
    {#each list as choice, i (i)}
      <button
        use:chip
        class="chip"
        class:current={i === current}
        role="radio"
        aria-checked={i === current}
        title={choice.tip}
        onclick={() => pick(current, i, send)}>{choice.label}</button
      >
    {/each}
  </div>
{/snippet}

{#snippet dial(id: DialId, label: string)}
  <Dial {id} {label} range={layout.dials[id]} dial={now.dials[id]} onmove={move} />
{/snippet}

<div class="panel">
  <div class="title">{layout.title}</div>

  <div class="scroll" class:scrolls bind:this={scroller}>
   <div class="content" bind:this={content}>
    <div class="heading">{layout.sections.frame}</div>
    {@render chips(layout.aspects, now.aspect, (i) => render.aspect(i), true, 'aspects')}
    {@render chips(layout.guides, now.guides, (i) => render.guides(i), false, 'guides')}

    <div class="rule"></div>
    <div class="heading">{layout.sections.shots}</div>
    <div class="row">
      <button use:chip class="chip save-shot" title={layout.saveShot.tip} onclick={() => render.saveShot()}
        >{layout.saveShot.label}</button
      >
    </div>
    {#if now.shots.length === 0}
      <div class="note no-shots">{layout.noShots}</div>
    {:else}
      <div class="shots">
        {#each now.shots as name, i (i + ':' + name)}
          <div class="shot">
            <button use:chip class="chip apply" title={layout.applyShotTip} onclick={() => render.applyShot(i)}
              >{name}</button
            >
            <button use:chip class="chip drop" title={layout.removeShotTip} onclick={() => render.removeShot(i)}
              >×</button
            >
          </div>
        {/each}
      </div>
    {/if}

    <div class="rule"></div>
    <div class="heading">{layout.sections.light}</div>
    {#each dials.filter((d) => d.section === 'light') as d (d.id)}
      {@render dial(d.id, d.label())}
    {/each}

    {#if layout.tiles.length > 0}
      <div class="rule"></div>
      <div class="heading">{layout.sections.material}</div>
      <div class="tiles">
        {#each layout.tiles as tile (tile.name)}
          <Tile
            {tile}
            current={now.wood && now.woodName === tile.name}
            onchoose={(name) => render.wood(name)}
            onedit={(name) => render.editMaterial(name)}
          />
        {/each}
      </div>
      <div class="note material-note">{layout.materialNote}</div>
    {/if}

    <div class="rule"></div>
    <div class="heading">{layout.sections.studio}</div>
    <div class="row">
      <span class="label">{layout.rows.background}</span>
      <button
        use:chip
        class="swatch"
        title={layout.backgroundTip}
        style:background={now.background}
        aria-label={layout.rows.background}
        onclick={() => render.pickBackground()}
      ></button>
      <button use:chip class="chip studio-reset" title={layout.studioReset.tip} onclick={() => render.resetBackground()}
        >{layout.studioReset.label}</button
      >
    </div>

    <div class="rule"></div>
    <div class="heading">{layout.sections.camera}</div>
    {#each dials.filter((d) => d.section === 'camera') as d (d.id)}
      {@render dial(d.id, d.label())}
    {/each}

    <div class="rule"></div>
    <div class="heading">{layout.sections.quality}</div>
    {@render chips(layout.qualities, now.quality, (i) => render.quality(i), false, 'qualities')}
    <div class="row">
      <button
        use:chip
        class="chip cutout"
        class:current={now.cutout}
        aria-pressed={now.cutout}
        title={layout.cutout.tip}
        onclick={() => render.cutout()}>{layout.cutout.label}</button
      >
    </div>
   </div>
  </div>

  <div class="pinned">
    <div class="heading">{layout.sections.export}</div>
    {@render chips(layout.exportSizes, now.exportSize, (i) => render.exportSize(i), true, 'exports')}
    {#if now.exportNote}<div class="note export-note">{now.exportNote}</div>{/if}
    <div class="note tier">{now.tierName}</div>
    {#if now.tierProgress >= 0}
      <div class="bar"><div class="fill" style:width="{Math.round(now.tierProgress * 100)}%"></div></div>
    {/if}
    <button
      use:chip
      class="shutter"
      disabled={!now.shutter.enabled}
      title={now.shutter.tip}
      onclick={() => now.shutter.enabled && render.shoot()}
    >
      <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">
        <path
          d="M2.5 5h2.2l1.1-1.6h4.4L11.3 5h2.2a1 1 0 0 1 1 1v6.5a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1z"
          fill="none"
          stroke="currentColor"
          stroke-width="1.3"
          stroke-linejoin="round"
        />
        <circle cx="8" cy="9" r="2.4" fill="none" stroke="currentColor" stroke-width="1.3" />
      </svg>
      <span>{now.shutter.label}</span>
    </button>
  </div>
</div>

<style>
  :global(html),
  :global(body) {
    height: 100%;
    overflow: hidden;
    background: var(--chrome);
  }
  :global(#app) {
    height: 100%;
  }
  .panel {
    box-sizing: border-box;
    height: 100%;
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding: 12px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-card, 10px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    color: var(--text);
  }
  .title {
    font-family: var(--title-family, inherit);
    font-size: var(--title-size, var(--font-title));
    font-weight: var(--title-weight, 600);
    letter-spacing: var(--title-tracking, normal);
    color: var(--text-muted);
  }
  .scroll {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
  }
  .content {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  .scroll.scrolls .content {
    padding-right: 6px;
  }
  /* The app's one scrollbar description (Theme::install()): a quiet
     chipActive handle on a transparent track, no arrows. */
  .scroll::-webkit-scrollbar {
    width: 8px;
  }
  .scroll::-webkit-scrollbar-track {
    background: transparent;
  }
  .scroll::-webkit-scrollbar-thumb {
    background: var(--chip-active);
    border-radius: var(--r-tiny, 4px);
  }
  .scroll::-webkit-scrollbar-button {
    display: none;
  }
  .pinned {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  .heading {
    margin-top: 2px;
    font-size: var(--font-badge);
    font-weight: 600;
    letter-spacing: 1px;
    color: var(--text-muted);
  }
  .rule {
    flex: none;
    height: 1px;
    margin-bottom: 2px;
    background: var(--border);
  }
  .row {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .label {
    flex: 1;
    font-size: var(--font-label);
  }
  .note {
    font-size: var(--font-badge);
    color: var(--text-muted);
  }
  /* The Qt panel paints the export note unstyled: the body text, at the
     body size - it is the line that says what the pinned button will write. */
  .note.export-note {
    font-size: var(--font-body);
    color: var(--text);
  }
  .chips {
    display: flex;
    flex-wrap: wrap;
    gap: 5px;
  }
  /* The Qt panel's 3 + 2 grid: five chips do not fit one row. */
  .chips.grid {
    display: grid;
    grid-template-columns: repeat(3, max-content);
  }
  .chips.qualities {
    gap: 7px;
  }
  .chip {
    height: 26px;
    padding: 0 11px;
    border: 1px solid var(--border);
    border-radius: var(--r-control, 7px);
    background: var(--chip);
    color: var(--text-muted);
    font: inherit;
    font-size: var(--font-label);
    white-space: nowrap;
    cursor: pointer;
  }
  .chip:hover:not(.current) {
    background: var(--chip-hover);
  }
  .chip.current {
    background: var(--chip-active);
    color: var(--text);
    border: 1.6px solid var(--accent);
  }
  .shots {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .shot {
    display: flex;
    gap: 4px;
  }
  .shot .apply {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .shot .drop {
    width: 26px;
    padding: 0;
    text-align: center;
  }
  .tiles {
    display: grid;
    grid-template-columns: repeat(3, 66px);
    gap: 7px;
  }
  .swatch {
    width: 46px;
    height: 18px;
    padding: 0;
    border: 1px solid var(--border);
    border-radius: var(--r-tiny, 4px);
    cursor: pointer;
  }
  .bar {
    height: 3px;
    background: var(--border);
  }
  .fill {
    height: 100%;
    background: var(--accent);
  }
  .shutter {
    height: 36px;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 8px;
    border: 1px solid var(--border);
    border-radius: var(--r-control, 9px);
    background: var(--accent);
    color: #fff;
    font: inherit;
    font-size: var(--font-label);
    cursor: pointer;
  }
  .shutter:hover:not(:disabled) {
    filter: brightness(1.12);
  }
  .shutter:active:not(:disabled) {
    filter: brightness(0.8);
  }
  .shutter:disabled {
    background: var(--chip);
    color: var(--text-disabled);
    cursor: default;
  }
</style>

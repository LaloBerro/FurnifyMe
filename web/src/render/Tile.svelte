<script lang="ts">
  // One wood tile (RenderSettingsPanel's MaterialTile, the image variant): the
  // thumbnail C++ decoded once, the name on a dark caption strip, and an
  // accent ring while it is the wood worn. A click chooses it; a double-click
  // opens its material card - whose first click has already chosen it, so the
  // second click of the pair (detail 2) sends nothing.
  import { chip } from '../actions';
  import type { Tile } from './api';

  let {
    tile,
    current,
    onchoose,
    onedit,
  }: { tile: Tile; current: boolean; onchoose: (name: string) => void; onedit: (name: string) => void } = $props();
</script>

<button
  use:chip
  class="tile"
  class:current
  title={tile.name}
  data-tile={tile.name}
  onclick={(event) => {
    if (event.detail >= 2 || current) return;
    onchoose(tile.name);
  }}
  ondblclick={() => onedit(tile.name)}
>
  {#if tile.thumb}<img src={tile.thumb} alt="" />{/if}
  <span class="caption">{tile.name}</span>
</button>

<style>
  .tile {
    position: relative;
    width: 66px;
    height: 40px;
    padding: 0;
    overflow: hidden;
    border: 1px solid var(--border);
    border-radius: var(--r-tiny, 6px);
    background: var(--panel);
    cursor: pointer;
  }
  .tile.current {
    border: 2px solid var(--accent);
  }
  img {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    object-fit: cover;
  }
  .caption {
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    height: 13px;
    line-height: 13px;
    background: rgba(0, 0, 0, 0.62);
    color: var(--text-muted);
    font-size: var(--font-badge);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .tile.current .caption {
    color: #fff;
  }
</style>

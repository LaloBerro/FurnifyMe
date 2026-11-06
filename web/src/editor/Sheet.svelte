<script lang="ts" module>
  // The shortcut sheet (migration Phase 7a): ShortcutSheet::words() with its
  // card's place in the viewport. Every row is generated in C++ from the
  // window's own actions and the key tables, so this page never writes a
  // binding down twice.
  export interface SheetRow {
    label: string;
    keys: string;
  }
  export interface SheetGroup {
    title: string;
    x: number;
    y: number;
    rows: SheetRow[];
  }
  export interface SheetState {
    shown: boolean;
    title?: string;
    x?: number;
    y?: number;
    width?: number;
    height?: number;
    padding?: number;
    titleHeight?: number;
    groupHeight?: number;
    rowHeight?: number;
    columnWidth?: number;
    groups?: SheetGroup[];
  }
</script>

<script lang="ts">
  // Drawn UNDER the Qt sheet, which keeps its place, Escape and the
  // click-outside rule and paints nothing - so this card only draws. Each
  // group stands where showSheet() placed it, column by column.
  let { sheet }: { sheet: SheetState } = $props();
  const pad = $derived(sheet.padding ?? 24);
</script>

<div
  class="sheet"
  data-hit
  role="dialog"
  aria-label={sheet.title}
  style:left="{sheet.x ?? 0}px"
  style:top="{sheet.y ?? 0}px"
  style:width="{sheet.width ?? 0}px"
  style:height="{sheet.height ?? 0}px"
>
  <div class="title" style:height="{sheet.titleHeight ?? 44}px" style:padding="0 {pad}px">{sheet.title}</div>
  {#each sheet.groups ?? [] as group (group.title)}
    <div
      class="group"
      style:left="{pad + group.x}px"
      style:top="{(sheet.titleHeight ?? 44) + group.y}px"
      style:width="{sheet.columnWidth ?? 0}px"
    >
      <div class="heading" style:height="{sheet.groupHeight ?? 30}px">{group.title}</div>
      {#each group.rows as row, i (i)}
        <div class="row" style:height="{sheet.rowHeight ?? 26}px">
          <span class="label">{row.label}</span><span class="keys">{row.keys}</span>
        </div>
      {/each}
    </div>
  {/each}
</div>

<style>
  .sheet {
    position: absolute;
    box-sizing: border-box;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-card, 10px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    overflow: hidden;
  }
  .title {
    display: flex;
    align-items: center;
    font-size: var(--font-title);
    color: var(--text);
  }
  .group {
    position: absolute;
  }
  .heading {
    display: flex;
    align-items: flex-end;
    font-size: var(--font-body);
    font-weight: 700;
    color: var(--accent-text);
  }
  .row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    font-size: var(--font-body);
    color: var(--text);
  }
  .label {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .keys {
    flex: none;
    font-size: var(--font-badge);
    color: var(--text-muted);
  }
</style>

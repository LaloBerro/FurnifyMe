<script lang="ts">
  // THE ITEMS DRAWER as a card on the editor's chrome page (migration Phase
  // 3h) - ItemsPanel, ported. ItemsModel decides everything (which rows show,
  // the marks, a refusal, every sentence); this card draws it and turns the
  // pointer and the keys into the model's slots:
  //   - a row selects on RELEASE (the graft from option W): a press that
  //     travels 6 px is a drag, and a drag must not change the selection;
  //   - the list holds its keys (ruling R3) while a row has been pressed - only
  //     the table's keys are claimed, every other shortcut still fires;
  //   - the filter and a rename are typed fields: they claim the keyboard
  //     outright while they are edited (ruling R5 of Phase 3b);
  //   - a drag is the Qt drawer's: zones with hysteresis, spring-loading,
  //     auto-scroll, a ghost over the viewport, Escape, a release outside the
  //     card cancelling - and C++'s dry run says whether the target refuses.
  // Where the card stands is the overlay's (an HtmlCardSlot in the Qt drawer's
  // TopLeft place); how tall it is, is ours, reported and capped at the room
  // under it.
  import { onDestroy, onMount, tick, untrack } from 'svelte';
  import { chip } from '../../actions';
  import type { CardRect, Keyboard } from '../settings/api';
  import type { DropKind, ItemRowData, ItemsApi, ItemsDrag, ItemsMenu, ItemsState, KeyEntry, ListKeys } from './api';
  import { autoScrollStep, kAutoScrollTickMs, travelled, zoneAt, type Zone } from './drop';
  import Glyph from './Glyph.svelte';
  import ItemRow from './ItemRow.svelte';
  import RowMenu from './RowMenu.svelte';

  let {
    items,
    keyboard,
    listKeys,
    rect,
    bottom,
    report,
  }: {
    items: ItemsApi;
    keyboard: Keyboard;
    listKeys: ListKeys;
    rect: CardRect;
    // The lowest the card may reach (the viewport less the strip).
    bottom: number;
    report: (name: string, height: number) => void;
  } = $props();

  const kMinHeight = 176;
  const kBottomClearance = 16;
  const kIntoFolder = -1;   // DocumentModel::kIntoFolder

  // One ItemsModel for the card's whole life - read once, on purpose.
  const api: ItemsApi = untrack(() => items);
  const words = api.words;
  const keys: KeyEntry[] = api.keys ?? [];

  let rows: ItemRowData[] = $state([...api.rows]);
  let st: ItemsState = $state({ ...api.state });
  let menu: ItemsMenu = $state({ ...api.menu });
  let drag: ItemsDrag = $state({ ...api.drag });
  let alive = true;

  let body: HTMLElement | undefined = $state();
  let list: HTMLElement | undefined = $state();
  let header: HTMLElement | undefined = $state();
  let card: HTMLElement | undefined = $state();
  let filterField: HTMLInputElement | undefined = $state();
  let filterText = $state(api.state.filter ?? '');
  let menuAt = $state({ x: 0, y: 0 });

  // --- holding the keys ---------------------------------------------------------
  let holding = false;
  function hold() {
    list?.focus({ preventScroll: true });
    // Every press asks again: C++ ends the hold when focus leaves the page.
    holding = true;
    listKeys.hold();
  }
  function letGo() {
    if (!holding) return;
    holding = false;
    listKeys.release();
  }
  // The typed fields and the drag each claim the keyboard in the card's name,
  // once each, and give it back once.
  const claims = new Set<string>();
  function claim(what: string) {
    if (claims.has(what)) return;
    claims.add(what);
    keyboard.claimKeyboard();
  }
  function unclaim(what: string) {
    if (!claims.delete(what)) return;
    keyboard.releaseKeyboard();
  }

  // --- the model's changes ------------------------------------------------------
  let lastScroll = api.state.scroll?.seq ?? 0;
  let lastRename = api.state.rename?.seq ?? 0;
  let renaming = $state(0);
  let renameSeed = $state('');

  api.changed.connect(() => {
    if (!alive) return;
    rows = [...api.rows];
    st = { ...api.state };
    menu = { ...api.menu };
    drag = { ...api.drag };
    if (document.activeElement !== filterField && st.filter !== filterText.trim()) filterText = st.filter;
    if (st.rename.seq !== lastRename) {
      lastRename = st.rename.seq;
      if (st.rename.id) {
        renaming = st.rename.id;
        renameSeed = st.rename.name;
      }
    }
    if (renaming && st.rename.id !== renaming) renaming = 0;
    if (st.scroll.seq !== lastScroll) {
      lastScroll = st.scroll.seq;
      const id = st.scroll.id;
      tick().then(() => rowElement(id)?.scrollIntoView?.({ block: 'nearest' }));
    }
    // C++ ended the drag (the drawer went away, a furniture closed): the hand
    // is let go of here too.
    if (dragging && !drag.active) stopDrag();
    if (dragging || drag.active) tick().then(() => (lastPoint ? aim(lastPoint.x, lastPoint.y) : undefined));
  });

  const rowElement = (id: number) =>
    list?.querySelector<HTMLElement>(`[data-row="${id}"]`) ?? null;
  const rowById = (id: number) => rows.find((r) => r.id === id);

  // --- the keys -----------------------------------------------------------------
  function commandFor(event: KeyboardEvent, fromFilter: boolean): string {
    for (const entry of keys) {
      if (entry.fromFilter !== fromFilter) continue;
      for (const k of entry.keys)
        if (k.key === event.key && k.shift === event.shiftKey && k.alt === event.altKey && k.ctrl === event.ctrlKey)
          return entry.command;
    }
    return '';
  }
  const forwardKeys = new Set(['ArrowDown', 'End', 'ArrowRight']);

  function onListKey(event: KeyboardEvent) {
    if (dragging) {
      if (event.key === 'Escape') {
        event.preventDefault();
        cancelDrag();
      }
      return;
    }
    const command = commandFor(event, false);
    if (!command) return;
    event.preventDefault();
    api.key(command, forwardKeys.has(event.key));
  }

  // --- the filter ---------------------------------------------------------------
  function onFilterInput() {
    api.setFilter(filterText);
  }
  function onFilterKey(event: KeyboardEvent) {
    event.stopPropagation();
    if (event.key === 'Escape') {
      event.preventDefault();
      if (filterText) {
        filterText = '';
        api.clearFilter();
      } else {
        // An empty field hands the keys to the list rather than swallowing the
        // Escape that cancels a sketch (ItemsFilterField).
        filterField?.blur();
      }
      return;
    }
    const command = commandFor(event, true);
    if (command) {
      event.preventDefault();
      api.key(command, true);
      filterField?.blur();
      hold();
    }
  }

  // --- presses on rows ----------------------------------------------------------
  type Press = { id: number; x: number; y: number; ctrl: boolean; shift: boolean };
  let press: Press | null = null;
  let dragging = $state(false);

  function rowOf(event: Event): ItemRowData | undefined {
    const el = (event.target as HTMLElement | null)?.closest<HTMLElement>('[data-row]');
    return el ? rowById(Number(el.dataset.row)) : undefined;
  }

  function onListMouseDown(event: MouseEvent) {
    const row = rowOf(event);
    hold();
    if (!row || event.button !== 0 || renaming) return;
    event.preventDefault();
    // A drag whose release never arrived must not turn this click into a move.
    if (dragging) cancelDrag();
    press = { id: row.id, x: event.clientX, y: event.clientY, ctrl: event.ctrlKey, shift: event.shiftKey };
    window.addEventListener('mousemove', onWindowMove);
    window.addEventListener('mouseup', onWindowUp);
  }

  function onWindowMove(event: MouseEvent) {
    if (!press) return;
    if (!dragging) {
      if (!(event.buttons & 1)) return;
      if (!travelled(event.clientX - press.x, event.clientY - press.y, words.dragThreshold ?? 6)) return;
      startDrag(press);
    }
    aim(event.clientX, event.clientY);
  }

  function onWindowUp(event: MouseEvent) {
    if (event.button !== 0) return;
    detach();
    if (dragging) {
      aim(event.clientX, event.clientY);
      finishDrag(true);
    } else if (press) {
      api.activate(press.id, press.ctrl, press.shift);
    }
    press = null;
  }

  function detach() {
    window.removeEventListener('mousemove', onWindowMove);
    window.removeEventListener('mouseup', onWindowUp);
  }

  function onDoubleClick(event: MouseEvent) {
    const row = rowOf(event);
    if (row) api.beginRename(row.id);
  }

  function onContextMenu(event: MouseEvent) {
    const row = rowOf(event);
    event.preventDefault();
    if (!row || dragging) return;
    hold();
    menuAt = { x: event.clientX, y: event.clientY };
    api.openMenu(row.id);
  }

  // --- the drag -----------------------------------------------------------------
  type Aim = { kind: DropKind; parent: number; before: number; row: number; zone: Zone | null; depth: number };
  let target: Aim = $state({ kind: 'none', parent: 0, before: 0, row: 0, zone: null, depth: 0 });
  let ghostAt = $state({ x: 0, y: 0 });
  let lastPoint: { x: number; y: number } | null = null;
  let springTimer: ReturnType<typeof setTimeout> | null = null;
  let springFolder = 0;
  let scrollTimer: ReturnType<typeof setInterval> | null = null;
  let scrollStep = 0;

  function startDrag(p: Press) {
    dragging = true;
    // Escape is this gesture's for as long as it lasts.
    claim('drag');
    window.addEventListener('keydown', onDragKey, true);
    window.addEventListener('blur', onWindowBlur);
    api.beginDrag(p.id, !p.ctrl && !p.shift);
  }

  function onDragKey(event: KeyboardEvent) {
    if (event.key !== 'Escape') return;
    event.preventDefault();
    event.stopPropagation();
    cancelDrag();
  }

  // Alt+Tab, another app: the release will never come to this drag.
  function onWindowBlur() {
    if (dragging) cancelDrag();
  }

  function send(next: Aim) {
    const same = next.kind === target.kind && next.parent === target.parent && next.before === target.before &&
      next.row === target.row && next.zone === target.zone;
    target = next;
    if (!same) api.dragOver(next.kind, next.parent, next.before);
  }

  function aim(x: number, y: number) {
    if (!dragging || !card || !list) return;
    lastPoint = { x, y };
    ghostAt = { x, y };
    const c = card.getBoundingClientRect();
    const none: Aim = { kind: 'none', parent: 0, before: 0, row: 0, zone: null, depth: 0 };
    if (x < c.left || x >= c.right || y < c.top || y >= c.bottom) {
      setScroll(0);
      stopSpring();
      send({ ...none, kind: 'outside' });
      return;
    }
    const l = list.getBoundingClientRect();
    setScroll(autoScrollStep(y, l.top, l.bottom));
    const h = header?.getBoundingClientRect();
    if (h && y >= c.top && y < h.bottom + 4) {
      stopSpring();
      // The title row is the end of the top level (C++ refuses it while
      // filtered - only into-folder drops are offered then).
      send({ ...none, kind: 'top' });
      return;
    }
    // Only what the list SHOWS (ruling P10).
    if (y < l.top || y >= l.bottom) {
      stopSpring();
      send(none);
      return;
    }
    const gap = 2;
    for (const row of rows) {
      const el = rowElement(row.id);
      if (!el) continue;
      const r = el.getBoundingClientRect();
      if (y < r.top - gap || y >= r.bottom) continue;
      const folder = row.kind === 'folder';
      if (st.filter) {
        if (!folder) {
          stopSpring();
          send(none);
          return;
        }
        stopSpring();
        send({ kind: 'into', parent: row.id, before: kIntoFolder, row: row.id, zone: 'into', depth: row.depth });
        return;
      }
      const zone = zoneAt(folder, y - r.top, r.height, target.row === row.id ? target.zone : null);
      if (zone === 'into') {
        send({ kind: 'into', parent: row.id, before: kIntoFolder, row: row.id, zone, depth: row.depth });
        // A folded folder the hand rests on opens after 700 ms (and stays
        // open); a hand wobbling inside the band keeps its running timer.
        if (!row.open) startSpring(row.id);
        else stopSpring();
        return;
      }
      stopSpring();
      const t = zone === 'above' ? row.above : row.below;
      send({ kind: 'line', parent: t.parent, before: t.before, row: row.id, zone, depth: t.depth });
      return;
    }
    stopSpring();
    send(none);
  }

  function startSpring(folder: number) {
    if (springFolder === folder && springTimer) return;
    stopSpring();
    springFolder = folder;
    springTimer = setTimeout(() => {
      springTimer = null;
      const id = springFolder;
      springFolder = 0;
      if (dragging && !refused) api.openFolder(id);
    }, words.springMs ?? 700);
  }
  function stopSpring() {
    if (springTimer) clearTimeout(springTimer);
    springTimer = null;
    springFolder = 0;
  }

  function setScroll(step: number) {
    scrollStep = step;
    if (step === 0) {
      if (scrollTimer) clearInterval(scrollTimer);
      scrollTimer = null;
      return;
    }
    if (scrollTimer) return;
    scrollTimer = setInterval(() => {
      if (!list || scrollStep === 0) return;
      const before = list.scrollTop;
      list.scrollTop = before + scrollStep;
      // At either end there is nothing left to scroll.
      if (list.scrollTop === before) {
        setScroll(0);
        return;
      }
      if (lastPoint) aim(lastPoint.x, lastPoint.y);
    }, kAutoScrollTickMs);
  }

  function stopDrag() {
    dragging = false;
    stopSpring();
    setScroll(0);
    window.removeEventListener('keydown', onDragKey, true);
    window.removeEventListener('blur', onWindowBlur);
    detach();
    unclaim('drag');
    press = null;
    lastPoint = null;
    target = { kind: 'none', parent: 0, before: 0, row: 0, zone: null, depth: 0 };
  }

  function finishDrag(commit: boolean) {
    if (!dragging) return;
    stopDrag();
    api.endDrag(commit);
  }
  function cancelDrag() {
    finishDrag(false);
  }

  // Whether the target the hand is over refuses: C++'s answer, read only when
  // it is about THIS target.
  const refused = $derived(
    drag.refused &&
      drag.target.kind === target.kind &&
      drag.target.parent === target.parent &&
      drag.target.before === target.before,
  );

  // The one cue (exactly one): a line, a ring, or the lit title.
  const indicator = $derived.by(() => {
    if (!dragging || !list || (target.kind !== 'line' && target.kind !== 'into')) return null;
    const el = rowElement(target.row);
    if (!el) return null;
    const top = el.offsetTop;
    if (target.kind === 'into') return { ring: true, top, height: el.offsetHeight, left: 0 };
    const y = target.zone === 'below' ? top + el.offsetHeight + 1 : top - 1;
    return { ring: false, top: y, height: 2, left: 6 + target.depth * 12 };
  });

  const ghostLabel = $derived(
    target.kind === 'outside'
      ? words.releaseToCancel
      : drag.ghost.more > 0
        ? (words.ghostMore ?? '%1 +%2').replace('%1', drag.ghost.name).replace('%2', String(drag.ghost.more))
        : drag.ghost.name,
  );

  // --- the card's height --------------------------------------------------------
  onMount(() => {
    const measure = () => {
      if (!body || !list) return;
      const border = card ? parseFloat(getComputedStyle(card).borderTopWidth) * 2 || 0 : 0;
      // The card's natural height: everything but the list, plus the whole
      // list - never asked of the rectangle the card is drawn at.
      const natural = body.offsetHeight - list.clientHeight + list.scrollHeight + border;
      const cap = Math.max(kMinHeight, bottom - rect.y - kBottomClearance);
      const h = Math.ceil(Math.min(Math.max(natural, kMinHeight), cap));
      if (h <= 0) return;
      (window as unknown as { __itemsHeight?: number }).__itemsHeight = h;
      report('items', h);
    };
    const observer = new ResizeObserver(measure);
    if (body) observer.observe(body);
    if (list) {
      observer.observe(list);
      for (const child of list.children) observer.observe(child);
    }
    const mo = new MutationObserver(() => {
      if (list) for (const child of list.children) observer.observe(child);
      measure();
    });
    if (list) mo.observe(list, { childList: true });
    measure();
    return () => {
      observer.disconnect();
      mo.disconnect();
    };
  });

  onDestroy(() => {
    alive = false;
    if (dragging) finishDrag(false);
    detach();
    for (const what of [...claims]) unclaim(what);
    letGo();
  });
</script>

<section
  class="items-card themed-rim"
  data-hit
  aria-label={words.title}
  bind:this={card}
  style="left: {rect.x}px; top: {rect.y}px; width: {rect.w}px; height: {rect.h}px;"
>
  <div class="body" bind:this={body}>
    <header bind:this={header} class:lit={dragging && target.kind === 'top'} class:refused={dragging && target.kind === 'top' && refused}>
      <span class="title">{words.title}</span>
      <span class="ledger ledger-count">{st.ledger.bodies}</span>
      <button
        use:chip
        class="new-folder"
        title={st.newFolder.tip}
        aria-label={st.newFolder.tip}
        disabled={!st.newFolder.enabled}
        onclick={() => api.newFolder()}
      >
        <span class="graphite-glyph"><Glyph name="folder" /></span>
        <span class="themed-glyph"><Glyph name="plus" /></span>
      </button>
    </header>
    <div class="ledger ledger-rule"></div>
    <input
      bind:this={filterField}
      bind:value={filterText}
      class="filter"
      placeholder={words.filter}
      aria-label={words.filter}
      spellcheck="false"
      autocomplete="off"
      onmousedown={() => claim('filter')}
      onfocus={() => claim('filter')}
      onblur={() => unclaim('filter')}
      oninput={onFilterInput}
      onkeydown={onFilterKey}
    />
    <div class="ledger ledger-heads"><span>{words.headName}</span><span>{words.headTools}</span></div>
    <div
      class="list"
      role="tree"
      tabindex="-1"
      aria-label={words.title}
      aria-activedescendant={st.current && rowById(st.current) ? `item-row-${st.current}` : undefined}
      bind:this={list}
      onmousedown={onListMouseDown}
      ondblclick={onDoubleClick}
      oncontextmenu={onContextMenu}
      onkeydown={onListKey}
    >
      {#each rows as row (row.kind + row.id)}
        <ItemRow
          {row}
          renaming={renaming === row.id}
          renameSeed={renaming === row.id ? renameSeed : ''}
          oneye={() => api.toggleEye(row.id)}
          ongrain={() => api.toggleGrain(row.id)}
          ontwisty={() => api.toggleFold(row.id)}
          oncommit={(text) => {
            renaming = 0;
            api.commitRename(row.id, text);
            hold();
          }}
          oncancel={() => {
            renaming = 0;
            api.cancelRename();
            hold();
          }}
          onfieldclaim={() => claim('rename')}
          onfieldrelease={() => unclaim('rename')}
        />
      {/each}
      {#if st.empty}
        <p class="empty">{words.empty}</p>
      {/if}
      {#if indicator}
        <div
          class="indicator"
          class:ring={indicator.ring}
          class:refused
          style="top: {indicator.top}px; height: {indicator.height}px; left: {indicator.left}px;"
        ></div>
      {/if}
    </div>
    {#if st.noMatch}
      <p class="no-match">{st.noMatch}</p>
    {/if}
    {#if st.foot}
      <div class="foot">
        <span>{st.foot}</span>
        <button use:chip class="clear" onclick={() => api.clearFilter()}>{words.clearFilter}</button>
      </div>
    {/if}
    <div class="ledger ledger-foot"><span>{st.ledger.selected}</span><span>{words.ledgerKeys}</span></div>
  </div>
</section>

{#if menu.open && menu.entries}
  <RowMenu
    entries={menu.entries}
    x={menuAt.x}
    y={menuAt.y}
    bounds={{ w: window.innerWidth, h: bottom }}
    {keyboard}
    onchoose={(id) => api.chooseMenu(id)}
    onclose={() => api.closeMenu()}
  />
{/if}

{#if dragging && drag.active}
  <div
    class="ghost"
    class:refused
    class:cancel={target.kind === 'outside'}
    style="left: {ghostAt.x + 14}px; top: {ghostAt.y + 10}px;"
  >
    <Glyph
      name={refused ? 'warning' : drag.ghost.kind === 'folder' ? 'folder' : drag.ghost.kind === 'outline' ? 'outline' : 'body'}
    />
    <span>{ghostLabel}</span>
  </div>
{/if}

<style>
  .items-card {
    position: fixed;
    box-sizing: border-box;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-card, 10px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    color: var(--text);
    font-size: var(--font-body);
    overflow: hidden;
  }
  .body {
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    gap: 8px;
    height: 100%;
    padding: 12px;
  }
  header {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 0 6px;
  }
  .title {
    flex: 1;
    color: var(--list-title-ink, var(--text-muted));
    font-family: var(--list-title-family, var(--title-family, inherit));
    font-size: var(--list-title-size, var(--font-title));
    font-weight: var(--list-title-weight, 600);
    letter-spacing: var(--list-title-tracking, normal);
  }
  header.lit .title {
    color: var(--accent-text);
  }
  header.refused .title {
    color: var(--danger-text);
  }
  .new-folder {
    display: grid;
    place-items: center;
    width: 24px;
    height: 24px;
    padding: 0;
    border: var(--list-chip-border, 1px solid var(--border));
    border-radius: var(--r-control, 6px);
    background: var(--chip);
    color: var(--text);
    cursor: pointer;
  }
  .new-folder:hover:not(:disabled) {
    background: var(--chip-hover);
  }
  .new-folder:disabled {
    color: var(--text-disabled);
    cursor: default;
  }
  /* Every mockup's header is a "+"; Graphite keeps the folder. */
  .themed-glyph {
    display: var(--list-plus-display, none);
  }
  .graphite-glyph {
    display: var(--list-folder-display, block);
  }
  .filter {
    box-sizing: border-box;
    height: 28px;
    padding: 0 8px;
    border: 1px solid var(--list-field-edge, var(--border));
    border-radius: var(--list-field-radius, var(--r-control, 6px));
    background: var(--list-field-fill, var(--chip));
    color: var(--text);
    font: inherit;
    outline: none;
  }
  .filter:focus {
    border-color: var(--list-field-edit-edge, var(--accent));
  }
  .filter::placeholder {
    color: var(--text-muted);
  }
  .list {
    position: relative;
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
    overflow-y: auto;
    outline: none;
    /* The rows scroll with NO scrollbar drawn (improvements item 12). */
    scrollbar-width: none;
  }
  .list::-webkit-scrollbar {
    display: none;
  }
  .empty,
  .no-match {
    margin: 0;
    color: var(--text-muted);
    white-space: pre-line;
  }
  .foot {
    display: flex;
    align-items: flex-end;
    gap: 4px;
    padding: 0 6px;
    color: var(--text-muted);
    font-size: var(--font-label);
  }
  .foot span {
    flex: 1;
  }
  .clear {
    padding: 0;
    border: none;
    background: transparent;
    color: var(--accent-text);
    font: inherit;
    cursor: pointer;
  }
  .indicator {
    position: absolute;
    right: 6px;
    background: var(--accent-text);
    pointer-events: none;
  }
  .indicator.ring {
    left: 0 !important;
    right: 0;
    box-sizing: border-box;
    border: 2px solid var(--accent-text);
    border-radius: 4px;
    background: transparent;
  }
  .indicator.refused {
    background: var(--danger-text);
  }
  .indicator.ring.refused {
    border-color: var(--danger-text);
    background: transparent;
  }
  /* A theme's ledger (Ultraviolet): shown only where its list asks for it. */
  .ledger {
    display: none;
  }
  .ledger-count {
    display: var(--list-ledger-inline, none);
  }
  .ledger-rule {
    display: var(--list-ledger-block, none);
    height: 1px;
    background: var(--list-rule, var(--border));
  }
  .ledger-heads,
  .ledger-foot {
    display: var(--list-ledger-flex, none);
    justify-content: space-between;
    padding: 0 10px 4px 14px;
  }
  .ledger-heads {
    border-bottom: 1px solid var(--list-rule-hi, var(--border));
  }
  .ledger-foot {
    padding-top: 8px;
    border-top: 1px solid var(--list-rule, var(--border));
  }
  .ledger-count,
  .ledger-heads span,
  .ledger-foot span {
    color: var(--text-muted);
    font-family: var(--list-mono, monospace);
    font-size: 11px;
    font-weight: 500;
    text-transform: uppercase;
  }
  .ghost {
    position: fixed;
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 5px 8px;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-small, 6px);
    background: var(--card-fill, var(--panel));
    color: var(--text);
    font-size: var(--font-body);
    pointer-events: none;
    white-space: nowrap;
  }
  .ghost.refused {
    color: var(--danger-text);
  }
  .ghost.cancel {
    color: var(--text-muted);
  }
</style>

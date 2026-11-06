<script lang="ts">
  // A GESTURE CHIP (migration Phase 4a): any of them, drawn from its face -
  // rows of parts read off the Qt chip's real controls (ruling R1). The Qt
  // chip stays the chip: it decides where this stands (`rect`), what a value
  // means, and when the gesture ends; this draws, mirrors each edit into the
  // Qt field, and answers Enter WITH what its fields hold (R3), because a
  // mirrored edit can lag one message behind the key that follows it.
  //
  // The card measures its content - never the rectangle it is drawn at - and
  // reports it; the Qt chip takes that size and places itself. Until the
  // rectangle carries it the card is not drawn at all (R4), so a chip never
  // jumps from the Qt chip's size to its own.
  import { onDestroy, onMount, tick, untrack } from 'svelte';
  import { chip as noFocus } from '../../actions';
  import type { CardRect } from '../settings/api';
  import type { ButtonPart, ChipsApi, Face, FieldPart } from './api';

  let {
    name,
    face,
    rect,
    chips,
    keyboard,
  }: {
    name: string;
    face: Face;
    rect: CardRect;
    chips: ChipsApi;
    keyboard: { claimKeyboard(): void; releaseKeyboard(): void };
  } = $props();

  // Whole device pixels round the Qt chip up by up to three logical pixels.
  const kRounding = 4;

  let content: HTMLElement | undefined = $state();
  let measured = $state({ w: 0, h: 0 });
  const settled = $derived(
    measured.w > 0 &&
      rect.w >= measured.w &&
      rect.w - measured.w <= kRounding &&
      rect.h >= measured.h &&
      rect.h - measured.h <= kRounding,
  );
  // On screen: at its own size, and not a glued chip mid camera move (Phase
  // 4b) - one that trails the model a frame and moves the mask every frame.
  const drawn = $derived(settled && !rect.moving);

  // What each field holds on the page, by id.
  let values: Record<string, string> = $state({});
  let focused = '';
  let holding = false;
  const inputs: Record<string, HTMLInputElement | undefined> = $state({});

  const fields = $derived(face.rows.flat().filter((p): p is FieldPart => p.kind === 'field'));

  // Seeded on a new gesture - even under the user's hands - and otherwise
  // following C++ for every field but the one being typed in (R5).
  let seededWith = -1;
  $effect(() => {
    const seed = face.seed;
    const list = fields;
    if (seed !== seededWith) {
      seededWith = seed;
      values = Object.fromEntries(list.map((f) => [f.id, f.value]));
      pendingFocus = face.focus;
      return;
    }
    const next = untrack(() => ({ ...values }));
    for (const f of list) if (f.id !== focused) next[f.id] = f.value;
    values = next;
  });

  // The field a new gesture types in, focused once the card is DRAWN - an
  // input under visibility: hidden cannot take focus (R4 holds the card
  // hidden until its rectangle carries its size).
  let pendingFocus = $state('');
  $effect(() => {
    if (!drawn || !pendingFocus) return;
    const target = pendingFocus;
    pendingFocus = '';
    tick().then(() => {
      const input = inputs[target];
      if (!input) return;
      // Claimed outright: a focus() in a page whose window is not active
      // fires no focus event (the name question's finding).
      claim();
      input.focus();
      input.select();
    });
  });

  function claim() {
    if (holding) return;
    holding = true;
    keyboard.claimKeyboard();
  }
  function release() {
    if (!holding) return;
    holding = false;
    keyboard.releaseKeyboard();
  }
  onDestroy(release);

  function onInput(id: string, event: Event) {
    const text = (event.currentTarget as HTMLInputElement).value;
    values = { ...values, [id]: text };
    chips.edit(name, id, text);
  }

  function onKey(event: KeyboardEvent) {
    if (event.isComposing) return;
    if (event.key === 'Enter') {
      event.preventDefault();
      event.stopPropagation();
      chips.commit(name, { ...values });
    } else if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      chips.cancel(name);
    }
  }

  function press(part: ButtonPart) {
    if (!part.enabled) return;
    chips.press(name, part.id);
  }

  // Reported when the measurement changes - and again whenever the rectangle
  // C++ hands back stops carrying it (a Qt chip that drew itself for a while
  // comes back at its own size, with the page's measurement unchanged).
  let lastSize = '';
  let lastRect = '';
  $effect(() => {
    const { w, h } = measured;
    if (w <= 0 || h <= 0) return;
    const size = `${w}x${h}`;
    const at = `${rect.w}x${rect.h}`;
    const stale = !settled && at !== lastRect;
    if (size === lastSize && !stale) return;
    lastSize = size;
    lastRect = at;
    chips.reportSize(name, w, h);
  });

  onMount(() => {
    const measure = () => {
      if (!content) return;
      // Layout offsets, never a transformed box.
      const w = Math.ceil(content.offsetWidth);
      const h = Math.ceil(content.offsetHeight);
      if (w <= 0 || h <= 0) return;
      if (w !== measured.w || h !== measured.h) measured = { w, h };
    };
    const observer = new ResizeObserver(measure);
    if (content) observer.observe(content);
    measure();
    return () => observer.disconnect();
  });
</script>

<div
  class="chip-card"
  class:invalid={face.invalid}
  data-gesture={name}
  data-hit={drawn ? '' : undefined}
  style:left="{rect.x}px"
  style:top="{rect.y}px"
  style:width="{rect.w}px"
  style:height="{rect.h}px"
  style:visibility={drawn ? undefined : 'hidden'}
>
  <div class="chip-content" bind:this={content}>
    {#each face.rows as row, r (r)}
      <div class="row">
        {#each row as part, p (p)}
          {#if part.kind === 'text'}
            <span class="text {part.tone}" data-user={part.user ? '' : undefined}>{part.text}</span>
          {:else if part.kind === 'rule'}
            <hr class="rule" />
          {:else if part.kind === 'menu'}
            <div class="menu-list" role="menu">
              {#if part.note}<p class="note">{part.note}</p>{/if}
              {#each part.groups as group (group.title)}
                <div class="group-title">{group.title}</div>
                {#each group.entries as entry (entry.id)}
                  <button
                    type="button"
                    use:noFocus
                    class="entry"
                    role="menuitemradio"
                    data-part={entry.id}
                    disabled={!entry.enabled}
                    aria-checked={entry.checked ? 'true' : 'false'}
                    onclick={() => press(entry)}
                  >
                    <span>{entry.text}</span>{#if entry.checked}<span class="tick">✓</span>{/if}
                  </button>
                  {#if entry.reason}<p class="reason">{entry.reason}</p>{/if}
                {/each}
              {/each}
            </div>
          {:else if part.kind === 'field'}
            <label class="field" class:stacked={part.stacked}>
              {#if part.label}<span class="label">{part.label}</span>{/if}
              <input
                type="text"
                data-field={part.id}
                title={part.tip}
                aria-label={part.name || part.label}
                spellcheck="false"
                autocomplete="off"
                disabled={!part.enabled}
                class:invalid={face.invalid || part.invalid}
                value={values[part.id] ?? part.value}
                bind:this={inputs[part.id]}
                oninput={(e) => onInput(part.id, e)}
                onkeydown={onKey}
                onmousedown={claim}
                onfocus={() => {
                  focused = part.id;
                  claim();
                }}
                onblur={() => {
                  if (focused === part.id) focused = '';
                  release();
                }}
              />
            </label>
          {:else if part.look === 'segment'}
            <div class="cell">
              {#if part.label}<span class="label">{part.label}</span>{/if}
              <button
                type="button"
                use:noFocus
                class="segment"
                class:on={part.checked}
                data-part={part.id}
                data-user={part.user ? '' : undefined}
                title={part.tip}
                disabled={!part.enabled}
                aria-pressed={part.checked ? 'true' : 'false'}
                onclick={() => press(part)}>{part.text}</button
              >
            </div>
          {:else}
            <button
              type="button"
              use:noFocus
              class="{part.kind} {part.look ?? ''}"
              data-part={part.id}
              title={part.tip}
              disabled={!part.enabled}
              aria-pressed={part.kind === 'toggle' ? (part.checked ? 'true' : 'false') : undefined}
              onclick={() => press(part)}
            >
              {#if part.kind === 'toggle'}<span class="box" class:on={part.checked}></span>{/if}
              {part.text}
            </button>
          {/if}
        {/each}
      </div>
    {/each}
  </div>
</div>

<style>
  .chip-card {
    position: fixed;
    box-sizing: border-box;
    border: 1px solid var(--card-edge, var(--border));
    border-radius: var(--r-card, 8px);
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
    color: var(--text);
    font-size: var(--font-label);
    overflow: hidden;
  }
  .chip-card.invalid {
    border-color: var(--danger);
  }
  /* Its own size, whatever rectangle it is drawn at: that is what it reports. */
  .chip-content {
    position: absolute;
    left: 0;
    top: 0;
    box-sizing: border-box;
    width: max-content;
    display: flex;
    flex-direction: column;
    gap: 6px;
    padding: 10px 14px;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 14px;
    min-height: 22px;
  }
  .text {
    white-space: nowrap;
  }
  .text.title {
    font-size: var(--font-body);
    font-weight: 600;
  }
  .text.hint {
    margin-left: auto;
    color: var(--text-muted);
    font-size: var(--font-badge);
  }
  .text.muted {
    color: var(--text-muted);
    font-size: var(--font-badge);
  }
  /* A value beside its label (Fillet/Chamfer's "R 20 mm"): at the row's far
     end, in the accent's text ink, as the Qt chip paints it. */
  .text.value {
    margin-left: auto;
    color: var(--accent-text);
  }
  .text.danger {
    color: var(--danger-text);
    font-size: var(--font-badge);
  }
  .field {
    display: flex;
    align-items: center;
    gap: 4px;
  }
  /* A field alone in its row is the card's one number (Extrude, Pull): it
     takes the row's width, as the Qt chips' fields span their card. */
  .field:only-child {
    flex: 1;
  }
  .field:only-child input {
    width: 100%;
    min-width: 180px;
  }
  .text.hint:only-child {
    margin-left: 0;
  }
  /* The joint card (Phase 4c): a label over its field or segment, the two
     sharing a row's width; a divider; a link; and its kind menu. */
  .field.stacked,
  .cell {
    flex: 1 1 0;
    min-width: 96px;
    flex-direction: column;
    align-items: stretch;
    gap: 2px;
  }
  .cell {
    display: flex;
  }
  .field.stacked .label,
  .cell .label {
    color: var(--text-muted);
    font-size: var(--font-badge);
  }
  .field.stacked input {
    width: 100%;
    text-align: right;
  }
  button.segment {
    justify-content: center;
    width: 100%;
  }
  button.segment.on {
    border-color: var(--accent);
    background: var(--chip-active);
  }
  button.link {
    height: auto;
    padding: 0;
    border: none;
    background: transparent;
    color: var(--accent-text);
    font-size: var(--font-badge);
  }
  button.link:hover:not(:disabled) {
    background: transparent;
    text-decoration: underline;
  }
  hr.rule {
    flex: 1;
    margin: 0;
    border: none;
    border-top: 1px solid var(--border);
  }
  .menu-list {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 6px;
    border: 1px solid var(--border);
    border-radius: var(--r-small, 6px);
    background: var(--panel);
  }
  .menu-list .group-title {
    margin-top: 4px;
    color: var(--text-muted);
    font-size: var(--font-badge);
    letter-spacing: 0.06em;
    text-transform: uppercase;
  }
  .menu-list .entry {
    justify-content: space-between;
    border-color: transparent;
    background: transparent;
  }
  .menu-list .entry[aria-checked="true"] {
    background: var(--chip-active);
  }
  .menu-list .entry .tick {
    color: var(--accent-text);
  }
  .menu-list .reason,
  .menu-list .note {
    margin: 0 0 2px 10px;
    color: var(--text-muted);
    font-size: var(--font-badge);
    white-space: normal;
    max-width: 320px;
  }
  /* A value on a row of its own is the card's live figure (Move's "120 mm"),
     in body type where the Qt chip paints it. */
  .text.value:only-child {
    margin-left: 0;
    font-size: var(--font-body);
  }
  input {
    box-sizing: border-box;
    width: 64px;
    height: 22px;
    padding: 2px 6px;
    border: 1px solid var(--accent);
    border-radius: var(--r-tiny, 4px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-body);
    outline: none;
  }
  input.invalid {
    border-color: var(--danger);
  }
  button {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    height: 22px;
    padding: 0 8px;
    border: 1px solid var(--border);
    border-radius: var(--r-tiny, 4px);
    background: var(--chip);
    color: var(--text);
    font: inherit;
    cursor: pointer;
    white-space: nowrap;
  }
  button:hover:not(:disabled) {
    background: var(--chip-hover);
  }
  button:disabled {
    color: var(--text-disabled);
    cursor: default;
  }
  button.toggle {
    border-color: transparent;
    background: transparent;
    padding: 0 2px;
  }
  .box {
    box-sizing: border-box;
    width: 14px;
    height: 14px;
    border: 1px solid var(--border);
    border-radius: 3px;
    background: var(--chip);
  }
  .box.on {
    border-color: var(--accent);
    background: var(--accent);
  }
</style>

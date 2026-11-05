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
      const target = face.focus;
      if (target)
        tick().then(() => {
          const input = inputs[target];
          if (!input) return;
          // Claimed outright: a focus() in a page whose window is not active
          // fires no focus event (the name question's finding).
          claim();
          input.focus();
          input.select();
        });
      return;
    }
    const next = untrack(() => ({ ...values }));
    for (const f of list) if (f.id !== focused) next[f.id] = f.value;
    values = next;
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

  onMount(() => {
    let last = '';
    const measure = () => {
      if (!content) return;
      // Layout offsets, never a transformed box.
      const w = Math.ceil(content.offsetWidth);
      const h = Math.ceil(content.offsetHeight);
      if (w <= 0 || h <= 0) return;
      measured = { w, h };
      const size = `${w}x${h}`;
      if (size === last) return;
      last = size;
      chips.reportSize(name, w, h);
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
  data-chip={name}
  data-hit={settled ? '' : undefined}
  style:left="{rect.x}px"
  style:top="{rect.y}px"
  style:width="{rect.w}px"
  style:height="{rect.h}px"
  style:visibility={settled ? undefined : 'hidden'}
>
  <div class="chip-content" bind:this={content}>
    {#each face.rows as row, r (r)}
      <div class="row">
        {#each row as part, p (p)}
          {#if part.kind === 'text'}
            <span class="text {part.tone}">{part.text}</span>
          {:else if part.kind === 'field'}
            <label class="field">
              <span class="label">{part.label}</span>
              <input
                type="text"
                data-field={part.id}
                title={part.tip}
                aria-label={part.label}
                spellcheck="false"
                autocomplete="off"
                disabled={!part.enabled}
                class:invalid={face.invalid}
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
          {:else}
            <button
              type="button"
              use:noFocus
              class={part.kind}
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
    font-family: var(--font-family, inherit);
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
  .text.value {
    font-size: var(--font-body);
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

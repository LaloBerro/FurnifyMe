<script lang="ts">
  // A Theme::Spec number - Text size, the line widths, Grid detail, Gizmo
  // size - as the drawer's spin box: the value with its suffix ("1.0x",
  // "10 pt"), a step either way, typing that applies live whenever the text
  // reads as a number (clamped to the field's range), and a wheel over the
  // field that steps it rather than reaching anything behind.
  //
  // A typed field keeps its focus (F17 is about chips), so it takes the
  // KEYBOARD from the chrome while it is being edited and gives it back on
  // Enter, Escape, blur, or the card going away - and Escape puts back the
  // value the edit started from.
  import { onDestroy } from 'svelte';
  import { chip } from '../../actions';
  import type { Keyboard, NumberSpec } from './api';

  let {
    field,
    value,
    keyboard,
    onchange,
  }: { field: NumberSpec; value: number; keyboard: Keyboard; onchange: (value: number) => void } = $props();

  const format = (v: number) => v.toFixed(field.decimals) + field.suffix;

  let editing = $state(false);
  let text = $state('');
  let input: HTMLInputElement | undefined = $state();
  let before = 0;
  // Something was sent during this edit - what Escape has to take back.
  let dirty = false;
  let claimed = false;

  $effect(() => {
    const v = value;
    if (!editing) text = format(v);
  });

  function parse(raw: string): number {
    let t = raw.trim();
    const suffix = field.suffix.trim();
    if (suffix && t.toLowerCase().endsWith(suffix.toLowerCase())) t = t.slice(0, -suffix.length).trim();
    return /^[+-]?(\d+(\.\d*)?|\.\d+)$/.test(t) ? Number(t) : NaN;
  }

  function settle(v: number) {
    const clamped = Math.min(field.max, Math.max(field.min, v));
    return Number(clamped.toFixed(field.decimals));
  }

  function claim() {
    if (claimed) return;
    claimed = true;
    keyboard.claimKeyboard();
  }

  function release() {
    if (!claimed) return;
    claimed = false;
    keyboard.releaseKeyboard();
  }

  function send(v: number) {
    if (editing) dirty = true;
    onchange(v);
  }

  function step(direction: number) {
    const next = settle(value + direction * field.step);
    // The field shows the step at once, as a spin box does, even mid-edit
    // (review I5) - the spec's echo is not waited for.
    if (editing) text = format(next);
    send(next);
  }

  function focus() {
    claim();
    if (!editing) {
      before = value;
      dirty = false;
    }
    editing = true;
  }

  // Enter and blur: what was typed is applied, clamped to the range, as a
  // spin box fixes up its text when it is left.
  function finish() {
    if (!editing) return;
    const v = parse(text);
    if (!Number.isNaN(v)) {
      const settled = settle(v);
      if (settled !== value) onchange(settled);
      text = format(settled);
    } else {
      text = format(value);
    }
    editing = false;
    release();
  }

  // While typing, only a value the field could hold is applied (review I4):
  // a "1" on the way to "12" is not a font size of 8 pt for one keystroke -
  // QSpinBox treats it as intermediate and says nothing either.
  function typed() {
    const v = parse(text);
    if (Number.isNaN(v) || v < field.min || v > field.max) return;
    if (field.decimals === 0 && !Number.isInteger(v)) return;
    send(Number(v.toFixed(field.decimals)));
  }

  function key(event: KeyboardEvent) {
    if (event.key === 'Enter') {
      event.preventDefault();
      finish();
      input?.blur();
      return;
    } else if (event.key === 'Escape') {
      event.preventDefault();
      // Put back what the edit started from - only if anything was sent.
      if (dirty) onchange(before);
      dirty = false;
      editing = false;
      text = format(before);
      release();
      input?.blur();
    } else if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
      event.preventDefault();
      step(event.key === 'ArrowUp' ? 1 : -1);
    }
  }

  function wheel(event: WheelEvent) {
    event.preventDefault();
    event.stopPropagation();
    if (event.deltaY !== 0) step(event.deltaY < 0 ? 1 : -1);
  }

  onDestroy(release);
</script>

<div class="row" data-number={field.id}>
  <span class="label">{field.label}</span>
  <div class="spin" title={field.tip}>
    <input
      bind:this={input}
      bind:value={text}
      type="text"
      inputmode="decimal"
      spellcheck="false"
      aria-label={field.label}
      onmousedown={claim}
      onfocus={focus}
      onblur={finish}
      oninput={typed}
      onkeydown={key}
      onwheel={wheel}
    />
    <span class="steps">
      <button use:chip class="up" aria-label="+" onclick={() => step(1)}></button>
      <button use:chip class="down" aria-label="−" onclick={() => step(-1)}></button>
    </span>
  </div>
</div>

<style>
  .row {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .label {
    flex: 1;
    min-width: 0;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .spin {
    display: flex;
    align-items: stretch;
    height: 22px;
    border: 1px solid var(--border);
    border-radius: var(--r-small, 5px);
    background: var(--chip);
    overflow: hidden;
  }
  input {
    width: 58px;
    padding: 0 6px;
    border: none;
    background: transparent;
    color: var(--text);
    font: inherit;
    font-size: var(--font-label);
    outline: none;
  }
  input:focus {
    color: var(--text);
    box-shadow: inset 0 0 0 1px var(--accent);
  }
  .steps {
    display: flex;
    flex-direction: column;
    width: 16px;
    border-left: 1px solid var(--border);
  }
  .steps button {
    flex: 1;
    padding: 0;
    border: none;
    background: transparent;
    cursor: pointer;
    position: relative;
  }
  .steps button:hover {
    background: var(--chip-hover);
  }
  .steps button::after {
    content: '';
    position: absolute;
    left: 50%;
    top: 50%;
    border-left: 3.5px solid transparent;
    border-right: 3.5px solid transparent;
    transform: translate(-50%, -50%);
  }
  .up::after {
    border-bottom: 4px solid var(--text-muted);
  }
  .down::after {
    border-top: 4px solid var(--text-muted);
  }
</style>

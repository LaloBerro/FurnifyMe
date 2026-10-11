<script lang="ts">
  // THE SETTINGS DRAWER as a card on the editor's chrome page (migration
  // Phase 3b) - AppearancePanel, ported. Where it stands is not this page's
  // call: C++ anchors an HtmlCardSlot exactly where the Qt card stood and
  // hands us its rectangle, so stacking under the gizmo, the toast stepping
  // around it and its visibility off the Settings action are unchanged.
  //
  // Four tabs, by meaning: Colours (the swatches, Save/Load colours, Reset),
  // Text & lines (Text size, Font, Edge lines, Outline lines, Button border),
  // Viewport and Working (MainWindow's rows, in its order). Which tab is open
  // is this card's own session state - never persisted, and it opens on
  // Colours, as the Qt drawer does.
  //
  // THE MIRROR LAW: every row that is not a Theme::Spec value is a view of a
  // QAction - checked and enabled read off `settings.actions` on every
  // actionsChanged, and a click asks for a trigger. Nothing here holds a
  // setting. Theme::Spec values arrive in `settings.spec` and are re-read on
  // every specChanged, whoever made the edit.
  import { onDestroy, untrack } from 'svelte';
  import { chip } from '../../actions';
  import type { ActionState, CardRect, Keyboard, NumberSpec, RowSpec, SettingsApi, SettingsSpec } from './api';
  import ColourList from './ColourList.svelte';
  import FontPicker from './FontPicker.svelte';
  import NumberField from './NumberField.svelte';
  import ResetButton from './ResetButton.svelte';
  import Segments, { type Segment } from './Segments.svelte';
  import Switch from './Switch.svelte';

  let { settings, keyboard, rect }: { settings: SettingsApi; keyboard: Keyboard; rect: CardRect } = $props();

  // One SettingsBridge for the card's whole life - read once, on purpose.
  const api: SettingsApi = untrack(() => settings);
  const layout = api.layout;
  const words = layout.words;
  const numbers: Record<string, NumberSpec> = Object.fromEntries(layout.numbers.map((n) => [n.id, n]));
  const textAndLines = ['basePt', 'font', 'edgeWidthPx', 'sketchLineWidthPx', 'chipStrokePx'];

  const indexActions = (list: ActionState[]) => Object.fromEntries(list.map((a) => [a.id, a]));
  let spec: SettingsSpec = $state(api.spec);
  let actions: Record<string, ActionState> = $state(indexActions(api.actions));
  let tab = $state(0);
  let hovered = $state(false);

  let alive = true;
  api.specChanged.connect(() => {
    if (alive) spec = api.spec;
  });
  api.actionsChanged.connect(() => {
    if (alive) actions = indexActions(api.actions);
  });
  onDestroy(() => (alive = false));

  const rowsOn = (index: number) => layout.rows.filter((r) => r.tab === index);
  const actionOf = (id: string) =>
    actions[id] ?? { id, text: '', tooltip: '', enabled: false, checkable: true, checked: false };

  function segmentsFor(row: RowSpec): Segment[] {
    if (row.kind === 'binary') {
      const a = actionOf(row.actions[0]);
      return [
        { action: a.id, side: 'off', label: row.options[0], current: !a.checked, enabled: a.enabled, tip: a.tooltip },
        { action: a.id, side: 'on', label: row.options[1], current: a.checked, enabled: a.enabled, tip: a.tooltip },
      ];
    }
    return row.actions.map((id, i) => {
      const a = actionOf(id);
      return { action: id, label: row.options[i], current: a.checked, enabled: a.enabled, tip: a.tooltip || row.options[i] };
    });
  }

  // What a press on a segment means is the ROW's rule, not the chip's.
  function pick(row: RowSpec, index: number) {
    if (row.kind === 'binary') {
      const a = actionOf(row.actions[0]);
      // Each half triggers only from the state it is NOT - the action is a
      // toggle, and triggering it from its own side would turn it off.
      if (index === 0 ? a.checked : !a.checked) settings.trigger(a.id);
      return;
    }
    const a = actionOf(row.actions[index]);
    // An exclusive group's current member is not re-triggered: a trigger
    // still runs whatever the action does, and re-applying a setting nobody
    // changed is a write this drawer has no business making.
    if (!a.enabled || a.checked) return;
    settings.trigger(a.id);
  }
</script>

<section
  class="settings-card"
  data-hit
  aria-label={words.title}
  style="left: {rect.x}px; top: {rect.y}px; width: {rect.w}px; height: {rect.h}px;"
  onmouseenter={() => (hovered = true)}
  onmouseleave={() => (hovered = false)}
>
  <header>
    <span class="title">{words.title}</span>
    <button
      use:chip
      class="close"
      class:shown={hovered}
      aria-label={words.close}
      title={words.close}
      onclick={() => settings.trigger('settings')}
    ></button>
  </header>

  <div class="tabs" role="tablist">
    {#each layout.tabs as t, i (t.name)}
      <button
        use:chip
        class="tab"
        class:current={tab === i}
        role="tab"
        aria-selected={tab === i}
        data-tab={i}
        title={t.tip}
        onclick={() => (tab = i)}>{t.name}</button
      >
    {/each}
  </div>

  <div class="page" role="tabpanel">
    {#if tab === 0}
      <ColourList
        tokens={layout.tokens}
        colours={spec.colours}
        pickTip={words.pickTip}
        onpick={(id) => settings.pickColour(id)}
      />
      <div class="files">
        <button use:chip class="file save" title={words.saveTip} onclick={() => settings.saveColours()}
          >{words.save}</button
        >
        <button use:chip class="file load" title={words.loadTip} onclick={() => settings.loadColours()}
          >{words.load}</button
        >
      </div>
      <div class="reset-row">
        <ResetButton
          label={words.reset}
          armedLabel={words.resetArmed}
          tip={words.resetTip}
          windowMs={words.resetMs}
          onreset={() => settings.reset()}
        />
      </div>
    {:else if tab === 1}
      {#each textAndLines as id (id)}
        {#if id === 'font'}
          <div class="row" data-row={words.fontLabel}>
            <span class="label">{words.fontLabel}</span>
            <FontPicker
              families={layout.families}
              current={spec.fontFamily}
              shown={spec.fontShown}
              tip={words.fontTip}
              {keyboard}
              onpreview={(family) => settings.setFontFamily(family)}
            />
          </div>
        {:else if numbers[id]}
          <NumberField
            field={numbers[id]}
            value={spec.numbers[id]}
            {keyboard}
            onchange={(v) => settings.setNumber(id, v)}
          />
        {/if}
      {/each}
    {:else}
      {#each rowsOn(tab) as row (row.label)}
        {#if row.kind === 'number'}
          {#if numbers[row.number]}
            <NumberField
              field={numbers[row.number]}
              value={spec.numbers[row.number]}
              {keyboard}
              onchange={(v) => settings.setNumber(row.number, v)}
            />
          {/if}
        {:else}
          <div class="row" class:stacked-row={row.stacked} data-row={row.label}>
            <span class="label">{row.label}</span>
            {#if row.kind === 'toggle'}
              <Switch
                id={row.actions[0]}
                checked={actionOf(row.actions[0]).checked}
                enabled={actionOf(row.actions[0]).enabled}
                tip={actionOf(row.actions[0]).tooltip}
                onpress={() => settings.trigger(row.actions[0])}
              />
            {:else}
              <Segments segments={segmentsFor(row)} stacked={row.stacked} onpick={(i) => pick(row, i)} />
            {/if}
          </div>
        {/if}
      {/each}
    {/if}
  </div>
</section>

<style>
  .settings-card {
    position: fixed;
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 12px;
    border: 1px solid var(--border);
    border-radius: 10px;
    background: var(--panel);
    color: var(--text);
    font-size: var(--font-body);
    overflow: hidden;
  }
  header {
    display: flex;
    align-items: center;
  }
  .title {
    flex: 1;
    font-size: var(--font-title);
    font-weight: 600;
    color: var(--text-muted);
  }
  .close {
    position: absolute;
    top: 6px;
    right: 6px;
    width: 20px;
    height: 20px;
    padding: 0;
    border: none;
    border-radius: 5px;
    background: transparent;
    visibility: hidden;
    cursor: pointer;
  }
  .close.shown {
    visibility: visible;
  }
  .close:hover {
    background: var(--chip-hover);
  }
  .close::before,
  .close::after {
    content: '';
    position: absolute;
    left: 50%;
    top: 50%;
    width: 10px;
    height: 1.5px;
    background: var(--text-muted);
  }
  .close::before {
    transform: translate(-50%, -50%) rotate(45deg);
  }
  .close::after {
    transform: translate(-50%, -50%) rotate(-45deg);
  }
  .tabs {
    display: flex;
    flex-wrap: wrap;
    gap: 3px;
  }
  .tab {
    position: relative;
    height: 24px;
    padding: 0 7px 2px;
    border: none;
    border-radius: 6px 6px 0 0;
    background: transparent;
    color: var(--text-muted);
    font: inherit;
    font-size: var(--font-label);
    white-space: nowrap;
    cursor: pointer;
  }
  .tab:hover:not(.current) {
    background: var(--chip-hover);
  }
  .tab.current {
    background: var(--chip-active);
    color: var(--text);
  }
  .tab.current::after {
    content: '';
    position: absolute;
    left: 2px;
    right: 2px;
    bottom: 0;
    height: 2px;
    background: var(--accent);
  }
  .page {
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .row.stacked-row {
    align-items: flex-start;
  }
  .label {
    flex: 1;
    min-width: 0;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .stacked-row .label {
    flex: none;
    line-height: 22px;
  }
  .files {
    display: flex;
    gap: 8px;
  }
  .file {
    flex: 1;
    height: 26px;
    border: 1px solid var(--border);
    border-radius: 6px;
    background: var(--chip);
    color: var(--text);
    font: inherit;
    font-size: var(--font-label);
    cursor: pointer;
  }
  .file:hover {
    background: var(--chip-hover);
  }
  .reset-row {
    display: flex;
    justify-content: flex-end;
  }
</style>

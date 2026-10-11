// A SettingsBridge stand-in for the page tests and the dev server. Its words
// are the C++ strings verbatim (SettingsEdits.cpp, SettingsBridge.cpp,
// MainWindow::settingsRows()); chrome_smoke sweeps the real page's text too.
import { MockSignal, type Call } from '../../hub/mock';
import type { ActionState, SettingsApi, SettingsLayout, SettingsSpec } from './api';

export function mockLayout(): SettingsLayout {
  return {
    tabs: [
      { name: 'Colours', tip: 'Every colour the app draws with' },
      { name: 'Text & lines', tip: "Text size, the font, and the app's line weights" },
      { name: 'Viewport', tip: 'What the 3D area shows around your furniture' },
      { name: 'Working', tip: 'The unit sizes are written in, and how often this furniture is saved to disk' },
    ],
    numbers: [
      { id: 'basePt', label: 'Text size', min: 8, max: 14, step: 1, decimals: 0, suffix: ' pt', tip: "How big the app's text is — everything else in the type scale moves with it" },
      { id: 'edgeWidthPx', label: 'Edge lines', min: 0, max: 4, step: 0.5, decimals: 1, suffix: ' px', tip: "How thick the lines along a body's own edges are — 0 leaves the shading with none at all" },
      { id: 'sketchLineWidthPx', label: 'Outline lines', min: 1, max: 6, step: 0.5, decimals: 1, suffix: ' px', tip: 'How thick the line an outline draws is — while it is being drawn and once it is closed' },
      { id: 'chipStrokePx', label: 'Button border', min: 0, max: 4, step: 1, decimals: 0, suffix: ' px', tip: 'How thick the line around the tool buttons is — 0 leaves only the fill' },
      { id: 'gridDensity', label: 'Grid detail', min: 0.5, max: 2, step: 0.1, decimals: 1, suffix: 'x', tip: 'How many lines the work-plane grid draws — higher packs more in, lower thins it out' },
      { id: 'gizmoScale', label: 'Gizmo size', min: 0.5, max: 2, step: 0.1, decimals: 1, suffix: 'x', tip: 'How large the Move, Rotate and Scale handles draw over a selected body' },
    ],
    tokens: [
      { id: 'viewport', name: 'Viewport', group: 'Viewport' },
      { id: 'gridMinor', name: 'Grid lines', group: '' },
      { id: 'axisX', name: 'X axis', group: 'Axes and gizmo' },
      { id: 'accent', name: 'Accent', group: '' },
    ],
    families: [
      { shown: 'DM Sans', family: 'DM Sans 14pt' },
      { shown: 'Arial', family: 'Arial' },
      { shown: 'Georgia', family: 'Georgia' },
    ],
    rows: [
      { kind: 'choice', tab: 0, label: 'Theme', actions: ['violet-glass-float', 'graphite'], options: ['Violet Glass Float', 'Graphite'], stacked: true, number: '' },
      { kind: 'toggle', tab: 2, label: 'Show the grid', actions: ['show-the-grid'], options: [], stacked: false, number: '' },
      { kind: 'number', tab: 2, label: 'Grid detail', actions: [], options: [], stacked: false, number: 'gridDensity' },
      { kind: 'binary', tab: 2, label: 'Projection', actions: ['orthographic'], options: ['Perspective', 'Orthographic'], stacked: false, number: '' },
      { kind: 'number', tab: 2, label: 'Gizmo size', actions: [], options: [], stacked: false, number: 'gizmoScale' },
      { kind: 'choice', tab: 3, label: 'Sizes in', actions: ['millimetres', 'centimetres'], options: ['Millimetres', 'Centimetres'], stacked: false, number: '' },
      { kind: 'choice', tab: 3, label: 'Autosave', actions: ['autosave-off', 'after-every-change'], options: ['Off', 'After every change'], stacked: true, number: '' },
    ],
    words: {
      title: 'Settings',
      close: 'Close this panel',
      save: 'Save colours...',
      saveTip: 'Write the colours and text size to a file\nKeep colours you like, or hand them to somebody else.',
      load: 'Load colours...',
      loadTip: 'Read colours back out of a file\nApplied as soon as it is opened.',
      reset: 'Reset colours and sizes',
      resetArmed: 'Reset — click again',
      resetTip: 'Put every colour, the text size and font, the line widths, Grid detail and Gizmo size back the way they shipped — click twice',
      pickTip: 'Pick a colour for %1',
      fontLabel: 'Font',
      fontTip: 'The typeface the whole app is set in',
      resetMs: 4000,
    },
  };
}

export function mockSpec(): SettingsSpec {
  return {
    colours: { viewport: '#1c1c1e', gridMinor: '#2a2a2e', axisX: '#e0564a', accent: '#6a00ff' },
    numbers: { basePt: 10, edgeWidthPx: 1, sketchLineWidthPx: 2, chipStrokePx: 1, gridDensity: 1, gizmoScale: 1 },
    fontFamily: 'DM Sans 14pt',
    fontShown: 'DM Sans',
  };
}

function action(id: string, text: string, checked: boolean, checkable = true): ActionState {
  return { id, text, tooltip: '', enabled: true, checkable, checked };
}

export function mockSettings() {
  const calls: Call[] = [];
  const record =
    (method: string) =>
    (...args: unknown[]) => {
      calls.push({ method, args });
    };
  const settings: SettingsApi & {
    calls: Call[];
    specChanged: MockSignal;
    actionsChanged: MockSignal;
  } = {
    calls,
    layout: mockLayout(),
    spec: mockSpec(),
    specChanged: new MockSignal(),
    actions: [
      action('violet-glass-float', 'Violet Glass Float', true),
      action('graphite', 'Graphite', false),
      action('show-the-grid', 'Show the grid', true),
      action('orthographic', 'Orthographic', false),
      action('millimetres', 'Millimetres', true),
      action('centimetres', 'Centimetres', false),
      action('autosave-off', 'Off', true),
      action('after-every-change', 'After every change', false),
    ],
    actionsChanged: new MockSignal(),
    setNumber: record('setNumber'),
    setColour: record('setColour'),
    setFontFamily: record('setFontFamily'),
    pickColour: record('pickColour'),
    reset: record('reset'),
    saveColours: record('saveColours'),
    loadColours: record('loadColours'),
    trigger: record('trigger'),
  };
  return settings;
}

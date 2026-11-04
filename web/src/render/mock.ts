// A RenderBridge stand-in for the dev server and the page tests: the panel's
// shipped words, a state that answers the slots the way the Qt panel would,
// and a record of every call.
import { MockSignal } from '../hub/mock';
import type { DialId, RenderApi, RenderLayout, RenderState } from './api';

export function sampleLayout(tiles: RenderLayout['tiles'] = []): RenderLayout {
  return {
    title: 'Render settings',
    sections: {
      frame: 'Frame',
      shots: 'Shots',
      light: 'Light',
      material: 'Material',
      studio: 'Studio',
      camera: 'Camera',
      quality: 'Quality',
      export: 'Export',
    },
    rows: { angle: 'Angle', strength: 'Strength', fov: 'FOV', background: 'Background' },
    aspects: [
      { label: 'Free', tip: "The window's own shape - nothing is masked off" },
      { label: '1:1', tip: 'A square picture' },
      { label: '4:5', tip: 'Taller than wide - a single piece, standing' },
      { label: '3:2', tip: "A photograph's own shape" },
      { label: '16:9', tip: 'Wide - a room, or a run of furniture' },
    ],
    guides: [
      { label: 'Off', tip: 'No lines inside the picture' },
      { label: 'Thirds', tip: 'Two lines each way - put what matters where they cross' },
      { label: 'Centre', tip: 'One line each way, through the middle' },
    ],
    qualities: [
      { label: 'Simple', tip: 'Instant frames with real shadows — for framing a shot or a quicker machine' },
      { label: 'Balanced', tip: 'Sharp the moment it appears, with no grain to wait out — the middle of the three' },
      { label: 'Deep', tip: 'The richest picture this machine reaches — slower, and it keeps polishing while you watch' },
    ],
    exportSizes: [
      { label: 'Window', tip: "Twice the viewport's own pixels — what this app has always saved" },
      { label: '720', tip: '1280 x 720, near enough' },
      { label: '1080', tip: 'Full HD' },
      { label: '1440', tip: 'Quad HD' },
      { label: '4K', tip: 'Ultra HD' },
    ],
    cutout: { label: 'Cut out', tip: 'Save Screenshot writes a PNG with no floor and no background — just the furniture, on transparency' },
    saveShot: {
      label: 'Save this view',
      tip: "Remember where the camera is, the picture's shape, the lens and the light — so the next image can be taken the same way",
    },
    studioReset: { label: 'Studio', tip: 'Put the backdrop back to the studio tone this render mode is calibrated for' },
    applyShotTip: "Put the camera, the picture's shape, the lens and the light back the way they were",
    removeShotTip: 'Forget this shot',
    noShots: 'No shots saved yet',
    materialNote: 'Double-click a wood to set its colour, surface and grain',
    backgroundTip: 'Pick the render-mode backdrop colour',
    dials: { lightAngle: { min: 0, max: 359 }, lightStrength: { min: 20, max: 400 }, fov: { min: 20, max: 120 } },
    tiles,
  };
}

export function sampleState(): RenderState {
  return {
    aspect: 0,
    guides: 1,
    exportSize: 0,
    quality: 2,
    cutout: false,
    dials: {
      lightAngle: { value: 35, readout: '35°' },
      lightStrength: { value: 200, readout: '200%' },
      fov: { value: 45, readout: '45°' },
    },
    wood: true,
    woodName: 'Oak natural',
    background: '#c1bfba',
    shots: [],
    exportNote: '2400 x 1552 px',
    tierName: 'Path tracing',
    tierProgress: 0.4,
    shutter: { label: 'Save Screenshot', tip: 'Save Screenshot', enabled: true },
  };
}

// A solid tile for a test: any data URL will do.
export const sampleThumb =
  'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mN8v+X+fwAJlgPmvLXK4QAAAABJRU5ErkJggg==';

export function mockRender(layout: RenderLayout = sampleLayout(), state: RenderState = sampleState()) {
  const calls: { method: string; args: unknown[] }[] = [];
  const stateChanged = new MockSignal();
  const layoutChanged = new MockSignal();
  const record =
    (method: string) =>
    (...args: unknown[]) => {
      calls.push({ method, args });
    };
  const api: RenderApi & {
    calls: typeof calls;
    stateChanged: MockSignal;
    layoutChanged: MockSignal;
    update(change: Partial<RenderState>): void;
  } = {
    calls,
    layout,
    state,
    layoutChanged,
    stateChanged,
    update(change) {
      api.state = { ...api.state, ...change };
      stateChanged.emit();
    },
    aspect: record('aspect'),
    guides: record('guides'),
    exportSize: record('exportSize'),
    quality: record('quality'),
    cutout: record('cutout'),
    setDial: (id: DialId, value: number) => calls.push({ method: 'setDial', args: [id, value] }),
    wood: record('wood'),
    editMaterial: record('editMaterial'),
    saveShot: record('saveShot'),
    applyShot: record('applyShot'),
    removeShot: record('removeShot'),
    pickBackground: record('pickBackground'),
    resetBackground: record('resetBackground'),
    shoot: record('shoot'),
    ready: record('ready'),
  };
  return api;
}

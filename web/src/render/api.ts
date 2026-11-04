// What the render page talks to: RenderBridge, the page's `render` object
// (migration Phase 3e). The Qt RenderSettingsPanel stays the state; `layout`
// is its words, dial ranges and wood tiles, re-read on `layoutChanged`;
// `state` is every current value, re-read on `stateChanged`. Every edit is a
// slot that reaches the panel's own edit path.
import type { Signal } from '../editor/api';

export interface Choice {
  label: string;
  tip: string;
}

export interface DialRange {
  min: number;
  max: number;
}

export type DialId = 'lightAngle' | 'lightStrength' | 'fov';

export interface Tile {
  name: string; // the user's own file name - their word, not this app's copy
  thumb: string; // a data: URL, decoded once by C++
}

export interface RenderLayout {
  title: string;
  sections: {
    frame: string;
    shots: string;
    light: string;
    material: string;
    studio: string;
    camera: string;
    quality: string;
    export: string;
  };
  rows: { angle: string; strength: string; fov: string; background: string };
  aspects: Choice[]; // Free, 1:1, 4:5, 3:2, 16:9
  guides: Choice[]; // Off, Thirds, Centre
  qualities: Choice[]; // Simple, Balanced, Deep
  exportSizes: Choice[]; // Window, 720, 1080, 1440, 4K
  cutout: Choice;
  saveShot: Choice;
  studioReset: Choice;
  applyShotTip: string;
  removeShotTip: string;
  noShots: string;
  materialNote: string;
  backgroundTip: string;
  dials: Record<DialId, DialRange>;
  tiles: Tile[];
}

export interface DialState {
  value: number; // slider units: degrees, or percent for the strength
  readout: string; // as the panel formats it
}

export interface RenderState {
  aspect: number;
  guides: number;
  exportSize: number;
  quality: number;
  cutout: boolean;
  dials: Record<DialId, DialState>;
  wood: boolean;
  woodName: string;
  background: string; // #rrggbb
  shots: string[]; // the user's own names
  exportNote: string;
  tierName: string;
  tierProgress: number; // negative: no polish bar
  shutter: { label: string; tip: string; enabled: boolean };
}

export interface RenderApi {
  layout: RenderLayout;
  state: RenderState;
  layoutChanged: Signal;
  stateChanged: Signal;
  aspect(index: number): void;
  guides(index: number): void;
  exportSize(index: number): void;
  quality(index: number): void;
  cutout(): void;
  setDial(id: DialId, value: number): void;
  wood(name: string): void;
  editMaterial(name: string): void;
  saveShot(): void;
  applyShot(index: number): void;
  removeShot(index: number): void;
  pickBackground(): void;
  resetBackground(): void;
  shoot(): void;
  ready(): void;
}

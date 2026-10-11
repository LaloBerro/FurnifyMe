// What the hub page talks to. In the app these are C++ objects on the web
// channel - HubModel as `hub`, HubWindow's window controls as `win`,
// ThemeBridge as `theme`; in a test or the dev server, mocks with the same
// shape (see mock.ts).

export interface Signal<A extends unknown[] = []> {
  connect(fn: (...args: A) => void): void;
}

export interface HubEntryData {
  id: string;
  name: string;
  scene: boolean;
  thumbUrl: string; // empty when there is no picture
  edited: number; // ms since epoch, 0 = never saved
  editedText: string;
  initial: string; // the placeholder's letter
}

export interface HubApi {
  furniture: HubEntryData[];
  scenes: HubEntryData[];
  changed: Signal;
  failure: Signal<[text: string]>;
  note: Signal<[text: string]>;
  refresh(): void;
  suggestedName(): Promise<string>;
  usedIn(furnitureId: string): Promise<string[]>;
  createFurniture(name: string): void;
  createScene(): void;
  rename(id: string, scene: boolean, name: string): void;
  remove(id: string, scene: boolean): void;
  choose(id: string, scene: boolean): void;
}

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface WindowApi {
  maximized: boolean;
  maximizedChanged: Signal<[maximized: boolean]>;
  maxHovered: Signal<[hovered: boolean]>;
  maxPressed: Signal<[pressed: boolean]>;
  minimize(): void;
  toggleMaximize(): void;
  close(): void;
  // Where the title bar and its maximize button are, so the native side can
  // answer Windows' hit tests (drag the caption, snap layouts on maximize).
  reportChrome(chrome: { titleBar: Rect; maxButton: Rect; buttons: Rect }): void;
}

export interface ThemeApi {
  tokens: Record<string, string>;
  changed: Signal;
}

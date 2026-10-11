import { channelObjects, hosted } from '../channel';

// The page's one door to C++. In the app, Qt WebEngine hands the page
// qt.webChannelTransport and WebOverlay injects qwebchannel.js before any page
// script runs; in a plain browser (npm run dev) neither exists, and a mock
// stands in so the page can still be laid out and styled.

export interface Signal<A extends unknown[]> {
  connect(fn: (...args: A) => void): void;
}

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface Bridge {
  // C++ -> page
  anchorMoved: Signal<[rev: number, x: number, y: number, visible: boolean]>;
  orbitActive: Signal<[active: boolean]>;
  cameraMoving: Signal<[moving: boolean]>;
  toastRequested: Signal<[text: string, kind: string]>;
  animatingChanged: Signal<[on: boolean]>;
  // page -> C++
  reportHitRects(rects: Rect[]): void;
  reportReady(): void;
  runCommand(id: string): void;
  reportChipShown(rev: number, x: number, y: number): void;
}

class MockSignal<A extends unknown[]> implements Signal<A> {
  private fns: ((...args: A) => void)[] = [];
  connect(fn: (...args: A) => void) {
    this.fns.push(fn);
  }
  emit(...args: A) {
    for (const fn of this.fns) fn(...args);
  }
}

function mockBridge(): Bridge {
  const anchorMoved = new MockSignal<[number, number, number, boolean]>();
  const toastRequested = new MockSignal<[string, string]>();
  let rev = 0;
  setInterval(() => {
    const t = Date.now() / 1000;
    anchorMoved.emit(++rev, 420 + Math.cos(t) * 80, 300 + Math.sin(t) * 40, true);
  }, 16);
  return {
    anchorMoved,
    orbitActive: new MockSignal(),
    cameraMoving: new MockSignal(),
    toastRequested,
    animatingChanged: new MockSignal(),
    reportHitRects: () => {},
    reportReady: () => {},
    runCommand: (id) => toastRequested.emit(`Ran ${id} (mock bridge)`, 'note'),
    reportChipShown: () => {},
  };
}

export async function connect(): Promise<Bridge> {
  if (!hosted()) return mockBridge();
  return (await channelObjects()).spike as Bridge;
}

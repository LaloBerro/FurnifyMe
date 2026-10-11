import { mount } from 'svelte';
import '../tokens.css';
import '../themes.css';
import { applyTokens } from '../theme';
import Hub from './Hub.svelte';
import type { HubApi, ThemeApi, WindowApi } from './api';
import { channelObjects, hosted } from '../channel';
import { entry, mockHub, mockTheme, mockWindow } from './mock';

// QWebChannel answers a slot's return value through a callback; the page's
// API speaks promises, so the two value-returning slots are wrapped once here.
function promised<T>(fn: (...args: unknown[]) => void) {
  return (...args: unknown[]) => new Promise<T>((resolve) => fn(...args, resolve));
}

async function start() {
  let hub: HubApi;
  let win: WindowApi;
  let theme: ThemeApi;
  if (hosted()) {
    const objects = await channelObjects();
    const raw = objects.hub as Record<string, unknown> & HubApi;
    hub = raw;
    hub.suggestedName = promised<string>(raw.suggestedName as never);
    hub.usedIn = promised<string[]>(raw.usedIn as never);
    win = objects.win as WindowApi;
    theme = objects.theme as ThemeApi;
  } else {
    // The dev server: a small library to look at.
    const now = Date.now();
    hub = mockHub(
      [entry('a', 'Bench', now - 3600e3), entry('b', 'Armchair', now), entry('c', 'Shoe rack', 0)],
      [entry('s', 'Dining', now - 86400e3, true)],
    );
    win = mockWindow();
    theme = mockTheme();
  }
  // The theme goes on BEFORE the first mount, so a page opened while a theme
  // is already chosen never paints a frame of Graphite first.
  applyTokens(theme.tokens);
  mount(Hub, { target: document.getElementById('app')!, props: { hub, win, theme } });
}

start();

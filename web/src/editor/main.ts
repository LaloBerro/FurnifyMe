import { mount } from 'svelte';
import '../tokens.css';
import '../overlay/overlay.css';
import Editor from './Editor.svelte';
import type { ChromeApi } from './api';
import type { ThemeApi } from '../hub/api';
import { channelObjects, hosted } from '../channel';
import { mockChrome } from './mock';
import { mockTheme } from '../hub/mock';

async function start() {
  let chrome: ChromeApi;
  let theme: ThemeApi;
  if (hosted()) {
    const objects = await channelObjects();
    chrome = objects.chrome as ChromeApi;
    theme = objects.theme as ThemeApi;
  } else {
    // The dev server: a status strip and a toast to look at.
    const mock = mockChrome();
    mock.status = { left: '1 body selected — 600 × 300 × 18 mm', right: 'Pull a face — drag the arrow', name: 'Bench', unsaved: true, shown: true };
    chrome = mock;
    theme = mockTheme();
    setTimeout(() => mock.toast.emit('Deleted Body 02', 'note', true, 1), 300);
  }
  mount(Editor, { target: document.getElementById('app')!, props: { chrome, theme } });
}

start();

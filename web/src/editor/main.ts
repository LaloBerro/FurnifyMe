import { mount } from 'svelte';
import '../tokens.css';
import '../themes.css';
import { applyTokens } from '../theme';
import '../overlay/overlay.css';
import Editor from './Editor.svelte';
import type { ChromeApi } from './api';
import type { ThemeApi } from '../hub/api';
import { channelObjects, hosted } from '../channel';
import { mockChrome } from './mock';
import { mockTheme } from '../hub/mock';
import type { SettingsApi } from './settings/api';
import { mockSettings } from './settings/mock';
import type { VersionsApi } from './versions/api';
import { mockVersions, sampleVersions } from './versions/mock';
import type { JointsApi } from './joints/api';
import { brokenRow, dowelRow, mockJoints } from './joints/mock';

async function start() {
  let chrome: ChromeApi;
  let theme: ThemeApi;
  let settings: SettingsApi | undefined;
  let versions: VersionsApi | undefined;
  let joints: JointsApi | undefined;
  if (hosted()) {
    const objects = await channelObjects();
    chrome = objects.chrome as ChromeApi;
    theme = objects.theme as ThemeApi;
    settings = objects.settings as SettingsApi | undefined;
    versions = objects.versions as VersionsApi | undefined;
    joints = objects.joints as JointsApi | undefined;
  } else {
    // The dev server: a status strip and a toast to look at.
    const mock = mockChrome();
    mock.status = { left: '1 body selected — 600 × 300 × 18 mm', right: 'Pull a face — drag the arrow', name: 'Bench', unsaved: true, shown: true };
    chrome = mock;
    theme = mockTheme();
    settings = mockSettings();
    versions = mockVersions([...sampleVersions]);
    joints = mockJoints([brokenRow(2), dowelRow(1, { expanded: true, selected: true })]);
    mock.cards = {
      settings: { x: window.innerWidth - 320, y: 120, w: 296, h: 380, shown: true },
      versions: { x: 60, y: 120, w: 240, h: 400, shown: true },
      joints: { x: 320, y: 120, w: 240, h: 400, shown: true },
    };
    setTimeout(() => mock.toast.emit('Deleted Body 02', 'note', true, 1), 300);
  }
  // The theme goes on before the first mount - see hub/main.ts.
  applyTokens(theme.tokens);
  mount(Editor, { target: document.getElementById('app')!, props: { chrome, theme, settings, versions, joints } });
}

start();

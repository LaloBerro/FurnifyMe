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
import type { ItemsApi } from './items/api';
import { mockItems } from './items/mock';
import type { MenuApi, ShellApi } from './shell/api';
import { mockMenu, mockShell } from './shell/mock';
import type { ChipsApi } from './chips/api';
import { booleanFace, mockChips, slatsFace } from './chips/mock';
import type { AddPieceApi, PiecesApi } from './scene/api';

async function start() {
  let chrome: ChromeApi;
  let theme: ThemeApi;
  let settings: SettingsApi | undefined;
  let versions: VersionsApi | undefined;
  let joints: JointsApi | undefined;
  let items: ItemsApi | undefined;
  let shell: ShellApi | undefined;
  let menu: MenuApi | undefined;
  let chips: ChipsApi | undefined;
  let pieces: PiecesApi | undefined;
  let addPiece: AddPieceApi | undefined;
  if (hosted()) {
    const objects = await channelObjects();
    chrome = objects.chrome as ChromeApi;
    theme = objects.theme as ThemeApi;
    settings = objects.settings as SettingsApi | undefined;
    versions = objects.versions as VersionsApi | undefined;
    joints = objects.joints as JointsApi | undefined;
    items = objects.items as ItemsApi | undefined;
    shell = objects.shell as ShellApi | undefined;
    menu = objects.menu as MenuApi | undefined;
    chips = objects.chips as ChipsApi | undefined;
    pieces = objects.pieces as PiecesApi | undefined;
    addPiece = objects.addPiece as AddPieceApi | undefined;
  } else {
    // The dev server: a status strip and a toast to look at.
    const mock = mockChrome();
    mock.status = { left: '1 body selected — 600 × 300 × 18 mm', right: 'Pull a face — drag the arrow', name: 'Bench', unsaved: true, shown: true };
    chrome = mock;
    theme = mockTheme();
    settings = mockSettings();
    versions = mockVersions([...sampleVersions]);
    joints = mockJoints([brokenRow(2), dowelRow(1, { expanded: true, selected: true })]);
    items = mockItems();
    shell = mockShell();
    menu = mockMenu();
    chips = mockChips({ boolean: booleanFace(), slats: slatsFace() });
    mock.cards = {
      settings: { x: window.innerWidth - 320, y: 120, w: 296, h: 380, shown: true },
      versions: { x: 60, y: 120, w: 240, h: 400, shown: true },
      joints: { x: 320, y: 120, w: 240, h: 400, shown: true },
      items: { x: 580, y: 120, w: 200, h: 360, shown: true },
      pill: { x: 16, y: 16, w: 420, h: 44, shown: true },
      rail: { x: 16, y: 68, w: 50, h: 470, shown: true },
      view: { x: window.innerWidth - 66, y: 150, w: 50, h: 124, shown: true },
      boolean: { x: 400, y: window.innerHeight - 160, w: 380, h: 44, shown: true, chip: true },
      slats: { x: 400, y: window.innerHeight - 100, w: 330, h: 70, shown: true, chip: true },
    };
    setTimeout(() => mock.toast.emit('Deleted Body 02', 'note', true, 1), 300);
  }
  // The theme goes on before the first mount - see hub/main.ts.
  applyTokens(theme.tokens);
  mount(Editor, { target: document.getElementById('app')!, props: { chrome, theme, settings, versions, joints, items, shell, menu, chips, pieces, addPiece } });
}

start();

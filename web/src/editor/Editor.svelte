<script lang="ts">
  // The editor's chrome as a page over the viewport (migration Phase 3a): the
  // toast and the status strip, for now. The page is see-through and takes
  // the pointer only where it draws (data-hit), which it reports to C++ so
  // every other press goes straight to the 3D view in the same event.
  import { onMount } from 'svelte';
  import type { ChromeApi, StatusState } from './api';
  import type { ThemeApi } from '../hub/api';
  import type { Rect } from './solver';
  import Toast, { type ToastMessage } from './Toast.svelte';
  import StatusStrip from './StatusStrip.svelte';
  import Question, { type QuestionState } from './Question.svelte';
  import NameQuestion from './NameQuestion.svelte';
  import Settings from './settings/Settings.svelte';
  import type { CardRect, SettingsApi } from './settings/api';
  import Versions from './versions/Versions.svelte';
  import type { VersionsApi } from './versions/api';
  import Joints from './joints/Joints.svelte';
  import type { JointsApi } from './joints/api';
  import Items from './items/Items.svelte';
  import type { ItemsApi } from './items/api';
  import Cluster from './shell/Cluster.svelte';
  import Pill from './shell/Pill.svelte';
  import Menu from './shell/Menu.svelte';
  import type { ChipState, MenuApi, MenuOpen, MenuTitle, ShellApi } from './shell/api';
  import GestureChip from './chips/Chip.svelte';
  import type { ChipsApi, Face } from './chips/api';
  import { applyTokens } from '../theme';
  import { reportHitsOnChange } from '../overlay/hits';

  let {
    chrome,
    theme,
    settings,
    versions,
    joints,
    items,
    shell,
    menu,
    chips: chipsApi,
  }: {
    chrome: ChromeApi;
    theme: ThemeApi;
    settings?: SettingsApi;
    versions?: VersionsApi;
    joints?: JointsApi;
    items?: ItemsApi;
    shell?: ShellApi;
    menu?: MenuApi;
    chips?: ChipsApi;
  } = $props();

  const kStripHeight = 24;

  let status: StatusState = $state({ left: '', right: '', name: '', unsaved: false, shown: true });
  let obstacles: Rect[] = $state([]);
  let undoEnabled = $state(true);
  let message: ToastMessage | null = $state(null);
  let pointer: { x: number; y: number } | null = $state(null);
  let viewW = $state(window.innerWidth);
  let viewH = $state(window.innerHeight);
  let toast: Toast | undefined = $state();
  let cards: Record<string, CardRect> = $state({});
  // The close question (Phase 3f) or the name question (Phase 3g), one at a
  // time: while one stands everything else here is inert and under its wash.
  let question: QuestionState = $state({ asking: false });
  // File -> Save version... asked for a pending version (Phase 3c, ruling
  // R4). Kept here, where something is always listening, until the card takes
  // it - the request can arrive before the card has mounted.
  let versionsRequest = $state(0);
  // The rail, the view cluster, the pill and the open menu (Phase 3i).
  let chips: Record<string, ChipState> = $state({});
  let titles: MenuTitle[] = $state([]);
  let menuOpen: MenuOpen = $state({ menu: -1, byKeyboard: false, levels: [] });
  let menuAnchor: { x: number; y: number; w: number; h: number } | null = $state(null);
  let animate = $state(true);
  // The gesture chips' faces (Phase 4a), by chip name.
  let faces: Record<string, Face> = $state({});
  $effect(() => {
    // Where the open menu's title stands, read after the pill has drawn.
    const index = menuOpen.menu;
    if (index < 0) {
      menuAnchor = null;
      return;
    }
    requestAnimationFrame(() => {
      const el = document.querySelector<HTMLElement>(`[data-pill] [data-title="${index}"]`);
      const r = el?.getBoundingClientRect();
      menuAnchor = r ? { x: r.left, y: r.top, w: r.width, h: r.height } : null;
    });
  });
  // Each card claims the keyboard in its own name, so one closing never takes
  // it from a field in another (Phase 3c review I2).
  const keyboardFor = (card: string) => ({
    claimKeyboard: () => chrome.claimKeyboardFor(card),
    releaseKeyboard: () => chrome.releaseKeyboardFor(card),
  });
  let seq = 0;

  const host = $derived({ w: viewW, h: viewH - (status.shown ? kStripHeight : 0) });

  onMount(() => {
    status = { ...chrome.status };
    obstacles = [...chrome.obstacles];
    undoEnabled = chrome.undoEnabled;
    chrome.statusChanged.connect(() => (status = { ...chrome.status }));
    chrome.obstaclesChanged.connect(() => (obstacles = [...chrome.obstacles]));
    chrome.undoEnabledChanged.connect((on) => (undoEnabled = on));
    const readCards = () => {
      cards = { ...(chrome.cards ?? {}) };
      // The harness reads where the page was told each card stands.
      (window as unknown as { __chromeCards?: unknown }).__chromeCards = cards;
    };
    readCards();
    chrome.cardsChanged?.connect(readCards);
    question = { ...(chrome.question ?? { asking: false }) };
    chrome.questionChanged?.connect((q) => (question = { ...q }));
    chrome.toast.connect((text, kind, offersUndo, stamp, px = -1, py = -1, undoOpen) => {
      pointer = px >= 0 && py >= 0 ? { x: px, y: py } : null;
      if (undoOpen !== undefined) undoEnabled = undoOpen;
      message = { id: ++seq, text, kind, offersUndo, stamp };
    });
    chrome.documentMoved.connect((stamp) => {
      // A toast that offers to undo one change must not outlive it.
      if (message && message.stamp >= 0 && stamp !== message.stamp) toast?.dismiss();
    });
    versions?.newRequested.connect(() => versionsRequest++);
    if (shell) {
      chips = { ...shell.chips };
      shell.chipsChanged.connect(() => (chips = { ...shell.chips }));
    }
    if (chipsApi) {
      faces = { ...(chipsApi.faces ?? {}) };
      chipsApi.facesChanged.connect((next) => (faces = { ...(next ?? chipsApi.faces) }));
    }
    if (menu) {
      titles = [...menu.titles];
      menuOpen = { ...menu.open };
      menu.changed.connect(() => {
        titles = [...menu.titles];
        menuOpen = { ...menu.open };
      });
    }
    animate = chrome.animate ?? true;
    chrome.animateChanged?.connect(() => (animate = chrome.animate ?? true));
    applyTokens(theme.tokens);
    theme.changed.connect(() => applyTokens(theme.tokens));
    const resize = () => {
      viewW = window.innerWidth;
      viewH = window.innerHeight;
    };
    window.addEventListener('resize', resize);
    reportHitsOnChange(chrome);
    // Last: every signal above is connected, so C++ may start talking here.
    chrome.ready();
    return () => window.removeEventListener('resize', resize);
  });
</script>

<div class="behind" inert={question.asking}>
{#if shell && cards.rail?.shown}
  <Cluster
    name="rail"
    entries={shell.rail}
    {chips}
    glyphs={shell.glyphs}
    rect={cards.rail}
    {animate}
    from="left"
    ontrigger={(id) => shell.trigger(id)}
    report={(rects) => shell.reportChipRects(rects)}
    reportSize={(w, h) => chrome.reportCardSize?.('rail', w, h)}
  />
{/if}
{#if shell && cards.view?.shown}
  <Cluster
    name="view"
    entries={shell.view}
    {chips}
    glyphs={shell.glyphs}
    rect={cards.view}
    {animate}
    ontrigger={(id) => shell.trigger(id)}
    report={(rects) => shell.reportChipRects(rects)}
    reportSize={(w, h) => chrome.reportCardSize?.('view', w, h)}
  />
{/if}
{#if shell && menu && cards.pill?.shown}
  <Pill
    {menu}
    {titles}
    open={menuOpen}
    wordmark={shell.wordmark}
    rect={cards.pill}
    {animate}
    report={(w, h, rects) => shell.reportPill(w, h, rects)}
  />
{/if}
<Toast
  bind:this={toast}
  {message}
  {undoEnabled}
  {obstacles}
  {host}
  {pointer}
  onundo={(stamp) => chrome.undo(stamp)}
  ondismissed={() => (message = null)}
/>
{#if versions && cards.versions?.shown}
  <Versions
    {versions}
    keyboard={keyboardFor('versions')}
    rect={cards.versions}
    report={(name, height) => chrome.reportCardHeight(name, height)}
    newRequest={versionsRequest}
    onrequesthandled={() => (versionsRequest = 0)}
  />
{/if}
{#if items && cards.items?.shown}
  <Items
    {items}
    keyboard={keyboardFor('items')}
    listKeys={{ hold: () => chrome.holdListKeys?.('items'), release: () => chrome.releaseListKeys?.('items') }}
    rect={cards.items}
    bottom={host.h}
    report={(name, height) => chrome.reportCardHeight(name, height)}
  />
{/if}
{#if joints && cards.joints?.shown}
  <Joints {joints} rect={cards.joints} report={(name, height) => chrome.reportCardHeight(name, height)} />
{/if}
{#if settings && cards.settings?.shown}
  <Settings {settings} keyboard={keyboardFor('settings')} rect={cards.settings} />
{/if}
{#if chipsApi}
  {#each Object.keys(faces) as name (name)}
    {#if cards[name]?.chip && cards[name].shown}
      <GestureChip {name} face={faces[name]} rect={cards[name]} chips={chipsApi} keyboard={keyboardFor(name)} />
    {/if}
  {/each}
{/if}
{#if menu && menuOpen.menu >= 0}
  <Menu {menu} open={menuOpen} anchor={menuAnchor} bounds={{ w: viewW, h: viewH }} />
{/if}
</div>
{#if status.shown}
  <StatusStrip {status} />
{/if}
{#if question.asking && question.kind === 'name'}
  <NameQuestion
    {question}
    {host}
    keyboard={keyboardFor('question')}
    oncreate={(name) => chrome.answerNameQuestion?.('create', name)}
    oncancel={() => chrome.answerNameQuestion?.('cancel', '')}
    onedit={(name) => chrome.nameQuestionEdited?.(name)}
  />
{:else if question.asking}
  <Question {question} {host} onanswer={(which) => chrome.answerQuestion?.(which)} />
{/if}

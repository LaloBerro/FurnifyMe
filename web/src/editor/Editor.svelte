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
  import { applyTokens } from '../theme';
  import { reportHitsOnChange } from '../overlay/hits';

  let {
    chrome,
    theme,
    settings,
    versions,
    joints,
    items,
  }: {
    chrome: ChromeApi;
    theme: ThemeApi;
    settings?: SettingsApi;
    versions?: VersionsApi;
    joints?: JointsApi;
    items?: ItemsApi;
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

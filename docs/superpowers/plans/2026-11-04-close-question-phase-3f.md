# The Close Question as HTML (Migration Phase 3f) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** In HTML-chrome mode the editor's chrome page draws "Save changes to <furniture> before
closing?" - the viewport-wide wash, the 380 px card, its title and body, and the three answers
(Save and close · Enter, Close without saving, Keep editing · Esc) - in place of
`UnsavedCloseCard`'s paint. Every rule the Qt card keeps still holds, because the Qt card still
keeps it.

**Architecture:** **The Qt card stays the state, the key claim and the one answer path (R1).**
`UnsavedCloseCard::setDrawnElsewhere(true)` makes it paint nothing, hide its buttons and pass the
pointer through - but it stays SHOWN while the question stands, so `isAsking()`, its app-wide key
filter (every `ShortcutOverride` in the window claimed, Enter = save, Esc = keep), the autosave
refusals and `updateActions()`'s modeling gate are untouched. It announces `questionChanged()` on
show and hide; `MainWindow` hands the words to `EditorChrome::setQuestion()`, and the page's three
buttons come back through `ChromeBridge::answerQuestion(which)` into the card's own `answer()`,
which hides first and then emits - so a failed save's Failure toast lands on a clear viewport,
exactly as before.

**The page owns the pointer while it asks.** Its scrim is one `data-hit` element over the whole
viewport, so the page's mask takes every press (nothing behind may be picked, orbited, zoomed or
hovered), and `askBeforeClosing()` raises the page's view over every Qt card before it raises the
window buttons back on top - the scrim blocks the viewport, not the window. When the question ends,
`ViewportOverlay::relayout()` puts the Qt cards back over the page.

**Spec:** `src/ui/UnsavedCloseCard.{h,cpp}` (the behaviour and the look being ported);
`MainWindow::askBeforeClosing()`, `onCloseSaveChosen()` and friends; CLAUDE.md "Close asks" and
"The HTML UI".

## Global Constraints
- Branch `close-card` from `main` (`d03a456`); build both presets; never merge, push or delete
  without asking.
- `gui_smoke` never enables HTML; its gate is 6448 + 1, 0 failures, twice, on the final tree.
- Copy verbatim from `UnsavedCloseCard` (tr context `"UnsavedCloseCard"`), read from the card at
  ask time; the furniture name is the user's and exempt from the banned-word sweep.
- The look: wash = the viewport token at 204/255; card 380 wide (16 px gutter in a narrow
  viewport), centred at 48% of the height, radius 14, padding 20 (16 below); title bold at the
  title size, wrapping; body muted; buttons 36 tall, 8 apart, radius 8, label left at 12 px, key
  hint right in the badge size; Save is accent-filled, Close without saving wears `dangerText`,
  Keep editing is plain.
- No button takes focus (`use:chip`); everything else on the page is `inert` while it asks.

## Rulings
- **R1** above. Cost if wrong: an invisible Qt widget stands over the viewport while the question
  does; Phase 6 must lift the claim out of it.
- **R2. The page is raised, not the scrim widened.** The Qt cards (rail, drawers) sit above the
  page's view in z-order; raising the view while asking is the one way its scrim can cover them.
  Cost if wrong: one relayout after every answer.

## Review Focus
1. A press anywhere behind the question - on the viewport, the rail, a drawer - does nothing.
2. Enter and Esc still answer, and Ctrl+Z still reaches nothing, with the page drawing.
3. The page dies while the question stands: the Qt card paints again at once.
4. The window buttons stay usable over the wash (the X while Close furniture's question stands).
5. A very long furniture name wraps the title rather than widening the card.

## Tasks
1. `UnsavedCloseCard`: `setDrawnElsewhere()`, `questionChanged()`, public `answer(Answer)`;
   `ChromeBridge`: `question` property and `answerQuestion()`; `EditorChrome::setQuestion()`.
2. The page: `web/src/editor/Question.svelte`, mounted by `Editor.svelte`; vitest.
3. `MainWindow`: wire it while the page is live; raise order in `askBeforeClosing()`; relayout
   after; chrome_smoke Question block (red first); captures.
4. Gate, docs, board.

## Gate record

**Commits** (branch `close-card` from `main` at `d03a456`): `b92d6c7` the phase; `fb7a936` review
fixes.

**Tests:** vitest 169 (9 new); chrome_smoke 222 (21 new: the question, the press, Esc and Ctrl+Z,
the page's answers, the rail after, a long name, the window buttons crisp under the wash, no
version, the hole after a resize, and the Qt card painting again when the page dies mid-question);
ctest 22/22 in both presets; hub_smoke 36; svelte-check clean.

**gui_smoke:** 6448 + 1, 0 failures, twice, on `b92d6c7`. On the final tree `fb7a936`: run 1 clean;
run 2 failed ONE wall-clock budget - "a selection restyle at 130 rows" at 22.2 ms against 16, in the
Items drawer this branch does not touch, with OBS, DaVinci Resolve and Unity Hub busy on the machine
(the same check read 12.9-13.8 ms in the three runs before). Not called a flake on that story: the pair
was run again, quiet, and both were clean - **6448 + 1, 0 failures, twice** (12.3 ms and 12.0 ms).

**Found while looking:** the wash dimmed the window buttons although they stood above the page in
widget order and took their presses - Qt composites the web page over plain widgets whatever their
stacking. Measured (the close glyph at 52 against the Qt card's 152); fixed by an even-odd hole the
page cuts from its wash at the rectangle C++ passes.

**Review (Opus, read-only):** 0 Critical, 3 Important, 3 Minor. Fixed, each watched red with its fix
switched off (three named failures, one run): I1 a version saved behind the question by a field's
blur; I2 the hole stale after a resize; I3 presses reaching the editor before the page reported its
wash; M4 the card's signals blocked during the window's destruction.

**Rulings:** R1 and R2 as planned. The wash stops above the status strip (the Qt scrim covered the
viewport, which held no strip then), so the unsaved dot stays readable; the strip is the page's own
hit region, so its presses still reach nothing behind.

**Deferred minors:** the page's full-viewport hit rect lingers for a frame after an answer (a press
in that frame is swallowed); the first ask emits questionChanged(true) twice (harmless - an equal
map is not re-sent).

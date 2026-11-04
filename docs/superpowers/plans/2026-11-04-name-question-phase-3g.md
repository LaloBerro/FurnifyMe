# The Name Question as HTML (Migration Phase 3g) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** In HTML-chrome mode the editor's chrome page draws File -> New furniture's "Name this
furniture" question - the viewport-wide wash, the 380 px card, its title, the name field, the body
line, and Cancel (Esc) / Create (Enter) side by side - in place of `NameFurnitureCard`'s paint.
The library hub already asks the same question in its own page (`web/src/hub/NameCard.svelte`,
Phase 2); this is the editor's copy, the last Qt question over the viewport.

**Architecture:** Phase 3f's shape, and one channel for both questions.
- **The Qt card stays the state and the answer path (R1).** `NameFurnitureCard::setDrawnElsewhere()`
  makes it paint nothing, hide its field and buttons and pass the pointer through, while it stays
  SHOWN - so `isAsking()`, its app-wide key claim (every `ShortcutOverride` in the window) and
  "hide first, then report" are untouched. `questionChanged()` on show, hide and a re-ask.
- **One question on the page at a time (R2).** The chrome's `question` map gains a `kind`
  ("close" or "name"). `MainWindow::syncHtmlQuestion()` is the one sender: the close question if it
  stands, else the name question, else none - so neither card's hide can wipe the other's question,
  whatever order their signals arrive in. The mask, the raise over the Qt cards and the hole for
  the window buttons are the close question's, unchanged.
- **The field is the page's, and the name rides WITH the answer (R3).** While drawn elsewhere the
  card's key filter hands every key aimed at the page's view through (Enter and Escape too): the
  page's field claims the keyboard (`claimKeyboardFor("question")`, ruling R5 of 3b), and its
  Enter answers `answerNameQuestion("create", name)` carrying the text it holds - never a name C++
  was told a channel message earlier, which a fast "type, Enter" would beat. Every edit is also
  mirrored (`nameQuestionEdited`) into the Qt field, so a key that reaches the card's filter from
  outside the page, or a page that dies mid-question, still has the typed name. The claim is
  given back when the question ends, whoever ends it.

**Spec:** `src/ui/NameFurnitureCard.{h,cpp}` (the behaviour and the look being ported);
`MainWindow::onNewFurniture()` / `askNewFurnitureName()`; CLAUDE.md "The HTML UI" (Phase 3f, R5).

## Global Constraints
- Branch `name-card` from `main` (`23ba7e8`); build both presets; never merge, push or delete
  without asking.
- `gui_smoke` never enables HTML; its gate is 6448 + 1, 0 failures, twice, on the final tree.
- Words from `NameFurnitureCard` (tr context `"NameFurnitureCard"`), read from the card at ask time.
- The look (the Qt card's numbers): wash = the viewport token at 204/255; card 380 wide (16 px
  gutter), centred at 44% of the height, radius 14, padding 20 (16 below), gap 12; title bold at
  the title size, wrapping; field 36 tall, radius 8, chip fill, border (accent while focused),
  10 px inset; body muted; Cancel left and Create right, 8 apart, 36 tall, label left and key hint
  right; Create accent-filled and dimmed while the name is empty; max length 120.
- No button takes focus (`use:chip`); everything else on the page is `inert` while it asks.

## Rulings
- **R1-R3** above. Cost if wrong: R1 - an invisible Qt widget while the question stands (Phase 6
  lifts it out); R2 - a third question would need a priority; R3 - the page's field is the one
  place a key can type while the question stands, so a page bug there could let a key through
  (the claim still keeps every window shortcut away).

## Review Focus
1. A fast "type, Enter" creates the furniture under the name typed, not the suggestion.
2. Keys aimed at the page's field reach it; every window shortcut stays dead while it asks.
3. An empty or blank name creates nothing - Create dims, Enter does nothing.
4. The page dies while the question stands: the Qt card paints again with what was typed.
5. File -> New furniture on a furniture with unsaved changes: the close question, then the name
   question, each drawn by the page in turn.

## Tasks
1. `NameFurnitureCard`: `setDrawnElsewhere()`, `questionChanged()`, `words()`, `setName()`,
   public `create(name)` / `cancel()`, the pass-through; `ChromeBridge`: `answerNameQuestion()`,
   `nameQuestionEdited()`; `EditorChrome`: give back the question's keyboard claim when it ends.
2. The page: `web/src/editor/Wash.svelte` (the wash and its hole, shared), `NameQuestion.svelte`,
   `Editor.svelte` by kind; vitest.
3. `MainWindow`: `syncHtmlQuestion()`, `raiseQuestions()`, the wiring, no version saved behind
   the name question; chrome_smoke Name block (red first); captures.
4. Gate, docs, board.

## Gate record

**Commits** (branch `name-card` from `main` at `23ba7e8`): `ada9d2f` the phase; `3e4cc4d` review
fixes.

**Tests:** vitest 184 (15 new); chrome_smoke 260 (38 new: the close-then-name hand-over, the
words and the suggestion, the keyboard at once and through a blur, presses on the viewport, the
rail and the window buttons, keys reaching the page's field through the claim, Ctrl+D dead, no
version, the mirror, a blank name, Escape, Cancel, the re-seed, Enter creating under the typed
name, an answer's own name over the mirror, a stray claim refused, and - with the render process
killed - the close question over the name question, then the Qt name card drawing what was typed
on the page); ctest 22/22 in both presets; hub_smoke 36; svelte-check clean.

**gui_smoke:** 6448 + 1, 0 failures, twice, on the final tree `3e4cc4d` (it never enables HTML;
the Qt card's own path is unchanged).

**The page was written before its tests** (a ledgered departure from TDD): each behaviour was then
switched off in turn and watched red on its own test - the name riding with Enter, the seed guard,
the composition guard, the release on destroy. The C++ checks and the review's were all watched
red before their fixes (five named failures in one run); M3's, a test-strength finding, by mutation
(`create()` ignoring its name: red on "the answer creates under the name it carries").

**One unexplained red, recorded:** the second chrome_smoke run on `ada9d2f`, straight after a
build, failed the render block's "the Qt panel docks back at once, and render mode stays on"
(Phase 3e, untouched here) and cascaded into a driver crash leaving render mode. The check now
prints its four terms; five runs since (three on `ada9d2f`, two on the final tree) were clean. Not
called a flake on that evidence - a recurrence will name its term.

**Review (Opus, read-only):** 0 Critical, 1 Important, 6 Minor. Re-graded M1 to Important (fast
"Ctrl+N, type, Enter" created the suggestion - Review Focus 1 by another door). Fixed: I1 the field
gave the keyboard back on a blur and could never take it again; M1 C++ claims the keyboard as the
name question is sent; M2 a "question" claim with no name question holds nothing; M3 the test
pins an answer's own name; M4 an edit under the close question still reaches the name; M5 focus
never returns to a hidden widget; M6 the card does not raise itself over the page.

**Rulings:** R1-R3 as planned. A press on the field always claims (C++ counts and clears the
question's claims), never released on a blur. Bare Alt aimed at the page passes the claim to the
menu bar's own handling exactly as it did with the Qt card's field (the reviewer's declined item)
- unchanged behaviour, not pursued.

**Deferred minors:** M5's card-side half and M6 carry no check of their own (the hidden-widget
case needs a question asked before the page is ready; M6 is invisible by construction).

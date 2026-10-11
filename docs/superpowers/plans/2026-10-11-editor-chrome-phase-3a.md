# Phase 3a — The Editor's Toasts and Status Bar in HTML — Implementation Plan

> **For agentic workers:** superpowers:executing-plans (native; the human partner asked the executor to decide alone).

**Goal:** In a web build the editor's toasts and its status bar are drawn by one transparent HTML page over the viewport (`EditorChrome`), behaving exactly as the Qt `Toast`/`ToastHost` and the Qt status bar do; the Qt chrome stays as the path `gui_smoke` tests until Phase 6.

**Architecture:** `MainWindow::useHtmlChrome(EditorChrome*)` switches the window into HTML-chrome mode: the Qt status bar is hidden but stays the STATE (every `showMessage`, the refusal withdrawal that reads it back, the state label and the name mark keep working exactly as now), and `EditorChrome` mirrors that state to the page; `onReported()` sends Notes and Failures to the page instead of `ToastHost`, with the same notes-off drop, the same Failure → status relay, the same document-moved dismissal and the same Undo guard. The page's toast is a port of `Toast` (look, timings, hover pause, band solver around the overlay's obstacles); its status strip is a port of the bar.

**Spec:** the report's Phase 3 ("toasts and the status bar first"); behaviour spec `src/ui/Toast.cpp`, `src/ui/UnsavedCloseCard.cpp` (`FurnitureNameMark`, `StatusMessageLine`), `MainWindow::onReported()` at `da971fd`.

## Global Constraints
- Qt mode unchanged: `gui_smoke` 6370 + 1, 0 failures, twice, at the end.
- Toast: 360 wide, 14 padding, radius 8, a 6 px stripe (accent / danger), a Failure adds a danger border and the 16 px warning glyph 8 px before the text, body font in `text`; Undo pill 64 x 26 radius 5 `chipHover` with `accentText` (`textDisabled` when Undo is closed); 24 px above the bottom; 160 ms fade with a 6 px rise on arrival only; Note 4000 ms, Failure 8000 ms; the countdown pauses while the pointer rests on the toast and resumes with at least 1500 ms; a second message replaces the first; a Note is dropped (not shown) with notifications off, a Failure never is; a toast whose document stamp no longer matches the document is dismissed.
- Status strip: chrome ground, label font; left the elided message in `textMuted` with the full text as its tooltip when cut; the name mark (a 7 px caution dot 6 px before "<name> — unsaved changes" while dirty, in `text`); the state label in `textMuted` on the right; thin `border` rules between the three; shown exactly when the Qt bar would be (Bottom bar on, not in render mode).
- No OS input in tests; no control takes focus (`use:chip`); the vocabulary sweep covers the page.

## Review Focus
1. A toast arriving while the pointer already rests where it lands must start paused.
2. Undo after the document moved on (Ctrl+Z, then the pill) must not undo the earlier checkpoint — the stamp dismissal must reach the page before a click can.
3. Notifications off: a Note never appears and the bar keeps the sentence; a Failure always appears.
4. A Failure's reason stays on the status strip after the toast is gone.
5. The toast must never cover an overlay card or the hint balloon, at any window width the window allows.

## Tasks
1. **Band solver (TS, vitest first)** `web/src/editor/solver.ts`: `place(host: {w,h}, size: {w,h}, obstacles: Rect[], bottomMargin=24, clearance=8)` — a port of `ToastHost::reposition()` (one band at a time; an obstacle left of centre raises the floor, right lowers the ceiling; no room → the band above the highest blocker; never off the top; 8 attempts).
2. **Toast + status strip page (vitest first)** `web/editor.html`, `web/src/editor/{main.ts, Editor.svelte, Toast.svelte, StatusStrip.svelte, api.ts, mock.ts, copy.ts}`: the timings, hover pause, replace, dismiss-on-stamp, notes, undo pill state, failure glyph; the strip's elision + tooltip, the dot, the visibility.
3. **`EditorChrome` (C++)** `src/web/EditorChrome.{h,cpp}`: `WebHost(viewport, Transparent)`, the hit mask from page rects, the `chrome` channel object (toast/documentMoved/undoEnabled/obstacles/status out; reportHitRects/undo in).
4. **`MainWindow` HTML-chrome mode**: `useHtmlChrome()`, routing in `onReported()`, the relays (stamp, undo-enabled, notes, obstacles on `laidOut` + balloon, status on `messageChanged` + `appStateChanged`), the bar hidden; `main.cpp` turns it on in a web build unless `--qt-chrome`.
5. **`chrome_smoke` harness** — the real editor in HTML-chrome mode: a delete's Note with Undo that undoes; a Failure's reason on the strip after its toast; notifications off; the stamp dismissal; hover pause; the toast clear of the rail, the drawer and the balloon; the strip's three parts; captures to look at.
6. **Gate**: `gui_smoke` twice, ctest, vitest, `hub_smoke`, `chrome_smoke`; review; fixes; docs.

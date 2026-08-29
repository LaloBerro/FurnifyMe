# Phase 3: Feedback and Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Never stop the user to tell them something — every outcome is delivered where they are already looking, and can be undone.

**Architecture:** A `ToastHost` owning a single painted `Toast` over the viewport replaces all seven modal dialogs; `ExtrudePreview` replaces the height dialog with a live preview built by the same code path as the commit; `Theme` gains type, motion and focus tokens.

**Tech Stack:** C++17, Qt 6.11 Widgets, OpenCascade 8.0.1, CMake, MSVC.

**Spec:** `docs/superpowers/specs/2026-08-29-feedback-and-polish-design.md`

## A note on this plan's form

This plan gives **interfaces, behaviour contracts, exact user-facing strings, and exact
test code** — the things that must not drift between tasks. It deliberately does **not**
transcribe the full body of each widget. In Phase 2 the plan's verbatim widget code was
wrong three separate times (a one-way latch that contradicted its own acceptance
criterion, a copy-pasted variable name, and a hint policy that failed the plan's own
test), and each implementer had to diagnose and correct it. The repository now contains
two widgets — `src/ui/HintBalloon.{h,cpp}` and `src/ui/WalkthroughPanel.{h,cpp}` — that
solve exactly the problems these tasks face, and reading them is better guidance than
transcribed pseudo-code. Every task below names the prior art it must follow.

## Global Constraints

- **Vocabulary is settled and must not drift.** Banned case-insensitively in any
  user-visible string: `Fuse`, `Solid`, `OCCT`, `mm3`, `(s)`, `Merge`, `Join`. A 3D object
  is a **body** (`Body 03`); operations are **Union / Subtract / Intersect**; the 2D shape
  is an **outline**; a closed outline is a **face**; the 3D area is the **viewport**.
  Singular and plural are written out. `gui_smoke` enforces this over action texts,
  tooltips, and every `paintedTexts()` source.
- **Lengths are formatted by `Measure`**, never by hand at a call site.
- **Punctuation:** status-bar and toast text takes no trailing period unless it is more
  than one sentence; tooltips lead with a fragment. Clauses separated by an em dash.
- `furnify_geometry` links no Qt. New Qt files go on `furnify_app`.
- C++17. No new dependencies. No qtsvg, no Qt Test module.
- **A widget over the viewport must not eat a click meant for the model.**
  `Qt::WA_TransparentForMouseEvents` excludes the widget *and its entire subtree* from
  hit-testing, so an interactive control must be a **sibling** parented to
  `OcctViewWidget`, positioned from the owner's `moveEvent`/`showEvent`, its visibility
  **derived** (not driven by a hide event that may never arrive), and destroyed with its
  owner. `WalkthroughPanel` does all of this correctly — follow it.
- **Test hit-testing with `view->childAt(point)` compared against the actual control
  pointer.** Asserting an attribute flag, or sending an event straight at the widget you
  hope is reachable, passes against a control no user can click.
- **Assert `isVisible()`**, or a stub that sets text and never calls `show()` passes.
- `MainWindow::appStateChanged()` is emitted at the end of `updateActions()`. Slots on it
  may read state and repaint; calling back into `updateActions()` recurses.
- Never drive the GUI with OS-level synthetic input. `gui_smoke` delivers Qt events
  in-process; keep it that way.
- Build: `cmake --build --preset windows`, then run `.\build\RelWithDebInfo\gui_smoke.exe <dir>`
  directly (it needs a GPU and is not in ctest). Headless: `cmake --build --preset
  windows-headless && ctest --preset windows-headless`, 5/5. Kill `furnifyme.exe` and
  `gui_smoke.exe` before building or the link fails on a locked file.

---

### Task 1: Toasts replace every modal dialog

**Files:**
- Create: `src/ui/Toast.h`, `src/ui/Toast.cpp`
- Modify: `CMakeLists.txt`, `src/MainWindow.h`, `src/MainWindow.cpp`, `src/ui/HintBalloon.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `Theme`, `OcctViewWidget`, `MainWindow::onUndo()`.
- Produces:
  ```cpp
  class Toast : public QWidget {          // one message, painted
  public:
      enum class Kind { Note, Failure };
      QStringList paintedTexts() const;   // for the banned-word sweep
  };

  class ToastHost : public QObject {
  public:
      explicit ToastHost(OcctViewWidget* viewport, QWidget* parent = nullptr);
      void show(const QString& text, Toast::Kind kind, bool undo);
      QString currentText() const;        // empty when nothing is showing
      bool isShowing() const;
      Toast* toast() const;               // for the suite's hit-testing
      QWidget* undoControl() const;       // sibling control, or nullptr
  signals:
      void undoRequested();
  };
  ```

**Behaviour contract:**
- At most one `Toast` exists and is visible at a time. `show()` on a live toast **replaces**
  its content and restarts the timer; it never stacks and never creates a second widget.
- `Note` dismisses after 4000 ms, `Failure` after 8000 ms. A failure carries a sentence the
  user must read and act on, which is the whole reason for the difference.
- The toast body sets `Qt::WA_TransparentForMouseEvents` — it must never eat a click meant
  for the model. The Undo control is a **sibling** parented to the viewport, following
  `WalkthroughPanel::syncSkipGeometry()` exactly: geometry synced from the toast's
  `moveEvent`/`showEvent`, visibility **derived** with `setVisible(...)` rather than left to
  a hide event, destroyed with the toast, and given `Qt::WA_NoMousePropagation` so its
  release cannot reach the viewport and re-pick.
- `undoRequested()` is connected to `MainWindow::onUndo()`. Using it dismisses the toast.
- The toast occupies the viewport's bottom strip. `HintBalloon::reposition()` already steps
  aside for `WalkthroughPanel`; extend that same rule so the balloon also clears a visible
  toast. Do not invent a second mechanism.

**The seven replacements.** Each of these `QMessageBox` calls in `src/MainWindow.cpp`
becomes a `myToasts->show(...)`. **The text is unchanged** — Phase 1 settled every one of
these sentences, and this task moves them, nothing more. Where a box had a title and a
body, the toast shows the body; the title's information must already be in the body, and
where it is not, prepend the title followed by an em dash.

| Site | Kind | Undo |
|---|---|---|
| `Screenshot failed` (~line 178) | `Failure` | no |
| `Can't close this outline` (~583) | `Failure` | no |
| `Extrude failed` (~619) | `Failure` | no |
| boolean success (~664) | `Note` | **yes** |
| boolean failed (~687) | `Failure` | no |
| `Export failed` (~736) | `Failure` | no |
| deletion, currently status-bar only | `Note` | **yes** |

Extrude's success also becomes a `Note` with Undo. The `QInputDialog` at `onExtrude` is
**Task 2's** job — leave it alone here.

- [ ] **Step 1: Write the failing test**

In `tests/gui_smoke.cpp`, add `#include "Toast.h"`, and insert a block after the existing
delete/undo checks:

```cpp
    // --- outcomes are reported without stopping the user ----------------------
    {
        ToastHost* toasts = window.findChild<ToastHost*>();
        check(toasts != nullptr, "the window has a toast host");

        // A modal would hang this suite rather than fail it, so assert the
        // absence of one directly: nothing in the app may create a dialog.
        check(window.findChildren<QDialog*>().isEmpty(),
              "no dialog is ever constructed for an outcome");

        if (toasts) {
            const int before = static_cast<int>(window.document().solids().size());
            check(before > 0, "there is a body to delete");
            view->setSelectedSolids({window.document().solids().front().id});
            settle(100);
            trigger(window, QStringLiteral("Delete Selected"));
            settle(150);

            check(toasts->isShowing(), "deleting a body raises a toast");
            check(toasts->toast() != nullptr && toasts->toast()->isVisible(),
                  "the toast is actually visible");
            check(!toasts->currentText().isEmpty(),
                  QStringLiteral("the toast says what happened (\"%1\")")
                      .arg(toasts->currentText()));

            // The Undo control must be reachable by a real click, not merely
            // present: a transparent overlay hides its whole subtree from
            // hit-testing, which is how Phase 2 shipped an unclickable control.
            QWidget* undo = toasts->undoControl();
            check(undo != nullptr && undo->isVisible(), "the toast offers Undo");
            if (undo) {
                const QPoint centre =
                    undo->mapTo(view, QPoint(undo->width() / 2, undo->height() / 2));
                check(view->childAt(centre) == undo,
                      "the Undo control is reachable by a real click");
                clickAt(view, centre.x(), centre.y());
                settle(200);
                check(static_cast<int>(window.document().solids().size()) == before,
                      "using the toast's Undo restores the body");
                check(!toasts->isShowing(), "using Undo dismisses the toast");
            }

            // A second message replaces the first; a stack of toasts is a
            // dialog with extra steps.
            toasts->show(QStringLiteral("First"), Toast::Kind::Note, false);
            settle(50);
            toasts->show(QStringLiteral("Second"), Toast::Kind::Note, false);
            settle(50);
            check(window.findChildren<Toast*>().size() == 1,
                  "a second message replaces the first rather than stacking");
            check(toasts->currentText() == QStringLiteral("Second"),
                  "the newest message is the one showing");
        }
    }
```

Extend the banned-word sweep to include `Toast::paintedTexts()` alongside the
`WalkthroughPanel` and `HintBalloon` sources already swept.

- [ ] **Step 2: Verify RED** — build; expect a compile failure on `Toast.h`.

- [ ] **Step 3: Write `Toast` and `ToastHost`** to the interface and behaviour contract
  above. Read `src/ui/HintBalloon.cpp` first for the painted-widget idiom, and
  `src/ui/WalkthroughPanel.cpp` for the sibling-control pattern this must copy.

- [ ] **Step 4: Add both to `CMakeLists.txt`** on the `furnify_app` target.

- [ ] **Step 5: Replace all seven dialogs**, per the table. Construct the host in
  `buildOverlay()` and connect `undoRequested` to `onUndo`. Verify no `QMessageBox`
  remains in `MainWindow.cpp` — grep for it; the include may go too.

- [ ] **Step 6: Extend `HintBalloon::reposition()`** to clear a visible toast by the same
  rule it already uses for the guide.

- [ ] **Step 7: Verify GREEN** — full `gui_smoke`, headless 5/5.

- [ ] **Step 8: Commit.**

---

### Task 2: Extrude becomes a live preview

**Files:**
- Create: `src/ui/ExtrudePreview.h`, `src/ui/ExtrudePreview.cpp`
- Modify: `CMakeLists.txt`, `src/MainWindow.h`, `src/MainWindow.cpp`
- Test: `tests/gui_smoke.cpp`

**Interfaces:**
- Consumes: `MainWindow::extrudePendingFace(double)`, `ModelingOps::extrude`,
  `OcctViewWidget`, `ToastHost` (Task 1).
- Produces:
  ```cpp
  class ExtrudePreview : public QWidget {
  public:
      ExtrudePreview(MainWindow* window, OcctViewWidget* view);
      void begin(const TopoDS_Face& face);   // show, focus the field, preview at 10 mm
      void cancel();                          // remove preview, keep the pending face
      double height() const;
      bool hasPreview() const;
      QLineEdit* field() const;               // for the suite
  };
  ```

**Behaviour contract:**
- The field starts at `10`, the value the dialog defaulted to.
- **Every edit rebuilds the preview through the same `ModelingOps::extrude` the commit
  uses.** A preview built by a different path than the commit is a lie, and this is the one
  place a user judges a number by what it looks like.
- The preview `AIS_Shape` is displayed transparent and is **never** added to
  `DocumentModel` — the document holds committed bodies only. It is removed on commit and
  on cancel.
- Enter commits through the existing `extrudePendingFace(height)`, which already records
  progress and reports dimensions. Escape cancels and leaves the pending face intact so the
  user can try again.
- **Invalid input never previews**: a value that will not parse, or that `extrude` rejects,
  leaves the last good preview on screen and marks the field. It must not flicker the
  viewport or clear the preview.
- The field is a real focusable widget parented to the viewport — the sibling rule from the
  global constraints applies to it exactly as to Task 1's Undo control.

- [ ] **Step 1: Write the failing test.** In `tests/gui_smoke.cpp`, add
  `#include "ExtrudePreview.h"`, and where the suite currently extrudes through
  `extrudePendingFace`, add a block that goes through the UI instead:

```cpp
    // --- extrude asks for a height without stopping the user ------------------
    {
        // Draw an outline and close it, so a face is pending.
        trigger(window, QStringLiteral("Start Sketch"));
        clickAt(view, 300, 300); clickAt(view, 420, 300);
        clickAt(view, 420, 380); clickAt(view, 300, 380);
        trigger(window, QStringLiteral("Finish Sketch"));
        settle(150);

        const int before = static_cast<int>(window.document().solids().size());
        trigger(window, QStringLiteral("Extrude..."));
        settle(150);

        ExtrudePreview* preview = window.findChild<ExtrudePreview*>();
        check(preview != nullptr, "extrude opens a preview rather than a dialog");
        check(preview != nullptr && preview->isVisible(), "the preview is visible");
        check(window.findChildren<QDialog*>().isEmpty(),
              "extrude never constructs a dialog");
        check(preview != nullptr && preview->hasPreview(),
              "a preview shape is shown before the user commits anything");
        check(static_cast<int>(window.document().solids().size()) == before,
              "previewing creates no body");

        if (preview && preview->field()) {
            preview->field()->setText(QStringLiteral("25"));
            settle(150);
            check(preview->hasPreview(), "editing the height keeps a live preview");

            // Garbage must not clear the preview or flicker the viewport.
            preview->field()->setText(QStringLiteral("abc"));
            settle(150);
            check(preview->hasPreview(),
                  "an unparseable height leaves the last good preview alone");
            check(static_cast<int>(window.document().solids().size()) == before,
                  "an unparseable height creates no body");

            preview->field()->setText(QStringLiteral("25"));
            settle(100);
            QKeyEvent commit(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(preview->field(), &commit);
            settle(250);
        }

        check(static_cast<int>(window.document().solids().size()) == before + 1,
              "Enter commits the extrude");
        ExtrudePreview* after = window.findChild<ExtrudePreview*>();
        check(after == nullptr || !after->isVisible(),
              "committing closes the preview");
        if (!window.document().solids().empty()) {
            const std::string dims =
                Measure::formatDimensions(window.document().solids().back().shape);
            check(dims.find("25") != std::string::npos,
                  QStringLiteral("the body is the height that was typed (\"%1\")")
                      .arg(QString::fromStdString(dims)));
        }
    }
```

- [ ] **Step 2: Verify RED.**
- [ ] **Step 3: Write `ExtrudePreview`** to the contract above.
- [ ] **Step 4: Add to `CMakeLists.txt`** on `furnify_app`.
- [ ] **Step 5: Rewrite `MainWindow::onExtrude()`** to open the preview. Remove the
  `QInputDialog` include if nothing else uses it. `extrudePendingFace` keeps its signature
  and its behaviour — the preview calls it.
- [ ] **Step 6: Verify GREEN**, full suite plus headless 5/5.
- [ ] **Step 7: Commit.**

---

### Task 3: Type, motion and focus become tokens

**Files:**
- Modify: `src/ui/Theme.h`, `src/ui/Theme.cpp`, `src/ui/ToolChip.cpp`, and every call site
  that currently builds a `QFont` by hand
- Test: `tests/gui_smoke.cpp`

**Interfaces produced:**
```cpp
namespace Theme {
QFont titleFont();     // panel and sheet titles
QFont bodyFont();      // everything the user reads
QFont labelFont();     // chip labels, status bar
QFont badgeFont();     // shortcut badges
QColor focusRing();    // visible keyboard focus
int motionMs();                // 160
QEasingCurve motionCurve();    // OutCubic
}
```

**Behaviour contract:**
- Four sizes is all this app has needed; a fifth is a smell. Every widget that paints text
  reads one of them.
- `ToolChip` paints a visible focus ring when it has keyboard focus. Focus that cannot be
  seen is an accessibility defect, not a polish item.
- `OcctViewWidget`'s camera animation **keeps its own 250 ms** and does not read
  `motionMs()`. A camera move is not a UI transition; reading well at the same speed as a
  chip hover would be a coincidence, not a rule. Say so in a comment where the constant is.

- [ ] **Step 1: Write the failing test.** Assert that no widget in the window paints with a
  font whose point size is absent from the four the theme defines, by walking
  `window.findChildren<QWidget*>()` and comparing `font().pointSizeF()` against the set —
  and that a focused `ToolChip` differs visually from an unfocused one, by grabbing both
  and comparing the images:

```cpp
    // --- one type scale, and focus you can see --------------------------------
    {
        QSet<double> scale;
        for (const QFont& f : {Theme::titleFont(), Theme::bodyFont(),
                               Theme::labelFont(), Theme::badgeFont()}) {
            scale.insert(f.pointSizeF());
        }
        check(scale.size() == 4, "the type scale has four distinct sizes");

        QStringList offenders;
        for (QWidget* w : window.findChildren<QWidget*>()) {
            if (!w->isVisible()) continue;
            if (!scale.contains(w->font().pointSizeF()))
                offenders << (w->metaObject()->className() +
                              QStringLiteral(" @ %1").arg(w->font().pointSizeF()));
        }
        check(offenders.isEmpty(),
              QStringLiteral("every visible widget uses the type scale (%1)")
                  .arg(offenders.isEmpty() ? QStringLiteral("all do")
                                           : offenders.join(QStringLiteral(", "))));

        ToolChip* chip = window.findChild<ToolChip*>();
        check(chip != nullptr, "there is a chip to focus");
        if (chip) {
            const QImage unfocused = chip->grab().toImage();
            chip->setFocus(Qt::TabFocusReason);
            settle(80);
            const QImage focused = chip->grab().toImage();
            check(focused != unfocused, "keyboard focus is visible on a chip");
        }
    }
```

- [ ] **Step 2: Verify RED.**
- [ ] **Step 3: Add the tokens to `Theme`.**
- [ ] **Step 4: Replace every hand-built `QFont`** in `src/` with a token. Grep for
  `QFont` and `setPointSize` to find them all.
- [ ] **Step 5: Paint the focus ring in `ToolChip`.**
- [ ] **Step 6: Verify GREEN.**
- [ ] **Step 7: Commit.**

---

### Task 4: Document the result

**Files:** Modify `CLAUDE.md`, `docs/superpowers/specs/2026-08-29-feedback-and-polish-design.md`

- [ ] **Step 1:** Add a `### Reporting outcomes` section to `CLAUDE.md` after the teaching
  surfaces section: that the app has no modal dialogs and why, the toast lifetimes and the
  reason they differ, the rule that a toast reporting a document change offers Undo, and
  that the extrude preview must be built by the same code path as the commit.
- [ ] **Step 2:** Add the new files to the architecture table.
- [ ] **Step 3:** Mark the spec `Status: implemented 2026-08-29`.
- [ ] **Step 4:** Build, run both suites, and capture the app with `PrintWindow`
  (`PW_RENDERFULLCONTENT`) rather than a screen-region grab — the region grab captures
  whatever window happens to be on top. Confirm from the still that a toast is legible and
  clear of the guide and the balloon. **Capture only; never inject input.**
- [ ] **Step 5: Commit.**

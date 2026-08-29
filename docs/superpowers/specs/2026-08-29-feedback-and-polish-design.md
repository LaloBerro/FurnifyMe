# Phase 3: Feedback and polish — design

Date: 2026-08-29
Status: implemented 2026-08-29. Approved by standing instruction — the user asked for
Phase 3 and Phase 4 to run without further questions, so the design decisions below were
made rather than asked.

Phase 3 of the UX overhaul. Phase 1 (language and numbers) and Phase 2 (help and
learnability) are merged. This phase changes how the app *answers* the user.

## Goal

Never stop the user to tell them something. Every outcome — success, failure, or a value
the app needs — is delivered where the user is already looking, and can be undone.

## Diagnosis

The app interrupts with a modal dialog seven times (`src/MainWindow.cpp`):

| Line | Dialog | Why it should not be modal |
|---|---|---|
| 178 | Screenshot failed | Nothing depends on the answer |
| 583 | Can't close this outline | The user is mid-sketch; the box steals the flow |
| 619 | Extrude failed | Same |
| 664 | Boolean succeeded | **Announcing success with a modal is the worst of the seven** |
| 687 | Boolean failed | Should name the fix and let the user act on it |
| 736 | Export failed | Nothing depends on the answer |
| `onExtrude` | `QInputDialog` for height | Asks for a number with no way to see what it does |

Two further gaps: nothing in the app can be undone from where it happened — Undo exists
only as a menu entry and `Ctrl+Z` — and the visual language has no type scale, no
consistent interaction states, and no shared motion timing.

## Decisions

1. **Toasts replace every one of the seven dialogs.** A toast is a non-blocking strip at
   the bottom of the viewport that says what happened and disappears on its own. Nothing
   in the app needs an answer from the user, so nothing needs to block them.
2. **A toast may carry one action, and that action is Undo.** Every toast reporting a
   change to the document offers it. Undo already exists and is already correct; this
   phase only puts it where the user is looking when they need it.
3. **Extrude becomes a live preview, not a question.** A height field appears over the
   viewport with the pending solid drawn at that height, updating as the user types. It
   is committed with Enter and abandoned with Escape.
4. **Errors keep their Phase 1 wording.** Phase 1 rewrote every message to name a cause
   and a fix; this phase changes only where they appear, never what they say.

## Architecture

### `Toast` and `ToastHost` — new, app layer

`src/ui/Toast.{h,cpp}`.

```cpp
// One message. Painted, not composed from labels, so it matches the balloon
// and the guide - and so paintedTexts() can reach it for the vocabulary sweep.
class Toast : public QWidget {
public:
    enum class Kind { Note, Failure };   // Failure tints the accent stripe
};

// Owns the queue. At most one toast is visible; a second arriving replaces the
// first rather than stacking, because a stack of toasts is a dialog with extra
// steps.
class ToastHost : public QObject {
public:
    explicit ToastHost(QWidget* viewport);

    // text is the whole message. undo, when true, draws an Undo control and
    // emits undoRequested() when it is used.
    void show(const QString& text, Toast::Kind kind, bool undo);
    QString currentText() const;        // empty when nothing is showing
    bool isShowing() const;

signals:
    void undoRequested();
};
```

**Dismissal is by time, by the next toast, or by using the action** — never by a button
the user must find. A `Note` lives 4 seconds, a `Failure` 8, because a failure carries a
sentence the user has to read and act on.

**The Undo control is a sibling parented to the viewport**, exactly as
`WalkthroughPanel`'s skip control is, and for the same reason recorded in `CLAUDE.md`: a
mouse-transparent overlay hides its whole subtree from hit-testing, so an interactive
child of one is unreachable by any real click. The toast body itself is transparent to
the mouse, so it never eats a click meant for the model underneath.

**The toast shares the viewport's bottom edge with `HintBalloon` and `WalkthroughPanel`.**
`HintBalloon::reposition()` already steps aside for the guide; the toast takes the bottom
strip and the balloon steps above it by the same rule, extended to consider both.

### `ExtrudePreview` — new, app layer

`src/ui/ExtrudePreview.{h,cpp}`. A small height field over the viewport plus a live
`AIS_Shape` of the prism at the current value.

- Opening it computes nothing: the field starts at 10 mm, which is what the dialog
  defaulted to.
- Each edit rebuilds the preview shape through the same `ModelingOps::extrude` the commit
  uses. A preview built by a different code path than the commit is a lie, and this is the
  one place a user judges a number by what it looks like.
- The preview shape is displayed with `Graphic3d_NameOfMaterial_Plastic` at 60%
  transparency and is removed on both commit and cancel. It is never added to
  `DocumentModel` — the document holds committed bodies only.
- Enter commits through the existing `extrudePendingFace(height)`, which already records
  progress and reports dimensions. Escape removes the preview and leaves the pending face
  intact, so the user can try again.
- **Invalid input never previews.** A value that fails to parse, or that
  `ModelingOps::extrude` rejects, leaves the last good preview on screen and marks the
  field, rather than flickering the viewport.

### Theme: a type scale, interaction states, and motion

`src/ui/Theme.{h,cpp}` gains three groups of tokens, so that the values live in the one
place the project already designates for them:

```cpp
namespace Theme {
// Type. Four sizes is all this app has ever needed; a fifth is a smell.
QFont titleFont();     // panel and sheet titles
QFont bodyFont();      // everything the user reads
QFont labelFont();     // chip labels, status bar
QFont badgeFont();     // shortcut badges, dimension chips

// Motion. One duration and one curve, so nothing in the app animates at a
// speed nothing else uses.
int   motionMs();              // 160
QEasingCurve motionCurve();    // OutCubic
}
```

Interaction states are already tokens (`chip`, `chipHover`, `chipActive`); this phase adds
the one that is missing — a visible **focus ring** — and applies all four consistently in
`ToolChip`. Keyboard focus that cannot be seen is an accessibility defect, not a polish
item.

`OcctViewWidget`'s camera animation keeps its own 250 ms: a camera move is not a UI
transition and reading well at the same speed as a chip hover is a coincidence, not a
rule.

## Non-goals

No toast stacking, no notification history, no sound. No change to any geometry, camera,
or vocabulary. No new modelling capability — Phase 4 adds those. `QMessageBox` remains
linked for anything genuinely blocking that arrives later; this phase simply has no such
case.

## Testing

- **Headless**: none needed — this phase adds no logic to `furnify_geometry`.
- **`gui_smoke`**:
  - triggering a failure path shows a toast whose text matches the Phase 1 wording, and
    **no modal dialog is created** — asserted by checking that the window has no visible
    `QDialog` child after the action, since a modal would hang the suite rather than fail
    it politely;
  - a toast reporting a document change offers Undo, the control is reachable through
    `view->childAt(point)`, and using it restores the previous body count;
  - a second toast replaces the first rather than stacking — exactly one `Toast` is
    visible at any time;
  - the extrude preview appears on `E`, shows a preview shape, commits on Enter with the
    typed height reflected in the resulting body's dimensions, and leaves the document
    unchanged on Escape;
  - an unparseable height leaves the previous preview and creates no body;
  - the toast's painted copy joins the banned-word sweep.
- The existing 210 checks continue to pass.

## Acceptance criteria

1. No `QMessageBox` remains in `MainWindow`; every one of the seven is a toast.
2. Every toast that reports a change to the document offers Undo, and it works.
3. Extrude never opens a dialog; the height is entered over the viewport with a live
   preview built by the same code path as the commit.
4. At most one toast is visible at a time.
5. Type, motion and focus are read from `Theme`, never hardcoded at a call site.
6. Nothing in this phase changes wording settled in Phase 1 or behaviour settled in
   Phase 2.
7. Headless suite and all existing `gui_smoke` checks pass, plus the new ones.

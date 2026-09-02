#pragma once
//
// The ONE implementation of the two-window handoff between MainWindow (the
// editor) and SelectorWindow (the picker) - main.cpp's real boot wiring and
// gui_smoke's test harness both call wire() rather than each carrying a
// close cousin of the same connections. That is the whole point:
// generate-don't-duplicate, ShortcutSheet's own law (CLAUDE.md) applied to
// wiring instead of documentation - a suite that drives its own
// reimplementation of the handoff proves nothing about what the real app
// does the moment the two drift, and Milestone 4's first review round found
// exactly that kind of drift: gui_smoke's original ad-hoc copy parented the
// SelectorWindow to the MainWindow it was testing, which main.cpp's own
// unparented pair never does - and parentage is precisely the piece of
// state Qt's quitOnLastWindowClosed() scan cares about (see below).
//
// --- the CRITICAL fix this file exists for -----------------------------
//
// Two unparented top-level windows and a handoff that ever hides one before
// showing the other is a quit trap: Qt's "last window closed" check fires
// the instant a top-level window's visibility goes to false, not only on a
// real close() - so hiding the source of a handoff before the target is
// shown can make Qt decide the application has no windows left and quit,
// mid-handoff, even though the OTHER window is about to be shown a moment
// later in the very same call stack. QCoreApplication::quit(), once
// triggered, posts a QEvent::Quit that unconditionally ends exec() the next
// time the loop processes events - later code in the same function does not
// undo it.
//
// Two belts close this, and BOTH are required - either alone leaves an
// ordering mistake somewhere else in the codebase free to reopen the trap:
//
//   1. Every leg of wire() below shows the TARGET first and hides the
//      SOURCE second, in both directions, so at every observable instant at
//      least one of the two windows is visible.
//   2. main.cpp calls QApplication::setQuitOnLastWindowClosed(false), so
//      even a FUTURE ordering mistake elsewhere can no longer make a hidden
//      window quit the app by accident. Quitting is wired explicitly
//      instead - see Hooks::quit below - closing the selector is the one
//      honest quit gesture this two-window model has.
#include <functional>

class MainWindow;
class SelectorWindow;

namespace EditorSelectorHandoff {

struct Hooks {
    // Called with the TARGET already shown and the SOURCE not yet hidden -
    // a test's proof of the ORDER itself, not just the settled end state (a
    // wrong order could still happen to reach the same end state by the
    // time a test gets around to checking it). Default no-op; production
    // (main.cpp) never sets these.
    std::function<void()> onOpenMidpoint;     // editor shown, selector not yet hidden
    std::function<void()> onReturnMidpoint;   // selector shown, editor not yet hidden

    // What runs when the selector itself is closed - the one honest quit
    // gesture in this two-window model (see the file comment's belt 2).
    // Defaults to the real QCoreApplication::quit() when left null. A test
    // substitutes a flag-setting lambda so it can verify THIS WIRING reaches
    // the call without exercising Qt's real quit machinery - harmless
    // either way, since quit() with no exec() event loop running (gui_smoke
    // never calls exec()) is a documented no-op, but a substitute keeps the
    // assertion about the wiring rather than about Qt's own behaviour.
    std::function<void()> quit;
};

// Wires `window` and `selector` together exactly as the real app's boot
// does: choosing a card shows the editor then hides the selector; the
// editor handing back shows the selector then hides the editor; closing the
// selector runs `hooks.quit`. Every connection uses `window`/`selector` as
// its own context object, so the wiring tears down on its own if either is
// destroyed - nothing here retains a pointer beyond the call itself.
//
// Callers are responsible for the ONE-TIME boot action (showing the
// selector) and for their own window construction (parented or not) -
// wire() only ever connects signals, it never shows or constructs anything
// itself, so it carries no opinion about boot order beyond the handoff's
// own two legs.
void wire(MainWindow& window, SelectorWindow& selector, Hooks hooks = Hooks());

}  // namespace EditorSelectorHandoff

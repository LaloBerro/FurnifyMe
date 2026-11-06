#pragma once
//
// The ONE implementation of the two-window handoff between MainWindow (the
// editor) and the library hub (HubWindow; the Qt SelectorWindow it replaced
// was deleted in migration Phase 6) - main.cpp's real boot wiring and
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
//      instead - see Hooks::quit below - through exactly two deliberate
//      gestures: closing the selector, and (since Milestone 5's "dont show
//      project selector when app closes") closing the editor, whose
//      closeEvent() saves first and emits MainWindow::quitRequested().
#include "MainWindow.h"
#include "ui/SceneWindow.h"

#include <QCoreApplication>
#include <QObject>

#include <functional>

namespace EditorSelectorHandoff {

struct Hooks {
    // Called with the TARGET already shown and the SOURCE not yet hidden -
    // a test's proof of the ORDER itself, not just the settled end state (a
    // wrong order could still happen to reach the same end state by the
    // time a test gets around to checking it). Default no-op; production
    // (main.cpp) never sets these.
    std::function<void()> onOpenMidpoint;     // editor shown, selector not yet hidden
    std::function<void()> onReturnMidpoint;   // selector shown, editor not yet hidden
    // The scene leg's own two, for the same reason and read at the same two
    // instants. They are SEPARATE from the pair above rather than shared
    // with it: a test that could not tell which leg fired a midpoint could
    // not tell a correctly ordered scene leg from an unordered one that
    // happened to run while the editor leg was being exercised.
    std::function<void()> onOpenSceneMidpoint;    // scene shown, selector not yet hidden
    std::function<void()> onReturnSceneMidpoint;  // selector shown, scene not yet hidden

    // What runs on either quit gesture (the selector's close, the editor's
    // quit request) - the one quit
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
// editor handing back (File -> Close furniture) shows the selector then
// hides the editor; choosing a scene card shows the scene window then hides
// the selector, and File -> Close scene reverses it; closing the selector,
// the editor or the scene window runs `hooks.quit`. Every connection uses `window`/`selector` as
// its own context object, so the wiring tears down on its own if either is
// destroyed - nothing here retains a pointer beyond the call itself.
//
// Callers are responsible for the ONE-TIME boot action (showing the
// selector) and for their own window construction (parented or not) -
// wire() only ever connects signals, it never shows or constructs anything
// itself, so it carries no opinion about boot order beyond the handoff's
// own two legs.
// `scene` may be null, and the scene leg is then simply not wired. That is
// for the suite, not for production: thirteen of this suite's probes reach
// wire() through one wireSelector() helper to test the EDITOR pair, and
// giving each of them a SceneWindow would stand up thirteen more
// OcctViewWidgets - thirteen more GL contexts - to exercise a leg none of
// them touches. main.cpp passes a real one, and so does the suite's own
// handoff block, which is where all three legs are pinned. Null is spelled
// out at the call site that means it rather than defaulted, so a caller
// cannot omit the scene window by simply not knowing about it.
//
// THE HUB IS A TEMPLATE PARAMETER (migration Phase 2): the Qt SelectorWindow
// and the HTML HubWindow carry the same signals (furnitureChosen,
// sceneChosen, closing) and refresh(), so both are wired by this one body -
// explicit instantiations in the .cpp - and neither the suite nor main.cpp
// has to know which hub it is holding to get the same handoff.
template <class Hub>
void wire(MainWindow& window, Hub& selector, SceneWindow* scene, Hooks hooks = Hooks());

template <class Hub>
inline void wire(MainWindow& window, Hub& selector, SceneWindow* scene, Hooks hooks)
{
    if (!hooks.quit) hooks.quit = [] { QCoreApplication::quit(); };

    // Direction one: a card chosen (an existing one, or a freshly created
    // one - SelectorWindow::createRequested() already routes through
    // furnitureChosen() for the id it just made, see its own header) shows
    // the editor FIRST, hides the selector SECOND. openFurniture() itself
    // runs only once the window is genuinely on screen and past the
    // midpoint hook, preserving the invariant every other caller of that
    // function relies on: CLAUDE.md's lazy-initializeViewer() contract
    // needs a real show()/paintEvent() before anything asks the viewer to
    // display shapes.
    //
    // show()+raise(), deliberately NOT activateWindow(): unlike show(),
    // which respects Qt::WA_ShowWithoutActivating, activateWindow() is an
    // EXPLICIT, unconditional request for real OS-level focus that the
    // attribute does nothing to suppress - and this function is shared
    // verbatim with gui_smoke's own test harness (see that file's own
    // wireSelector()), which must never steal the developer's focus while
    // it runs. raise() alone already brings the window to the front of its
    // own stacking order, which is all a handoff needs.
    QObject::connect(&selector, &Hub::furnitureChosen, &window,
                     [&window, &selector, hooks](const QString& id) {
                         window.show();
                         window.raise();
                         if (hooks.onOpenMidpoint) hooks.onOpenMidpoint();
                         selector.hide();
                         window.openFurniture(id);
                     });

    // Direction two: the editor hands control back (File -> Close furniture,
    // a failed openFurniture() - the native X quits instead since Milestone
    // 5, see below) - shows the selector FIRST, hides the editor SECOND. This is the CRITICAL fix itself:
    // MainWindow::showInitScreen() no longer hides the window on its own -
    // it only emits returnedToSelector() once its own state reset is done
    // (see MainWindow.cpp), and THIS lambda is the one and only place that
    // ever hides it, always after the selector is already showing. Same
    // show()+raise()-not-activateWindow() reasoning as above.
    QObject::connect(&window, &MainWindow::returnedToSelector, &selector,
                     [&window, &selector, hooks] {
                         selector.refresh();
                         selector.show();
                         selector.raise();
                         if (hooks.onReturnMidpoint) hooks.onReturnMidpoint();
                         window.hide();
                     });

    // The quit gestures - TWO since Milestone 5, both explicit wires. Paired
    // with QApplication::setQuitOnLastWindowClosed(false) in main.cpp (belt 1
    // of the file comment's fix), this is belt 2 - quitting is deliberate
    // rather than a side effect of whichever window Qt's last-window-closed
    // scan happened to see hidden at a bad moment during a handoff. Closing
    // the selector quits as it always did; closing the EDITOR quits too now
    // ("dont show project selector when app closes") - MainWindow emits
    // quitRequested() only once its close-time save has succeeded, so a
    // refused save never reaches the hook.
    // Leg three, the scene editor - the same two directions and the same
    // show-the-target-first law, written here beside the other two rather
    // than anywhere else, because a leg wired somewhere else is a leg free
    // to get that order wrong on its own.
    if (scene) {
        QObject::connect(&selector, &Hub::sceneChosen, scene,
                         [scene, &selector, hooks](const QString& id) {
                             scene->show();
                             scene->raise();
                             if (hooks.onOpenSceneMidpoint) hooks.onOpenSceneMidpoint();
                             selector.hide();
                             // After the midpoint and after the window is
                             // genuinely on screen, for the same reason the
                             // editor leg loads its furniture last: the
                             // viewer attaches on a real first show.
                             scene->openScene(id);
                         });

        QObject::connect(scene, &SceneWindow::closeRequested, &selector,
                         [scene, &selector, hooks] {
                             selector.refresh();
                             selector.show();
                             selector.raise();
                             if (hooks.onReturnSceneMidpoint) hooks.onReturnSceneMidpoint();
                             scene->hide();
                         });

        // And the scene window's own X quits, exactly as the editor's does
        // since Milestone 5 - it is not a third route back to the library.
        QObject::connect(scene, &SceneWindow::quitRequested, &selector,
                         [hooks] { hooks.quit(); });
    }

    QObject::connect(&selector, &Hub::closing, &window,
                     [hooks] { hooks.quit(); });
    QObject::connect(&window, &MainWindow::quitRequested, &selector,
                     [hooks] { hooks.quit(); });
}

}  // namespace EditorSelectorHandoff

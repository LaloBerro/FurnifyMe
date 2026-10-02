#include "EditorSelectorHandoff.h"

#include "MainWindow.h"
#include "ui/SceneWindow.h"
#include "SelectorWindow.h"

#include <QCoreApplication>
#include <QObject>

namespace EditorSelectorHandoff {

void wire(MainWindow& window, SelectorWindow& selector, SceneWindow* scene,
          Hooks hooks)
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
    QObject::connect(&selector, &SelectorWindow::furnitureChosen, &window,
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
        QObject::connect(&selector, &SelectorWindow::sceneChosen, scene,
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

    QObject::connect(&selector, &SelectorWindow::closing, &window,
                     [hooks] { hooks.quit(); });
    QObject::connect(&window, &MainWindow::quitRequested, &selector,
                     [hooks] { hooks.quit(); });
}

}  // namespace EditorSelectorHandoff

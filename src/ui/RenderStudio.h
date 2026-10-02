#pragma once
//
// THE RENDER LAYER, owned by nobody in particular.
//
// Everything about TAKING A PICTURE - the settings panel and its dock, the
// frame and guides, the Back chip, the app bar's slide-away, the polish
// ticker, and the mode's own entry and exit - used to live in MainWindow,
// which is ten thousand lines about furniture. A scene needs all of it and
// none of the furniture, so it comes out here and both windows drive it.
//
// It owns THE WIDGETS AND THE VIEWPORT-FACING STATE ONLY. The host still owns
// its own document: this class re-emits "save this view", "apply shot N" and
// "forget shot N", and MainWindow writes them to its DocumentModel while
// SceneWindow writes them to its SceneModel. Pushing the shot list down here
// would mean an interface both document types implement, dragging a UI
// concept into furnify_geometry for no gain - the boundary already works
// where it is.
//
// EditorSelectorHandoff is this project's own precedent: one implementation
// of a thing two callers need, rather than two that drift apart.
//
// THE EXTRACTION'S OWN GATE, recorded here because it is the only thing that
// makes "a move, not a redesign" a claim rather than a hope: the entire
// existing render-mode suite passed with the IDENTICAL check count it had
// before this class existed (5028). A changed count would have meant
// something stopped running.
#include <QObject>
#include <QStringList>

class AppBar;
class CardSlide;
class OcctViewWidget;
class QAction;
class QMainWindow;
class QTimer;
class QWidget;
class RenderFrameGuides;
class RenderSettingsPanel;
class ToolChip;
class ViewportOverlay;

class RenderStudio : public QObject {
    Q_OBJECT

public:
    // `renderModeAction` is the host's own checkable action; the Back chip
    // MIRRORS it rather than owning a second route out of the mode, so
    // whatever decides that action's availability still decides the chip's.
    // `bar` may be null for a host with no app bar to slide away.
    RenderStudio(OcctViewWidget* view, QMainWindow* host, ViewportOverlay* overlay,
                 AppBar* bar, QAction* renderModeAction, QObject* parent = nullptr);

    // THE SINGLE AUTHORITY for whether render mode is on. Docks the panel,
    // slides the bar away, raises the Back chip and starts the polish ticker;
    // emits enabledChanged() so the host can re-derive its own surfaces.
    void setEnabled(bool on);
    bool isEnabled() const { return myEnabled; }

    // Re-derives every surface this class owns from live state. The host calls
    // it from its own appStateChanged - the sibling-visibility law every
    // overlay in this app follows: derived on every state change, never a
    // one-shot at the control that moved.
    void refresh();

    RenderSettingsPanel* panel() const { return myPanel; }
    RenderFrameGuides* frame() const { return myFrame; }
    ToolChip* exitChip() const { return myExitChip; }

signals:
    // Something the host persists or re-derives has changed.
    void settingsChanged();
    void shotSaveRequested();
    void shotApplied(int index);
    void shotRemoved(int index);
    void enabledChanged(bool on);
    // The polish ticker fired - render mode is on and the accumulation may
    // have deepened. Live data with no appStateChanged of its own to ride.
    void tierTick();

private:
    // Puts the panel beside the viewport, or takes it back out. The ONE place
    // the central widget changes for render mode, so the reparent and its
    // explicit resize cannot drift apart.
    //
    // The reparent is the compare pane's own move and carries its lesson:
    // setCentralWidget() leaves the widget it hands back at its OLD geometry
    // until something forces QMainWindowLayout to run again, and neither
    // invalidate() nor activate() is that something - so the size is set
    // explicitly from the widget being replaced.
    void setDockOpen(bool open);

    OcctViewWidget* myView = nullptr;
    QMainWindow* myHost = nullptr;
    ViewportOverlay* myOverlay = nullptr;
    AppBar* myBar = nullptr;
    CardSlide* myBarSlide = nullptr;
    RenderSettingsPanel* myPanel = nullptr;
    RenderFrameGuides* myFrame = nullptr;
    ToolChip* myExitChip = nullptr;
    QTimer* myTierTicker = nullptr;
    QWidget* myDock = nullptr;
    bool myEnabled = false;
};

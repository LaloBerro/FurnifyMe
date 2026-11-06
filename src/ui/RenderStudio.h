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
#include <QPointer>

#include "DocumentModel.h"
#include <QStringList>

#include <functional>

class HtmlCardSlot;
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
                 QWidget* bar, QAction* renderModeAction, QObject* parent = nullptr);

    // THE SINGLE AUTHORITY for whether render mode is on. Docks the panel,
    // slides the bar away, raises the Back chip and starts the polish ticker;
    // emits enabledChanged() so the host can re-derive its own surfaces.
    // WHAT A SHOT CARRIES, written down once. The user's own list - "the shot
    // must store: camera pose, aspect, perpsective and light" - and both
    // windows that can take a picture read it from here rather than each
    // keeping a copy that would drift the day a sixth field joined. Static,
    // and taking the viewport, because a shot is entirely about the render
    // layer and nothing about which window is hosting it.
    static DocumentModel::Shot shotFrom(const OcctViewWidget* view, const QString& name);
    // The caller refreshes its own surfaces afterwards; this touches only the
    // viewport.
    static void applyShotTo(OcctViewWidget* view, const DocumentModel::Shot& shot);

    void setEnabled(bool on);
    bool isEnabled() const { return myEnabled; }

    // Re-derives every surface this class owns from live state. The host calls
    // it from its own appStateChanged - the sibling-visibility law every
    // overlay in this app follows: derived on every state change, never a
    // one-shot at the control that moved.
    void refresh();

    // PUSHES THE VIEWPORT'S LIVE VALUES ONTO THE CARD'S OWN CONTROLS, which
    // is the one direction refresh() does not cover: refresh() re-derives
    // what the studio DRAWS, while these are the rows the user reads.
    //
    // It exists because a reopened scene rendered with its saved aspect,
    // guides, light, FOV, export size, quality and cutout while every one of
    // the card's rows showed its default until touched - the settings were
    // applied to the view and nothing told the card. There was no single
    // entry point to call: the host set rows one setter at a time, so a
    // caller had to know all eight and would silently miss the ninth.
    //
    // Silent by contract: it must not emit the panel's own change signals, or
    // syncing the card would write the values straight back and look like a
    // user edit.
    void syncPanelFromView();

    RenderSettingsPanel* panel() const { return myPanel; }

    // THE HTML PANEL (migration Phase 3e): a page's view that stands in the
    // Qt panel's place in the dock while `live` says the page can draw. The
    // Qt panel stays the state either way - the page drives it - and docks
    // back the moment `live` turns false. Call htmlPanelLiveChanged() when
    // that answer changes; a dock already open swaps on the spot. Never called
    // by gui_smoke, so the suite docks the Qt panel exactly as before.
    void setHtmlPanel(QWidget* view, std::function<bool()> live);
    void htmlPanelLiveChanged();
    // Which of the two the dock holds - null while render mode is off.
    QWidget* dockedPanel() const { return myDocked; }
    RenderFrameGuides* frame() const { return myFrame; }
    // The Back chip's place (Phase 7b: the page draws it, the Qt chip is
    // gone). refresh() is the one place it is shown.
    HtmlCardSlot* exitSlot() const { return myExitSlot; }
    // The pill's slide-away - instant, since the page flies its own card.
    CardSlide* barSlide() const { return myBarSlide; }

private:
    // THE PANEL'S CONTROLS, wired to the viewport once. Every one of these is
    // a pure viewport concern - no document, no checkpoint - which is exactly
    // why they belong to the render layer rather than to whichever window is
    // hosting it. They were MainWindow's, and a scene window that did not
    // repeat them had fifteen controls on screen that moved and changed
    // nothing. Each one emits settingsChanged() so a host can persist or
    // refresh in its own way.
    void wireViewportControls();

public:
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
    QWidget* myBar = nullptr;
    CardSlide* myBarSlide = nullptr;
    RenderSettingsPanel* myPanel = nullptr;
    RenderFrameGuides* myFrame = nullptr;
    HtmlCardSlot* myExitSlot = nullptr;
    QTimer* myTierTicker = nullptr;
    QWidget* myDock = nullptr;
    bool myEnabled = false;
    QPointer<QWidget> myHtmlView;
    std::function<bool()> myHtmlLive;
    QPointer<QWidget> myDocked;
    // The page while it can draw, else the Qt panel.
    QWidget* panelToDock() const;
};

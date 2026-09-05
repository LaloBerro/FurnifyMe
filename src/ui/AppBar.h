#pragma once
// Milestone 5, item 3: the app bar is a FLOATING ROUNDED PILL anchored
// top-left over the viewport, one of the ViewportOverlay-anchored family
// rather than the window's own menu strip. It used to be installed through
// QMainWindow::setMenuWidget - a window-spanning strip above a viewport that
// started below it - and that is gone: the viewport is full-bleed to the
// window's top edge now, and this widget floats over it exactly as the rail
// and the axis gizmo card do.
//
// It carries only the app mark, the wordmark and the window's real QMenuBar
// - the reparented-in one, never rebuilt, so the menus, their shortcuts, the
// generated ShortcutSheet and the vocabulary sweep all keep working
// untouched. The four view controls that used to live here as bar buttons
// (Persp/Ortho, the unit chip, Wireframe, Fit All) moved to a dedicated
// icon-only ToolCluster anchored top-right, under the axis gizmo - see
// MainWindow::buildOverlay() - because a pill sized to its own content has
// no room left for them and the mockup's own picture puts them elsewhere.
//
// It decides nothing of its own: updateActions() stays the single place that
// says what is available, and the menu bar inside this pill is the real one
// every shortcut and every menu action already answers to.
//
// TRAP, unchanged from before this task: QMainWindow::menuBar() is
// qobject_cast<QMenuBar*>(layout()->menuBar()), and that slot is EMPTY now -
// nothing calls setMenuWidget() any more - so menuBar() would create a fresh,
// empty menu bar the moment anything called it. MainWindow never does;
// this class is built on the QMenuBar MainWindow::buildMenus() hands it, and
// AppBar::menus() is how a caller reaches the real object.
#include <QWidget>

class QMenuBar;

class AppBar : public QWidget {
    Q_OBJECT

public:
    // `menuBar` is adopted (reparented) into this pill's own layout, never
    // rebuilt. `parent` is left null by MainWindow::buildAppBar() - the
    // widget is unparented until MainWindow::buildOverlay() hands it to
    // ViewportOverlay::addWidget(), which is what actually reparents it onto
    // the viewport and shows it.
    explicit AppBar(QMenuBar* menuBar, QWidget* parent = nullptr);

    // The menu bar this pill was given. Not QMainWindow::menuBar() - see the
    // trap above - so a caller that needs the real object asks here.
    QMenuBar* menus() const { return myMenus; }

    // The wordmark, exactly as painted, so a caller comparing against it uses
    // this string rather than a second copy of the same literal.
    QString wordmark() const;

    // Wordmark plus nothing else - the four controls that used to paint
    // their own words here (Persp/Ortho, the unit readout, Wireframe, Fit
    // All) moved out to icon-only chips, whose own tooltips the rail's
    // existing sweep already reaches through ToolCluster/ToolChip. This pill
    // paints exactly one string of its own.
    QStringList paintedTexts() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // The wordmark and the app mark are both PAINTED, not child widgets, so
    // the layout only holds an empty spacer wide enough to keep their space
    // clear - re-measured here because it moves when the base type size (the
    // wordmark) or nothing (the mark's own fixed pixel size never changes)
    // does.
    void applyTheme();
    // Recomputes the pill's own horizontal padding from its CURRENT height,
    // so the fully-rounded ends (radius = height / 2, the mockup's own rule)
    // never clip the mark, the wordmark or the menu bar. Height depends only
    // on the layout's fixed top/bottom margins and the tallest child's own
    // sizeHint - never on the left/right margins this sets - which is what
    // makes computing the radius from a layout sizeHint taken BEFORE those
    // margins are applied safe rather than circular. Derived on every theme
    // change, never a literal: a base-size edit changes the menu bar's own
    // row height, which changes the pill's height, which changes the radius
    // a fixed padding could go stale against. (A card grown by
    // Theme::wholeDevicePixels() during ViewportOverlay::relayout() can move
    // the height by at most three pixels afterward - too small a radius
    // change for a second recompute site to be worth the complexity.)
    void updatePillMargins();

    QMenuBar* myMenus = nullptr;
    class QSpacerItem* myMarkSpace = nullptr;
};

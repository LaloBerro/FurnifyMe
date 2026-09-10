#pragma once
// The Add-shape flyout (Milestone 5, "add primitive shapes", pick A): a
// compact 3x2 card of the six ready-made shapes, opened beside the rail's
// Shapes chip and closed by picking one, by Escape, or by a click anywhere
// else. The DECISION this widget owns is only which shape was asked for -
// what a pick does (a body standing on the ground at the camera's focus,
// selected, one undoable checkpoint, no dialogs) is
// MainWindow::addPrimitiveShape()'s contract, so the flyout stays a picker
// and nothing else.
//
// Dismissal is ShortcutSheet's own discipline, both halves: an outside
// click is only visible through an application-wide event filter (the
// press lands on the viewport, never here), and swallowing that press is
// not enough - the viewport picks on the RELEASE, so the filter stays
// installed one event longer than the flyout is visible, specifically to
// swallow it.
#include "ModelingOps.h"

#include <QStringList>
#include <QWidget>

class ShapeFlyout : public QWidget {
    Q_OBJECT

public:
    explicit ShapeFlyout(QWidget* parent = nullptr);

    // Shows beside `anchorTopRight` (viewport coordinates) - the Shapes
    // chip's own right edge - clamped inside the parent.
    void openAt(const QPoint& anchorTopRight);
    void closeFlyout();

    // The tile for `kind`, for gui_smoke's childAt-real probes.
    QWidget* tileFor(ModelingOps::PrimitiveKind kind) const;

    // Every string this flyout paints - the banned-word sweep's food.
    QStringList paintedTexts() const;

signals:
    void shapePicked(ModelingOps::PrimitiveKind kind);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyTheme();

    std::vector<QWidget*> myTiles;   // index == PrimitiveKind order
    class QLabel* myCaption = nullptr;
    // ShortcutSheet's one-event-longer rule: true from the outside press
    // that closed the flyout until its release has been swallowed too.
    bool mySwallowNextRelease = false;
};

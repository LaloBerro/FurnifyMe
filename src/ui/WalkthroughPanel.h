#pragma once
//
// The guided first build: four steps that tick as the user genuinely performs
// them. Step state is DERIVED from live application state on every
// appStateChanged, never stored as a cursor - a stored cursor is a second
// source of truth that drifts from the first.
//
#include <QStringList>
#include <QWidget>

class MainWindow;
class QHideEvent;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;

class WalkthroughPanel : public QWidget {
    Q_OBJECT

public:
    WalkthroughPanel(MainWindow* window, QWidget* parent);

    int completedSteps() const { return myCompleted; }
    bool isFinished() const { return myFinished; }

    // The four step strings, exposed so gui_smoke's banned-word sweep can
    // reach them.
    QStringList stepTexts() const;

    // Every string this panel paints - the title and the skip control's
    // label, in addition to the four steps - so the sweep has no blind spot
    // left: none of this is ever put on a QAction text or tooltip, so
    // nothing else makes any of it visible to that check.
    QStringList paintedTexts() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    // A widget parented under a still-hidden top level (as this one is, built
    // during MainWindow's constructor) does not become genuinely visible - or
    // receive a real show event - until that top level is shown. That is
    // exactly the moment a returning user's already-learned progress needs to
    // be re-checked, since it may have changed since construction.
    void showEvent(QShowEvent* event) override;
    // mySkip is a sibling, not a child (see mySkip below), so it has no
    // layout of its own to follow this widget's - these keep it glued to
    // skipRect() by hand across every move, resize, and hide/show.
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override { return QSize(260, 168); }

private:
    void refresh();
    void finish();
    QRect skipRect() const;
    void syncSkipGeometry();

    MainWindow* myWindow = nullptr;
    int myCompleted = 0;
    bool myFinished = false;

    // The one interactive spot on this otherwise click-through panel. A
    // SIBLING of this widget (parented to the same viewport), not a child:
    // Qt::WA_TransparentForMouseEvents excludes a widget's entire subtree
    // from hit-testing, not just the widget itself (verified directly against
    // this machine's Qt build - QWidgetPrivate::childAtRecursiveHelper skips
    // straight past a transparent widget's children), so a child here would
    // have been just as unreachable by a real click as the transparent parent
    // itself. See WalkthroughPanel.cpp. Owned by Qt's parent-child mechanism,
    // not by this pointer.
    QWidget* mySkip = nullptr;

    // Latches: a step stays ticked once reached, because the state that proves
    // it (a sketch in progress, a pending face) is transient by nature.
    bool myStartedSketch = false;
    bool myPlacedPoints = false;
    bool myClosedOutline = false;

    // NOT the stored cursor the design forbids: this is per-session state,
    // captured freshly every time the panel transitions into showing - once
    // in the constructor, and again on every restore-from-reset in
    // refresh() - and read back through document().count() > myBodyBaseline
    // on every refresh() rather than being trusted on its own. Its only job
    // is telling "a body that already existed when the guide most recently
    // appeared" apart from "a body made since" - without it,
    // document().count() > 0 stays true forever once any body exists, so a
    // reset while one is still around would re-complete the guide on the
    // spot instead of genuinely restoring it.
    int myBodyBaseline = 0;
};

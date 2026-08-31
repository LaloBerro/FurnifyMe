#pragma once
// The window's menu strip, replaced wholesale through
// QMainWindow::setMenuWidget: a wordmark, the window's real QMenuBar, a
// stretch, then the readouts and toggles that used to float over the
// viewport.
//
// It owns nothing it shows. The QMenuBar inside it is the one the menus were
// built on - reparented in, never rebuilt - and every button either mirrors a
// QAction MainWindow keeps or reports a click for MainWindow to act on. The
// bar decides nothing: updateActions() stays the single place that says what
// is available, and the unit button triggers the View -> Units action for the
// unit it is NOT showing rather than growing a toggle of its own.
//
// TRAP, and the reason MainWindow builds its own QMenuBar rather than asking
// the window for one: QMainWindow::menuBar() is
// qobject_cast<QMenuBar*>(layout()->menuBar()), and that slot now holds an
// AppBar. The cast fails, so menuBar() CREATES a new, empty menu bar - and
// the setMenuBar() it then calls hides and deleteLater()s whatever sat in the
// slot, which is this widget. Nothing may call menuBar() once the bar is
// installed. The same mechanism is why the menu bar cannot be handed over
// after the fact: QLayoutPrivate::menubar is a raw pointer that reparenting
// does not clear, so a QMenuBar that has ever been in that slot is deleted by
// the setMenuWidget() call that replaces it.
#include <QAbstractButton>
#include <QStringList>
#include <QWidget>

class QAction;
class QMenuBar;

// A compact bordered button wearing the same anatomy ToolChip settled in
// Task 1 - 1px border() always, chipHover() on hover, chipActive() plus an
// inset accent() ring when checked, everything dimmed together when disabled.
// Its painted card is its whole widget rect - the family reserves no margin
// and paints no shadow (see Theme.h). It is that chip
// without the glyph and the shortcut badge, which is what the bar's row of
// controls wants; it is deliberately NOT a second button look, and the state
// colours below are read in the same order ToolChip reads them.
//
// With an action it is a mirror: text, enabled, checkable, checked and
// tooltip all come from the action and nothing is stored here. Without one it
// is a plain readout button - the view label and the unit chip - whose text
// its owner sets and whose click its owner interprets.
class BarButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit BarButton(QAction* action = nullptr, QWidget* parent = nullptr);

    QAction* action() const { return myAction; }
    // Widen the button to fit the widest string it will ever show, so a
    // readout whose text changes ("Persp" to "Top", "mm" to "cm") does not
    // shuffle everything to its right every time the camera moves.
    void reserveWidthFor(const QStringList& candidates);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    // Qt would otherwise flip the checked state locally, before the action has
    // been triggered - the button must never be the source of truth for it.
    void nextCheckState() override {}

private:
    void syncFromAction();
    // The one appearance value a bar button cannot re-derive inside
    // paintEvent(): the per-widget stylesheet that pins its font to
    // labelFont(). See ToolChip::applyTheme(), which is the same rule one
    // control over.
    void applyTheme();

    QAction* myAction = nullptr;
    int myReservedTextWidth = 0;
    bool myHovered = false;
};

class AppBar : public QWidget {
    Q_OBJECT

public:
    AppBar(QMenuBar* menuBar, QAction* wireframe, QAction* fitAll,
           QWidget* parent = nullptr);

    void setViewLabel(const QString& text);   // "Persp", "Top", ...
    void setUnitLabel(const QString& text);   // "mm" / "cm"

    // The menu bar this bar was given. Not QMainWindow::menuBar() - see the
    // trap at the top of this file - so a caller that needs the real object
    // asks here.
    QMenuBar* menus() const { return myMenus; }

    QWidget* viewLabelButton() const;
    QWidget* unitButton() const;

    // The wordmark, exactly as painted, so a caller comparing against it uses
    // this string rather than a second copy of the same literal.
    QString wordmark() const;

    // Wordmark plus every button's label. All of it is painted rather than
    // carried on a QAction or a tooltip, so the vocabulary sweep cannot see
    // any of it without this.
    QStringList paintedTexts() const;

signals:
    void viewLabelClicked();   // -> MainWindow snaps to the axonometric pose
    void unitClicked();        // -> MainWindow triggers the other unit's action

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // The wordmark is PAINTED, not a child widget, so the layout only holds
    // an empty spacer wide enough to keep its space clear - and that width is
    // measured with wordmarkFont(), which moves when the base type size does.
    // A spacer is not a widget and gets no repaint, so it is re-measured
    // here; without it a larger base size painted the wordmark straight
    // through the menu bar.
    void applyTheme();

    QMenuBar* myMenus = nullptr;
    class QSpacerItem* myWordmarkSpace = nullptr;
    BarButton* myViewLabel = nullptr;
    BarButton* myUnit = nullptr;
    BarButton* myWireframe = nullptr;
    BarButton* myFit = nullptr;
};

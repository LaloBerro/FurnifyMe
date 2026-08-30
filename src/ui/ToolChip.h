#pragma once
// A labelled button driven entirely by a QAction. It never stores its own
// enabled/checked state: menus, chips and keyboard shortcuts therefore cannot
// drift apart, and MainWindow::updateActions() needs no knowledge of chips.
#include "IconSet.h"

#include <QAbstractButton>

class QAction;
class QFocusEvent;

class ToolChip : public QAbstractButton {
    Q_OBJECT

public:
    // How a chip presents itself.
    //
    // Labelled is the original and the default: glyph, label and a shortcut
    // badge on one row, sized to its own text.
    //
    // IconOnly is the rail's form - a 34x34 square (plus the shadow margin
    // every chip now reserves) carrying nothing but the glyph. The label and
    // the shortcut do not disappear, they move into the tooltip: a rail that
    // spelled its commands out would be a toolbar, and the menus keep every
    // command labelled and discoverable regardless. Everything else about a
    // chip - that it mirrors a QAction and stores nothing, the border, the
    // inset checked ring, the disabled dimming, the focus ring - is identical
    // in both modes, because they are one control with two widths, not two
    // controls.
    enum class ChipMode { Labelled, IconOnly };

    ToolChip(QAction* action, IconSet::Glyph glyph,
             ChipMode mode = ChipMode::Labelled, QWidget* parent = nullptr);

    QAction* action() const { return myAction; }
    ChipMode mode() const { return myMode; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    // Keyboard focus is otherwise invisible on a custom-painted widget - Qt
    // does not repaint one on its own just because focus moved, the way it
    // does for built-in styled controls. Both just call update(); the ring
    // itself is painted in paintEvent() from window()->focusWidget() == this,
    // NOT hasFocus() - hasFocus() (and QApplication::focusWidget()) stay
    // false for every widget in a window that is not the OS-active one,
    // which gui_smoke relies on (it drives real MainWindows shown with
    // WA_ShowWithoutActivating so the suite never steals OS focus). A
    // window's own focusWidget() is remembered independently of activation,
    // so the ring's colour and weight - not its presence - are what change
    // with window()->isActiveWindow(): full amber while this app is what the
    // user is actually typing into, a muted ring once they have moved to
    // another window, so a chip does not keep shouting focus at someone who
    // has moved on.
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    // Qt would otherwise flip our checked state locally, before the action has
    // been triggered - the chip must never be the source of truth for it.
    void nextCheckState() override {}

private:
    void syncFromAction();

    QAction* myAction = nullptr;
    ChipMode myMode = ChipMode::Labelled;
    QString myShortcut;
    bool myHovered = false;
};

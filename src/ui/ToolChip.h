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
    // IconOnly is the rail's form - a 34x34 square (plus
    // Theme::surfaceShadowMargin() per side, which is now zero, so 34x34 of
    // painted card) carrying nothing but the glyph. The label and
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

    // The text-glyph variant (Milestone 5, item 3): the unit chip ("mm"/"cm")
    // is a WORD, not a shape, and IconSet has no glyph that could stand in for
    // it - so this paints `textGlyph` centred in the icon-only square instead
    // of a rasterised QIcon. Kept as a second constructor on the SAME class,
    // per the mockup's own ruling, rather than a sibling button class: every
    // other contract - mirrors an action when one is given, the border, the
    // inset checked ring, the disabled dimming, the focus ring - is identical,
    // and the unit chip (like the app bar's old unit button before it) is
    // action-less by design, since a click triggers whichever unit action is
    // NOT currently active rather than toggling one action of its own. Only
    // IconOnly makes sense for a text glyph - there is no separate "label" to
    // paint beside it - but the mode parameter is still taken, not hard-coded,
    // so a future Labelled use is not a silent behaviour change away.
    ToolChip(QAction* action, const QString& textGlyph,
             ChipMode mode = ChipMode::IconOnly, QWidget* parent = nullptr);

    // Updates the painted text glyph - the unit chip's own "mm" <-> "cm"
    // swap, driven by whoever owns the reading (MainWindow, on
    // appStateChanged) exactly as the old app bar's setUnitLabel() was. A
    // no-op on a chip built with an IconSet::Glyph instead.
    void setTextGlyph(const QString& text);
    // The text glyph currently painted, or empty for a chip built with an
    // IconSet::Glyph instead - QAbstractButton::text() stays empty for this
    // variant (syncFromAction() is what sets it, and this variant is built
    // action-less), so a caller checking what the unit chip actually reads
    // asks here rather than at text().
    QString textGlyph() const { return myUsesTextGlyph ? myTextGlyph : QString(); }

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
    // Re-derives the two appearance values this widget cannot ask for at paint
    // time: the QIcon, which IconSet rasterises out of text() and
    // textDisabled() at whatever those were when it was built, and the
    // per-widget stylesheet that pins this chip's own font to labelFont().
    // Everything else a chip paints is read from Theme inside paintEvent(),
    // so update() covers it. Hooked to Theme::notifier() rather than to any
    // one window, because a chip has no MainWindow and should not need one.
    void applyTheme();

    // Shared construction body for both constructors.
    void init();

    QAction* myAction = nullptr;
    ChipMode myMode = ChipMode::Labelled;
    // Kept so applyTheme() can rasterise the glyph again in the new text
    // colours. The QIcon this widget holds is the cache; this is the source
    // it was built from. Meaningless while myUsesTextGlyph is true.
    IconSet::Glyph myGlyph = IconSet::Glyph::Items;
    // The text-glyph variant's own state - see the constructor's comment.
    bool myUsesTextGlyph = false;
    QString myTextGlyph;
    QString myShortcut;
    bool myHovered = false;
};

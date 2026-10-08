#pragma once
// One row of the Items drawer (T-063, design batch A). A QWidget of its own
// class for two reasons, and both are about what the row SAYS rather than how
// it is laid out - ItemsPanel still builds its children and owns its look:
//
//   - THE SELECTION MARK (rulings D3/D4). A selected row keeps the chipActive
//     fill ItemsPanel::restyleRows() sets through its stylesheet, and paints a
//     3 px bar at its left edge on top of it. The fill alone measured about
//     1.5:1 against the card, under WCAG 1.4.11's 3:1, and the fill is what
//     the list uses as its focus cue - so the bar is the mark that carries the
//     contrast. It is not a focus ring: it marks the SELECTION, which in this
//     list is the focus (CLAUDE.md, "no control paints a keyboard focus ring").
//   - A SCREEN READER'S VIEW OF IT (finding F7). The class name is what
//     QAccessible's factory keys on: installAccessibility() teaches Qt to
//     answer a row as a tree item with Selected and, on a folder, Expanded or
//     Collapsed - and every state change here raises a StateChanged event, so
//     a reader hears the row it is on change. NEVER an Alert: Qt's Windows
//     plugin answers one with a system sound (CLAUDE.md, F26).
#include <QColor>
#include <QPointer>
#include <QString>
#include <QWidget>

class ItemsRowWidget : public QWidget {
    Q_OBJECT

public:
    enum class Kind { Body, Outline, Folder };
    ItemsRowWidget(Kind kind, QWidget* parent);

    Kind kind() const { return myKind; }

    // Whether the row wears the selection mark - fill and bar. Never true for
    // an outline row (ruling D4): an outline is not part of the selection the
    // status bar counts, so the list must not show it as one.
    void setMarked(bool marked);
    bool marked() const { return myMarked; }

    // A folder's twisty, as a state: open or closed. Ignored on other kinds.
    void setExpanded(bool expanded);
    bool expanded() const { return myExpanded; }

    // The row the list's keys continue from. A reader hears it as focused
    // while the list itself holds the keyboard.
    void setCurrent(bool current) { myCurrent = current; }
    bool current() const { return myCurrent; }

    // The outline Extrude will consume (ruling D4). It wears NO fill and NO
    // bar - its name says it instead (", waiting to extrude"). THE HOOK FOR
    // DESIGN BATCH C: the outline glyph that batch draws reads this flag to
    // tint itself in accentText; nothing paints off it yet.
    void setPendingOutline(bool pending);
    bool pendingOutline() const { return myPendingOutline; }

    // A child that shows a tooltip although it is transparent for the mouse
    // (the folder row's "2/4" count): Qt never sends such a child a ToolTip
    // event, so the row answers for it over the child's own rectangle.
    void setPassThroughTip(QWidget* child) { myTipChild = child; }
    // What a hover at `pos` (this row's coordinates) shows: the pass-through
    // child's tooltip over that child, the row's own anywhere else.
    QString toolTipAt(const QPoint& pos) const;

    // The bar's width and its ink. accentText(), not accent(), by measured
    // contrast (WCAG 1.4.11, 3:1 for a graphical mark): at the shipped look
    // the accent violet #6a00ff reads 2.3:1 on the card's panel and 1.7:1 on
    // the selection fill, accentText #bb9cff 6.9:1 and 4.8:1. gui_smoke pins
    // both ratios from the tokens and from the painted pixel.
    static constexpr int kBarWidth = 3;
    static QColor barColour();
    // The bar's rectangle in this row's coordinates: the full left edge, less
    // the fill's corner radius top and bottom so it stays inside the fill.
    QRect barRect() const;

    // Registers the row's accessibility factory with Qt. Idempotent; the
    // drawer calls it before it builds its first row, so no row is ever
    // answered by Qt's generic widget interface first and cached that way.
    static void installAccessibility();

protected:
    bool event(QEvent* event) override;
    void paintEvent(class QPaintEvent* event) override;

private:
    void announceState(bool selected, bool expanded);

    Kind myKind;
    bool myMarked = false;
    bool myExpanded = false;
    bool myCurrent = false;
    bool myPendingOutline = false;
    QPointer<QWidget> myTipChild;
};

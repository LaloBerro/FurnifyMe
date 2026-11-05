#pragma once

// AN HTML CARD'S PLACE IN THE OVERLAY (migration Phase 3b). The editor's
// chrome page draws a card - Settings first - but WHERE it stands is still
// ViewportOverlay's call: stacking under the gizmo, stepping the toast around
// it through occupiedRects(), showing and hiding off its action. So the card
// keeps a Qt widget in the overlay: this one - the card's fixed size, painting
// nothing, taking no click (the page's own mask takes the card's presses) -
// and the page is told where it stands through placed().
//
// No WebEngine here: it is an ordinary widget, so it builds and is tested in
// both presets.

#include <QSize>
#include <QString>
#include <QWidget>

class HtmlCardSlot : public QWidget
{
    Q_OBJECT
public:
    // Named `htmlCardSlot_<name>`; sized through Theme::wholeDevicePixels().
    HtmlCardSlot(const QString& name, QSize size, QWidget* viewport);
    QString name() const { return myName; }
    // The page measured its card (Phase 3c: the Versions drawer grows with
    // its list). Rounded through Theme::wholeDevicePixels(); the width stays.
    void setContentHeight(int logicalHeight);
    // The page measured both (Phase 3i: the pill's row is as wide as its menu
    // titles in the page's own font). Emits contentHeightChanged() too - the
    // overlay relays out either way.
    void setContentSize(QSize logicalSize);
    // A slot the overlay may stretch past its natural size (Phase 3i: the
    // rail is the LeftEdge spine, as tall as the viewport in a theme whose
    // rail runs full height). Its natural size stays the minimum and the
    // sizeHint; resize() reaches it, where a fixed size would refuse.
    void setStretchable(bool stretchable);
    // The natural size - the Qt card's, or what the page last measured -
    // never the stretched one, so minimum-size derivations that read
    // sizeHint() read the card, not the window.
    QSize sizeHint() const override { return myNatural; }

signals:
    // Moved, resized, shown or hidden - the page re-reads the rectangle.
    void placed();
    // The height moved: the overlay relays out, so whatever stacks below
    // follows.
    void contentHeightChanged();

protected:
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void applyNatural();

    QString myName;
    QSize myNatural;
    bool myStretchable = false;
};

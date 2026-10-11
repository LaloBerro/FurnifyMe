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

signals:
    // Moved, resized, shown or hidden - the page re-reads the rectangle.
    void placed();

protected:
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QString myName;
};

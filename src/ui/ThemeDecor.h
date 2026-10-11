#pragma once

// A THEME'S DECORATION over the viewport (themes part 2): what a mockup draws
// that belongs to no card - Ultraviolet's registration crosshairs at the
// corners of every card, and its "Grid 100 mm  Snap 10 mm" readout beside the
// Items drawer. One transparent layer the size of the viewport, never hit by
// a click (WA_TransparentForMouseEvents), hidden in every theme that does not
// ask for it - Graphite, which the suites run in, never shows it.
//
// The marks stand at the corners of ViewportOverlay::occupiedRects(), so a
// card added later is marked for free; the readout stands to the right of
// whatever rectangle `anchor` answers (the drawer, or the rail while the
// drawer is shut), and its words come from `readout`.

#include <QString>
#include <QWidget>

#include <functional>

class ViewportOverlay;

class ThemeDecor : public QWidget {
    Q_OBJECT
public:
    ThemeDecor(QWidget* viewport, ViewportOverlay* overlay, std::function<QRect()> anchor,
               std::function<QString()> readout);

    // Re-reads the theme and the readout's words; repaints only on a change.
    void refresh();
    QString readoutText() const { return myReadout; }

protected:
    void paintEvent(class QPaintEvent* event) override;

private:
    ViewportOverlay* myOverlay = nullptr;
    std::function<QRect()> myAnchor;
    std::function<QString()> myReadoutSource;
    QString myReadout;
};

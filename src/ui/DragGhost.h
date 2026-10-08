#pragma once
// WHAT A DRAG CARRIES, following the hand over the viewport (T-063): the
// first carried row's glyph and name, and "+N" for the rest. Parented to
// OcctViewWidget so it follows the pointer outside the drawer, mouse-
// transparent and focus-less.
//
// RASTERISED ONCE PER DRAG, three variants at the threshold - live, refused,
// "Release to cancel" - and paintEvent() only draws a cached pixmap. One
// dirty overlay repaints the whole overlay tree on every frame (CLAUDE.md),
// and no paintEvent may build text or decode an asset. Hidden at rest, so it
// costs the orbit nothing.
#include "IconSet.h"

#include <QPixmap>
#include <QWidget>

class DragGhost : public QWidget {
    Q_OBJECT

public:
    enum class Variant { Live, Refused, Cancel };

    explicit DragGhost(QWidget* viewport);
    void begin(IconSet::Glyph glyph, const QString& name, int extraCount);
    void setVariant(Variant variant);
    Variant variant() const { return myVariant; }
    // `viewportPos` is the pointer in the viewport's coordinates.
    void follow(const QPoint& viewportPos);
    void end();
    QString labelText() const { return myLabel; }
    // How many pixmaps have been built since construction - three per drag.
    int rasterCount() const { return myRasterCount; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap rasterise(Variant variant) const;
    QPixmap myPixmaps[3];
    Variant myVariant = Variant::Live;
    IconSet::Glyph myGlyph = IconSet::Glyph::Body;
    QString myLabel;
    int myRasterCount = 0;
};

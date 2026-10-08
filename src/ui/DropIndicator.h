#pragma once
// WHERE A DRAG WILL LAND, drawn inside the Items drawer's scroll content
// (T-063). One mark at a time: a 2 px insertion line with a small ring at its
// left end ("put it here"), indented to the target parent's depth; or a ring
// stroke around a folder row ("into this folder") with NO fill, so it never
// reads as selection. accentText, or dangerText when the move is refused -
// words-grade inks, because this line is the only cue that says where the
// drop lands and it must read 3:1 on the panel (WCAG 1.4.11).
//
// Mouse-transparent and focus-less: the drag's events belong to the row that
// was pressed, never to this.
#include <QWidget>

class DropIndicator : public QWidget {
    Q_OBJECT

public:
    enum class Shape { None, Line, Ring };
    static constexpr int kLineWidthPx = 2;
    static constexpr int kRingRadiusPx = 3;

    explicit DropIndicator(QWidget* host);
    // `y` is the line's centre and `left`/`right` its extent, in the HOST's
    // coordinates; the ring sits at `left`.
    void showLine(int y, int left, int right, bool refused);
    // `rowRect` in the host's coordinates.
    void showRing(const QRect& rowRect, bool refused);
    void clear();
    Shape shape() const { return myShape; }
    bool refused() const { return myRefused; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Shape myShape = Shape::None;
    bool myRefused = false;
};

#pragma once
// Positions cluster widgets against the edges of the viewport. Not a widget
// itself: the clusters are direct children of the viewport, which is the
// arrangement verified to composite correctly over OCCT's OpenGL surface.
#include <QObject>
#include <QPointer>

#include <vector>

class QWidget;

class ViewportOverlay : public QObject {
    Q_OBJECT

public:
    enum class Anchor { TopLeft, LeftCenter, BottomLeft, TopRight, RightCenter };

    explicit ViewportOverlay(QWidget* viewport);

    void addWidget(QWidget* widget, Anchor anchor);
    void relayout();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Entry {
        QPointer<QWidget> widget;
        Anchor anchor;
    };

    QWidget* myViewport = nullptr;
    std::vector<Entry> myEntries;
};

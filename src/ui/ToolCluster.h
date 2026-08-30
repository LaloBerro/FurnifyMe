#pragma once
// A vertical stack of chips with consistent spacing, sized to its contents -
// or, when it carries a stretch, to whatever height its container gives it.
// The left-edge rail is one of these.
#include <QVector>
#include <QWidget>

class ToolChip;
class QVBoxLayout;

class ToolCluster : public QWidget {
    Q_OBJECT

public:
    explicit ToolCluster(QWidget* parent = nullptr);
    void addChip(ToolChip* chip);

    // A thin border() rule with breathing room either side, dividing the
    // rail's groups. Pure decoration: it is transparent to mouse events, so
    // the gap it occupies clicks through to whatever is behind the cluster
    // exactly as the gaps between chips already do.
    void addSeparator();

    // Pushes everything added after it to the far end of the cluster. A
    // stretch only means anything in a cluster that is allowed to be TALLER
    // than its contents, so this also relaxes the layout's size constraint -
    // the two go together, and separating them would let a caller add a
    // stretch that silently does nothing.
    void addStretch();

    // The chips, in the order they were added. The suite walks this and
    // compares QAction pointers: asserting the rail's contents by their
    // visible text would pass against the right buttons in the wrong order
    // as easily as the wrong buttons, and would break the moment a label
    // changed for reasons that have nothing to do with the rail.
    const QVector<ToolChip*>& chips() const { return myChips; }

protected:
    // The cluster is itself one of Theme's floating surfaces - see the
    // constructor for why that is not optional over the GL viewport.
    void paintEvent(QPaintEvent* event) override;

private:
    QVBoxLayout* myLayout = nullptr;
    QVector<ToolChip*> myChips;
};

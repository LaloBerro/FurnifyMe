#pragma once
// A vertical stack of chips with consistent spacing, sized to its contents.
#include <QWidget>

class ToolChip;
class QVBoxLayout;

class ToolCluster : public QWidget {
    Q_OBJECT

public:
    explicit ToolCluster(QWidget* parent = nullptr);
    void addChip(ToolChip* chip);

private:
    QVBoxLayout* myLayout = nullptr;
};

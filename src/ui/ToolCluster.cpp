#include "ToolCluster.h"

#include "ToolChip.h"

#include <QVBoxLayout>

ToolCluster::ToolCluster(QWidget* parent)
    : QWidget(parent)
{
    // No background of its own: the chips carry the visuals and the gaps
    // between them show the 3D view through.
    setAttribute(Qt::WA_NoSystemBackground);
    myLayout = new QVBoxLayout(this);
    myLayout->setContentsMargins(0, 0, 0, 0);
    myLayout->setSpacing(4);
    myLayout->setSizeConstraint(QLayout::SetFixedSize);
}

void ToolCluster::addChip(ToolChip* chip)
{
    chip->setParent(this);
    myLayout->addWidget(chip);
    adjustSize();
}

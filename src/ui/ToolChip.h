#pragma once
// A labelled button driven entirely by a QAction. It never stores its own
// enabled/checked state: menus, chips and keyboard shortcuts therefore cannot
// drift apart, and MainWindow::updateActions() needs no knowledge of chips.
#include "IconSet.h"

#include <QAbstractButton>

class QAction;

class ToolChip : public QAbstractButton {
    Q_OBJECT

public:
    ToolChip(QAction* action, IconSet::Glyph glyph, QWidget* parent = nullptr);

    QAction* action() const { return myAction; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    // Qt would otherwise flip our checked state locally, before the action has
    // been triggered - the chip must never be the source of truth for it.
    void nextCheckState() override {}

private:
    void syncFromAction();

    QAction* myAction = nullptr;
    QString myShortcut;
    bool myHovered = false;
};

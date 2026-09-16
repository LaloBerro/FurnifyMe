#pragma once

#include <QAbstractButton>
#include <QPointer>

class QAction;

// The small x in a panel's top-right corner, shown only while the pointer is
// over that panel.
//
// It holds NO state of its own: the panel's visibility is derived from a
// checkable QAction (Items, Versions, Joints, Settings), so this button
// triggers that action exactly as the rail chip and the menu entry do, and a
// panel closed from here, from the rail or from the menu all take the same
// path. See CLAUDE.md's "The shell is action-driven".
//
// It parents itself to the panel and watches it: Enter/Leave decide whether
// the button is shown, Resize keeps it in the corner. Moving the pointer from
// the panel ONTO the button sends the panel a Leave, so neither widget may
// decide alone - each asks whether the pointer is over the other before
// hiding, or the button would blink out from under the cursor aimed at it.
class PanelCloseButton : public QAbstractButton {
    Q_OBJECT

public:
    // `panel` is the parent and the widget whose hover shows this button;
    // `action` is the checkable action the panel's visibility is derived from.
    PanelCloseButton(QAction* action, QWidget* panel);

    // The inset from the panel's top-right corner, so a panel that paints its
    // own padding can line the button up with its title row.
    void setCornerInset(int px);

    static int buttonSize();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void replace();
    void syncVisible();

    QPointer<QAction> myAction;
    QWidget* myPanel = nullptr;
    int myInset = 8;
    bool myHovered = false;
};

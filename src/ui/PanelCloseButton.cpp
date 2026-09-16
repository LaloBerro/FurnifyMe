#include "PanelCloseButton.h"

#include "Theme.h"

#include <QAction>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kSize = 20;      // the button's own square
constexpr int kGlyph = 8;      // the x, centred in it
constexpr double kRadius = 5.0;
}  // namespace

int PanelCloseButton::buttonSize() { return kSize; }

PanelCloseButton::PanelCloseButton(QAction* action, QWidget* panel)
    : QAbstractButton(panel), myAction(action), myPanel(panel)
{
    setCursor(Qt::PointingHandCursor);
    // Never a tab stop: this is a pointer-only affordance that is not even on
    // screen unless the pointer is already over the panel.
    setFocusPolicy(Qt::NoFocus);
    // Every interactive control over the GL surface carries it - a press that
    // reached the viewport underneath would re-pick the model behind the card.
    setAttribute(Qt::WA_NoMousePropagation);
    setAttribute(Qt::WA_Hover, true);
    Theme::makeSurfaceTransparent(this);
    resize(kSize, kSize);
    hide();

    if (myAction) {
        setToolTip(tr("Close this panel"));
        connect(this, &QAbstractButton::clicked, this, [this] {
            // trigger(), not setChecked(): the action is the single source of
            // truth and MainWindow::updateActions() is what hides the panel.
            if (myAction && myAction->isChecked()) myAction->trigger();
        });
    }
    if (myPanel) {
        myPanel->installEventFilter(this);
        replace();
    }
}

void PanelCloseButton::setCornerInset(int px)
{
    myInset = px;
    replace();
}

void PanelCloseButton::replace()
{
    if (!myPanel) return;
    move(myPanel->width() - kSize - myInset, myInset);
    raise();
}

void PanelCloseButton::syncVisible()
{
    // Shown while the pointer is over the panel OR over the button itself.
    // Qt sends the panel a Leave when the pointer crosses onto this child, so
    // asking only the panel would hide the button the instant it was aimed at.
    const bool wanted = (myPanel && myPanel->isVisible()) &&
                        (myPanel->underMouse() || underMouse() || myHovered);
    if (wanted == isVisible()) return;
    setVisible(wanted);
    if (wanted) raise();
}

bool PanelCloseButton::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == myPanel) {
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::Leave:
            syncVisible();
            break;
        case QEvent::Resize:
            replace();
            break;
        case QEvent::Show:
            replace();
            syncVisible();
            break;
        case QEvent::Hide:
            hide();
            break;
        default:
            break;
        }
    }
    return QAbstractButton::eventFilter(watched, event);
}

void PanelCloseButton::enterEvent(QEnterEvent* event)
{
    myHovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void PanelCloseButton::leaveEvent(QEvent* event)
{
    myHovered = false;
    update();
    syncVisible();
    QAbstractButton::leaveEvent(event);
}

void PanelCloseButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // A quiet square that only fills once the pointer is on it - the panel
    // underneath is already a card, so an always-filled chip would read as a
    // second control rather than as a dismissal.
    if (myHovered || isDown()) {
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), kRadius, kRadius);
        painter.fillPath(path, isDown() ? Theme::chipActive() : Theme::chipHover());
    }

    QPen pen(myHovered ? Theme::text() : Theme::textMuted());
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    const QPointF c = QRectF(rect()).center();
    const double h = kGlyph / 2.0;
    painter.drawLine(QPointF(c.x() - h, c.y() - h), QPointF(c.x() + h, c.y() + h));
    painter.drawLine(QPointF(c.x() + h, c.y() - h), QPointF(c.x() - h, c.y() + h));
}

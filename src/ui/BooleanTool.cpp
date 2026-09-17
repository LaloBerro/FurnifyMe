#include "BooleanTool.h"

#include "../MainWindow.h"
#include "../OcctViewWidget.h"
#include "Theme.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QShowEvent>

#include <algorithm>

namespace {
constexpr int kPad = 14;
constexpr int kGap = 14;
constexpr int kRowHeight = 22;
constexpr int kCardRadius = 12;
// The same fixed spot SlatsTool uses, for the same reason: a boolean covers
// two or more whole bodies, so a chip anchored to one of them would sit ON
// the work it is showing you - and the region is the thing that has to stay
// visible.
constexpr int kBottomMargin = 24;

QFont titleFontBold()
{
    QFont f = Theme::bodyFont();
    f.setBold(true);
    return f;
}
}  // namespace

BooleanTool::BooleanTool(MainWindow* window, OcctViewWidget* view)
    : QWidget(view)
    , myWindow(window)
    , myView(view)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // Every click on this card is its own; none reaches the viewport behind
    // it, which would otherwise re-pick on the release and end the very
    // gesture this card is running.
    setAttribute(Qt::WA_NoMousePropagation);
    Theme::makeSurfaceTransparent(this);

    auto* keep = new QCheckBox(tr("Keep the other bodies"), this);
    keep->setCursor(Qt::PointingHandCursor);
    // No focus: a checkbox that takes focus would swallow the digits and the
    // arrow keys the viewport wants, and this card claims its two keys
    // application-wide anyway.
    keep->setFocusPolicy(Qt::NoFocus);
    keep->setToolTip(tr("Leave the bodies this works with in the document instead of "
                        "using them up"));
    connect(keep, &QCheckBox::toggled, this, [this](bool on) {
        if (myWindow) myWindow->setBooleanKeepTool(on);
    });
    myKeepTool = keep;

    applyStyles();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyStyles();
        layoutCard();
        update();
    });
    hide();
}

BooleanTool::~BooleanTool()
{
    if (myClaimInstalled && QCoreApplication::instance())
        QCoreApplication::instance()->removeEventFilter(this);
}

QAbstractButton* BooleanTool::keepToolBox() const
{
    return myKeepTool;
}

void BooleanTool::refresh()
{
    // DERIVED, never stored: one read of the window's own predicate decides
    // whether this card exists on screen at all.
    const bool wanted = myWindow && myWindow->booleanActive();
    if (!wanted) {
        if (!isHidden()) hide();
        return;
    }

    myTitle = titleText();
    myHint = hintText();
    syncControls();
    layoutCard();
    replace();
    if (isHidden()) show();
    raise();
    update();
}

void BooleanTool::syncControls()
{
    if (!myKeepTool || !myWindow) return;
    // Blocked: pushing the window's own state onto the control is not an
    // edit, and reporting it would write the value straight back to where it
    // came from.
    myKeepTool->blockSignals(true);
    myKeepTool->setChecked(myWindow->booleanKeepTool());
    myKeepTool->blockSignals(false);
}

void BooleanTool::applyStyles()
{
    if (myKeepTool) {
        myKeepTool->setFont(Theme::labelFont());
        myKeepTool->setStyleSheet(QStringLiteral("QCheckBox { background: transparent; "
                                                 "color: %1; } "
                                                 "QCheckBox::indicator { width: 14px; "
                                                 "height: 14px; }")
                                      .arg(Theme::text().name()));
    }
}

void BooleanTool::layoutCard()
{
    const QFontMetrics title(titleFontBold());
    const QFontMetrics label(Theme::labelFont());
    const int titleWidth = title.horizontalAdvance(myTitle);
    const int hintWidth = label.horizontalAdvance(myHint);
    const int keepWidth = myKeepTool ? myKeepTool->sizeHint().width() : 0;

    const int width = kPad * 2 + titleWidth + kGap + keepWidth + kGap + hintWidth;
    const int height = kPad * 2 + std::max({title.height(), label.height(), kRowHeight});
    // Whole DEVICE pixels: a card whose logical size does not cover whole
    // device rows leaves one row its own painter cannot reach (Theme's rule).
    setFixedSize(Theme::wholeDevicePixels(QSize(width, height)));

    if (myKeepTool) {
        const int y = (this->height() - kRowHeight) / 2;
        myKeepTool->setGeometry(kPad + titleWidth + kGap, y, keepWidth, kRowHeight);
    }
}

void BooleanTool::replace()
{
    if (!myView || isHidden()) return;
    const int x = (myView->width() - width()) / 2;
    const int y = myView->height() - height() - kBottomMargin;
    const QPoint origin = myView->mapTo(myView->window(), QPoint(0, 0));
    const double dpr = myView->devicePixelRatioF();
    move(Theme::snapToDevicePixels(std::max(0, x), origin.x(), dpr),
         Theme::snapToDevicePixels(std::max(0, y), origin.y(), dpr));
    raise();
}

QString BooleanTool::titleText() const
{
    return myWindow ? myWindow->booleanOperationName() : QString();
}

QString BooleanTool::hintText() const
{
    // Both verbs named in painted text. A modeless card with invisible keys
    // is how ExtrudePreview's own pair went unnoticed for a whole branch.
    return tr("Enter applies — Esc cancels");
}

QStringList BooleanTool::paintedTexts() const
{
    QStringList texts;
    texts << myTitle << myHint;
    if (myKeepTool) texts << myKeepTool->text() << myKeepTool->toolTip();
    return texts;
}

void BooleanTool::cancel()
{
    if (myWindow) myWindow->cancelBoolean();
}

void BooleanTool::commit()
{
    if (myWindow) myWindow->booleanApply();
}

void BooleanTool::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    Theme::paintSurface(painter, rect(), kCardRadius);

    const QFontMetrics title(titleFontBold());
    painter.setFont(titleFontBold());
    painter.setPen(Theme::text());
    painter.drawText(QRect(kPad, 0, title.horizontalAdvance(myTitle), height()),
                     Qt::AlignVCenter | Qt::AlignLeft, myTitle);

    const QFontMetrics label(Theme::labelFont());
    painter.setFont(Theme::labelFont());
    painter.setPen(Theme::textMuted());
    painter.drawText(QRect(width() - kPad - label.horizontalAdvance(myHint), 0,
                           label.horizontalAdvance(myHint), height()),
                     Qt::AlignVCenter | Qt::AlignRight, myHint);
}

void BooleanTool::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!myClaimInstalled) {
        QCoreApplication::instance()->installEventFilter(this);
        myClaimInstalled = true;
    }
}

void BooleanTool::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myClaimInstalled) {
        QCoreApplication::instance()->removeEventFilter(this);
        myClaimInstalled = false;
    }
}

bool BooleanTool::eventFilter(QObject* watched, QEvent* event)
{
    if (isHidden()) return QWidget::eventFilter(watched, event);

    const bool isKey = event->type() == QEvent::KeyPress ||
                       event->type() == QEvent::ShortcutOverride;
    if (!isKey) return QWidget::eventFilter(watched, event);

    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != window()) return QWidget::eventFilter(watched, event);

    auto* key = static_cast<QKeyEvent*>(event);
    if (key->modifiers() != Qt::NoModifier && key->modifiers() != Qt::KeypadModifier)
        return QWidget::eventFilter(watched, event);

    const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
    const bool escape = key->key() == Qt::Key_Escape;
    if (!enter && !escape) return QWidget::eventFilter(watched, event);

    // The ShortcutOverride claim, taken BEFORE QShortcutMap can: an accepted
    // override says "deliver this as an ordinary key press to me", and
    // without it an action bound to the same key takes it first.
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (enter) commit();
    else cancel();
    return true;
}

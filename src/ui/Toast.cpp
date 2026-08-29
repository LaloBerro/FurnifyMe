#include "Toast.h"

#include "OcctViewWidget.h"
#include "Theme.h"

#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <utility>

namespace {
constexpr int kPad = 14;
constexpr int kWidth = 360;
constexpr int kUndoWidth = 64;
constexpr int kUndoHeight = 26;
constexpr int kBottomMargin = 24;

constexpr int kNoteMs = 4000;
constexpr int kFailureMs = 8000;

// The one interactive spot on an otherwise click-through toast. Follows
// WalkthroughPanel's SkipControl exactly (see WalkthroughPanel.cpp for the
// full reasoning, verified there against this machine's Qt 6.11.1): this is
// a SIBLING of Toast, parented to the same viewport, never a child - a child
// would be just as unreachable by a real click as the transparent toast body
// itself, since Qt::WA_TransparentForMouseEvents excludes a widget's entire
// subtree from hit-testing, not just the widget carrying it.
class UndoControl : public QWidget {
public:
    UndoControl(std::function<void()> onClick, QWidget* parent)
        : QWidget(parent)
        , myOnClick(std::move(onClick))
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        // Closes the same class of bug documented on HintBalloon: an
        // unhandled release would otherwise propagate to the viewport behind
        // this control and trigger a real pick underneath the toast.
        setAttribute(Qt::WA_NoMousePropagation);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        event->accept();
        if (myOnClick) myOnClick();
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath bg;
        bg.addRoundedRect(rect().adjusted(0, 0, -1, -1), 5.0, 5.0);
        painter.fillPath(bg, Theme::chipHover());
        painter.setPen(Theme::accent());
        painter.drawText(rect(), Qt::AlignCenter, QObject::tr("Undo"));
    }

private:
    std::function<void()> myOnClick;
};

}  // namespace

Toast::Toast(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // The body must never eat a click meant for the model behind it - only
    // the sibling Undo control (see UndoControl above) is ever clickable.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    myUndo = new UndoControl([this] { emit undoClicked(); }, parent);
    myUndo->hide();
    hide();
}

Toast::~Toast()
{
    // Sibling, not a child - Qt's parent-child cascade does not clean it up
    // when this widget alone is destroyed, and it holds a raw `this` via its
    // click callback. QPointer makes the delete a safe no-op if the two are
    // instead torn down together by their shared parent, in either order -
    // see WalkthroughPanel::~WalkthroughPanel() for the same reasoning.
    delete myUndo;
}

void Toast::setMessage(const QString& text, Kind kind, bool undo)
{
    myText = text;
    myKind = kind;
    myHasUndo = undo;
    syncUndoGeometry();
    update();
}

QSize Toast::sizeHint() const
{
    const int textWidth = kWidth - kPad * 2 - (myHasUndo ? kUndoWidth + kPad : 0);
    const QFontMetrics metrics(font());
    const QRect bounds = metrics.boundingRect(QRect(0, 0, std::max(textWidth, 1), 1000),
                                              Qt::TextWordWrap, myText);
    const int minHeight = myHasUndo ? kUndoHeight + kPad * 2 : 0;
    return QSize(kWidth, std::max(bounds.height() + kPad * 2, minHeight));
}

QRect Toast::undoRect() const
{
    return QRect(width() - kPad - kUndoWidth, (height() - kUndoHeight) / 2,
                kUndoWidth, kUndoHeight);
}

void Toast::syncUndoGeometry()
{
    if (!myUndo) return;
    // mySkip's counterpart: shares this widget's parent, so undoRect() -
    // defined in this widget's own local coordinates - needs translating by
    // pos() to land in that shared coordinate space.
    myUndo->setGeometry(undoRect().translated(pos()));
    // Visibility is DERIVED here, not left to a hide event that may never
    // arrive - see WalkthroughPanel::syncSkipGeometry() for why that matters:
    // hide() on a widget that was never shown delivers no QHideEvent at all.
    myUndo->setVisible(isVisible() && myHasUndo);
    myUndo->raise();
}

void Toast::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncUndoGeometry();
}

void Toast::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myUndo) myUndo->hide();
}

void Toast::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    syncUndoGeometry();
}

void Toast::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncUndoGeometry();
}

void Toast::paintEvent(QPaintEvent* /*event*/)
{
    if (myText.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath panel;
    panel.addRoundedRect(rect().adjusted(0, 0, -1, -1), 8.0, 8.0);
    painter.fillPath(panel, Theme::panel());
    painter.setPen(QPen(myKind == Kind::Failure ? Theme::textMuted() : Theme::accent(), 1.0));
    painter.drawPath(panel);

    const int textWidth = width() - kPad * 2 - (myHasUndo ? kUndoWidth + kPad : 0);
    painter.setPen(Theme::text());
    painter.drawText(QRect(kPad, 0, textWidth, height()),
                     Qt::TextWordWrap | Qt::AlignVCenter | Qt::AlignLeft, myText);
}

QStringList Toast::paintedTexts() const
{
    QStringList texts{ QObject::tr("Undo") };
    if (!myText.isEmpty()) texts << myText;
    return texts;
}

ToastHost::ToastHost(OcctViewWidget* viewport, QWidget* parent)
    : QObject(parent)
    , myViewport(viewport)
{
    myToast = new Toast(viewport);

    connect(myToast, &Toast::undoClicked, this, [this] {
        emit undoRequested();
        dismiss();
    });

    myTimer = new QTimer(this);
    myTimer->setSingleShot(true);
    connect(myTimer, &QTimer::timeout, this, &ToastHost::dismiss);

    // Repositions on a viewport resize, the same reason HintBalloon installs
    // this filter on its own parent - a toast that appeared before a resize
    // would otherwise sit stranded wherever the old viewport bounds put it.
    if (myViewport) myViewport->installEventFilter(this);
}

void ToastHost::show(const QString& text, Toast::Kind kind, bool undo)
{
    myToast->setMessage(text, kind, undo);
    reposition();
    myToast->show();
    myToast->raise();
    if (myToast->undoControl()) myToast->undoControl()->raise();
    myTimer->start(kind == Toast::Kind::Failure ? kFailureMs : kNoteMs);
}

QString ToastHost::currentText() const
{
    return (myToast && myToast->isVisible()) ? myToast->text() : QString();
}

bool ToastHost::isShowing() const
{
    return myToast && myToast->isVisible();
}

QWidget* ToastHost::undoControl() const
{
    return myToast ? myToast->undoControl() : nullptr;
}

void ToastHost::dismiss()
{
    myTimer->stop();
    if (myToast) myToast->hide();
}

void ToastHost::reposition()
{
    if (!myViewport || !myToast) return;
    myToast->resize(myToast->sizeHint());
    const int x = (myViewport->width() - myToast->width()) / 2;
    const int y = myViewport->height() - myToast->height() - kBottomMargin;
    myToast->move(x, y);
}

bool ToastHost::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == myViewport && event->type() == QEvent::Resize &&
        myToast && myToast->isVisible()) {
        reposition();
    }
    return QObject::eventFilter(watched, event);
}

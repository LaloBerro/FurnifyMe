#include "ShortcutSheet.h"

#include "Theme.h"

#include <QAction>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#include <algorithm>

namespace {
constexpr int kPadding = 24;
constexpr int kRowHeight = 26;
constexpr int kTitleHeight = 44;
constexpr int kColumnGap = 48;
}  // namespace

ShortcutSheet::ShortcutSheet(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    hide();
}

void ShortcutSheet::rebuild()
{
    myRows.clear();
    if (!parentWidget()) return;

    for (QAction* candidate : parentWidget()->findChildren<QAction*>()) {
        if (candidate->shortcut().isEmpty()) continue;
        Row row;
        row.label = candidate->text().remove(QLatin1Char('&'));
        row.keys = candidate->shortcut().toString(QKeySequence::NativeText);
        myRows.push_back(row);
    }
    std::sort(myRows.begin(), myRows.end(),
              [](const Row& a, const Row& b) { return a.label < b.label; });
}

void ShortcutSheet::showSheet()
{
    rebuild();

    const QFontMetrics metrics(font());
    int widest = 0;
    for (const Row& row : myRows) {
        widest = std::max(widest, metrics.horizontalAdvance(row.label) +
                                      metrics.horizontalAdvance(row.keys));
    }
    const int width = widest + kColumnGap + kPadding * 2;
    const int height = kTitleHeight + kRowHeight * rowCount() + kPadding;

    if (parentWidget()) {
        move((parentWidget()->width() - width) / 2,
             (parentWidget()->height() - height) / 2);
    }
    resize(width, height);
    show();
    raise();
    setFocus();
}

bool ShortcutSheet::event(QEvent* event)
{
    // QShortcutMap resolves an enabled window-context shortcut (Cancel Sketch
    // is bound to Escape too) before a key press ever reaches the focused
    // widget. ShortcutOverride is the mechanism Qt gives a widget to claim a
    // key back from the map: accepting it here routes Escape to
    // keyPressEvent below instead of letting the sketch get cancelled out
    // from under the sheet. Every other key falls through untouched, so this
    // is not a blanket grab.
    if (event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (isVisible() && keyEvent->key() == Qt::Key_Escape &&
            keyEvent->modifiers() == Qt::NoModifier) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void ShortcutSheet::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath panel;
    panel.addRoundedRect(rect().adjusted(0, 0, -1, -1), 10.0, 10.0);
    painter.fillPath(panel, Theme::panel());
    painter.setPen(QPen(Theme::border(), 1.0));
    painter.drawPath(panel);

    QFont titleFont = font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(Theme::text());
    painter.drawText(QRect(kPadding, 0, width() - kPadding * 2, kTitleHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, tr("Keyboard shortcuts"));

    painter.setFont(font());
    int y = kTitleHeight;
    for (const Row& row : myRows) {
        const QRect line(kPadding, y, width() - kPadding * 2, kRowHeight);
        painter.setPen(Theme::text());
        painter.drawText(line, Qt::AlignVCenter | Qt::AlignLeft, row.label);
        painter.setPen(Theme::textMuted());
        painter.drawText(line, Qt::AlignVCenter | Qt::AlignRight, row.keys);
        y += kRowHeight;
    }
}

void ShortcutSheet::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ShortcutSheet::mousePressEvent(QMouseEvent* /*event*/)
{
    hide();
}

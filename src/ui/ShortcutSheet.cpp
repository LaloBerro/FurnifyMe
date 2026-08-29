#include "ShortcutSheet.h"

#include "Theme.h"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#include <algorithm>

namespace {
constexpr int kPadding = 24;
constexpr int kRowHeight = 26;
constexpr int kTitleHeight = 44;
constexpr int kGroupHeight = 30;
constexpr int kColumnGap = 48;

QString plainText(const QAction* action)
{
    return QString(action->text()).remove(QLatin1Char('&'));
}

// Every binding an action carries, not just the first: Keyboard Shortcuts is
// on both ? and F1, and a sheet that named only one of them would be the same
// half-truth as a hand-written list.
QString keysFor(const QAction* action)
{
    QStringList keys;
    for (const QKeySequence& sequence : action->shortcuts()) {
        if (!sequence.isEmpty()) keys << sequence.toString(QKeySequence::NativeText);
    }
    return keys.join(QStringLiteral(", "));
}
}  // namespace

ShortcutSheet::ShortcutSheet(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // A click inside the sheet must not reach whatever is behind it either -
    // the same reasoning as HintBalloon's, and cheaper than enumerating
    // handlers as bugs turn up.
    setAttribute(Qt::WA_NoMousePropagation);
    setFocusPolicy(Qt::StrongFocus);
    // A sheet centred on the viewport at the moment it opened is off-centre
    // the moment the window is resized. HintBalloon solved exactly this with
    // a filter on its parent; this is the same fix.
    if (parent) parent->installEventFilter(this);
    hide();
}

std::vector<ShortcutSheet::Group> ShortcutSheet::buildGroups() const
{
    std::vector<Group> groups;
    if (!parentWidget()) return groups;

    // Track what the menu walk covered, so the sweep below can catch anything
    // bound outside a menu rather than silently dropping it.
    std::vector<const QAction*> seen;

    if (QMenuBar* bar = parentWidget()->findChild<QMenuBar*>()) {
        for (QAction* top : bar->actions()) {
            QMenu* menu = top->menu();
            if (!menu) continue;
            Group group;
            group.title = plainText(top);
            for (QAction* candidate : menu->actions()) {
                if (candidate->isSeparator() || candidate->shortcut().isEmpty()) continue;
                group.rows.push_back(Row{plainText(candidate), keysFor(candidate)});
                seen.push_back(candidate);
            }
            if (!group.rows.empty()) groups.push_back(std::move(group));
        }
    }

    Group elsewhere;
    elsewhere.title = tr("Other");
    for (QAction* candidate : parentWidget()->findChildren<QAction*>()) {
        if (candidate->shortcut().isEmpty()) continue;
        if (std::find(seen.begin(), seen.end(), candidate) != seen.end()) continue;
        elsewhere.rows.push_back(Row{plainText(candidate), keysFor(candidate)});
    }
    if (!elsewhere.rows.empty()) groups.push_back(std::move(elsewhere));

    return groups;
}

void ShortcutSheet::rebuild()
{
    myGroups = buildGroups();
}

int ShortcutSheet::rowCount() const
{
    int total = 0;
    for (const Group& group : myGroups) total += static_cast<int>(group.rows.size());
    return total;
}

QStringList ShortcutSheet::paintedTexts() const
{
    QStringList texts{tr("Keyboard shortcuts")};
    for (const Group& group : buildGroups()) {
        texts << group.title;
        for (const Row& row : group.rows) texts << row.label << row.keys;
    }
    return texts;
}

void ShortcutSheet::recentre()
{
    if (!parentWidget()) return;
    move((parentWidget()->width() - width()) / 2,
         (parentWidget()->height() - height()) / 2);
}

void ShortcutSheet::showSheet()
{
    rebuild();

    const QFontMetrics metrics(font());
    int widest = 0;
    for (const Group& group : myGroups) {
        widest = std::max(widest, metrics.horizontalAdvance(group.title));
        for (const Row& row : group.rows) {
            widest = std::max(widest, metrics.horizontalAdvance(row.label) +
                                          metrics.horizontalAdvance(row.keys));
        }
    }
    const int width = widest + kColumnGap + kPadding * 2;
    int height = kTitleHeight + kPadding;
    for (const Group& group : myGroups) {
        height += kGroupHeight + kRowHeight * static_cast<int>(group.rows.size());
    }

    resize(width, height);
    recentre();
    show();
    raise();
    setFocus();
    mySwallowRelease = false;
    // Only while the sheet is up: a filter on the whole application is how a
    // click landing on any other widget in this window can be seen at all -
    // the press goes straight to the viewport, never through this widget or
    // its parent.
    QCoreApplication::instance()->installEventFilter(this);
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

bool ShortcutSheet::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        recentre();
    }

    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget && widget->window() == window()) {
        // The press that dismissed the sheet was swallowed; its release has
        // to go too, or OcctViewWidget::mouseReleaseEvent performs a real
        // pick on the way out - the sheet would look modal and still act as
        // a hole punched through to the model behind it.
        // A release that never lands in this window - the drag left it, or
        // another window took the gesture - would otherwise leave the filter
        // armed and eat some later, innocent release. The next press proves
        // the gesture is over, so stand down and let it through untouched.
        if (event->type() == QEvent::MouseButtonPress && mySwallowRelease &&
            !isVisible()) {
            mySwallowRelease = false;
            QCoreApplication::instance()->removeEventFilter(this);
            return QWidget::eventFilter(watched, event);
        }
        if (event->type() == QEvent::MouseButtonRelease && mySwallowRelease) {
            mySwallowRelease = false;
            // The sheet is already hidden by now, so hideEvent() deliberately
            // left this filter installed for exactly this event; take it down
            // here instead.
            if (!isVisible()) QCoreApplication::instance()->removeEventFilter(this);
            return true;
        }
        if (isVisible() && (event->type() == QEvent::MouseButtonPress ||
                            event->type() == QEvent::MouseButtonDblClick)) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint local = mapFromGlobal(mouse->globalPosition().toPoint());
            if (!rect().contains(local)) {
                mySwallowRelease = true;
                hide();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
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

    QFont groupFont = font();
    groupFont.setBold(true);

    int y = kTitleHeight;
    for (const Group& group : myGroups) {
        painter.setFont(groupFont);
        painter.setPen(Theme::accent());
        painter.drawText(QRect(kPadding, y, width() - kPadding * 2, kGroupHeight),
                         Qt::AlignBottom | Qt::AlignLeft, group.title);
        y += kGroupHeight;

        painter.setFont(font());
        for (const Row& row : group.rows) {
            const QRect line(kPadding, y, width() - kPadding * 2, kRowHeight);
            painter.setPen(Theme::text());
            painter.drawText(line, Qt::AlignVCenter | Qt::AlignLeft, row.label);
            painter.setPen(Theme::textMuted());
            painter.drawText(line, Qt::AlignVCenter | Qt::AlignRight, row.keys);
            y += kRowHeight;
        }
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

void ShortcutSheet::mousePressEvent(QMouseEvent* event)
{
    // A click inside the sheet is not a dismissal - the spec dismisses on Esc
    // or a click OUTSIDE, and reading a list you have to keep still to read
    // should not close it. Accepting it, together with WA_NoMousePropagation,
    // is what keeps it off the viewport behind.
    event->accept();
}

void ShortcutSheet::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // Nothing to watch the whole application for while the sheet is down -
    // unless this hide was caused by a click outside, whose release has not
    // arrived yet. Removing the filter here would let that release through to
    // the viewport, which picks on it: the sheet would close and select
    // something behind it in the same gesture. eventFilter() takes the filter
    // down once it has swallowed that release.
    if (!mySwallowRelease) QCoreApplication::instance()->removeEventFilter(this);
}

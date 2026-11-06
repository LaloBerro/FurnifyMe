#include "ShortcutSheet.h"

#include <limits>

#include "Theme.h"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

#include <algorithm>

namespace {
constexpr int kPadding = 24;
constexpr int kRowHeight = 26;
constexpr int kTitleHeight = 44;
constexpr int kGroupHeight = 30;
constexpr int kColumnGap = 48;
// What the sheet keeps clear of its window's edges when it has to fit.
constexpr int kWindowMargin = 16;

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

// The one place a group header's font is built - showSheet() measures with
// it, paintEvent() paints with it, and there is exactly one bold-bodyFont()
// call for both to agree with instead of two copies that happen to match.
QFont groupFont()
{
    QFont f = Theme::bodyFont();
    f.setBold(true);
    return f;
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
    // See Theme::makeSurfaceTransparent()'s own comment. This sheet was never
    // given a window mask the way the rest of the paintSurface() family was
    // (Milestone 5 item 2 missed it - it is parented to MainWindow itself
    // rather than to the viewport, and its corners went unaudited), so this
    // is its first genuinely transparent, antialiased corner.
    Theme::makeSurfaceTransparent(this);
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

    {
        for (QMenu* menu : myMenus) {
            if (!menu) continue;
            Group group;
            group.title = plainText(menu->menuAction());
            for (QAction* candidate : menu->actions()) {
                if (candidate->isSeparator() || candidate->shortcut().isEmpty()) continue;
                group.rows.push_back(Row{plainText(candidate), keysFor(candidate)});
                seen.push_back(candidate);
            }
            if (!group.rows.empty()) groups.push_back(std::move(group));
        }
    }

    // The widgets' own key tables, after the menus and before the catch-all.
    for (const auto& [title, rows] : myKeyTables) {
        Group group;
        group.title = title;
        for (const auto& [label, keys] : rows()) group.rows.push_back(Row{label, keys});
        if (!group.rows.empty()) groups.push_back(std::move(group));
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

void ShortcutSheet::addKeyTable(const QString& title, std::function<KeyRows()> rows)
{
    if (rows) myKeyTables.emplace_back(title, std::move(rows));
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

    // Measured with the same fonts paintEvent() actually draws with below -
    // group titles at bold bodyFont() (bold is wider than regular, so
    // measuring with the plain font here would clip a long title), row
    // labels at plain bodyFont(), the key badges at badgeFont() - so the
    // sizing this computes can never drift from what ends up on screen.
    const QFontMetrics groupMetrics(groupFont());
    const QFontMetrics bodyMetrics(Theme::bodyFont());
    const QFontMetrics badgeMetrics(Theme::badgeFont());
    int widest = 0;
    for (const Group& group : myGroups) {
        widest = std::max(widest, groupMetrics.horizontalAdvance(group.title));
        for (const Row& row : group.rows) {
            widest = std::max(widest, bodyMetrics.horizontalAdvance(row.label) +
                                          badgeMetrics.horizontalAdvance(row.keys));
        }
    }
    // IN COLUMNS WHEN ONE WOULD NOT FIT (T-064): one column grew past 1500 px,
    // taller than a 1080-row screen. Groups flow down a column and on to the
    // next once the window's height is reached - a group is never split, so a
    // heading always stands over its own rows.
    myColumnWidth = widest + kColumnGap;
    const int room = parentWidget() ? parentWidget()->height() - 2 * kWindowMargin - kTitleHeight - kPadding
                                    : std::numeric_limits<int>::max();
    myPlaces.clear();
    myColumns = 1;
    int columnHeight = 0;
    int tallest = 0;
    for (const Group& group : myGroups) {
        const int groupHeight = kGroupHeight + kRowHeight * static_cast<int>(group.rows.size());
        if (columnHeight > 0 && columnHeight + groupHeight > room) {
            ++myColumns;
            columnHeight = 0;
        }
        myPlaces.emplace_back((myColumns - 1) * (myColumnWidth + kColumnGap), columnHeight);
        columnHeight += groupHeight;
        tallest = std::max(tallest, columnHeight);
    }
    const int width = kPadding * 2 + myColumns * myColumnWidth + (myColumns - 1) * kColumnGap;
    const int height = kTitleHeight + kPadding + tallest;
    myContentExtent = QSize(width, height);

    // Grown by Theme::surfaceShadowMargin() per side beyond the content size
    // computed above. That margin is zero - the family paints no shadow and
    // reserves no room for one (see Theme.h) - so this sheet's widget rect
    // and its painted card are the same rectangle, and paintEvent() applies
    // the same zero on the inside. No sibling control depends on this
    // widget's geometry.
    const int margin = Theme::surfaceShadowMargin();
    resize(width + margin * 2, height + margin * 2);
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
    emit sheetChanged();
}

void ShortcutSheet::setDrawnElsewhere(bool elsewhere)
{
    if (myDrawnElsewhere == elsewhere) return;
    myDrawnElsewhere = elsewhere;
    update();
}

QVariantMap ShortcutSheet::words() const
{
    if (!isVisible()) return {{QStringLiteral("shown"), false}};
    QVariantList groups;
    for (std::size_t g = 0; g < myGroups.size(); ++g) {
        QVariantList rows;
        for (const Row& row : myGroups[g].rows)
            rows.push_back(QVariantMap{{QStringLiteral("label"), row.label}, {QStringLiteral("keys"), row.keys}});
        const QPoint at = g < myPlaces.size() ? myPlaces[g] : QPoint();
        groups.push_back(QVariantMap{{QStringLiteral("title"), myGroups[g].title},
                                     {QStringLiteral("x"), at.x()},
                                     {QStringLiteral("y"), at.y()},
                                     {QStringLiteral("rows"), rows}});
    }
    const int margin = Theme::surfaceShadowMargin();
    return {{QStringLiteral("shown"), true},
            {QStringLiteral("title"), tr("Keyboard shortcuts")},
            {QStringLiteral("width"), width() - margin * 2},
            {QStringLiteral("height"), height() - margin * 2},
            {QStringLiteral("padding"), kPadding},
            {QStringLiteral("titleHeight"), kTitleHeight},
            {QStringLiteral("groupHeight"), kGroupHeight},
            {QStringLiteral("rowHeight"), kRowHeight},
            {QStringLiteral("columnWidth"), myColumnWidth},
            {QStringLiteral("groups"), groups}};
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
        emit sheetChanged();
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
    if (myDrawnElsewhere) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // `body` is the visible card, inset from this widget's own bounds by
    // Theme::surfaceShadowMargin() - see showSheet() for the growth.
    const int margin = Theme::surfaceShadowMargin();
    const QRect body = rect().adjusted(margin, margin, -margin, -margin);
    Theme::paintSurface(painter, body, 10);

    painter.setFont(Theme::titleFont());
    painter.setPen(Theme::text());
    painter.drawText(QRect(body.left() + kPadding, body.top(), body.width() - kPadding * 2, kTitleHeight),
                     Qt::AlignVCenter | Qt::AlignLeft, tr("Keyboard shortcuts"));

    for (std::size_t g = 0; g < myGroups.size(); ++g) {
        const Group& group = myGroups[g];
        // Where showSheet() stood this group: its column, its depth in it.
        const QPoint at = g < myPlaces.size() ? myPlaces[g] : QPoint();
        const int x = body.left() + kPadding + at.x();
        int y = body.top() + kTitleHeight + at.y();
        painter.setFont(groupFont());
        painter.setPen(Theme::accentText());
        painter.drawText(QRect(x, y, myColumnWidth, kGroupHeight), Qt::AlignBottom | Qt::AlignLeft, group.title);
        y += kGroupHeight;

        for (const Row& row : group.rows) {
            const QRect line(x, y, myColumnWidth, kRowHeight);
            painter.setFont(Theme::bodyFont());
            painter.setPen(Theme::text());
            painter.drawText(line, Qt::AlignVCenter | Qt::AlignLeft, row.label);
            // The key combination is a badge, not body text - Theme::badgeFont().
            painter.setFont(Theme::badgeFont());
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
    emit sheetChanged();
}

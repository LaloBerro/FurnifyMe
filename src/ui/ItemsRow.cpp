#include "ItemsRow.h"

#include "Theme.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QHelpEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QToolTip>

#include <algorithm>

namespace {

// The selection fill's corner radius - ItemsPanel::restyleRows()'s
// "border-radius: 4px". The bar stops this far from the row's top and bottom
// so it sits on the fill's straight edge, never across its rounded corners.
constexpr int kFillRadius = 4;

// The list that holds the keys for this row: the nearest ancestor that takes
// focus at all (the drawer, Qt::ClickFocus - the scroll area between is
// NoFocus by ruling R12b). Never the window, which would read as focused
// whenever nothing in particular is.
const QWidget* keyHolderOf(const QWidget* row)
{
    for (const QWidget* up = row->parentWidget(); up && !up->isWindow(); up = up->parentWidget())
        if (up->focusPolicy() != Qt::NoFocus) return up;
    return nullptr;
}

// What a screen reader is told a row IS. A tree item: rows nest in folders
// and a folder opens and closes, which is exactly a tree's vocabulary - a
// list item has no Expanded state to say it with.
class ItemsRowAccessible : public QAccessibleWidget {
public:
    explicit ItemsRowAccessible(ItemsRowWidget* row)
        : QAccessibleWidget(row, QAccessible::TreeItem)
    {
    }

    QAccessible::State state() const override
    {
        QAccessible::State s = QAccessibleWidget::state();
        const auto* row = static_cast<const ItemsRowWidget*>(widget());
        s.selectable = true;
        s.selected = row->marked();
        s.focusable = true;
        if (row->current()) {
            const QWidget* holder = keyHolderOf(row);
            const QWidget* top = row->window();
            s.focused = holder && top && top->focusWidget() == holder;
        }
        if (row->kind() == ItemsRowWidget::Kind::Folder) {
            s.expandable = true;
            s.expanded = row->expanded();
            s.collapsed = !row->expanded();
        }
        return s;
    }
};

QAccessibleInterface* itemsRowFactory(const QString& className, QObject* object)
{
    if (object && object->isWidgetType() && className == QLatin1String("ItemsRowWidget"))
        return new ItemsRowAccessible(static_cast<ItemsRowWidget*>(object));
    return nullptr;
}

}  // namespace

ItemsRowWidget::ItemsRowWidget(Kind kind, QWidget* parent) : QWidget(parent), myKind(kind) {}

void ItemsRowWidget::installAccessibility()
{
    static bool installed = false;
    if (installed) return;
    installed = true;
    QAccessible::installFactory(&itemsRowFactory);
}

QColor ItemsRowWidget::barColour() { return Theme::accentText(); }

QRect ItemsRowWidget::barRect() const
{
    return QRect(0, kFillRadius, kBarWidth, std::max(0, height() - 2 * kFillRadius));
}

void ItemsRowWidget::setMarked(bool marked)
{
    if (myMarked == marked) return;
    myMarked = marked;
    update();
    announceState(/*selected=*/true, /*expanded=*/false);
}

void ItemsRowWidget::setExpanded(bool expanded)
{
    if (myExpanded == expanded) return;
    myExpanded = expanded;
    if (myKind == Kind::Folder) announceState(/*selected=*/false, /*expanded=*/true);
}

void ItemsRowWidget::setPendingOutline(bool pending)
{
    // Painted by nothing yet - see the header: design batch C's glyph tint.
    myPendingOutline = pending;
}

void ItemsRowWidget::announceState(bool selected, bool expanded)
{
    // A StateChanged naming exactly what moved. updateAccessibility() is
    // cheap with nothing listening, and it is never an Alert.
    QAccessible::State changed;
    changed.selected = selected;
    changed.expanded = expanded;
    changed.collapsed = expanded;
    QAccessibleStateChangeEvent event(this, changed);
    QAccessible::updateAccessibility(&event);
}

QString ItemsRowWidget::toolTipAt(const QPoint& pos) const
{
    if (myTipChild && myTipChild->isVisible() && !myTipChild->toolTip().isEmpty() &&
        myTipChild->geometry().contains(pos))
        return myTipChild->toolTip();
    return toolTip();
}

bool ItemsRowWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const QString text = toolTipAt(help->pos());
        if (text.isEmpty()) {
            QToolTip::hideText();
            event->ignore();
        } else {
            QToolTip::showText(help->globalPos(), text, this);
        }
        return true;
    }
    return QWidget::event(event);
}

void ItemsRowWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    // The stylesheet's fill first: a QWidget SUBCLASS gets no styled
    // background unless it draws PE_Widget itself (Qt's stylesheet rule for
    // custom widgets), and restyleRows()'s "#itemsRow" look is that fill.
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    // ...then the bar over its left edge. Whole logical pixels, no
    // antialiasing: a pixel probe reads the token itself.
    if (myMarked) painter.fillRect(barRect(), barColour());
}

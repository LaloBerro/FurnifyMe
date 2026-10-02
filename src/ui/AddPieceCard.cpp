#include "AddPieceCard.h"

#include "Theme.h"

#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
constexpr int kWidth = 260;
constexpr int kRadius = 12;
constexpr int kPad = 14;
// The scroll area is capped so a library of forty furniture does not grow a
// card taller than the viewport. The height below it is added up FROM THE
// PARTS - QScrollArea::sizeHint() does not answer for its widget, which this
// project has now paid for twice (see CLAUDE.md).
constexpr int kListMaxHeight = 280;
}  // namespace

AddPieceCard::AddPieceCard(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    Theme::makeSurfaceTransparent(this);
    setFocusPolicy(Qt::StrongFocus);
    hide();

    myColumn = new QVBoxLayout(this);
    myColumn->setContentsMargins(kPad, kPad, kPad, kPad);
    myColumn->setSpacing(8);

    myTitle = new QLabel(tr("Which furniture?"), this);
    myColumn->addWidget(myTitle);

    myEmpty = new QLabel(tr("The library is empty — make a furniture first"), this);
    myEmpty->setWordWrap(true);
    myColumn->addWidget(myEmpty);

    // Both bars off, as the Items drawer's own scroll area has them: nothing
    // is drawn and nothing is clickable, and the wheel is the whole
    // interaction (improvements item 12).
    myScroll = new QScrollArea(this);
    myScroll->setFrameShape(QFrame::NoFrame);
    myScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    myScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    myScroll->setWidgetResizable(true);
    myScroll->setMaximumHeight(kListMaxHeight);
    myList = new QWidget(myScroll);
    myListColumn = new QVBoxLayout(myList);
    myListColumn->setContentsMargins(0, 0, 0, 0);
    myListColumn->setSpacing(4);
    myListColumn->addStretch(1);
    myScroll->setWidget(myList);
    myColumn->addWidget(myScroll);

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyTheme();
        update();
    });
}

void AddPieceCard::showFor(const QVector<FurnitureStore::FurnitureInfo>& furniture)
{
    rebuild(furniture);
    // Sized from the parts, never from the scroll area - see kListMaxHeight.
    const int listHeight = std::min(kListMaxHeight, myList->sizeHint().height());
    int height = kPad * 2 + myTitle->sizeHint().height() + myColumn->spacing();
    if (furniture.isEmpty()) height += myEmpty->sizeHint().height();
    else height += listHeight;
    setFixedSize(Theme::wholeDevicePixels(QSize(kWidth, height)));
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
}

void AddPieceCard::rebuild(const QVector<FurnitureStore::FurnitureInfo>& furniture)
{
    for (const Row& row : myRows) {
        if (row.button) row.button->deleteLater();
    }
    myRows.clear();

    for (const FurnitureStore::FurnitureInfo& info : furniture) {
        auto* button = new QPushButton(info.name, myList);
        button->setCursor(Qt::PointingHandCursor);
        const QString id = info.id;
        connect(button, &QPushButton::clicked, this, [this, id] {
            // Hidden BEFORE the answer is reported, so a toast the answer
            // raises lands on a clear viewport - UnsavedCloseCard's own rule.
            hide();
            emit chosen(id);
        });
        myListColumn->insertWidget(myListColumn->count() - 1, button);
        myRows.push_back(Row{ id, button });
    }

    myEmpty->setVisible(furniture.isEmpty());
    myScroll->setVisible(!furniture.isEmpty());
    applyTheme();
}

QString AddPieceCard::entryIdAt(int index) const
{
    return index >= 0 && index < myRows.size() ? myRows[index].id : QString();
}

QPushButton* AddPieceCard::entryButtonAt(int index) const
{
    return index >= 0 && index < myRows.size() ? myRows[index].button : nullptr;
}

QStringList AddPieceCard::paintedTexts() const
{
    return { tr("Which furniture?"), tr("The library is empty — make a furniture first") };
}

void AddPieceCard::applyTheme()
{
    if (myTitle) {
        QFont bold = Theme::bodyFont();
        bold.setBold(true);
        myTitle->setFont(bold);
        myTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                   .arg(Theme::text().name()));
    }
    if (myEmpty) {
        myEmpty->setFont(Theme::labelFont());
        myEmpty->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                   .arg(Theme::textMuted().name()));
    }
    if (myList) myList->setStyleSheet(QStringLiteral("background: transparent;"));
    if (myScroll) myScroll->setStyleSheet(QStringLiteral("background: transparent;"));
    for (const Row& row : myRows) {
        if (!row.button) continue;
        row.button->setFont(Theme::bodyFont());
        row.button->setStyleSheet(
            QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3;"
                           " border-radius: 8px; padding: 6px 10px; text-align: left; }"
                           "QPushButton:hover { border-color: %4; }")
                .arg(Theme::panel().name(), Theme::text().name(), Theme::border().name(),
                     Theme::accent().name()));
    }
}

void AddPieceCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    Theme::paintSurface(painter, rect(), kRadius);
}

void AddPieceCard::keyPressEvent(QKeyEvent* event)
{
    // Escape closes and adds nothing. No other claim: this card is one
    // question with a list of answers, and a card that swallowed every key
    // would stop the viewport behind it being orbited to see what is there.
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

#include "AddPieceCard.h"

#include "KeyClaim.h"
#include "Theme.h"

#include <QAction>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
constexpr int kWidth = 260;
constexpr int kRadius = Theme::Space::Snug;
constexpr int kPad = 14;
// The scroll area is capped so a library of forty furniture does not grow a
// card taller than the viewport. The height below it is added up FROM THE
// PARTS - QScrollArea::sizeHint() does not answer for its widget, which this
// project has now paid for twice (see CLAUDE.md).
constexpr int kListMaxHeight = 280;
}  // namespace

// ONE AUTHOR for this sentence. It was written out twice in this file - once
// where it is painted and once where the banned-word sweep reads it - which is
// the app's own generate-documentation-never-write-it-twice rule broken in the
// copy layer, and the worse half of it: the sweep could have gone on passing a
// string the user never sees.
QString AddPieceCard::emptyLibraryText()
{
    return tr("The library is empty — make a furniture first");
}

QString AddPieceCard::libraryButtonText()
{
    return tr("Go to the library");
}

QString AddPieceCard::titleText()
{
    return tr("Which furniture?");
}

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

    myTitle = new QLabel(titleText(), this);
    myColumn->addWidget(myTitle);

    myEmpty = new QLabel(emptyLibraryText(), this);
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

    // The empty library's way out. Built here, hidden until a library with
    // nothing in it is shown, and inert until the window hands it an action.
    myLibraryButton = new QPushButton(libraryButtonText(), this);
    myLibraryButton->setCursor(Qt::PointingHandCursor);
    myLibraryButton->setFocusPolicy(Qt::NoFocus);
    myLibraryButton->hide();
    connect(myLibraryButton, &QPushButton::clicked, this, [this] {
        if (!myLibraryAction || !myLibraryAction->isEnabled()) return;
        // Hidden BEFORE the action runs, so a close question it raises stands
        // over a clear viewport - the answer-after-hiding rule this card
        // already keeps for a chosen furniture.
        hide();
        myLibraryAction->trigger();
    });
    myColumn->addWidget(myLibraryButton);

    // THE KEY THIS CARD CLAIMS, SAID ON THE CARD. Escape closed it and nothing
    // on screen said so; every chip that claims a key paints it, in the one
    // spelling KeyClaim keeps.
    myKeyHint = new QLabel(KeyClaim::closeHintText(), this);
    myColumn->addWidget(myKeyHint);

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyTheme();
        update();
    });
}

void AddPieceCard::showFor(const QVector<FurnitureStore::FurnitureInfo>& furniture)
{
    rebuild(furniture);
    // THE LAYOUT ANSWERS FOR THE HEIGHT, not arithmetic over the parts. The
    // hand-added version clipped its own title, which is the same class of
    // mistake this project has already paid for twice with QScrollArea: a
    // height worked out by adding up what the author remembered is a height
    // that forgets a margin. activate() first, so the hint is the real one -
    // ScenePiecesPanel's own lesson, one card over.
    //
    // The scroll area inside is capped (kListMaxHeight), so a library of forty
    // furniture still cannot grow this card past the viewport.
    myColumn->invalidate();
    myColumn->activate();
    setFixedSize(Theme::wholeDevicePixels(QSize(kWidth, sizeHint().height())));
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
}

void AddPieceCard::rebuild(const QVector<FurnitureStore::FurnitureInfo>& furniture)
{
    // Out of the layout NOW, deleted later - ScenePiecesPanel::rebuild()'s
    // finding: a row left in the column until the event loop came round was
    // measured into the card's height by showFor(), which runs straight after.
    for (const Row& row : myRows) {
        if (!row.button) continue;
        row.button->hide();
        myListColumn->removeWidget(row.button);
        row.button->deleteLater();
    }
    myRows.clear();

    for (const FurnitureStore::FurnitureInfo& info : furniture) {
        auto* button = new QPushButton(info.name, myList);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);   // no invisible focus - see ToolChip
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
    // Only an empty library offers the way back, and only when there is an
    // action behind it - a control that does nothing is not offered at all.
    myLibraryButton->setVisible(furniture.isEmpty() && myLibraryAction != nullptr);
    applyTheme();
}

void AddPieceCard::setLibraryAction(QAction* action)
{
    myLibraryAction = action;
    if (!action) return;
    // MIRRORED, never stored: the button is enabled exactly while the action
    // is, and the window's own availability rule decides that.
    auto sync = [this] {
        if (myLibraryAction && myLibraryButton)
            myLibraryButton->setEnabled(myLibraryAction->isEnabled());
    };
    connect(action, &QAction::changed, this, sync);
    sync();
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
    return { titleText(), emptyLibraryText(), libraryButtonText(),
             KeyClaim::closeHintText() };
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
    if (myKeyHint) {
        // The chips' own hint type: label size, muted ink.
        myKeyHint->setFont(Theme::labelFont());
        myKeyHint->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                     .arg(Theme::textMuted().name()));
    }
    if (myLibraryButton) {
        myLibraryButton->setFont(Theme::bodyFont());
        // The card's one forward step when it has nothing to list, so it wears
        // the accent's TEXT ink - accent() itself is for fills and strokes.
        myLibraryButton->setStyleSheet(
            QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3;"
                           " border-radius: 8px; padding: 6px 10px; text-align: left; }"
                           "QPushButton:hover { border-color: %4; }"
                           "QPushButton:disabled { color: %5; }")
                .arg(Theme::panel().name(), Theme::accentText().name(), Theme::border().name(),
                     Theme::accent().name(), Theme::textDisabled().name()));
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

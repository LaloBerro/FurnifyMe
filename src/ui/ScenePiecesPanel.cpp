#include "ScenePiecesPanel.h"

#include "IconSet.h"
#include "InlineRename.h"
#include "ToolChip.h"

#include <algorithm>
#include "Theme.h"

#include <QEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {
constexpr int kWidth = 200;
constexpr int kRadius = Theme::Space::Snug;
constexpr int kPad = 10;
constexpr int kRowInset = 6;
constexpr int kRowButtonPx = 24;
// The armed remove control's own side padding around "Remove?".
constexpr int kArmedPadPx = 6;
// The tallest the list of rows may grow. Past this it scrolls - with both
// bars OFF, so nothing is drawn and the wheel is the whole interaction, the
// Items drawer's own answer to the same problem (improvements item 12).
constexpr int kRowsMaxHeight = 420;
// How deep each scroll fade reaches into the list: one row's full pitch. It
// was two thirds of a row first, and the capture showed why that is not
// enough - the list's cap happens to land in the GAP below a row, so a
// shallow fade dimmed empty card and the last row read as the last row. A
// fade a row deep always reaches into a row's own text, which is the cue.
constexpr int kFadePx = 32;

// One edge of the list fading into the card. Painted, not a stylesheet: a
// gradient from the card's own panel() fill at the edge to nothing, over the
// rows, and transparent for the mouse so a click on a half-faded row still
// reaches that row. Read panel() at paint time, so a theme edit recolours it.
class ListEdgeFade : public QWidget {
public:
    ListEdgeFade(bool atTop, QWidget* parent) : QWidget(parent), myAtTop(atTop)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        Theme::makeSurfaceTransparent(this);
        hide();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        QColor solid = Theme::panel();
        QColor clear = solid;
        clear.setAlpha(0);
        QLinearGradient ramp(0.0, 0.0, 0.0, height());
        ramp.setColorAt(0.0, myAtTop ? solid : clear);
        ramp.setColorAt(1.0, myAtTop ? clear : solid);
        painter.fillRect(rect(), ramp);
    }

private:
    bool myAtTop = false;
};
}  // namespace

// ONE AUTHOR. This sentence was written out three times in this file - the
// tooltip itself, the banned-word sweep's list, and the restyle that puts it
// back when a row disarms - so a reworded tooltip could have left the sweep
// policing a string nobody paints.
QString ScenePiecesPanel::removeTipText()
{
    return tr("Take this piece out of the scene");
}

QString ScenePiecesPanel::armedRemoveText()
{
    return tr("Remove?");
}

QString ScenePiecesPanel::armedRemoveTipText()
{
    return tr("Click again to take it out of the scene");
}

QString ScenePiecesPanel::emptyText()
{
    // The VERB is the action's own - "Add piece" on the row below this line,
    // in the Arrange menu and here - so the card, the row and the menu read as
    // one gesture rather than three.
    return tr("No pieces yet — add a piece from the library");
}

ScenePiecesPanel::ScenePiecesPanel(QWidget* parent)
    : QWidget(parent)
{
    // A floating card painted in paintEvent() rather than filled by a
    // stylesheet - a stylesheet background is a square and this has rounded
    // corners - and every click is its own, so none reaches the viewport
    // behind it. ItemsPanel's own two attributes, for its own two reasons.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    Theme::makeSurfaceTransparent(this);
    setFixedWidth(Theme::wholeDevicePixels(QSize(kWidth, kWidth)).width());

    myColumn = new QVBoxLayout(this);
    myColumn->setContentsMargins(kPad, kPad, kPad, kPad);
    myColumn->setSpacing(4);

    myTitle = new QLabel(tr("Pieces"), this);
    myColumn->addWidget(myTitle);

    // The empty state says what the window is for rather than leaving a
    // heading over nothing - and the Add piece row under it is the thing to
    // click (see setAddAction()).
    myEmpty = new QLabel(emptyText(), this);
    myEmpty->setWordWrap(true);
    myColumn->addWidget(myEmpty);

    // The rows live in a scroll area; the title stays outside it, so a long
    // list scrolls under a heading that does not move.
    myScroll = new QScrollArea(this);
    myScroll->setFrameShape(QFrame::NoFrame);
    myScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    myScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    myScroll->setWidgetResizable(true);
    myRowsHost = new QWidget(myScroll);
    myRowsColumn = new QVBoxLayout(myRowsHost);
    myRowsColumn->setContentsMargins(0, 0, 0, 0);
    myRowsColumn->setSpacing(4);
    myRowsColumn->addStretch(1);
    myScroll->setWidget(myRowsHost);
    myColumn->addWidget(myScroll);

    // The two scroll fades, children of the CARD laid over the scroll area's
    // edges rather than children of the scroll area: anything inside it
    // scrolls with the rows. Every input that can move an edge or the scroll
    // position re-derives them.
    myTopFade = new ListEdgeFade(true, this);
    myBottomFade = new ListEdgeFade(false, this);
    myScroll->installEventFilter(this);
    connect(myScroll->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { syncFades(); });
    connect(myScroll->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this] { syncFades(); });

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyTheme();
        update();
    });
}

void ScenePiecesPanel::setAddAction(QAction* action)
{
    if (myAddChip || !action) return;
    // LABELLED, at the card's full width: in a list of rows it reads as one
    // more row - the one that makes the next - rather than as a toolbar
    // button that wandered in. Last in the column, under the scroll area, so
    // it stays on screen however long the list grows.
    myAddChip = new ToolChip(action, IconSet::Glyph::Add, ToolChip::ChipMode::Labelled, this);
    myAddChip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    myColumn->addWidget(myAddChip);
    myColumn->invalidate();
    myColumn->activate();
    updateGeometry();
    adjustSize();
}

bool ScenePiecesPanel::topFadeShown() const
{
    return myTopFade && myTopFade->isVisible();
}

bool ScenePiecesPanel::bottomFadeShown() const
{
    return myBottomFade && myBottomFade->isVisible();
}

void ScenePiecesPanel::syncFades()
{
    if (!myTopFade || !myBottomFade || !myScroll) return;
    const QRect area = myScroll->geometry();
    const int depth = std::min(kFadePx, area.height() / 2);
    myTopFade->setGeometry(area.left(), area.top(), area.width(), depth);
    myBottomFade->setGeometry(area.left(), area.bottom() - depth + 1, area.width(), depth);
    // ONLY WHILE THERE IS SOMETHING PAST THAT EDGE. A fade over a list that
    // fits would read as a scroll cue with nothing to scroll to.
    const QScrollBar* bar = myScroll->verticalScrollBar();
    const bool listShown = !myScroll->isHidden();
    myTopFade->setVisible(listShown && bar->value() > bar->minimum());
    myBottomFade->setVisible(listShown && bar->value() < bar->maximum());
    myTopFade->raise();
    myBottomFade->raise();
}

bool ScenePiecesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == myScroll &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
         event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        syncFades();
    }
    return QWidget::eventFilter(watched, event);
}

void ScenePiecesPanel::setRows(const QVector<Row>& rows)
{
    // EVERYTHING A ROW DRAWS goes into the signature, which is the lesson
    // ItemsPanel::refresh() records three times over: a field a row shows but
    // the early-out does not compare is a field that stops updating.
    QString signature;
    for (const Row& row : rows) {
        signature += QString::number(row.pieceId) + QLatin1Char('\x1f') + row.name +
                     QLatin1Char('\x1f') + (row.visible ? QLatin1Char('1') : QLatin1Char('0')) +
                     QLatin1Char('\x1f') + row.reason + QLatin1Char('\x1e');
    }
    signature += QLatin1Char('|') + QString::number(mySelected) + QLatin1Char('|') +
                 QString::number(myArmedRemove);
    if (myBuilt && signature == mySignature) return;
    mySignature = signature;
    myWanted = rows;
    rebuild();
}

void ScenePiecesPanel::setSelected(int pieceId)
{
    if (mySelected == pieceId) return;
    mySelected = pieceId;
    // The selection is part of what a row draws, so it is part of the
    // signature - cleared here so the next setRows() genuinely rebuilds.
    mySignature.clear();
    setRows(myWanted);
}

void ScenePiecesPanel::rebuild()
{
    myBuilt = true;
    // OUT OF THE LAYOUT NOW, deleted later. deleteLater() alone left every old
    // row in the column - visible and counted - until control next reached an
    // event loop, and the measuring just below runs before that: the card was
    // sized for the old rows AND the new ones, and the list held a scroll
    // range for rows that were already gone. Found by the scroll fade, which
    // stood over a three-row list with 373 px to scroll.
    for (const BuiltRow& row : myRows) {
        if (!row.widget) continue;
        row.widget->hide();
        myRowsColumn->removeWidget(row.widget);
        row.widget->deleteLater();
    }
    myRows.clear();

    for (const Row& wanted : myWanted) {
        auto* row = new QWidget(myRowsHost);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(kRowInset, 2, kRowInset, 2);
        line->setSpacing(6);

        // THE REASON WHERE THE NAME GOES when the furniture could not be
        // resolved. Not beside it: a row that reads like an ordinary piece and
        // renders nothing is a row the user believes.
        const bool broken = !wanted.reason.isEmpty();
        auto* name = new QLabel(broken ? wanted.reason : wanted.name, row);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        // THE NAME GIVES WAY, NEVER THE CONTROLS. A label's minimum width is
        // its whole text, so one broken row - whose reason is a sentence -
        // made the rows' host wider than the card, and with the horizontal bar
        // off every row's eye and remove control was pushed past the clip: the
        // whole list lost its controls to one long line. Ignored horizontally,
        // the label takes what is left and clips; the whole text stays one
        // hover away - on the ROW, since the label itself is transparent to
        // the mouse and so would never be asked for a tooltip.
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row->setToolTip(broken ? wanted.reason : wanted.name);
        line->addWidget(name, 1);

        auto* eye = new QPushButton(row);
        eye->setCheckable(true);
        eye->setChecked(wanted.visible);
        eye->setFixedSize(kRowButtonPx, kRowButtonPx);
        eye->setIcon(IconSet::icon(IconSet::Glyph::Body));
        eye->setToolTip(tr("Show or hide this piece"));
        const int id = wanted.pieceId;
        connect(eye, &QPushButton::toggled, this,
                [this, id](bool shown) { emit visibilityToggled(id, shown); });
        line->addWidget(eye);

        auto* remove = new QPushButton(row);
        remove->setFixedSize(kRowButtonPx, kRowButtonPx);
        remove->setToolTip(removeTipText());
        remove->setCursor(Qt::PointingHandCursor);
        connect(remove, &QPushButton::clicked, this, [this, id] {
            if (myArmedRemove != id) {
                // ASKS FIRST. Nothing is written until the second click, and
                // arming one row disarms any other.
                //
                // RESTYLED IN PLACE, never rebuilt: a rebuild deletes the very
                // button the second click is about to land on. A human would
                // hit its replacement at the same pixel and never notice, which
                // is exactly how this would have shipped unnoticed - the test
                // held the pointer and found it dead.
                myArmedRemove = id;
                applyTheme();
                return;
            }
            myArmedRemove = 0;
            emit removeRequested(id);
        });
        line->addWidget(remove);

        myRowsColumn->insertWidget(myRowsColumn->count() - 1, row);
        // SHOWN EXPLICITLY. A widget inserted into the layout of a parent that
        // is not itself visible yet stays hidden, and a hidden child
        // contributes nothing to the layout's sizeHint - so the card measured
        // its own height as if it had no rows at all, and adjustSize() had
        // nothing to grow to.
        row->show();

        BuiltRow built;
        built.data = wanted;
        built.widget = row;
        built.name = name;
        built.eye = eye;
        built.remove = remove;
        myRows.push_back(built);
    }

    if (myEmpty) myEmpty->setVisible(myWanted.isEmpty());
    applyTheme();

    // ACTIVATED, then sized, and in that order. updateGeometry() alone only
    // SCHEDULES an invalidation, so ViewportOverlay::relayout() - which runs
    // synchronously right after this, and sizes every anchored card by
    // adjustSize() - read a stale sizeHint and left the card at its old
    // height. QVBoxLayout then squeezed the title and the rows into a space
    // they did not fit, which Qt resolves by crushing them together: measured
    // at 40 px against a sizeHint of 133, and seen as a row's name drawn
    // across the title. invalidate() + activate() makes the layout answer now.
    myRowsColumn->invalidate();
    myRowsColumn->activate();
    myRowsHost->adjustSize();
    myScroll->setVisible(!myWanted.isEmpty());
    myColumn->invalidate();
    myColumn->activate();
    updateGeometry();
    adjustSize();
    syncFades();
}

int ScenePiecesPanel::rowIdAt(int index) const
{
    return index >= 0 && index < myRows.size() ? myRows[index].data.pieceId : 0;
}

QWidget* ScenePiecesPanel::rowWidgetAt(int index) const
{
    return index >= 0 && index < myRows.size() ? myRows[index].widget : nullptr;
}

QString ScenePiecesPanel::rowTextAt(int index) const
{
    return index >= 0 && index < myRows.size() && myRows[index].name
               ? myRows[index].name->text()
               : QString();
}

QPushButton* ScenePiecesPanel::rowEyeAt(int index) const
{
    return index >= 0 && index < myRows.size() ? myRows[index].eye : nullptr;
}

QPushButton* ScenePiecesPanel::rowRemoveAt(int index) const
{
    return index >= 0 && index < myRows.size() ? myRows[index].remove : nullptr;
}

QStringList ScenePiecesPanel::paintedTexts() const
{
    // THIS APP'S OWN COPY ONLY. A piece's name is the user's word choice, and
    // sweeping it would fail a scene holding a furniture called "Fuse My
    // Table" - the exemption every surface that paints user text already
    // keeps, applied at the surface that knows which string is whose.
    QStringList texts{tr("Pieces"), emptyText(),
                      tr("Show or hide this piece"),
                      removeTipText(),
                      armedRemoveText(),
                      armedRemoveTipText()};
    if (myAddChip) texts << myAddChip->text() << myAddChip->toolTip();
    for (const BuiltRow& row : myRows) {
        // A refusal sentence is this app's copy, so it IS swept.
        if (!row.data.reason.isEmpty()) texts << row.data.reason;
    }
    return texts;
}

void ScenePiecesPanel::applyTheme()
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
    for (const BuiltRow& row : myRows) {
        if (row.remove) {
            // A PAINTED GLYPH AT REST, A WORD WHEN ARMED. It used to be a
            // typed "x" that became a caution "!", and the "!" explained
            // itself only in a tooltip - so the one moment the control needed
            // to say what the next click does, it said nothing. Armed, it reads
            // "Remove?" in the failure ink and widens to hold the word; it
            // grows LEFTWARD, the row's right edge staying put, so the pixel
            // the first click landed on is still inside it for the second.
            const bool armed = row.data.pieceId == myArmedRemove;
            if (armed) {
                row.remove->setIcon(QIcon());
                row.remove->setText(armedRemoveText());
                row.remove->setFont(Theme::labelFont());
                const int wide = QFontMetrics(Theme::labelFont())
                                     .horizontalAdvance(armedRemoveText()) + 2 * kArmedPadPx;
                row.remove->setFixedSize(std::max(kRowButtonPx, wide), kRowButtonPx);
            } else {
                row.remove->setText(QString());
                row.remove->setIcon(IconSet::icon(IconSet::Glyph::Delete));
                row.remove->setIconSize(QSize(16, 16));
                row.remove->setFixedSize(kRowButtonPx, kRowButtonPx);
            }
            row.remove->setToolTip(armed ? armedRemoveTipText() : removeTipText());
            row.remove->setStyleSheet(
                QStringLiteral("QPushButton { background: transparent; border: none;"
                               " color: %1; padding: 0px; }")
                    .arg((armed ? Theme::dangerText() : Theme::textMuted()).name()));
        }
        if (!row.name) continue;
        row.name->setFont(Theme::bodyFont());
        // A BROKEN PIECE AND A HIDDEN ONE ARE NOT THE SAME THING, and they
        // used to read identically muted on the grounds that neither is on
        // screen. That is true and it is not what the user needs to know: a
        // hidden piece is a choice they made, a broken one is a reference
        // that cannot be read, and the product principle is that it breaks
        // LOUDLY. A row that looks merely dimmed is a row they believe.
        const QColor nameColour = !row.data.reason.isEmpty()
                                      ? Theme::dangerText()
                                      : (!row.data.visible ? Theme::textMuted() : Theme::text());
        row.name->setStyleSheet(
            QStringLiteral("background: transparent; color: %1;").arg(nameColour.name()));
    }
}

void ScenePiecesPanel::mousePressEvent(QMouseEvent* event)
{
    // SAME HIT TEST AS THE RENAME BELOW, same mapping, for the same reason: a
    // row's geometry() is relative to the rows' host inside the scroll area,
    // not to this card, so an unmapped rect selects a neighbouring piece.
    //
    // The eye and the remove button are CHILD widgets, so a press on either is
    // delivered to them and never reaches here - there is no need to exclude
    // their rectangles, and excluding them by geometry is what would rot the
    // day a row grows a third control.
    for (const BuiltRow& row : myRows) {
        if (!row.widget) continue;
        const QRect inPanel(row.widget->mapTo(this, QPoint(0, 0)), row.widget->size());
        if (!inPanel.contains(event->pos())) continue;
        emit selectionRequested(row.data.pieceId);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ScenePiecesPanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    // Double-click a row to rename the PIECE. The selector's cards and the
    // versions drawer's rows already rename in place through this one helper,
    // so this list does too rather than growing an editor of its own.
    for (const BuiltRow& row : myRows) {
        if (!row.widget) continue;
        // MAPPED INTO THIS PANEL'S OWN SPACE. A row's geometry() is relative
        // to its immediate parent, which since the list gained a scroll area
        // is the rows' host and not the card - so testing it against the
        // panel's event->pos() matched a NEIGHBOURING row, and renamed the
        // wrong piece. The editor's rect needs the same mapping, or it would
        // open in the wrong place even once the right row is found.
        const QRect inPanel(row.widget->mapTo(this, QPoint(0, 0)), row.widget->size());
        if (!inPanel.contains(event->pos())) continue;
        const int id = row.data.pieceId;
        const QString current = row.data.name;
        InlineRename::beginRename(this, inPanel, current,
                                  [this, id](QString chosen) {
                                      if (!chosen.trimmed().isEmpty())
                                          emit renameCommitted(id, chosen.trimmed());
                                  });
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

QSize ScenePiecesPanel::sizeHint() const
{
    // FROM THE PARTS. QScrollArea::sizeHint() does not answer for its widget -
    // it returns a cached size bounded to roughly 24 text lines and ignores
    // sizeAdjustPolicy entirely - so the rows' own host is asked instead and
    // the result capped. sizeHint, never heightForWidth: the content sits in a
    // widgetResizable scroll area, and asking its layout for a height AT A
    // WIDTH re-enters the very layout pass doing the asking.
    int height = kPad * 2;
    if (myTitle) height += myTitle->sizeHint().height();
    if (myWanted.isEmpty()) {
        if (myEmpty) height += myColumn->spacing() + myEmpty->sizeHint().height();
    } else if (myRowsHost) {
        height += myColumn->spacing() + std::min(kRowsMaxHeight,
                                                 myRowsHost->sizeHint().height());
    }
    // The Add piece row, at the foot in every state.
    if (myAddChip) height += myColumn->spacing() + myAddChip->sizeHint().height();
    return QSize(width(), height);
}

void ScenePiecesPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    Theme::paintSurface(painter, rect(), kRadius);
}

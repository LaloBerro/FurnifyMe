#include "ScenePiecesPanel.h"

#include "IconSet.h"
#include "InlineRename.h"

#include <algorithm>
#include "Theme.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
constexpr int kWidth = 200;
constexpr int kRadius = 12;
constexpr int kPad = 10;
constexpr int kRowInset = 6;
constexpr int kRowButtonPx = 24;
// The tallest the list of rows may grow. Past this it scrolls - with both
// bars OFF, so nothing is drawn and the wheel is the whole interaction, the
// Items drawer's own answer to the same problem (improvements item 12).
constexpr int kRowsMaxHeight = 420;
}  // namespace

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
    // heading over nothing.
    myEmpty = new QLabel(tr("Add a furniture to put it in this scene"), this);
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

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyTheme();
        update();
    });
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
    for (const BuiltRow& row : myRows) {
        if (row.widget) row.widget->deleteLater();
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
        remove->setToolTip(tr("Take this piece out of the scene"));
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
    QStringList texts{tr("Pieces"), tr("Add a furniture to put it in this scene"),
                      tr("Show or hide this piece"),
                      tr("Take this piece out of the scene"),
                      tr("Click again to take it out")};
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
            const bool armed = row.data.pieceId == myArmedRemove;
            row.remove->setText(armed ? QStringLiteral("!") : QStringLiteral("x"));
            row.remove->setToolTip(armed ? tr("Click again to take it out")
                                         : tr("Take this piece out of the scene"));
            row.remove->setStyleSheet(
                QStringLiteral("QPushButton { background: transparent; border: none;"
                               " color: %1; }")
                    .arg((armed ? Theme::caution() : Theme::textMuted()).name()));
        }
        if (!row.name) continue;
        row.name->setFont(Theme::bodyFont());
        // A broken piece and a hidden one both read MUTED, for the same
        // reason: neither is on screen.
        const bool dim = !row.data.reason.isEmpty() || !row.data.visible;
        row.name->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                    .arg((dim ? Theme::textMuted() : Theme::text()).name()));
    }
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
    return QSize(width(), height);
}

void ScenePiecesPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    Theme::paintSurface(painter, rect(), kRadius);
}

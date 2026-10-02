#include "ScenePiecesPanel.h"

#include "IconSet.h"
#include "InlineRename.h"
#include "Theme.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kWidth = 200;
constexpr int kRadius = 12;
constexpr int kPad = 10;
constexpr int kRowInset = 6;
constexpr int kRowButtonPx = 24;
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

    myColumn->addStretch(1);

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
    signature += QLatin1Char('|') + QString::number(mySelected);
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
        auto* row = new QWidget(this);
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

        myColumn->insertWidget(myColumn->count() - 1, row);

        BuiltRow built;
        built.data = wanted;
        built.widget = row;
        built.name = name;
        built.eye = eye;
        myRows.push_back(built);
    }

    if (myEmpty) myEmpty->setVisible(myWanted.isEmpty());
    applyTheme();
    updateGeometry();
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

QStringList ScenePiecesPanel::paintedTexts() const
{
    // THIS APP'S OWN COPY ONLY. A piece's name is the user's word choice, and
    // sweeping it would fail a scene holding a furniture called "Fuse My
    // Table" - the exemption every surface that paints user text already
    // keeps, applied at the surface that knows which string is whose.
    QStringList texts{tr("Pieces"), tr("Add a furniture to put it in this scene"),
                      tr("Show or hide this piece")};
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
        if (!row.widget || !row.widget->geometry().contains(event->pos())) continue;
        const int id = row.data.pieceId;
        const QString current = row.data.name;
        InlineRename::beginRename(this, row.widget->geometry(), current,
                                  [this, id](QString chosen) {
                                      if (!chosen.trimmed().isEmpty())
                                          emit renameCommitted(id, chosen.trimmed());
                                  });
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ScenePiecesPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    Theme::paintSurface(painter, rect(), kRadius);
}

#include "JointChip.h"

#include "GestureChip.h"
#include "KeyClaim.h"
#include "MainWindow.h"
#include "Measure.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShowEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {

// The card's own metrics. kPad and the frame are GestureChip's, shared with
// the pull and bevel chips; everything below the kind line is this card's own,
// because no other chip has rows.
constexpr int kPad = GestureChip::kPad;
constexpr int kKindHeight = 24;
constexpr int kBlockGap = 8;
constexpr int kLabelHeight = 14;
constexpr int kLabelGap = 3;
constexpr int kBoxHeight = 24;
constexpr int kColumnGap = 12;
constexpr int kPairGap = 6;
constexpr int kRowGap = 7;
constexpr int kColumnWidth = 92;
constexpr int kMaxColumnWidth = 140;
constexpr int kMoreHeight = 18;
constexpr int kMoreGap = 6;
constexpr int kRuleGap = 8;
constexpr int kHintHeight = 14;
constexpr int kNamesGap = 8;
constexpr int kMaxNamesWidth = 220;

// The kind menu's own metrics.
constexpr int kMenuPad = 8;
constexpr int kMenuHeaderHeight = 16;
constexpr int kMenuRowHeight = 22;
constexpr int kMenuReasonHeight = 15;
constexpr int kMenuRowGap = 2;
constexpr int kMenuReasonIndent = 10;
constexpr int kMenuMinWidth = 200;
constexpr int kMenuMaxWidth = 380;
constexpr int kMenuGapUnderButton = 4;

QString unitWord()
{
    return QString::fromStdString(Measure::unitSuffix());
}

// A length as the FIELD carries it: formatted in the display unit, with the
// unit word and the thousands comma stripped. The comma has to go because this
// text is read back by Measure::parseLength(), whose grammar has none - the
// lesson PullArrow's drag past a thousand paid for.
QString numberText(double millimetres)
{
    QString formatted = QString::fromStdString(Measure::formatLength(millimetres));
    const QString suffix = QLatin1Char(' ') + unitWord();
    if (formatted.endsWith(suffix)) formatted.chop(suffix.length());
    formatted.remove(QLatin1Char(','));
    return formatted;
}

// The ten kinds in menu order, grouped by the three families - the round-1
// mockup's own grouping, and Joinery::validKindsFor()'s own order inside it.
const std::vector<std::pair<QString, std::vector<Joinery::Kind>>>& kindGroups()
{
    static const std::vector<std::pair<QString, std::vector<Joinery::Kind>>> groups = {
        {QCoreApplication::translate("JointChip", "Fasteners"),
         {Joinery::Kind::Dowel, Joinery::Kind::PocketScrew, Joinery::Kind::Biscuit,
          Joinery::Kind::Domino, Joinery::Kind::Screw}},
        {QCoreApplication::translate("JointChip", "Housings"),
         {Joinery::Kind::Dado, Joinery::Kind::Rabbet, Joinery::Kind::Groove}},
        {QCoreApplication::translate("JointChip", "Interlocks"),
         {Joinery::Kind::MortiseTenon, Joinery::Kind::HalfLap}},
    };
    return groups;
}

// One row of the kind menu. A plain button that paints its own name, its tick
// when it is the joint's current kind, and nothing at all when the contact
// refuses it beyond the disabled ink - the reason is a line of its own beneath,
// painted by the menu, because it belongs to the menu's copy rather than to the
// control.
class KindEntry : public QAbstractButton {
public:
    KindEntry(Joinery::Kind kind, QWidget* parent) : QAbstractButton(parent), myKind(kind)
    {
        setText(QString::fromStdString(Joinery::kindName(kind)));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_NoMousePropagation);
        Theme::makeSurfaceTransparent(this);
    }

    Joinery::Kind kind() const { return myKind; }
    void setCurrent(bool current)
    {
        if (myCurrent == current) return;
        myCurrent = current;
        update();
    }
    bool isCurrent() const { return myCurrent; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool hot = isEnabled() && (underMouse() || isDown());
        if (hot || myCurrent) {
            QPainterPath path;
            path.addRoundedRect(QRectF(rect()), 5.0, 5.0);
            painter.fillPath(path, myCurrent ? Theme::chipActive() : Theme::chipHover());
        }
        const QRect text = rect().adjusted(8, 0, -8, 0);
        painter.setFont(Theme::labelFont());
        painter.setPen(isEnabled() ? Theme::text() : Theme::textDisabled());
        painter.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, QAbstractButton::text());
        if (myCurrent) {
            painter.setPen(Theme::accent().lighter(150));
            painter.drawText(text, Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("✓"));
        }
    }

private:
    Joinery::Kind myKind;
    bool myCurrent = false;
};

}   // namespace

// --- the kind menu ----------------------------------------------------------
//
// A card of this app's own, not a QMenu popup: a popup is a top-level window,
// which would activate over whatever the user is doing (the no-input law), sit
// outside the one composited PrintWindow capture the suite measures, and - the
// reason that decides it - install a SECOND application-wide Escape claim
// beside the chip's. Owning both keys in one filter is what keeps "at most one
// claim" true while the menu is open.
//
// A sibling in the viewport rather than a child of the chip, so it can stand
// taller than the card it hangs from. ShapeFlyout's dismissal rules apply, and
// the chip's own filter enforces them - see JointChip::eventFilter().
class JointKindMenu : public QWidget {
public:
    JointKindMenu(QWidget* parent, std::function<void(Joinery::Kind)> onPick)
        : QWidget(parent), myPick(std::move(onPick))
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_NoMousePropagation);
        Theme::makeSurfaceTransparent(this);
        for (const auto& group : kindGroups()) {
            for (const Joinery::Kind kind : group.second) {
                auto* entry = new KindEntry(kind, this);
                connect(entry, &QAbstractButton::clicked, this, [this, kind] {
                    if (myPick) myPick(kind);
                });
                myEntries.push_back(entry);
            }
        }
        hide();
    }

    // Lays the menu out for `contact`: every kind offered or greyed, each
    // refusal carrying validityOf()'s own reason. A contact that cannot be
    // measured at all greys every kind and says why ONCE, at the top - ten
    // copies of one sentence is not a reason, it is noise.
    void rebuild(Joinery::Kind current, const Joinery::ContactResult& contact)
    {
        myLines.clear();
        myReasons.clear();

        const QFontMetrics badge(Theme::badgeFont());
        const QFontMetrics label(Theme::labelFont());
        const QString contactError =
            contact.ok ? QString() : QString::fromStdString(contact.error);

        int content = kMenuMinWidth - kMenuPad * 2;
        for (KindEntry* entry : myEntries) {
            const QString reason =
                contact.ok ? QString::fromStdString(Joinery::validityOf(entry->kind(), contact.contact))
                           : QString();
            myReasons.emplace_back(entry->kind(), reason);
            content = std::max(content, label.horizontalAdvance(entry->text()) + 40);
            if (!reason.isEmpty())
                content = std::max(content, badge.horizontalAdvance(reason) + kMenuReasonIndent);
        }
        if (!contactError.isEmpty())
            content = std::max(content, badge.horizontalAdvance(contactError));
        content = std::min(content, kMenuMaxWidth - kMenuPad * 2);

        int y = kMenuPad;
        const auto addLine = [&](const QString& raw, const QFont& font, int x, int maxWidth) {
            const QFontMetrics metrics(font);
            Line line;
            line.raw = raw;
            line.text.setTextFormat(Qt::PlainText);
            line.text.setText(metrics.elidedText(raw, Qt::ElideRight, maxWidth));
            line.text.prepare(QTransform(), font);
            line.font = font;
            line.at = QPointF(x, y);
            myLines.push_back(std::move(line));
        };

        if (!contactError.isEmpty()) {
            addLine(contactError, Theme::badgeFont(), kMenuPad, content);
            y += kMenuReasonHeight + kMenuRowGap;
        }

        QFont header = Theme::badgeFont();
        header.setCapitalization(QFont::AllUppercase);
        std::size_t index = 0;
        for (const auto& group : kindGroups()) {
            addLine(group.first, header, kMenuPad, content);
            y += kMenuHeaderHeight;
            for (std::size_t i = 0; i < group.second.size() && index < myEntries.size(); ++i, ++index) {
                KindEntry* entry = myEntries[index];
                const QString reason = index < myReasons.size() ? myReasons[index].second : QString();
                entry->setGeometry(kMenuPad, y, content, kMenuRowHeight);
                entry->setEnabled(contact.ok && reason.isEmpty());
                entry->setCurrent(entry->kind() == current);
                entry->show();
                y += kMenuRowHeight + kMenuRowGap;
                if (contact.ok && !reason.isEmpty()) {
                    addLine(reason, Theme::badgeFont(), kMenuPad + kMenuReasonIndent,
                            content - kMenuReasonIndent);
                    y += kMenuReasonHeight;
                }
            }
        }
        y += kMenuPad - kMenuRowGap;
        setFixedSize(Theme::wholeDevicePixels(QSize(content + kMenuPad * 2, y)));
        update();
    }

    QAbstractButton* entryFor(Joinery::Kind kind) const
    {
        for (KindEntry* entry : myEntries) {
            if (entry->kind() == kind) return entry;
        }
        return nullptr;
    }

    QString reasonFor(Joinery::Kind kind) const
    {
        for (const auto& pair : myReasons) {
            if (pair.first == kind) return pair.second;
        }
        return QString();
    }

    // Every string this menu paints - the entries' own names included, since
    // they are painted by KindEntry rather than by the lines below.
    QStringList paintedTexts() const
    {
        QStringList texts;
        for (const Line& line : myLines) texts << line.raw;
        for (KindEntry* entry : myEntries) texts << entry->text();
        return texts;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        Theme::paintSurface(painter, rect(), 10);
        Theme::drawCrispBorder(painter, QRectF(rect()), Theme::border(), 10);
        painter.setPen(Theme::textMuted());
        for (const Line& line : myLines) {
            painter.setFont(line.font);
            painter.drawStaticText(line.at, line.text);
        }
    }

    // A press on the menu's own padding is the menu's, and goes no further.
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }

private:
    struct Line {
        QStaticText text;
        QPointF at;
        QFont font;
        QString raw;
    };

    std::function<void(Joinery::Kind)> myPick;
    std::vector<KindEntry*> myEntries;
    std::vector<Line> myLines;
    std::vector<std::pair<Joinery::Kind, QString>> myReasons;
};

// --- the chip ---------------------------------------------------------------

JointChip::JointChip(MainWindow* window, OcctViewWidget* view)
    : QWidget(view), myWindow(window), myView(view)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // Every control here is a real child of this card, so - unlike BevelArrow,
    // whose card is transparent to the mouse and whose single field is a
    // sibling - the card takes the mouse itself. WA_NoMousePropagation is what
    // stops a click on its padding reaching the viewport and re-picking, and
    // the three handlers below accept press, release and double-click alike so
    // no half of a gesture can leak through either.
    setAttribute(Qt::WA_NoMousePropagation);
    Theme::makeSurfaceTransparent(this);

    myKindButton = new QPushButton(this);
    myKindButton->setFocusPolicy(Qt::NoFocus);
    myKindButton->setCursor(Qt::PointingHandCursor);
    myKindButton->setToolTip(tr("Switch this joint to another kind"));
    connect(myKindButton, &QPushButton::clicked, this, [this] {
        if (kindMenuOpen())
            closeKindMenu();
        else
            openKindMenu();
    });

    myMoreButton = new QPushButton(this);
    myMoreButton->setFocusPolicy(Qt::NoFocus);
    myMoreButton->setCursor(Qt::PointingHandCursor);
    myMoreButton->setToolTip(tr("Show or hide the joint's other numbers"));
    connect(myMoreButton, &QPushButton::clicked, this, [this] { setMoreOpen(!myMoreOpen); });

    for (int i = 0; i < kSlotCount; ++i) {
        auto* edit = new QLineEdit(this);
        edit->setAttribute(Qt::WA_NoMousePropagation);
        edit->setAlignment(Qt::AlignRight);
        edit->hide();
        myFields[static_cast<std::size_t>(i)] = edit;
        myInvalid[static_cast<std::size_t>(i)] = false;
        const Slot slot = static_cast<Slot>(i);
        connect(edit, &QLineEdit::textChanged, this,
                [this, slot](const QString&) { onFieldEdited(slot); });
    }

    const auto makeSegment = [this](const QString& tip) {
        auto* button = new QPushButton(this);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tip);
        button->hide();
        return button;
    };

    myStoppedButton = makeSegment(tr("Stop the channel short of the end"));
    connect(myStoppedButton, &QPushButton::clicked, this, [this] { syncToggleTexts(); });
    myHaunchedButton = makeSegment(tr("Leave a haunch on the tenon"));
    connect(myHaunchedButton, &QPushButton::clicked, this, [this] { syncToggleTexts(); });

    for (int i = 0; i < 2; ++i) {
        myHostBodies[static_cast<std::size_t>(i)] = 0;
        QPushButton* host = makeSegment(tr("Cut this joint into this piece"));
        myHostButtons[static_cast<std::size_t>(i)] = host;
        connect(host, &QPushButton::clicked, this, [this, i] {
            const int body = myHostBodies[static_cast<std::size_t>(i)];
            // The current host stays the current host: a click on it is not an
            // edit, so it takes no checkpoint. Re-checked by hand because the
            // button is checkable and Qt has already toggled it.
            if (myWindow && body > 0 && myJointId > 0) myWindow->setJointHost(myJointId, body);
            syncToggleTexts();
        });

        QPushButton* face = makeSegment(tr("Drill the pocket from this face"));
        myDrilledFromButtons[static_cast<std::size_t>(i)] = face;
        const Joinery::DrilledFrom from = static_cast<Joinery::DrilledFrom>(i);
        connect(face, &QPushButton::clicked, this, [this, from] {
            // A parameter, so it is PENDING - Enter applies it with every other
            // changed field, Escape puts it back.
            myPendingDrilledFrom = from;
            syncToggleTexts();
        });
    }

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &JointChip::applyTheme);
    hide();

    if (myWindow) connect(myWindow, &MainWindow::appStateChanged, this, &JointChip::refresh);
    if (myView) connect(myView, &OcctViewWidget::cameraChanged, this, &JointChip::reposition);
}

JointChip::~JointChip()
{
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeEventFilter(this);
    // The menu is a sibling in the viewport, so Qt's parent-child cascade does
    // not reach it when this card alone is destroyed; QPointer makes the delete
    // a safe no-op when the shared parent tears both down instead.
    delete myKindMenu;
}

void JointChip::applyTheme()
{
    // The field stylesheet is restyleField()'s ALONE - it is the one that knows
    // whether the field is marked invalid. A second copy here was dead, and a
    // maintainer editing the prominent dead one would have changed nothing on
    // screen with the unused-variable warning switched off to hide it.
    for (int i = 0; i < kSlotCount; ++i) {
        QLineEdit* edit = myFields[static_cast<std::size_t>(i)];
        if (!edit) continue;
        // An explicitly set font does not follow QApplication::setFont - see
        // ExtrudePreview's constructor.
        edit->setFont(Theme::bodyFont());
        restyleField(static_cast<Slot>(i));
    }

    if (myKindButton) {
        myKindButton->setFont(Theme::labelFont());
        myKindButton->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; color: %2; border: 2px solid %3; "
                           "border-radius: 7px; padding: 0px 8px; text-align: left; } "
                           "QPushButton:hover { background-color: %4; }")
                .arg(Theme::chipActive().name(), Theme::text().name(), Theme::accent().name(),
                     Theme::chipHover().name()));
    }
    if (myMoreButton) {
        myMoreButton->setFont(Theme::badgeFont());
        myMoreButton->setStyleSheet(
            QStringLiteral("QPushButton { background: transparent; border: none; color: %1; "
                           "text-align: left; padding: 0px; }")
                .arg(Theme::accent().lighter(150).name()));
    }
    const QString segmentStyle =
        QStringLiteral("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 5px; padding: 0px 6px; } "
                       "QPushButton:checked { background-color: %4; color: %5; border: 1px solid %6; } "
                       "QPushButton:hover { border: 1px solid %6; }")
            .arg(Theme::chip().name(), Theme::textMuted().name(), Theme::border().name(),
                 Theme::chipActive().name(), Theme::text().name(), Theme::accent().name());
    for (QPushButton* button : {myStoppedButton, myHaunchedButton, myHostButtons[0],
                                myHostButtons[1], myDrilledFromButtons[0],
                                myDrilledFromButtons[1]}) {
        if (!button) continue;
        button->setFont(Theme::labelFont());
        button->setStyleSheet(segmentStyle);
    }

    if (myJointId > 0) {
        relayout();
        if (myKindMenu && myKindMenu->isVisible()) {
            Joinery::ContactResult contact;
            if (myWindow) myWindow->jointContact(myJointId, contact);
            myKindMenu->rebuild(myJoint.kind, contact);
            placeKindMenu();
        }
    }
    update();
}

void JointChip::restyleField(Slot slot)
{
    QLineEdit* edit = myFields[static_cast<std::size_t>(slot)];
    if (!edit) return;
    const bool invalid = myInvalid[static_cast<std::size_t>(slot)];
    const QColor border = invalid ? Theme::danger() : Theme::border();
    const QColor focus = invalid ? Theme::danger() : Theme::focusRing();
    edit->setStyleSheet(
        QStringLiteral("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 5px; padding: 1px 6px; } "
                       "QLineEdit:focus { border: 2px solid %4; padding: 0px 5px; } "
                       "QLineEdit:disabled { color: %5; }")
            .arg(Theme::chrome().name(), Theme::text().name(), border.name(), focus.name(),
                 Theme::textDisabled().name()));
}

// `carried`, never `slots`: Qt #defines `slots` to nothing, so a local of that
// name silently loses its declaration and the compiler reports a syntax error
// several lines later.
QLineEdit* JointChip::field() const
{
    const std::vector<Slot> carried = familySlots();
    return carried.empty() ? nullptr : myFields[static_cast<std::size_t>(carried.front())];
}

QLineEdit* JointChip::fieldFor(Slot slot) const
{
    const std::vector<Slot> carried = familySlots();
    if (std::find(carried.begin(), carried.end(), slot) == carried.end()) return nullptr;
    return myFields[static_cast<std::size_t>(slot)];
}

bool JointChip::isFieldInvalid(Slot slot) const
{
    return myInvalid[static_cast<std::size_t>(slot)];
}

QPushButton* JointChip::hostButtonFor(int bodyId) const
{
    for (std::size_t i = 0; i < myHostButtons.size(); ++i) {
        if (myHostBodies[i] == bodyId && myHostButtons[i] && myHostButtons[i]->isVisible())
            return myHostButtons[i];
    }
    return nullptr;
}

QPushButton* JointChip::drilledFromButton(Joinery::DrilledFrom from) const
{
    QPushButton* button = myDrilledFromButtons[static_cast<std::size_t>(from)];
    return button && button->isVisible() ? button : nullptr;
}

bool JointChip::hasMore() const
{
    // Everything except a half-lap, whose two depths ARE its whole parameter
    // set: there is nothing behind a More line that would open on nothing.
    return myJointId > 0 && myJoint.kind != Joinery::Kind::HalfLap;
}

void JointChip::setMoreOpen(bool open)
{
    if (myMoreOpen == open || !hasMore()) return;
    myMoreOpen = open;
    relayout();
    reposition();
}

bool JointChip::kindMenuOpen() const
{
    return myKindMenu && myKindMenu->isVisible();
}

QWidget* JointChip::kindMenu() const
{
    return myKindMenu;
}

void JointChip::openKindMenu()
{
    if (myJointId <= 0 || !myWindow || !myView) return;
    if (!myKindMenu) {
        myKindMenu = new JointKindMenu(myView, [this](Joinery::Kind kind) { onKindPicked(kind); });
    }
    Joinery::ContactResult contact;
    myWindow->jointContact(myJointId, contact);
    myKindMenu->rebuild(myJoint.kind, contact);
    placeKindMenu();
    myKindMenu->show();
    myKindMenu->raise();
}

void JointChip::closeKindMenu()
{
    if (myKindMenu) myKindMenu->hide();
}

void JointChip::placeKindMenu()
{
    if (!myKindMenu || !myView || !myKindButton) return;
    const QPoint anchor = myKindButton->mapTo(myView, QPoint(0, myKindButton->height()));
    int x = anchor.x();
    int y = anchor.y() + kMenuGapUnderButton;
    x = std::clamp(x, GestureChip::kEdgeInset,
                   std::max(GestureChip::kEdgeInset,
                            myView->width() - myKindMenu->width() - GestureChip::kEdgeInset));
    // Above the button when there is no room below it, rather than clamped over
    // the card it belongs to.
    if (y + myKindMenu->height() > myView->height() - GestureChip::kEdgeInset)
        y = anchor.y() - myKindButton->height() - kMenuGapUnderButton - myKindMenu->height();
    y = std::clamp(y, GestureChip::kEdgeInset,
                   std::max(GestureChip::kEdgeInset,
                            myView->height() - myKindMenu->height() - GestureChip::kEdgeInset));
    const QPoint origin = myView->mapTo(myView->window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    myKindMenu->move(Theme::snapToDevicePixels(x, origin.x(), dpr),
                     Theme::snapToDevicePixels(y, origin.y(), dpr));
}

void JointChip::onKindPicked(Joinery::Kind kind)
{
    closeKindMenu();
    if (myJointId <= 0 || !myWindow) return;
    if (kind == myJoint.kind) return;   // already this kind: no checkpoint, nothing said
    myWindow->setJointKind(myJointId, kind);
}

QAbstractButton* JointChip::kindMenuEntry(Joinery::Kind kind) const
{
    return myKindMenu ? myKindMenu->entryFor(kind) : nullptr;
}

QString JointChip::kindMenuReason(Joinery::Kind kind) const
{
    return myKindMenu ? myKindMenu->reasonFor(kind) : QString();
}

QString JointChip::kindButtonText() const
{
    if (myJointId <= 0) return QString();
    return QStringLiteral("%1 ▾").arg(QString::fromStdString(Joinery::kindName(myJoint.kind)));
}

QString JointChip::labelText() const
{
    if (myJointId <= 0) return QString();
    return QString::fromStdString(Joinery::kindName(myJoint.kind));
}

QString JointChip::valueText() const
{
    const std::vector<Slot> carried = familySlots();
    if (carried.empty()) return QString();
    const bool lap = myJoint.kind == Joinery::Kind::HalfLap;
    QLineEdit* first = myFields[static_cast<std::size_t>(carried.front())];
    if (!lap) return first ? first->text() : QString();
    QLineEdit* second = myFields[static_cast<std::size_t>(Slot::DepthB)];
    return QStringLiteral("%1, %2").arg(first ? first->text() : QString(),
                                        second ? second->text() : QString());
}

QString JointChip::hintText() const
{
    // BevelArrow's own wording, to the letter: two chips with one Enter/Escape
    // contract must not word it two ways.
    return tr("Enter applies, Esc cancels");
}

std::vector<JointChip::Slot> JointChip::familySlots() const
{
    if (myJointId <= 0) return {};
    switch (Joinery::familyOf(myJoint.kind)) {
        case Joinery::Family::Fasteners:
            return {Slot::Count, Slot::Size, Slot::Inset, Slot::DepthA, Slot::DepthB};
        case Joinery::Family::Housing:
            return {Slot::DepthA, Slot::Width, Slot::Stop};
        case Joinery::Family::Interlock:
            if (myJoint.kind == Joinery::Kind::HalfLap) return {Slot::DepthA, Slot::DepthB};
            return {Slot::Length, Slot::Thickness};
    }
    return {};
}

namespace {

// The bare word for a slot - what a refusal names, and what the label reads
// before its unit is added. Kind-dependent where one field means two things:
// depthA is a drill depth in a fastener, a channel's depth in a housing and
// the half a lap takes out in an interlock.
QString slotWord(JointChip::Slot slot, Joinery::Kind kind)
{
    using Slot = JointChip::Slot;
    const Joinery::Family family = Joinery::familyOf(kind);
    switch (slot) {
        case Slot::Count:     return QCoreApplication::translate("JointChip", "Count");
        case Slot::Size:      return QCoreApplication::translate("JointChip", "Size");
        case Slot::Inset:     return QCoreApplication::translate("JointChip", "Inset");
        case Slot::Width:     return QCoreApplication::translate("JointChip", "Width");
        case Slot::Stop:      return QCoreApplication::translate("JointChip", "Stop");
        case Slot::Thickness: return QCoreApplication::translate("JointChip", "Tenon thickness");
        case Slot::Length:    return QCoreApplication::translate("JointChip", "Tenon length");
        case Slot::DepthA:
        case Slot::DepthB:
            if (kind == Joinery::Kind::HalfLap)
                return QCoreApplication::translate("JointChip", "Depth removed");
            if (family == Joinery::Family::Housing)
                return QCoreApplication::translate("JointChip", "Depth");
            return QCoreApplication::translate("JointChip", "Drill depth");
    }
    return QString();
}

}   // namespace

QString JointChip::slotLabel(Slot slot) const
{
    const QString word = slotWord(slot, myJoint.kind);
    // Anything but a count is a LENGTH, and the label names the unit it is read
    // in - ExtrudePreview's own rule, because a field that displays one unit
    // and reads another is the trap this app exists not to set.
    if (slot == Slot::Count) return word;
    return QStringLiteral("%1 (%2)").arg(word, unitWord());
}

QString JointChip::textFor(Slot slot, const Joinery::Parameters& params) const
{
    switch (slot) {
        case Slot::Count:     return QString::number(params.count);
        case Slot::Size:      return numberText(params.sizeMm);
        case Slot::Inset:     return numberText(params.insetMm);
        case Slot::DepthA:    return numberText(params.depthAMm);
        case Slot::DepthB:    return numberText(params.depthBMm);
        case Slot::Width:     return numberText(params.widthMm);
        case Slot::Stop:      return numberText(params.stopMm);
        case Slot::Thickness: return numberText(params.thicknessMm);
        case Slot::Length:    return numberText(params.lengthMm);
    }
    return QString();
}

bool JointChip::parseSlot(Slot slot, Joinery::Parameters& into, QString& why) const
{
    QLineEdit* edit = myFields[static_cast<std::size_t>(slot)];
    if (!edit) return true;
    const QString text = edit->text().trimmed();
    const QString word = slotWord(slot, myJoint.kind);

    if (slot == Slot::Count) {
        bool ok = false;
        const int value = text.toInt(&ok);
        if (!ok) {
            why = tr("Count needs a whole number — like 3");
            return false;
        }
        into.count = value;
        return true;
    }

    // Through Measure::parseLength, never toDouble: the label names the display
    // unit, so typing 4 with centimetres showing has to mean 40 mm.
    double millimetres = 0.0;
    if (!Measure::parseLength(text.toStdString(), millimetres)) {
        why = tr("%1 needs a number — like 12 or 12.5").arg(word);
        return false;
    }
    // An inset and a stop are distances that can legitimately be nothing at
    // all; a size, a depth or a thickness of zero is not a joint.
    const bool zeroAllowed = slot == Slot::Inset || slot == Slot::Stop;
    if (millimetres < 0.0 || (!zeroAllowed && millimetres <= 0.0)) {
        why = zeroAllowed ? tr("%1 can't be below 0").arg(word)
                          : tr("%1 has to be more than 0").arg(word);
        return false;
    }

    switch (slot) {
        case Slot::Size:      into.sizeMm = millimetres; break;
        case Slot::Inset:     into.insetMm = millimetres; break;
        case Slot::DepthA:    into.depthAMm = millimetres; break;
        case Slot::DepthB:    into.depthBMm = millimetres; break;
        case Slot::Width:     into.widthMm = millimetres; break;
        case Slot::Stop:      into.stopMm = millimetres; break;
        case Slot::Thickness: into.thicknessMm = millimetres; break;
        case Slot::Length:    into.lengthMm = millimetres; break;
        case Slot::Count:     break;
    }
    return true;
}

void JointChip::onFieldEdited(Slot slot)
{
    if (myJointId <= 0) return;
    Joinery::Parameters probe = myJoint.params;
    QString why;
    const bool parses = parseSlot(slot, probe, why);
    // A count below one is refused at the chip too - not only in MainWindow,
    // where the toast lives - so the field says so while it is being typed.
    const bool ok = parses && !(slot == Slot::Count && probe.count < 1);
    const std::size_t index = static_cast<std::size_t>(slot);
    if (myInvalid[index] == !ok) return;
    myInvalid[index] = !ok;
    restyleField(slot);
}

QString JointChip::seedSignature(const DocumentModel::Joint& joint) const
{
    const Joinery::Parameters& p = joint.params;
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17|%18")
        .arg(static_cast<int>(joint.kind))
        .arg(joint.bodyA)
        .arg(joint.bodyB)
        .arg(p.count)
        .arg(p.sizeMm)
        .arg(p.depthAMm)
        .arg(p.depthBMm)
        .arg(p.insetMm)
        .arg(p.endMarginMm)
        .arg(p.angleDeg)
        .arg(p.widthMm)
        .arg(p.stopped ? 1 : 0)
        .arg(p.stopMm)
        .arg(p.thicknessMm)
        .arg(p.lengthMm)
        .arg(p.haunched ? 1 : 0)
        .arg(static_cast<int>(p.drilledFrom))
        // The names and the display unit are painted and READ BACK, so a rename
        // or a unit switch has to re-seed, not merely repaint - ExtrudePreview's
        // own lesson, one field further along.
        + QStringLiteral("|%1|%2|%3").arg(myNameA, myNameB, unitWord());
}

void JointChip::reseed(const DocumentModel::Joint& joint)
{
    myJoint = joint;
    for (int i = 0; i < kSlotCount; ++i) {
        QLineEdit* edit = myFields[static_cast<std::size_t>(i)];
        if (!edit) continue;
        const QString text = textFor(static_cast<Slot>(i), joint.params);
        mySeededText[static_cast<std::size_t>(i)] = text;
        edit->blockSignals(true);
        edit->setText(text);
        edit->blockSignals(false);
        myInvalid[static_cast<std::size_t>(i)] = false;
        restyleField(static_cast<Slot>(i));
    }
    if (myStoppedButton) myStoppedButton->setChecked(joint.params.stopped);
    if (myHaunchedButton) myHaunchedButton->setChecked(joint.params.haunched);
    myPendingDrilledFrom = joint.params.drilledFrom;
    // The two pieces in a stable order - by id, so a host swap moves the tick
    // rather than trading the two buttons' places under the cursor.
    myHostBodies[0] = std::min(joint.bodyA, joint.bodyB);
    myHostBodies[1] = std::max(joint.bodyA, joint.bodyB);
    syncToggleTexts();
}

void JointChip::syncToggleTexts()
{
    if (myStoppedButton)
        myStoppedButton->setText(myStoppedButton->isChecked() ? tr("On") : tr("Off"));
    if (myHaunchedButton)
        myHaunchedButton->setText(myHaunchedButton->isChecked() ? tr("On") : tr("Off"));
    // The stop distance is only a number while the channel is stopped.
    if (QLineEdit* stop = myFields[static_cast<std::size_t>(Slot::Stop)])
        stop->setEnabled(myStoppedButton == nullptr || myStoppedButton->isChecked());

    if (myJointId > 0) {
        const QString nameFor[2] = {
            myHostBodies[0] == myJoint.bodyA ? myNameA : myNameB,
            myHostBodies[1] == myJoint.bodyA ? myNameA : myNameB,
        };
        for (std::size_t i = 0; i < myHostButtons.size(); ++i) {
            if (!myHostButtons[i]) continue;
            myHostButtons[i]->setText(nameFor[i]);
            myHostButtons[i]->setChecked(myHostBodies[i] == myJoint.bodyA);
        }
    }

    Joinery::Derivation derivation;
    const bool derived = myWindow && myJointId > 0 &&
                         myWindow->jointDerivationOf(myJointId, derivation) && derivation.ok;
    for (std::size_t i = 0; i < myDrilledFromButtons.size(); ++i) {
        QPushButton* button = myDrilledFromButtons[i];
        if (!button) continue;
        const Joinery::DrilledFrom from = static_cast<Joinery::DrilledFrom>(i);
        bool named = false;
        const QString word =
            derived ? QString::fromStdString(
                          Joinery::drilledFromFaceName(derivation.contact, from, named))
                    : QString();
        // The face's own word when the board lines up with a world axis, and
        // the two sides' plain names when it does not - the honest sentence
        // Joinery returns there is a sentence, not a button's worth of text.
        button->setText(named ? tr("%1 face").arg(word)
                              : (from == Joinery::DrilledFrom::InsetFace ? tr("Inset side")
                                                                         : tr("Far side")));
        button->setChecked(from == myPendingDrilledFrom);
    }
}

void JointChip::updateAnchor()
{
    gp_Pnt anchor;
    if (myWindow && myJointId > 0 && myWindow->jointAnchor(myJointId, anchor)) myAnchor = anchor;
}

void JointChip::refresh()
{
    if (!myWindow || !myView) return;

    // THE predicate, in one place - MainWindow::jointChipJointId(), the same
    // answer updateActions() and the drawing gate read. Nothing here decides
    // visibility for itself.
    const int id = myWindow->jointChipJointId();
    DocumentModel::Joint joint;
    if (id <= 0 || !myWindow->jointOf(id, joint)) {
        end();
        return;
    }

    myNameA = QString::fromStdString(myWindow->document().nameOf(joint.bodyA));
    myNameB = QString::fromStdString(myWindow->document().nameOf(joint.bodyB));
    const bool fresh = id != myJointId;
    myJointId = id;
    if (fresh) {
        // More does not persist: a chip that opens on another joint opens
        // small, exactly as the mockup's own "deselecting closes it back up".
        myMoreOpen = false;
        closeKindMenu();
    }

    const QString seed = seedSignature(joint);
    if (fresh || seed != mySeed) {
        mySeed = seed;
        reseed(joint);
        relayout();
        if (kindMenuOpen()) {
            Joinery::ContactResult contact;
            myWindow->jointContact(myJointId, contact);
            myKindMenu->rebuild(myJoint.kind, contact);
            placeKindMenu();
        }
    }

    updateAnchor();
    reposition();
    if (!isVisible()) {
        show();
        raise();
    }
}

void JointChip::end()
{
    if (myJointId == 0 && !isVisible()) return;
    closeKindMenu();
    myJointId = 0;
    mySeed.clear();
    hide();
}

void JointChip::relayout()
{
    if (myJointId <= 0) return;
    ++myLayoutCount;
    myTexts.clear();
    myRuleY = -1;

    const QFontMetrics badge(Theme::badgeFont());
    const QFontMetrics label(Theme::labelFont());
    const Joinery::Family family = Joinery::familyOf(myJoint.kind);
    const bool lap = myJoint.kind == Joinery::Kind::HalfLap;
    const bool pocket = myJoint.kind == Joinery::Kind::PocketScrew;
    const bool hostChoice = family == Joinery::Family::Housing ||
                            (family == Joinery::Family::Interlock && !lap);

    // Everything starts hidden and only what this kind carries is placed, so a
    // control can never survive a kind switch it does not belong to.
    for (QLineEdit* edit : myFields) {
        if (edit) edit->hide();
    }
    for (QPushButton* button : {myStoppedButton, myHaunchedButton, myHostButtons[0],
                                myHostButtons[1], myDrilledFromButtons[0],
                                myDrilledFromButtons[1]}) {
        if (button) button->hide();
    }

    // THE WIDTH, measured with the fonts these strings are painted in -
    // CLAUDE.md's rule, which exists because a title measured non-bold and
    // painted bold clips.
    const QString kindText = kindButtonText();
    const int kindWidth = label.horizontalAdvance(kindText) + 22;
    const QString names = myNameA + QStringLiteral(" ↔ ") + myNameB;
    int column = kColumnWidth;
    for (const Slot slot : familySlots())
        column = std::max(column, std::min(badge.horizontalAdvance(slotLabel(slot)) + 4,
                                           kMaxColumnWidth));
    int content = column * 2 + kColumnGap;
    content = std::max(content, kindWidth + kNamesGap +
                                    std::min(label.horizontalAdvance(names), kMaxNamesWidth));
    content = std::max(content, badge.horizontalAdvance(hintText()));

    int y = kPad;
    const auto addText = [&](const QString& raw, const QFont& font, const QPointF& at, bool userData,
                             int maxWidth) {
        const QFontMetrics metrics(font);
        PaintedText painted;
        painted.raw = raw;
        painted.text.setTextFormat(Qt::PlainText);
        painted.text.setText(metrics.elidedText(raw, Qt::ElideRight, std::max(maxWidth, 8)));
        painted.text.prepare(QTransform(), font);
        painted.at = at;
        painted.font = font;
        painted.muted = true;
        painted.userData = userData;
        myTexts.push_back(std::move(painted));
    };

    // --- the kind line: the kind button and the two pieces ------------------
    myKindButton->setText(kindText);
    myKindButton->setGeometry(kPad, y, kindWidth, kKindHeight);
    myKindButton->show();
    addText(names, Theme::labelFont(),
            QPointF(kPad + kindWidth + kNamesGap, y + (kKindHeight - label.height()) / 2.0),
            /*userData=*/true, content - kindWidth - kNamesGap);
    y += kKindHeight + kBlockGap;

    const int cellHeight = kLabelHeight + kLabelGap + kBoxHeight;
    const auto placeField = [&](Slot slot, int x, int width) {
        QLineEdit* edit = myFields[static_cast<std::size_t>(slot)];
        if (!edit) return;
        addText(slotLabel(slot), Theme::badgeFont(), QPointF(x, y), /*userData=*/false, width);
        edit->setGeometry(x, y + kLabelHeight + kLabelGap, width, kBoxHeight);
        edit->show();
    };
    const auto placeToggle = [&](QPushButton* button, const QString& text, int x, int width) {
        if (!button) return;
        addText(text, Theme::badgeFont(), QPointF(x, y), /*userData=*/false, width);
        button->setGeometry(x, y + kLabelHeight + kLabelGap, width, kBoxHeight);
        button->show();
    };
    // A label for a row of two controls, optionally naming the two pieces the
    // way the mockup's drill-depth pair does. The app's own word and the user's
    // names are two draw calls, so the sweep's two channels stay apart.
    const auto placePairLabel = [&](const QString& word, bool withNames) {
        addText(word, Theme::badgeFont(), QPointF(kPad, y), /*userData=*/false, content);
        if (!withNames) return;
        const int used = badge.horizontalAdvance(word) + 6;
        addText(QStringLiteral("· %1, %2").arg(myNameA, myNameB), Theme::badgeFont(),
                QPointF(kPad + used, y), /*userData=*/true, content - used);
    };
    const auto placePair = [&](QWidget* first, QWidget* second) {
        const int half = (content - kPairGap) / 2;
        first->setGeometry(kPad, y + kLabelHeight + kLabelGap, half, kBoxHeight);
        first->show();
        second->setGeometry(kPad + half + kPairGap, y + kLabelHeight + kLabelGap,
                            content - half - kPairGap, kBoxHeight);
        second->show();
    };

    // --- the main number, the one the family is defined by ------------------
    if (lap) {
        placePairLabel(slotLabel(Slot::DepthA), /*withNames=*/true);
        placePair(myFields[static_cast<std::size_t>(Slot::DepthA)],
                  myFields[static_cast<std::size_t>(Slot::DepthB)]);
    } else {
        const Slot main = familySlots().front();
        placeField(main, kPad, column);
    }
    y += cellHeight;

    // --- More, and what it opens -------------------------------------------
    if (hasMore()) {
        myMoreButton->setText(myMoreOpen ? tr("Fewer ▴") : tr("More ▾"));
        myMoreButton->setGeometry(kPad, y + kMoreGap,
                                  badge.horizontalAdvance(myMoreButton->text()) + 6, kMoreHeight);
        myMoreButton->show();
        y += kMoreGap + kMoreHeight;

        if (myMoreOpen) {
            y += kRuleGap;
            myRuleY = y;
            y += 1 + kRuleGap;

            if (family == Joinery::Family::Fasteners) {
                placeField(Slot::Size, kPad, column);
                placeField(Slot::Inset, kPad + column + kColumnGap, column);
                y += cellHeight + kRowGap;
                placePairLabel(slotLabel(Slot::DepthA), /*withNames=*/true);
                placePair(myFields[static_cast<std::size_t>(Slot::DepthA)],
                          myFields[static_cast<std::size_t>(Slot::DepthB)]);
                y += cellHeight;
                if (pocket) {
                    y += kRowGap;
                    placePairLabel(tr("Drilled from"), /*withNames=*/false);
                    placePair(myDrilledFromButtons[0], myDrilledFromButtons[1]);
                    y += cellHeight;
                }
            } else if (family == Joinery::Family::Housing) {
                placeField(Slot::Width, kPad, column);
                placeToggle(myStoppedButton, tr("Stopped"), kPad + column + kColumnGap, column);
                y += cellHeight + kRowGap;
                placeField(Slot::Stop, kPad, column);
                y += cellHeight + kRowGap;
            } else {
                placeField(Slot::Thickness, kPad, column);
                placeToggle(myHaunchedButton, tr("Haunched"), kPad + column + kColumnGap, column);
                y += cellHeight + kRowGap;
            }

            if (hostChoice) {
                placePairLabel(tr("Cut into"), /*withNames=*/false);
                placePair(myHostButtons[0], myHostButtons[1]);
                y += cellHeight;
            }
        }
    } else {
        myMoreButton->hide();
    }

    y += kBlockGap;
    addText(hintText(), Theme::badgeFont(), QPointF(kPad, y), /*userData=*/false, content);
    y += kHintHeight + kPad;

    // Through Theme::wholeDevicePixels(), not straight to setFixedSize() - a
    // fractional device row over the GL surface is a row this card's own
    // painter cannot reach. Re-measured here, so More opening and closing each
    // land on a whole size of their own.
    setFixedSize(Theme::wholeDevicePixels(QSize(content + kPad * 2, y)));

    const std::vector<QLineEdit*> order = tabOrder();
    for (std::size_t i = 1; i < order.size(); ++i) setTabOrder(order[i - 1], order[i]);
    syncToggleTexts();
    update();
}

std::vector<QLineEdit*> JointChip::tabOrder() const
{
    std::vector<QLineEdit*> order;
    for (const Slot slot : familySlots()) {
        QLineEdit* edit = myFields[static_cast<std::size_t>(slot)];
        if (edit && edit->isVisible() && edit->isEnabled()) order.push_back(edit);
    }
    return order;
}

bool JointChip::focusNextPrevChild(bool next)
{
    // Tab moves between THIS card's fields while More is open, rather than
    // walking out into the window's own chain halfway through an edit.
    const std::vector<QLineEdit*> order = tabOrder();
    if (order.size() < 2) return QWidget::focusNextPrevChild(next);
    QWidget* focused = window() ? window()->focusWidget() : nullptr;
    const auto at = std::find(order.begin(), order.end(), focused);
    if (at == order.end()) return QWidget::focusNextPrevChild(next);
    const std::size_t index = static_cast<std::size_t>(std::distance(order.begin(), at));
    const std::size_t count = order.size();
    const std::size_t target = next ? (index + 1) % count : (index + count - 1) % count;
    order[target]->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    order[target]->selectAll();
    return true;
}

void JointChip::commit()
{
    if (myJointId <= 0 || !myWindow) return;
    // The menu owns the keys while it is open: Enter closes it rather than
    // committing a chip the user is not looking at.
    if (kindMenuOpen()) {
        closeKindMenu();
        return;
    }

    Joinery::Parameters params = myJoint.params;
    bool changed = false;
    QString refusal;
    for (const Slot slot : familySlots()) {
        QLineEdit* edit = myFields[static_cast<std::size_t>(slot)];
        if (!edit) continue;
        // Only what the user actually changed is parsed back: a field left
        // alone keeps the joint's own exact value rather than the rounded one
        // its display carries (13.25 mm reads as "13.3" at one decimal).
        if (edit->text() == mySeededText[static_cast<std::size_t>(slot)]) continue;
        QString why;
        if (!parseSlot(slot, params, why)) {
            myInvalid[static_cast<std::size_t>(slot)] = true;
            restyleField(slot);
            if (refusal.isEmpty()) refusal = why;
            continue;
        }
        changed = true;
    }
    if (!refusal.isEmpty()) {
        // Never silent: a chip that does nothing on Enter is indistinguishable
        // from a broken one.
        myWindow->refuseJointEdit(refusal);
        return;
    }

    const Joinery::Family family = Joinery::familyOf(myJoint.kind);
    if (family == Joinery::Family::Housing && myStoppedButton &&
        myStoppedButton->isChecked() != myJoint.params.stopped) {
        params.stopped = myStoppedButton->isChecked();
        changed = true;
    }
    if (myJoint.kind == Joinery::Kind::MortiseTenon && myHaunchedButton &&
        myHaunchedButton->isChecked() != myJoint.params.haunched) {
        params.haunched = myHaunchedButton->isChecked();
        changed = true;
    }
    if (myJoint.kind == Joinery::Kind::PocketScrew &&
        myPendingDrilledFrom != myJoint.params.drilledFrom) {
        params.drilledFrom = myPendingDrilledFrom;
        changed = true;
    }

    if (!changed) return;   // nothing to apply is not a checkpoint
    if (!myWindow->editJointParameters(myJointId, params)) {
        // editJointParameters() said why; the field that carried it is marked
        // so the card shows WHICH number was refused.
        if (params.count < 1) {
            myInvalid[static_cast<std::size_t>(Slot::Count)] = true;
            restyleField(Slot::Count);
        }
        return;
    }
    // On success the window's own appStateChanged has already re-seeded this
    // card through refresh() - the signature moved with the parameters.
}

void JointChip::cancel()
{
    if (kindMenuOpen()) {
        closeKindMenu();
        return;
    }
    if (myJointId <= 0 || !myWindow) return;
    DocumentModel::Joint joint;
    if (!myWindow->jointOf(myJointId, joint)) return;
    // The fields go back to the joint's own values. The joint itself is
    // deliberately untouched - and so is the selection, so the card stays up
    // and the user can simply type again.
    reseed(joint);
    update();
}

QStringList JointChip::paintedTexts() const
{
    QStringList texts;
    if (myJointId <= 0) return texts;
    texts << kindButtonText() << hintText();
    if (myMoreButton && myMoreButton->isVisible()) texts << myMoreButton->text();
    for (const PaintedText& painted : myTexts) {
        if (!painted.userData) texts << painted.raw;
    }
    for (QPushButton* button : {myStoppedButton, myHaunchedButton, myDrilledFromButtons[0],
                                myDrilledFromButtons[1]}) {
        if (button && button->isVisible()) texts << button->text();
    }
    if (myKindMenu) texts << myKindMenu->paintedTexts();
    return texts;
}

QStringList JointChip::paintedNames() const
{
    QStringList names;
    if (myJointId <= 0) return names;
    names << myNameA << myNameB;
    return names;
}

void JointChip::reposition()
{
    if (!myView || myJointId <= 0) return;
    QPoint at;
    // projectToScreen() answers false while the viewer is mid-rebuild (see
    // OcctViewWidget::viewReady()), which is exactly what a GL-context-loss
    // recovery does underneath this signal - the card simply stays where it is
    // until the view has a window again.
    if (!myView->projectToScreen(myAnchor, at)) return;
    // Beside the anchor, flipped rather than clamped when it would run off the
    // right edge, and snapped to whole device pixels - GestureChip's one
    // implementation, shared with the pull and bevel chips.
    move(GestureChip::placeBeside(this, myView, at));
    if (kindMenuOpen()) placeKindMenu();
}

void JointChip::replace()
{
    reposition();
    if (!isVisible()) return;
    raise();
    if (myKindMenu && myKindMenu->isVisible()) myKindMenu->raise();
}

void JointChip::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    GestureChip::paintFrame(painter, this, false);

    if (myRuleY >= 0) {
        painter.fillRect(QRectF(kPad, myRuleY, width() - kPad * 2, 1.0), Theme::border());
    }
    // Prepared in relayout(): no measuring, eliding or shaping here. This card
    // is charged to every frame of an orbit, like every other overlay.
    for (const PaintedText& painted : myTexts) {
        painter.setFont(painted.font);
        painter.setPen(painted.muted ? Theme::textMuted() : Theme::text());
        painter.drawStaticText(painted.at, painted.text);
    }
}

void JointChip::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    mySwallowNextRelease = false;
    // Installed for exactly as long as the card is up - ShortcutSheet's and
    // ExtrudePreview's lifetime rule for an application-wide filter.
    QCoreApplication::instance()->installEventFilter(this);
}

void JointChip::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    closeKindMenu();
    if (!mySwallowNextRelease) QCoreApplication::instance()->removeEventFilter(this);
}

void JointChip::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

void JointChip::mouseReleaseEvent(QMouseEvent* event)
{
    // A widget that accepts a press must accept the release too - the viewport
    // picks on the RELEASE, and a press this card swallowed followed by a
    // release it did not would re-pick behind it.
    event->accept();
}

void JointChip::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
}

bool JointChip::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (mySwallowNextRelease && type == QEvent::MouseButtonRelease) {
        mySwallowNextRelease = false;
        if (!isVisible()) QCoreApplication::instance()->removeEventFilter(this);
        return true;
    }
    if (!isVisible()) return QWidget::eventFilter(watched, event);

    // Only events headed for this card's own window - gui_smoke builds several
    // at once, and a key in the library window is not this chip's.
    const auto ours = [this](QObject* target) {
        if (auto* widget = qobject_cast<QWidget*>(target)) return widget->window() == window();
        if (auto* handle = qobject_cast<QWindow*>(target))
            return window() != nullptr && handle == window()->windowHandle();
        return false;
    };

    if (kindMenuOpen()) {
        if (type == QEvent::MouseButtonPress && ours(watched)) {
            // Outside-ness is GEOMETRIC, never the watched object's identity: a
            // real press reaches an application filter first as the top-level
            // window's event, so an identity test would read the menu's own
            // rows as outside and swallow them - ShapeFlyout paid for that one.
            const QPoint global = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
            if (!myKindMenu->rect().contains(myKindMenu->mapFromGlobal(global))) {
                closeKindMenu();
                mySwallowNextRelease = true;
                return true;
            }
        }
        if ((type == QEvent::KeyPress || type == QEvent::ShortcutOverride) && ours(watched)) {
            auto* key = static_cast<QKeyEvent*>(event);
            const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;
            const bool closes = key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return ||
                                key->key() == Qt::Key_Enter;
            if (mods == Qt::NoModifier && closes) {
                // The menu's Escape is the CHIP's filter, not a second
                // application-wide claim of its own - see JointKindMenu.
                if (type == QEvent::KeyPress) closeKindMenu();
                event->accept();
                return true;
            }
        }
    }

    // A text field that is not ours keeps its own Enter and Escape: an inline
    // rename in a drawer must still commit with Enter while this card is up.
    if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
        if (edit->parentWidget() != this) return QWidget::eventFilter(watched, event);
    }

    int key = 0;
    if (KeyClaim::claim(this, watched, event, /*wantEnter=*/true, /*exemptLineEdits=*/false, &key)) {
        if (key == Qt::Key_Escape)
            cancel();
        else if (key != 0)
            commit();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

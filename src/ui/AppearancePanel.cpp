#include "AppearancePanel.h"

#include "Theme.h"

#include <QAbstractButton>
#include <QColorDialog>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace {

// Wider than the items drawer's 240 - a row here carries a name AND a swatch,
// and "Focus ring — inactive window" is a long name. Fixed, for the same
// reason the drawer's is: a floating card that changed width with its content
// would move the viewport's usable area around under the user.
constexpr int kWidth = 296;
// Tall enough to be worth scrolling and short enough to fit under the axis
// gizmo at the shortest viewport MainWindow allows (the rail's own height
// plus two edge margins). The scroll area inside is what makes the remaining
// rows reachable; growing this card to fit all of them would make it the
// tallest thing in the shell.
constexpr int kHeight = 380;
// The rail's and the drawer's radius, not the family default of 8: this card
// sits against the same top edge as those two and a different corner between
// neighbours reads as a mistake.
constexpr int kRadius = 10;
constexpr int kPad = 12;

constexpr int kSwatchWidth = 46;
constexpr int kSwatchHeight = 18;
constexpr int kSwatchRadius = 4;

// `background: transparent` on a child of this card is not decoration: the
// app-wide stylesheet paints every QWidget chrome-black, so a label or a
// container that stamped its own rectangle would be a black bar across the
// card. Addressed by object name so the rule reaches THAT widget and not its
// whole subtree - an unqualified rule set on a container hands its children
// the same fill and costs them their own chrome, which is the bug
// ItemsPanel::showSelection() already had to name its rows to avoid.
void makeTransparent(QWidget* widget, const QString& name)
{
    widget->setObjectName(name);
    widget->setStyleSheet(QStringLiteral("#%1 { background: transparent; border: none; }")
                              .arg(name));
}

}  // namespace

// --- Swatch ------------------------------------------------------------------

// One token's colour, as a button. Custom-painted rather than a QPushButton
// with a background stylesheet for the same reason every other card in this
// shell is: a stylesheet background is a square, and this is a rounded chip
// with the family's own 1px border. It also gives the suite a pixel to sample
// that is unambiguously the token's colour and nothing else.
class Swatch : public QAbstractButton {
public:
    explicit Swatch(QWidget* parent) : QAbstractButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(kSwatchWidth, kSwatchHeight);
    }

    void setColour(const QColor& colour)
    {
        if (myColour == colour) return;
        myColour = colour;
        update();
    }
    QColor colour() const { return myColour; }

    QSize sizeHint() const override { return QSize(kSwatchWidth, kSwatchHeight); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // The card underneath, first and across the whole rect, so the four
        // corners outside the rounded chip read as panel() rather than as
        // whatever was in the backing store.
        painter.fillRect(rect(), Theme::panel());

        QPainterPath path;
        path.addRoundedRect(rect(), kSwatchRadius, kSwatchRadius);
        painter.fillPath(path, myColour);
        Theme::drawCrispBorder(painter, QRectF(rect()), Theme::border(), kSwatchRadius);

        if (this == window()->focusWidget()) {
            const bool active = window()->isActiveWindow();
            Theme::drawCrispBorder(painter, QRectF(rect()).adjusted(2, 2, -2, -2),
                                   active ? Theme::focusRing() : Theme::focusRingMuted(),
                                   kSwatchRadius - 2, active ? 2.0 : 1.5);
        }
    }

private:
    QColor myColour;
};

// --- AppearancePanel ---------------------------------------------------------

QString AppearancePanel::nameForToken(const QString& id)
{
    // The user's words, never the code's. `gridMinor` is a member name;
    // "Grid lines" is what somebody choosing a colour is looking for. Swept
    // for banned words through paintedTexts() like every other painted string
    // in the shell.
    static const QHash<QString, QString> names = {
        {QStringLiteral("viewport"), QObject::tr("Viewport")},
        {QStringLiteral("gridMinor"), QObject::tr("Grid lines")},
        {QStringLiteral("gridMajor"), QObject::tr("Grid — every tenth line")},
        {QStringLiteral("axisX"), QObject::tr("X axis")},
        {QStringLiteral("axisY"), QObject::tr("Y axis")},
        {QStringLiteral("highlightHover"), QObject::tr("Hover highlight")},
        {QStringLiteral("highlightSelected"), QObject::tr("Selection highlight")},
        {QStringLiteral("sketchPointMarker"), QObject::tr("Outline points")},
        {QStringLiteral("chrome"), QObject::tr("Top bar and status bar")},
        {QStringLiteral("panel"), QObject::tr("Panels")},
        {QStringLiteral("border"), QObject::tr("Borders")},
        {QStringLiteral("chip"), QObject::tr("Buttons")},
        {QStringLiteral("chipHover"), QObject::tr("Buttons — hovered")},
        {QStringLiteral("chipActive"), QObject::tr("Buttons — pressed")},
        {QStringLiteral("text"), QObject::tr("Text")},
        {QStringLiteral("textMuted"), QObject::tr("Quieter text")},
        {QStringLiteral("textDisabled"), QObject::tr("Unavailable text")},
        {QStringLiteral("accent"), QObject::tr("Accent")},
        {QStringLiteral("danger"), QObject::tr("Warnings")},
        {QStringLiteral("focusRing"), QObject::tr("Keyboard focus ring")},
        {QStringLiteral("focusRingMuted"), QObject::tr("Focus ring — window inactive")},
    };
    return names.value(id);
}

AppearancePanel::AppearancePanel(QWidget* parent)
    : QWidget(parent)
{
    // A floating card, painted in paintEvent(). See ItemsPanel.h for the two
    // rules that follow from sitting over OCCT's GL surface: it paints its
    // entire rect opaquely, and it swallows the mouse rather than letting a
    // press through to re-pick the model behind it.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    // Through Theme::wholeDevicePixels() - see Theme.h. setFixedSize() is why
    // this card has to do it for itself: ViewportOverlay's own rounding is a
    // silent no-op on a fixed-size widget, and a card whose logical height is
    // not a whole number of device rows leaves the bottom row unpainted, which
    // over the GL surface is black rather than transparent.
    setFixedSize(Theme::wholeDevicePixels(QSize(kWidth, kHeight)));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kPad, kPad, kPad, kPad);
    outer->setSpacing(8);

    myTitle = new QLabel(tr("Appearance"), this);
    outer->addWidget(myTitle);

    // --- the scrolling list of colour rows ---------------------------------
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    makeTransparent(scroll, QStringLiteral("appearanceScroll"));
    makeTransparent(scroll->viewport(), QStringLiteral("appearanceScrollViewport"));

    auto* content = new QWidget(scroll);
    makeTransparent(content, QStringLiteral("appearanceContent"));
    auto* list = new QVBoxLayout(content);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(4);

    // Driven off Theme::colourTokens() rather than a second list written out
    // here: a token added to the spec and to that table gets a row, a swatch
    // and a persisted value with no third place to remember. A token with no
    // name in nameForToken() would get a nameless row, which is why gui_smoke
    // asserts every token has one.
    for (const Theme::ColourToken& token : Theme::colourTokens()) {
        auto* row = new QWidget(content);
        makeTransparent(row, QStringLiteral("appearanceRow_") + token.id);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(8);

        const QString name = nameForToken(token.id);
        auto* label = new QLabel(name, row);
        label->setWordWrap(false);
        makeTransparent(label, QStringLiteral("appearanceName_") + token.id);
        line->addWidget(label, 1);

        auto* swatch = new Swatch(row);
        swatch->setToolTip(tr("Pick a colour for %1").arg(name));
        const QString id = token.id;
        connect(swatch, &QAbstractButton::clicked, this,
                [this, id] { openColourDialog(id); });
        line->addWidget(swatch);

        list->addWidget(row);
        myRows.push_back(Row{token.id, name, swatch});
    }
    list->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    // --- the type controls --------------------------------------------------
    auto* sizeRow = new QWidget(this);
    makeTransparent(sizeRow, QStringLiteral("appearanceSizeRow"));
    auto* sizeLine = new QHBoxLayout(sizeRow);
    sizeLine->setContentsMargins(0, 0, 0, 0);
    sizeLine->setSpacing(8);
    mySizeLabel = new QLabel(tr("Text size"), sizeRow);
    makeTransparent(mySizeLabel, QStringLiteral("appearanceSizeLabel"));
    sizeLine->addWidget(mySizeLabel, 1);
    mySize = new QSpinBox(sizeRow);
    mySize->setRange(static_cast<int>(Theme::kMinBasePt), static_cast<int>(Theme::kMaxBasePt));
    mySize->setSuffix(tr(" pt"));
    mySize->setToolTip(tr("How big the app's text is — everything else in the "
                          "type scale moves with it"));
    connect(mySize, &QSpinBox::valueChanged, this, [this](int pt) {
        if (mySyncing) return;
        setBaseSize(pt);
    });
    sizeLine->addWidget(mySize);
    outer->addWidget(sizeRow);

    auto* familyRow = new QWidget(this);
    makeTransparent(familyRow, QStringLiteral("appearanceFamilyRow"));
    auto* familyLine = new QHBoxLayout(familyRow);
    familyLine->setContentsMargins(0, 0, 0, 0);
    familyLine->setSpacing(8);
    myFamilyLabel = new QLabel(tr("Font"), familyRow);
    makeTransparent(myFamilyLabel, QStringLiteral("appearanceFamilyLabel"));
    familyLine->addWidget(myFamilyLabel);
    myFamily = new QComboBox(familyRow);
    myFamily->setToolTip(tr("The typeface the whole app is set in"));
    {
        // The bundled family first, because it is the default and the one
        // this shell was designed around; then everything installed, so a
        // user who wants their own can have it.
        QStringList families;
        const QString bundled = Theme::defaultSpec().fontFamily;
        if (!bundled.isEmpty()) families << bundled;
        for (const QString& family : QFontDatabase::families()) {
            if (family != bundled) families << family;
        }
        myFamily->addItems(families);
    }
    connect(myFamily, &QComboBox::currentTextChanged, this, [this](const QString& family) {
        if (mySyncing) return;
        setFontFamily(family);
    });
    familyLine->addWidget(myFamily, 1);
    outer->addWidget(familyRow);

    myReset = new QPushButton(tr("Reset to the original look"), this);
    myReset->setToolTip(tr("Put every colour and the text size back the way "
                           "they shipped"));
    connect(myReset, &QPushButton::clicked, this, &AppearancePanel::reset);
    outer->addWidget(myReset);

    // The controls are filled in from the live spec here, and re-filled on
    // every broadcast - including the ones this panel itself causes, which is
    // what keeps a swatch correct after a Reset it did not have to special-case.
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this,
            &AppearancePanel::applyTheme);
}

void AppearancePanel::applyTheme()
{
    mySyncing = true;

    const Theme::Spec& live = Theme::spec();
    for (const Theme::ColourToken& token : Theme::colourTokens()) {
        for (Row& row : myRows) {
            if (row.id == token.id && row.swatch) row.swatch->setColour(live.*(token.member));
        }
    }
    if (mySize) mySize->setValue(static_cast<int>(live.basePt));
    if (myFamily) {
        const int index = myFamily->findText(live.fontFamily);
        if (index >= 0) myFamily->setCurrentIndex(index);
    }
    if (myTitle) {
        // A per-widget stylesheet wins over the app-wide one regardless of
        // selector specificity, so the size sticks here - this is a panel
        // title, Theme::titleFont(). Same shape as the items drawer's.
        myTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                              "font-weight: 600; font-size: %2pt;")
                                   .arg(Theme::textMuted().name())
                                   .arg(Theme::titleFont().pointSizeF()));
    }

    mySyncing = false;
    update();
}

void AppearancePanel::setTokenColour(const QString& id, const QColor& colour)
{
    if (!colour.isValid()) return;
    Theme::Spec next = Theme::spec();
    for (const Theme::ColourToken& token : Theme::colourTokens()) {
        if (token.id != id) continue;
        next.*(token.member) = colour;
        // Straight to Theme, never to the swatch: the swatch is repainted by
        // applyTheme() off the broadcast this causes, so there is one
        // direction of data flow and no way for the two to disagree.
        Theme::setSpec(next);
        return;
    }
}

void AppearancePanel::setBaseSize(double pt)
{
    Theme::Spec next = Theme::spec();
    next.basePt = std::clamp(pt, Theme::kMinBasePt, Theme::kMaxBasePt);
    Theme::setSpec(next);
}

void AppearancePanel::setFontFamily(const QString& family)
{
    Theme::Spec next = Theme::spec();
    next.fontFamily = family;
    Theme::setSpec(next);
}

void AppearancePanel::reset()
{
    Theme::setSpec(Theme::defaultSpec());
}

void AppearancePanel::openColourDialog(const QString& id)
{
    QColor current;
    for (const Row& row : myRows) {
        if (row.id == id && row.swatch) current = row.swatch->colour();
    }

    // One picker at a time. A second swatch clicked while one is open
    // replaces it rather than stacking - the same rule ToastHost applies to
    // messages, and for the same reason: a pile of pickers is a dialog with
    // extra steps.
    if (myDialog) {
        myDialog->close();
        myDialog = nullptr;
    }

    auto* dialog = new QColorDialog(current, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("%1 colour").arg(nameForToken(id)));
    // MODELESS - open(), never exec(). exec() would spin a nested event loop
    // and freeze the application while the user chose, which is both against
    // the no-modal law and fatal to the live preview this panel exists for.
    dialog->setModal(false);
    dialog->setOption(QColorDialog::NoButtons, false);

    // Live on every move inside the picker, not only on OK: this is the one
    // place in the app where a user judges a value by what it looks like.
    connect(dialog, &QColorDialog::currentColorChanged, this,
            [this, id](const QColor& colour) { setTokenColour(id, colour); });
    // Cancel puts back what was there when the picker opened. Without this a
    // cancelled pick would keep whichever colour the cursor last passed over.
    connect(dialog, &QColorDialog::rejected, this,
            [this, id, current] { setTokenColour(id, current); });
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (myDialog == dialog) myDialog = nullptr;
    });

    myDialog = dialog;
    dialog->open();
}

QWidget* AppearancePanel::swatchFor(const QString& id) const
{
    for (const Row& row : myRows) {
        if (row.id == id) return row.swatch;
    }
    return nullptr;
}

QWidget* AppearancePanel::resetButton() const { return myReset; }

QStringList AppearancePanel::paintedTexts() const
{
    QStringList texts;
    if (myTitle) texts << myTitle->text();
    for (const Row& row : myRows) texts << row.name;
    if (mySizeLabel) texts << mySizeLabel->text();
    if (myFamilyLabel) texts << myFamilyLabel->text();
    if (myReset) texts << myReset->text();
    return texts;
}

void AppearancePanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    // Every pixel of this widget is the card - see ItemsPanel::paintEvent().
    Theme::paintSurface(painter, rect(), kRadius);
}

void AppearancePanel::wheelEvent(QWheelEvent* event)
{
    // Accepted whether or not anything scrolled: an ignored wheel here would
    // propagate to the viewport underneath and zoom the camera, which is the
    // wheel-shaped version of the press-and-release bug WA_NoMousePropagation
    // already closes for this card.
    event->accept();
}

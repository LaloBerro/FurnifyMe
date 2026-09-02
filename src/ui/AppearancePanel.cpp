#include "AppearancePanel.h"

#include "Theme.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QColorDialog>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
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

// A colour file is a spec and nothing else - the same string QSettings holds -
// so there is no size at which reading more of one is useful. A cap rather
// than a trusting readAll(): Load colours takes a path from a file dialog,
// which is to say from anywhere, and a serialised spec is a few hundred bytes.
// Anything past this is not a colour file, and deserializeSpec() will refuse
// the truncated head just as it would refuse the whole.
constexpr qint64 kMaxColourFileBytes = 64 * 1024;

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
        {QStringLiteral("gizmoAxisX"), QObject::tr("Orientation gizmo — X")},
        {QStringLiteral("gizmoAxisY"), QObject::tr("Orientation gizmo — Y")},
        {QStringLiteral("gizmoAxisZ"), QObject::tr("Orientation gizmo — Z")},
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
        // Failures, not warnings: this token marks a refusal that already
        // happened - a Failure toast's stripe and an unparseable field's
        // outline - and the app has nothing it would call a warning.
        {QStringLiteral("danger"), QObject::tr("Failures")},
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

    auto* strokeRow = new QWidget(this);
    makeTransparent(strokeRow, QStringLiteral("appearanceStrokeRow"));
    auto* strokeLine = new QHBoxLayout(strokeRow);
    strokeLine->setContentsMargins(0, 0, 0, 0);
    strokeLine->setSpacing(8);
    myStrokeLabel = new QLabel(tr("Button border"), strokeRow);
    makeTransparent(myStrokeLabel, QStringLiteral("appearanceStrokeLabel"));
    strokeLine->addWidget(myStrokeLabel, 1);
    myStroke = new QSpinBox(strokeRow);
    // The spec's field is a double, but a border is judged in whole pixels
    // and the crisp-border idiom is built around integer alignment - so the
    // control offers integers over the full legal range, 0 included.
    myStroke->setRange(static_cast<int>(Theme::kMinChipStrokePx),
                       static_cast<int>(Theme::kMaxChipStrokePx));
    myStroke->setSuffix(tr(" px"));
    myStroke->setToolTip(tr("How thick the line around the tool buttons is — "
                            "0 leaves only the fill"));
    connect(myStroke, &QSpinBox::valueChanged, this, [this](int px) {
        if (mySyncing) return;
        setChipStroke(px);
    });
    strokeLine->addWidget(myStroke);
    outer->addWidget(strokeRow);

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
    // LIVE while the list is open: `highlighted` fires for whichever row the
    // keyboard or the cursor is on, before anything is chosen, so arrowing
    // down the list re-dresses the app one family at a time. `activated`
    // alone - which is all this had - meant a user picked a typeface from a
    // list of names and only then found out what it looked like.
    //
    // It cannot loop: setFontFamily() lands in Theme::setSpec(), which is a
    // no-op for a spec it already holds, and the applyTheme() it broadcasts
    // writes the combo under mySyncing.
    connect(myFamily, &QComboBox::highlighted, this, [this](int index) {
        if (mySyncing || index < 0) return;
        setFontFamily(myFamily->itemText(index));
    });
    // view() builds the popup container on first call, which is what makes it
    // filterable here rather than at the moment it is first shown.
    if (myFamily->view() && myFamily->view()->window())
        myFamily->view()->window()->installEventFilter(this);
    familyLine->addWidget(myFamily, 1);
    outer->addWidget(familyRow);

    // --- the look as a file -------------------------------------------------
    auto* fileRow = new QWidget(this);
    makeTransparent(fileRow, QStringLiteral("appearanceFileRow"));
    auto* fileLine = new QHBoxLayout(fileRow);
    fileLine->setContentsMargins(0, 0, 0, 0);
    fileLine->setSpacing(8);
    mySave = new QPushButton(tr("Save colours..."), fileRow);
    mySave->setToolTip(tr("Write the colours and text size to a file\n"
                          "Keep a look you like, or hand it to somebody else."));
    connect(mySave, &QPushButton::clicked, this, &AppearancePanel::chooseSaveFile);
    fileLine->addWidget(mySave, 1);
    myLoad = new QPushButton(tr("Load colours..."), fileRow);
    myLoad->setToolTip(tr("Read a look back out of a file\n"
                          "Applied as soon as it is opened."));
    connect(myLoad, &QPushButton::clicked, this, &AppearancePanel::chooseLoadFile);
    fileLine->addWidget(myLoad, 1);
    outer->addWidget(fileRow);

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
    if (myStroke) myStroke->setValue(static_cast<int>(live.chipStrokePx));
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

void AppearancePanel::setChipStroke(double px)
{
    Theme::Spec next = Theme::spec();
    next.chipStrokePx = std::clamp(px, Theme::kMinChipStrokePx, Theme::kMaxChipStrokePx);
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

QString AppearancePanel::colourFileSuffix() { return QStringLiteral(".furnifytheme"); }

QString AppearancePanel::colourFileFilter()
{
    return tr("FurnifyMe colours (*%1)").arg(colourFileSuffix());
}

bool AppearancePanel::saveColoursTo(const QString& path) const
{
    if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    const QByteArray payload = Theme::serializeSpec().toUtf8();
    // Both halves checked: a write can come up short on a full disk without
    // the open having failed, and a colour file that is half a spec is a file
    // that will be refused on the way back in - better to say so now, while
    // the user still knows which path they chose.
    if (file.write(payload) != payload.size()) return false;
    return file.flush();
}

bool AppearancePanel::loadColoursFrom(const QString& path)
{
    if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString text = QString::fromUtf8(file.read(kMaxColourFileBytes));

    // THE refusal, and it is deserializeSpec()'s rather than a second parser
    // written here: its contract is that `loaded` is untouched when it says
    // no, so there is no state in which half a bad file has been applied.
    Theme::Spec loaded;
    if (!Theme::deserializeSpec(text, loaded)) return false;

    // Straight into the same broadcast every swatch click uses, so the panel
    // re-reads itself, the viewport re-dresses and MainWindow's debounced
    // write stores it - none of which this function has to know about.
    Theme::setSpec(loaded);
    return true;
}

void AppearancePanel::chooseSaveFile()
{
    // A NATIVE dialog, and the one place this app opens something modal on
    // purpose. The no-modal law is about the app blocking the user to ask its
    // own questions; choosing a path on disk is the operating system's
    // question, asked in the operating system's own surface, and File ->
    // Save Screenshot and Export STEP already ask it exactly this way. Writing
    // an in-app file browser to avoid the word "modal" would be worse for the
    // user and a great deal more code.
    const QString path = QFileDialog::getSaveFileName(this, tr("Save colours"), QString(),
                                                      colourFileFilter());
    if (path.isEmpty()) return;   // cancelled - not a failure
    if (!saveColoursTo(path)) emit colourSaveFailed(path);
}

void AppearancePanel::chooseLoadFile()
{
    // Native, for the reason spelled out in chooseSaveFile().
    const QString path = QFileDialog::getOpenFileName(this, tr("Load colours"), QString(),
                                                      colourFileFilter());
    if (path.isEmpty()) return;
    if (!loadColoursFrom(path)) emit colourLoadRefused(path);
}

void AppearancePanel::beginFamilyPreview()
{
    myFamilyBeforePreview = Theme::spec().fontFamily;
}

void AppearancePanel::cancelFamilyPreview()
{
    if (myFamilyBeforePreview.isEmpty()) return;
    const QString restore = myFamilyBeforePreview;
    // Cleared FIRST: setFontFamily() broadcasts, applyTheme() runs re-entrantly
    // off that broadcast, and a preview that were still recorded here at that
    // moment would describe a popup that has already been answered.
    myFamilyBeforePreview.clear();
    setFontFamily(restore);
}

bool AppearancePanel::eventFilter(QObject* watched, QEvent* event)
{
    if (myFamily && myFamily->view() && watched == myFamily->view()->window()) {
        switch (event->type()) {
            case QEvent::Show:
                beginFamilyPreview();
                break;
            case QEvent::KeyPress:
                // Escape is answered here and then LET THROUGH, so the popup
                // still closes the way Qt closes it - this restores the
                // family, it does not take over the key.
                if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
                    cancelFamilyPreview();
                break;
            case QEvent::Hide:
                // A popup closed any other way - a click on a row, a click
                // outside - keeps whatever the preview landed on, so this only
                // drops the fallback rather than applying it.
                myFamilyBeforePreview.clear();
                break;
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
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
    // GENUINELY modeless, and it is show() rather than open() that makes it
    // so. Both return immediately - neither spins exec()'s nested event loop -
    // but QDialog::open() FORCES Qt::WindowModal on the way past, which locks
    // the rail, the viewport and the toast's Undo pill for as long as a
    // colour is being chosen. That is a dialog blocking the user to ask a
    // question, which is the thing this app does not do; and it is
    // self-defeating besides, since the live preview exists precisely so the
    // user can look at their model while they choose. setModal(false) alone
    // does not survive open(), which is why this is a different call and not
    // an extra line.
    dialog->setModal(false);

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
    dialog->show();
}

QWidget* AppearancePanel::swatchFor(const QString& id) const
{
    for (const Row& row : myRows) {
        if (row.id == id) return row.swatch;
    }
    return nullptr;
}

QWidget* AppearancePanel::resetButton() const { return myReset; }
QWidget* AppearancePanel::saveButton() const { return mySave; }
QWidget* AppearancePanel::loadButton() const { return myLoad; }

QStringList AppearancePanel::paintedTexts() const
{
    QStringList texts;
    if (myTitle) texts << myTitle->text();
    for (const Row& row : myRows) texts << row.name;
    if (mySizeLabel) texts << mySizeLabel->text();
    if (myStrokeLabel) texts << myStrokeLabel->text();
    if (myFamilyLabel) texts << myFamilyLabel->text();
    if (mySave) texts << mySave->text();
    if (myLoad) texts << myLoad->text();
    if (myReset) texts << myReset->text();
    // The file dialogs' own filter string is copy too - it names the app and
    // the file kind in a surface the user reads - and it is neither a QAction
    // nor a tooltip, so nothing else would sweep it.
    texts << colourFileFilter();
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

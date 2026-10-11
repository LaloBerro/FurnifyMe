#include "AppearancePanel.h"

#include "SettingsEdits.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHash>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QAction>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
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
constexpr int kPad = Theme::Space::Snug;

constexpr int kSwatchWidth = 46;
// What the colour list keeps between its swatches and the scroll area's right
// edge - see the contentsMargins() call that uses it.
constexpr int kSwatchGutter = 6;
constexpr int kSwatchHeight = 18;
constexpr int kSwatchRadius = 4;

// The tab bar's own rhythm and the two chip shapes below it. All three are
// sized in logical pixels rather than derived from the type scale on purpose:
// a switch is a fixed piece of furniture, and a pill that grew with the font
// would push the four tabs onto two lines at the top of the scale.
constexpr int kTabHeight = 24;
// The gap between the tab bar's two rows - the same 3px the chips within a
// row already use, so the grid reads even in both directions.
constexpr int kTabRowGap = 3;
constexpr int kSegHeight = 22;
constexpr int kSwitchWidth = 38;
constexpr int kSwitchHeight = 20;
constexpr int kSwitchKnob = 14;

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
        setFocusPolicy(Qt::NoFocus);
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

    }

private:
    QColor myColour;
};

// --- OptionChip --------------------------------------------------------------

// The one control class behind the tab bar, every on/off row and every
// choice row. Three drawings, one contract: it HOLDS NOTHING. `myCurrent` is
// pushed in by whoever owns the truth - setCurrentTab() for a tab,
// syncMirrors() reading QAction::isChecked() for a row - and a click emits
// clicked() and changes no pixel by itself. That is ToolChip's own
// action-mirroring discipline at drawer scale, and it is why the View menu
// and this drawer cannot disagree: there is no second copy of the state to
// disagree with.
class OptionChip : public QAbstractButton {
public:
    enum class Style { Tab, Segment, Switch };

    OptionChip(Style style, const QString& text, QWidget* parent)
        : QAbstractButton(parent), myStyle(style)
    {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        // NO KEYBOARD FOCUS - ToolChip's rule (see its init()): a chip that
        // held focus with no ring to show it would be focus nobody can see,
        // and a click on one would take focus away from a field being typed
        // in. Every chip in the app is a mirror of something with a menu
        // entry or a shortcut, which is where the keyboard reaches it.
        setFocusPolicy(Qt::NoFocus);
        // Every interactive control over the GL surface carries it - a press
        // that reached the viewport underneath would re-pick the model behind
        // this card. See CLAUDE.md's "Widgets over the viewport".
        setAttribute(Qt::WA_NoMousePropagation);
        setAttribute(Qt::WA_Hover, true);
        Theme::makeSurfaceTransparent(this);
        if (myStyle == Style::Switch)
            setFixedSize(kSwitchWidth, kSwitchHeight);
        else
            setFixedHeight(myStyle == Style::Tab ? kTabHeight : kSegHeight);
    }

    void setCurrent(bool current)
    {
        if (myCurrent == current) return;
        myCurrent = current;
        update();
    }
    bool current() const { return myCurrent; }

    QSize sizeHint() const override
    {
        if (myStyle == Style::Switch) return QSize(kSwitchWidth, kSwitchHeight);
        const QFontMetrics fm(Theme::labelFont());
        const int height = myStyle == Style::Tab ? kTabHeight : kSegHeight;
        return QSize(fm.horizontalAdvance(text()) + (myStyle == Style::Tab ? 14 : 18), height);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF body(rect());
        const bool off = !isEnabled();

        if (myStyle == Style::Switch) {
            // A pill and a knob. The knob's travel IS the state, so a
            // screenshot of this card says which way every switch is set
            // without reading a word.
            const qreal radius = body.height() / 2.0;
            QPainterPath path;
            path.addRoundedRect(body, radius, radius);
            QColor fill = myCurrent ? Theme::accent() : Theme::chip();
            if (off) fill = Theme::chip();
            else if (underMouse() && !myCurrent) fill = Theme::chipHover();
            painter.fillPath(path, fill);
            Theme::drawCrispBorder(painter, body, off ? Theme::border() : Theme::border(),
                                   radius);
            const qreal inset = (body.height() - kSwitchKnob) / 2.0;
            const qreal x = myCurrent ? body.width() - kSwitchKnob - inset : inset;
            painter.setPen(Qt::NoPen);
            painter.setBrush(off ? Theme::textDisabled() : Theme::text());
            painter.drawEllipse(QRectF(x, inset, kSwitchKnob, kSwitchKnob));
            return;
        }

        if (myStyle == Style::Tab) {
            // No box: a filled ground under the current tab and a 2px accent
            // rule along its bottom edge, which is what the mockup draws and
            // what keeps four tabs legible in 272 logical pixels.
            if (myCurrent) {
                QPainterPath path;
                path.addRoundedRect(body.adjusted(0, 0, 0, -2), 6, 6);
                painter.fillPath(path, Theme::chipActive());
                painter.fillRect(QRectF(body.left() + 2, body.bottom() - 2,
                                        body.width() - 4, 2),
                                 Theme::accent());
            } else if (underMouse() && !off) {
                QPainterPath path;
                path.addRoundedRect(body.adjusted(0, 0, 0, -2), 6, 6);
                painter.fillPath(path, Theme::chipHover());
            }
            painter.setFont(Theme::labelFont());
            painter.setPen(off ? Theme::textDisabled()
                               : (myCurrent ? Theme::text() : Theme::textMuted()));
            painter.drawText(rect(), Qt::AlignCenter, text());
            return;
        }

        QPainterPath path;
        path.addRoundedRect(body, 6, 6);
        QColor fill = Theme::chip();
        if (myCurrent)                    fill = Theme::chipActive();
        else if (underMouse() && !off)    fill = Theme::chipHover();
        painter.fillPath(path, fill);
        Theme::drawCrispBorder(painter, body,
                               myCurrent && !off ? Theme::accent() : Theme::border(), 6,
                               myCurrent && !off ? 1.6 : 1.0);
        painter.setFont(Theme::labelFont());
        painter.setPen(off ? Theme::textDisabled()
                           : (myCurrent ? Theme::text() : Theme::textMuted()));
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    Style myStyle;
    bool myCurrent = false;
};

// --- AppearancePanel ---------------------------------------------------------

QString AppearancePanel::familyDisplayName(const QString& family)
{
    return SettingsEdits::familyDisplayName(family);
}

bool AppearancePanel::isRetiredToken(const QString& id)
{
    return SettingsEdits::isRetiredToken(id);
}

QString AppearancePanel::nameForToken(const QString& id)
{
    return SettingsEdits::tokenName(id);
}

AppearancePanel::AppearancePanel(QWidget* parent)
    : QWidget(parent)
{
    // A floating card, painted in paintEvent(). See ItemsPanel.h for the
    // reasoning: it swallows the mouse rather than letting a press through to
    // re-pick the model behind it.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    // See Theme::makeSurfaceTransparent()'s own comment.
    Theme::makeSurfaceTransparent(this);
    // Through Theme::wholeDevicePixels() - see Theme.h. setFixedSize() is why
    // this card has to do it for itself: ViewportOverlay's own rounding is a
    // silent no-op on a fixed-size widget, and a card whose logical height is
    // not a whole number of device rows leaves the bottom row unpainted, which
    // over the GL surface is black rather than transparent.
    setFixedSize(Theme::wholeDevicePixels(QSize(kWidth, kHeight)));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kPad, kPad, kPad, kPad);
    outer->setSpacing(8);

    myTitle = new QLabel(tr("Settings"), this);
    outer->addWidget(myTitle);

    // --- the tab bar --------------------------------------------------------
    //
    // Four chips (the Colours/Text & lines split, then the Units/Files fold),
    // each one a view of myTab and nothing else - a click calls setCurrentTab(), which is the
    // single place the current tab is decided and the single place every
    // chip is re-synced from it.
    //
    // MEASURE TEXT WITH THE FONT YOU PAINT IT WITH: OptionChip paints its
    // label in Theme::labelFont() (see its paintEvent), so its own font() is
    // set to that here rather than left at whatever it would otherwise
    // inherit (QApplication's default, which is bodyFont() - a different
    // size on the same scale) - a chip that measured itself with one font
    // and painted with another is exactly the mistake this project's rule
    // exists to catch, and it is also what lets a plain QFontMetics(chip->
    // font()) probe outside this class read the SAME width sizeHint() uses.
    //
    // Four chips fit this card's 272 logical pixels of content width in one
    // row at the shipped type scale (266 of them, measured); five did not,
    // and wrapped. The bar still wraps to a SECOND row rather than clipping a
    // label or widening the card whenever they stop fitting - a translation,
    // a font that runs wide. The wrap is measured, not guessed: each chip's own sizeHint() (built from the same
    // QFontMetrics(labelFont) the no-clipping suite check reads) is packed
    // greedily into row one until the next chip would overflow this card's
    // content width, and everything after that goes to row two. A
    // hand-picked 3/2 split would silently go stale the day a tab's name or
    // the font changes; this reflows instead.
    auto* tabRow = new QWidget(this);
    makeTransparent(tabRow, QStringLiteral("settingsTabRow"));
    auto* tabColumn = new QVBoxLayout(tabRow);
    tabColumn->setContentsMargins(0, 0, 0, 0);
    tabColumn->setSpacing(kTabRowGap);
    auto* tabRow1 = new QHBoxLayout();
    tabRow1->setContentsMargins(0, 0, 0, 0);
    tabRow1->setSpacing(3);
    auto* tabRow2 = new QHBoxLayout();
    tabRow2->setContentsMargins(0, 0, 0, 0);
    tabRow2->setSpacing(3);
    tabColumn->addLayout(tabRow1);
    tabColumn->addLayout(tabRow2);

    QString tabNames[kTabCount];
    QString tabTips[kTabCount];
    for (int i = 0; i < kTabCount; ++i) {
        tabNames[i] = SettingsEdits::tabs()[i].name;
        tabTips[i] = SettingsEdits::tabs()[i].tip;
    }
    const int tabContentWidth = kWidth - 2 * kPad;
    // MEASURED FIRST, PLACED SECOND. The split has to be decided with every
    // chip's width already in hand, because a greedy fill cannot know that
    // the chip it is about to leave behind is the LAST one.
    OptionChip* tabChips[kTabCount] = {};
    int tabWidths[kTabCount] = {};
    for (int i = 0; i < kTabCount; ++i) {
        auto* chip = new OptionChip(OptionChip::Style::Tab, tabNames[i], tabRow);
        chip->setFont(Theme::labelFont());
        chip->setToolTip(tabTips[i]);
        const Tab tab = static_cast<Tab>(i);
        connect(chip, &QAbstractButton::clicked, this, [this, tab] { setCurrentTab(tab); });
        tabChips[i] = chip;
        tabWidths[i] = chip->sizeHint().width();
        myTabButtons[i] = chip;
        myExtraTexts << tabNames[i];
    }

    // How many fit on row one, greedily - the honest maximum.
    int split = kTabCount;
    {
        int used = 0;
        for (int i = 0; i < kTabCount; ++i) {
            const int spacingBefore = used > 0 ? tabRow1->spacing() : 0;
            if (used > 0 && used + spacingBefore + tabWidths[i] > tabContentWidth) {
                split = i;
                break;
            }
            used += spacingBefore + tabWidths[i];
        }
    }
    // AND THEN BALANCED, when the greedy answer would strand exactly one chip
    // on row two. Four tabs over one orphan reads as a rendering accident
    // rather than as a second row - the lone "Files" sat under "Colours"
    // looking like a heading for the panel. Moving one chip down makes it
    // 3 + 2, which reads as a wrap. Only ever moves ONE chip, and only when
    // the two that end up on row two genuinely fit.
    if (split == kTabCount - 1 && split > 1) {
        const int pairWidth = tabWidths[split - 1] + tabRow2->spacing() + tabWidths[split];
        if (pairWidth <= tabContentWidth) --split;
    }

    for (int i = 0; i < kTabCount; ++i) {
        // NATURAL WIDTH, PACKED LEFT, on both rows. They used to be added
        // with a stretch factor, which made row one's chips share the width
        // evenly and left row two's chips floating in the middle of an
        // otherwise empty row. A tab bar that wraps has to wrap like text:
        // the next line starts where the first one did.
        (i < split ? tabRow1 : tabRow2)->addWidget(tabChips[i]);
    }
    // The slack goes to the END of each row rather than between the chips,
    // which is what keeps the second row left-aligned under the first.
    tabRow1->addStretch(1);
    tabRow2->addStretch(1);
    outer->addWidget(tabRow);

    // --- the four pages -----------------------------------------------------
    myPages = new QStackedWidget(this);
    makeTransparent(myPages, QStringLiteral("settingsPages"));
    for (int i = 0; i < kTabCount; ++i) {
        auto* page = new QWidget(myPages);
        makeTransparent(page, QStringLiteral("settingsPage%1").arg(i));
        auto* box = new QVBoxLayout(page);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(6);
        // Every page but Colours ends in a stretch, so a short tab's rows sit
        // at the top rather than spreading down a 380px card. appendRow()
        // inserts BEFORE it; Colours has no stretch because its scroll area
        // already carries one.
        if (i != static_cast<int>(Tab::Colours)) box->addStretch(1);
        myTabPages[i] = page;
        myPageLayouts[i] = box;
        myPages->addWidget(page);
    }
    outer->addWidget(myPages, 1);

    QWidget* coloursPage = myTabPages[static_cast<int>(Tab::Colours)];
    QVBoxLayout* colours = myPageLayouts[static_cast<int>(Tab::Colours)];
    QWidget* textLinesPage = myTabPages[static_cast<int>(Tab::TextLines)];
    QVBoxLayout* textLines = myPageLayouts[static_cast<int>(Tab::TextLines)];

    // --- the scrolling list of colour rows ---------------------------------
    auto* scroll = new QScrollArea(coloursPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    makeTransparent(scroll, QStringLiteral("appearanceScroll"));
    makeTransparent(scroll->viewport(), QStringLiteral("appearanceScrollViewport"));
    // THE BAR IS THEMED, like everything else on this card - by the app-wide
    // sheet, which every scrollbar in the app now shares (see applyTheme()).
    // Named so the suite can find this one.
    scroll->verticalScrollBar()->setObjectName(QStringLiteral("appearanceScrollBar"));

    auto* content = new QWidget(scroll);
    makeTransparent(content, QStringLiteral("appearanceContent"));
    auto* list = new QVBoxLayout(content);
    // A RIGHT MARGIN the swatches can breathe in. Every label on the left of
    // this list sits inside the card's own kPad; the swatches on the right
    // ran flush to the scroll area's edge, so the list read as pushed against
    // the wall. Right-side only: the card's padding already spaces the left,
    // and doubling it here would indent the names away from every other row
    // in the drawer.
    list->setContentsMargins(0, 0, kSwatchGutter, 0);
    list->setSpacing(4);

    // Driven off Theme::colourTokens() rather than a second list written out
    // here: a token added to the spec and to that table gets a row, a swatch
    // and a persisted value with no third place to remember. A token with no
    // name in nameForToken() would get a nameless row, which is why gui_smoke
    // asserts every token has one.
    // WHICH GROUP EACH TOKEN OPENS. Twenty-seven swatches ran as one
    // undifferentiated list, and the first three - Viewport, Grid lines, Grid
    // every tenth line - are near-identical darks, so the top of the panel
    // read as three rows of the same colour with no clue why. A heading above
    // the token that starts each run says what the run is ABOUT, which is the
    // thing the colours themselves cannot say when they are nearly equal.
    //
    // Keyed on the token id rather than on a position, so adding a token to
    // Theme::Spec puts it in the run it belongs to instead of silently
    // shifting a heading onto the wrong row.
    for (const Theme::ColourToken& token : Theme::colourTokens()) {
        // A RETIRED token gets no row: the app draws no keyboard focus rings
        // any more (the user asked for them gone), so a swatch for one would
        // be a colour the user can change and never see. The fields stay in
        // Theme::Spec so a .furnifytheme saved before the removal still loads
        // rather than being refused for an unknown key.
        if (isRetiredToken(token.id)) continue;

        if (const QString heading = SettingsEdits::groupHeadingFor(token.id); !heading.isEmpty()) {
            auto* head = new QLabel(heading, content);
            makeTransparent(head, QStringLiteral("appearanceGroup_") + token.id);
            myGroupHeadings.push_back(head);
            myExtraTexts << heading;
            list->addWidget(head);
        }
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
    colours->addWidget(scroll, 1);

    // --- the type controls, on TEXT & LINES now -----------------------------
    //
    // Split off Colours (a user review of the four-tab drawer: "the color
    // tab doesnt not make any sense, maybe divide it in two tabs"): these
    // five are Theme::Spec values exactly like the swatches, but nothing
    // about them is a COLOUR, and a tab holding 28 swatches plus five
    // unrelated type/line controls read as a grab bag wearing one label. In
    // the order CLAUDE.md's Settings table lists them: Text size, Font,
    // Edge lines, Outline lines, Button border.
    auto* sizeRow = new QWidget(textLinesPage);
    makeTransparent(sizeRow, QStringLiteral("appearanceSizeRow"));
    auto* sizeLine = new QHBoxLayout(sizeRow);
    sizeLine->setContentsMargins(0, 0, 0, 0);
    sizeLine->setSpacing(8);
    mySizeLabel = new QLabel(SettingsEdits::numberField(QStringLiteral("basePt"))->label, sizeRow);
    makeTransparent(mySizeLabel, QStringLiteral("appearanceSizeLabel"));
    sizeLine->addWidget(mySizeLabel, 1);
    mySize = new QSpinBox(sizeRow);
    {
        const SettingsEdits::NumberField& f = *SettingsEdits::numberField(QStringLiteral("basePt"));
        mySize->setRange(static_cast<int>(f.min), static_cast<int>(f.max));
        mySize->setSuffix(f.suffix);
        mySize->setToolTip(f.tip);
    }
    connect(mySize, &QSpinBox::valueChanged, this, [this](int pt) {
        if (mySyncing) return;
        setBaseSize(pt);
    });
    sizeLine->addWidget(mySize);
    // BEFORE THE TRAILING STRETCH, like every other page. These five rows
    // used addWidget(), which appends AFTER the stretch the page was built
    // with - so the stretch sat on top and the whole tab was bottom-aligned,
    // with ~180 px of void above Text size while Colours, Viewport, Units and
    // Files all started at the top. It read as broken because it was.
    textLines->insertWidget(textLines->count() - 1, sizeRow);

    auto* familyRow = new QWidget(textLinesPage);
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
        //
        // Each entry SHOWS the family's name and CARRIES the real family as
        // its data (the 2026-10 review's F22): the bundled DM Sans registers
        // as "DM Sans 14pt" - an optical-size cut, named after the size it
        // was drawn for - and a row reading "Font  DM Sans 14pt" right under
        // "Text size  10 pt" reads as two sizes that disagree.
        // familyDisplayName() is the one place that name is shortened.
        //
        // EACH SHOWN NAME ONCE. The bundled file registers its typographic
        // family ("DM Sans") beside the cut Theme applies ("DM Sans 14pt"), so
        // the two read the same once the size is dropped - and they ARE the
        // same face. The first one listed wins, which puts the bundled cut
        // first; applyTheme() finds a stored family by its shown name when its
        // own entry was the one folded away.
        for (const auto& [shown, family] : SettingsEdits::fontFamilies())
            myFamily->addItem(shown, family);
    }
    connect(myFamily, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (mySyncing || index < 0) return;
        setFontFamily(myFamily->itemData(index).toString());
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
        setFontFamily(myFamily->itemData(index).toString());
    });
    // view() builds the popup container on first call, which is what makes it
    // filterable here rather than at the moment it is first shown.
    if (myFamily->view() && myFamily->view()->window())
        myFamily->view()->window()->installEventFilter(this);
    familyLine->addWidget(myFamily, 1);
    textLines->insertWidget(textLines->count() - 1, familyRow);

    // Milestone 5, item 6: two more numeric tokens, on the exact same
    // QDoubleSpinBox template Grid detail set - a field + kMin/kMax
    // constants + defaultSpec + operator== + serialize/deserialize +
    // accessor + this row + applyTheme sync + paintedTexts.
    auto* edgeWidthRow = new QWidget(textLinesPage);
    makeTransparent(edgeWidthRow, QStringLiteral("appearanceEdgeWidthRow"));
    auto* edgeWidthLine = new QHBoxLayout(edgeWidthRow);
    edgeWidthLine->setContentsMargins(0, 0, 0, 0);
    edgeWidthLine->setSpacing(8);
    myEdgeWidthLabel = new QLabel(SettingsEdits::numberField(QStringLiteral("edgeWidthPx"))->label, edgeWidthRow);
    makeTransparent(myEdgeWidthLabel, QStringLiteral("appearanceEdgeWidthLabel"));
    edgeWidthLine->addWidget(myEdgeWidthLabel, 1);
    myEdgeWidth = new QDoubleSpinBox(edgeWidthRow);
    {
        const SettingsEdits::NumberField& f = *SettingsEdits::numberField(QStringLiteral("edgeWidthPx"));
        myEdgeWidth->setRange(f.min, f.max);
        myEdgeWidth->setSingleStep(f.step);
        myEdgeWidth->setDecimals(f.decimals);
        myEdgeWidth->setSuffix(f.suffix);
        myEdgeWidth->setToolTip(f.tip);
    }
    connect(myEdgeWidth, &QDoubleSpinBox::valueChanged, this, [this](double px) {
        if (mySyncing) return;
        setEdgeWidth(px);
    });
    edgeWidthLine->addWidget(myEdgeWidth);
    textLines->insertWidget(textLines->count() - 1, edgeWidthRow);

    auto* sketchLineWidthRow = new QWidget(textLinesPage);
    makeTransparent(sketchLineWidthRow, QStringLiteral("appearanceSketchLineWidthRow"));
    auto* sketchLineWidthLine = new QHBoxLayout(sketchLineWidthRow);
    sketchLineWidthLine->setContentsMargins(0, 0, 0, 0);
    sketchLineWidthLine->setSpacing(8);
    mySketchLineWidthLabel = new QLabel(SettingsEdits::numberField(QStringLiteral("sketchLineWidthPx"))->label, sketchLineWidthRow);
    makeTransparent(mySketchLineWidthLabel, QStringLiteral("appearanceSketchLineWidthLabel"));
    sketchLineWidthLine->addWidget(mySketchLineWidthLabel, 1);
    mySketchLineWidth = new QDoubleSpinBox(sketchLineWidthRow);
    {
        const SettingsEdits::NumberField& f = *SettingsEdits::numberField(QStringLiteral("sketchLineWidthPx"));
        mySketchLineWidth->setRange(f.min, f.max);
        mySketchLineWidth->setSingleStep(f.step);
        mySketchLineWidth->setDecimals(f.decimals);
        mySketchLineWidth->setSuffix(f.suffix);
        mySketchLineWidth->setToolTip(f.tip);
    }
    connect(mySketchLineWidth, &QDoubleSpinBox::valueChanged, this, [this](double px) {
        if (mySyncing) return;
        setSketchLineWidth(px);
    });
    sketchLineWidthLine->addWidget(mySketchLineWidth);
    textLines->insertWidget(textLines->count() - 1, sketchLineWidthRow);

    auto* strokeRow = new QWidget(textLinesPage);
    makeTransparent(strokeRow, QStringLiteral("appearanceStrokeRow"));
    auto* strokeLine = new QHBoxLayout(strokeRow);
    strokeLine->setContentsMargins(0, 0, 0, 0);
    strokeLine->setSpacing(8);
    myStrokeLabel = new QLabel(SettingsEdits::numberField(QStringLiteral("chipStrokePx"))->label, strokeRow);
    makeTransparent(myStrokeLabel, QStringLiteral("appearanceStrokeLabel"));
    strokeLine->addWidget(myStrokeLabel, 1);
    myStroke = new QSpinBox(strokeRow);
    // The spec's field is a double, but a border is judged in whole pixels
    // and the crisp-border idiom is built around integer alignment - so the
    // control offers integers over the full legal range, 0 included.
    {
        const SettingsEdits::NumberField& f = *SettingsEdits::numberField(QStringLiteral("chipStrokePx"));
        myStroke->setRange(static_cast<int>(f.min), static_cast<int>(f.max));
        myStroke->setSuffix(f.suffix);
        myStroke->setToolTip(f.tip);
    }
    connect(myStroke, &QSpinBox::valueChanged, this, [this](int px) {
        if (mySyncing) return;
        setChipStroke(px);
    });
    strokeLine->addWidget(myStroke);
    textLines->insertWidget(textLines->count() - 1, strokeRow);

    // --- the colours as a file, back on COLOURS ------------------------------
    auto* fileRow = new QWidget(coloursPage);
    makeTransparent(fileRow, QStringLiteral("appearanceFileRow"));
    auto* fileLine = new QHBoxLayout(fileRow);
    fileLine->setContentsMargins(0, 0, 0, 0);
    fileLine->setSpacing(8);
    mySave = new QPushButton(tr("Save colours..."), fileRow);
    mySave->setToolTip(tr("Write the colours and text size to a file\n"
                          "Keep colours you like, or hand them to somebody else."));
    connect(mySave, &QPushButton::clicked, this, &AppearancePanel::chooseSaveFile);
    // The three buttons take no keyboard focus, like every chip on this card.
    mySave->setFocusPolicy(Qt::NoFocus);
    fileLine->addWidget(mySave, 1);
    myLoad = new QPushButton(tr("Load colours..."), fileRow);
    myLoad->setToolTip(tr("Read colours back out of a file\n"
                          "Applied as soon as it is opened."));
    connect(myLoad, &QPushButton::clicked, this, &AppearancePanel::chooseLoadFile);
    myLoad->setFocusPolicy(Qt::NoFocus);
    fileLine->addWidget(myLoad, 1);
    colours->addWidget(fileRow);

    // RESET, demoted and guarded. It used to be the widest control on the tab,
    // full width under Save and Load, and one click threw away every colour
    // with no Undo - the 400 ms debounce then wrote that to disk. Now it sits
    // at the right at its own width and takes two clicks (onResetClicked()).
    // Named for what it puts back rather than "the original look": a look is
    // a material's in this app's vocabulary, and colours are not the half of
    // it a user would guess - the sizes go back too.
    auto* resetRow = new QWidget(coloursPage);
    makeTransparent(resetRow, QStringLiteral("appearanceResetRow"));
    auto* resetLine = new QHBoxLayout(resetRow);
    resetLine->setContentsMargins(0, 0, 0, 0);
    resetLine->addStretch(1);
    myReset = new QPushButton(resetLabel(), resetRow);
    myReset->setToolTip(tr("Put every colour, the text size and font, the line widths, "
                           "Grid detail and Gizmo size back the way they shipped — "
                           "click twice"));
    connect(myReset, &QPushButton::clicked, this, &AppearancePanel::onResetClicked);
    myReset->setFocusPolicy(Qt::NoFocus);
    resetLine->addWidget(myReset);
    colours->addWidget(resetRow);
    myResetArm = new QTimer(this);
    myResetArm->setObjectName(QStringLiteral("appearanceResetArm"));
    myResetArm->setSingleShot(true);
    myResetArm->setInterval(kResetConfirmMs);
    connect(myResetArm, &QTimer::timeout, this, &AppearancePanel::disarmReset);
    fitResetWidth();

    // The tab chips read myTab, which nothing has pushed onto them yet.
    setCurrentTab(myTab);

    // The controls are filled in from the live spec here, and re-filled on
    // every broadcast - including the ones this panel itself causes, which is
    // what keeps a swatch correct after a Reset it did not have to special-case.
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this,
            &AppearancePanel::applyTheme);

}

// --- tabs --------------------------------------------------------------------

void AppearancePanel::setCurrentTab(Tab tab)
{
    const int index = static_cast<int>(tab);
    if (index < 0 || index >= kTabCount) return;
    myTab = tab;
    // Deliberately NOT guarded on "it did not change": this is the one place
    // the chips are re-synced from myTab, and the constructor calls it to
    // push the initial state onto chips that have never been told anything.
    if (myPages) myPages->setCurrentIndex(index);
    for (int i = 0; i < kTabCount; ++i) {
        if (myTabButtons[i]) myTabButtons[i]->setCurrent(i == index);
    }
}

QAbstractButton* AppearancePanel::tabButton(Tab tab) const
{
    const int index = static_cast<int>(tab);
    if (index < 0 || index >= kTabCount) return nullptr;
    return myTabButtons[index];
}

QWidget* AppearancePanel::pageFor(Tab tab) const
{
    const int index = static_cast<int>(tab);
    if (index < 0 || index >= kTabCount) return nullptr;
    return myTabPages[index];
}

QAbstractButton* AppearancePanel::controlFor(QAction* action) const
{
    for (const Mirror& mirror : myMirrors) {
        if (mirror.action == action) return mirror.control;
    }
    return nullptr;
}

QAbstractButton* AppearancePanel::alternateControlFor(QAction* action) const
{
    for (auto it = myOffMirrors.cbegin(); it != myOffMirrors.cend(); ++it) {
        if (it.value() == action) return it.key();
    }
    return nullptr;
}

bool AppearancePanel::controlIsCurrent(const QAbstractButton* control) const
{
    for (const Mirror& mirror : myMirrors) {
        if (mirror.control == control) return mirror.control->current();
    }
    for (auto it = myOffMirrors.cbegin(); it != myOffMirrors.cend(); ++it) {
        if (it.key() == control) return it.key()->current();
    }
    return false;
}

void AppearancePanel::appendRow(Tab tab, QWidget* row)
{
    const int index = static_cast<int>(tab);
    if (index < 0 || index >= kTabCount || !myPageLayouts[index] || !row) return;
    QVBoxLayout* box = myPageLayouts[index];
    // Colours ends in its own scroll area rather than a stretch, so its rows
    // simply append; every other page keeps the stretch last.
    const int at = (tab == Tab::Colours) ? box->count() : std::max(0, box->count() - 1);
    box->insertWidget(at, row);
}

QHBoxLayout* AppearancePanel::makeRow(Tab tab, const QString& key, const QString& label)
{
    QWidget* page = pageFor(tab);
    auto* row = new QWidget(page);
    makeTransparent(row, QStringLiteral("settingsRow_") + key);
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(8);
    if (!label.isEmpty()) {
        auto* name = new QLabel(label, row);
        name->setWordWrap(false);
        makeTransparent(name, QStringLiteral("settingsLabel_") + key);
        line->addWidget(name, 1);
        myExtraTexts << label;
    }
    appendRow(tab, row);
    return line;
}

// --- mirrored rows -----------------------------------------------------------

void AppearancePanel::addToggleRow(Tab tab, const QString& label, QAction* action)
{
    if (!action) return;
    QHBoxLayout* line = makeRow(tab, label, label);
    auto* pill = new OptionChip(OptionChip::Style::Switch, QString(),
                                line->parentWidget());
    // The tooltip is the ACTION's, not a second sentence written here: the
    // menu entry and this pill describe one setting, and a drawer that
    // explained it differently would be two sources of copy for one thing.
    pill->setToolTip(action->toolTip());
    // A click TRIGGERS. It never sets myCurrent - that arrives back through
    // QAction::changed and syncMirrors(), so the pill cannot show a state the
    // action refused to take.
    connect(pill, &QAbstractButton::clicked, this, [action] { action->trigger(); });
    connect(action, &QAction::changed, this, &AppearancePanel::syncMirrors);
    line->addWidget(pill);
    myMirrors.push_back(Mirror{action, pill});
    syncMirrors();
}

void AppearancePanel::addChoiceRow(Tab tab, const QString& label,
                                   const QVector<QAction*>& options,
                                   const QStringList& optionLabels, bool stacked)
{
    if (options.isEmpty() || options.size() != optionLabels.size()) return;
    QHBoxLayout* line = makeRow(tab, label, label);
    QWidget* row = line->parentWidget();

    // A stacked set needs a column of its own inside the row, so five
    // autosave modes read as a list rather than as five chips squeezed into
    // 272 logical pixels.
    QBoxLayout* target = line;
    if (stacked) {
        auto* column = new QVBoxLayout();
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(4);
        line->addLayout(column, 1);
        target = column;
        // The name belongs beside the FIRST option, not floating in the
        // middle of a five-row column - a label vertically centred against a
        // stack reads as a heading for the gap between the third and fourth
        // entries.
        if (QLayoutItem* first = line->itemAt(0)) {
            if (QWidget* name = first->widget())
                line->setAlignment(name, Qt::AlignTop);
        }
    }

    for (int i = 0; i < options.size(); ++i) {
        QAction* option = options.at(i);
        if (!option) continue;
        auto* chip = new OptionChip(OptionChip::Style::Segment, optionLabels.at(i), row);
        chip->setToolTip(option->toolTip().isEmpty() ? optionLabels.at(i)
                                                     : option->toolTip());
        // An exclusive group's already-current member must not be triggered:
        // QActionGroup would keep it checked anyway, but a trigger still runs
        // whatever the action does, and re-applying a setting nobody changed
        // is a write this drawer has no business making.
        connect(chip, &QAbstractButton::clicked, this, [option] {
            if (option->isChecked()) return;
            option->trigger();
        });
        connect(option, &QAction::changed, this, &AppearancePanel::syncMirrors);
        target->addWidget(chip);
        myMirrors.push_back(Mirror{option, chip});
        myExtraTexts << optionLabels.at(i);
    }
    syncMirrors();
}

void AppearancePanel::addBinaryChoiceRow(Tab tab, const QString& label, QAction* action,
                                         const QString& offLabel, const QString& onLabel)
{
    if (!action) return;
    QHBoxLayout* line = makeRow(tab, label, label);
    QWidget* row = line->parentWidget();

    auto* offChip = new OptionChip(OptionChip::Style::Segment, offLabel, row);
    auto* onChip = new OptionChip(OptionChip::Style::Segment, onLabel, row);
    offChip->setToolTip(action->toolTip());
    onChip->setToolTip(action->toolTip());
    // Each half triggers only from the state it is NOT: the action is a
    // toggle, so triggering it from its own side would turn the setting off.
    connect(offChip, &QAbstractButton::clicked, this, [action] {
        if (action->isChecked()) action->trigger();
    });
    connect(onChip, &QAbstractButton::clicked, this, [action] {
        if (!action->isChecked()) action->trigger();
    });
    connect(action, &QAction::changed, this, &AppearancePanel::syncMirrors);
    line->addWidget(offChip);
    line->addWidget(onChip);

    // Both halves are registered against the same action. syncMirrors() reads
    // isChecked() for the ON chip and its negation for the OFF one, which is
    // what myOffMirrors records - there is still exactly one piece of truth,
    // asked twice.
    myMirrors.push_back(Mirror{action, onChip});
    myOffMirrors.insert(offChip, action);
    myExtraTexts << offLabel << onLabel;
    syncMirrors();
}

void AppearancePanel::syncMirrors()
{
    for (const Mirror& mirror : myMirrors) {
        if (!mirror.action || !mirror.control) continue;
        mirror.control->setCurrent(mirror.action->isChecked());
        mirror.control->setEnabled(mirror.action->isEnabled());
    }
    for (auto it = myOffMirrors.cbegin(); it != myOffMirrors.cend(); ++it) {
        if (!it.key() || !it.value()) continue;
        it.key()->setCurrent(!it.value()->isChecked());
        it.key()->setEnabled(it.value()->isEnabled());
    }
}

void AppearancePanel::addRows(const std::vector<SettingsEdits::Row>& rows)
{
    using Kind = SettingsEdits::Row::Kind;
    for (const SettingsEdits::Row& row : rows) {
        switch (row.kind) {
        case Kind::Toggle:
            if (!row.actions.isEmpty()) addToggleRow(row.tab, row.label, row.actions.front());
            break;
        case Kind::Choice:
            addChoiceRow(row.tab, row.label, row.actions, row.optionLabels, row.stacked);
            break;
        case Kind::Binary:
            if (!row.actions.isEmpty() && row.optionLabels.size() == 2)
                addBinaryChoiceRow(row.tab, row.label, row.actions.front(), row.optionLabels[0],
                                   row.optionLabels[1]);
            break;
        case Kind::Number:
            if (row.numberId == QLatin1String("gridDensity")) addGridDetailRow(row.tab);
            else if (row.numberId == QLatin1String("gizmoScale")) addGizmoSizeRow(row.tab);
            break;
        }
    }
}

// --- this drawer's own spec rows, placed by the caller -----------------------

void AppearancePanel::addGridDetailRow(Tab tab)
{
    if (myGridDensity) return;
    // A QDoubleSpinBox rather than the stroke row's integer QSpinBox - the
    // spec field is a multiplier, not a pixel count, and
    // Theme::kMinGridDensity..kMaxGridDensity is a sub-1.0 to low-single-
    // digits band where whole numbers would waste most of the range. The
    // suffix is a bare "x" rather than anything Measure would format: this is
    // not a length, and routing it through Measure would be exactly the
    // "hand-format at the call site" mistake CLAUDE.md's numbers rule warns
    // against for lengths, applied to a value that was never a length at all.
    QHBoxLayout* line = makeRow(tab, QStringLiteral("gridDetail"), QString());
    QWidget* row = line->parentWidget();
    myGridDensityLabel = new QLabel(SettingsEdits::numberField(QStringLiteral("gridDensity"))->label, row);
    makeTransparent(myGridDensityLabel, QStringLiteral("settingsGridDetailLabel"));
    line->addWidget(myGridDensityLabel, 1);
    myGridDensity = new QDoubleSpinBox(row);
    {
        const SettingsEdits::NumberField& f = *SettingsEdits::numberField(QStringLiteral("gridDensity"));
        myGridDensity->setRange(f.min, f.max);
        myGridDensity->setSingleStep(f.step);
        myGridDensity->setDecimals(f.decimals);
        myGridDensity->setSuffix(f.suffix);
        myGridDensity->setToolTip(f.tip);
    }
    myGridDensity->setAttribute(Qt::WA_NoMousePropagation);
    connect(myGridDensity, &QDoubleSpinBox::valueChanged, this, [this](double density) {
        if (mySyncing) return;
        setGridDensity(density);
    });
    line->addWidget(myGridDensity);
    applyTheme();
}

void AppearancePanel::addGizmoSizeRow(Tab tab)
{
    if (myGizmoScale) return;
    // The same bare-"x" multiplier template Grid detail set - this is not a
    // length either, so it never goes near Measure.
    QHBoxLayout* line = makeRow(tab, QStringLiteral("gizmoSize"), QString());
    QWidget* row = line->parentWidget();
    myGizmoScaleLabel = new QLabel(SettingsEdits::numberField(QStringLiteral("gizmoScale"))->label, row);
    makeTransparent(myGizmoScaleLabel, QStringLiteral("settingsGizmoSizeLabel"));
    line->addWidget(myGizmoScaleLabel, 1);
    myGizmoScale = new QDoubleSpinBox(row);
    {
        const SettingsEdits::NumberField& f = *SettingsEdits::numberField(QStringLiteral("gizmoScale"));
        myGizmoScale->setRange(f.min, f.max);
        myGizmoScale->setSingleStep(f.step);
        myGizmoScale->setDecimals(f.decimals);
        myGizmoScale->setSuffix(f.suffix);
        myGizmoScale->setToolTip(f.tip);
    }
    myGizmoScale->setAttribute(Qt::WA_NoMousePropagation);
    connect(myGizmoScale, &QDoubleSpinBox::valueChanged, this, [this](double scale) {
        if (mySyncing) return;
        setGizmoScale(scale);
    });
    line->addWidget(myGizmoScale);
    applyTheme();
}

void AppearancePanel::applyTheme()
{
    mySyncing = true;

    const Theme::Spec& live = Theme::spec();

    // The Colours tab's group headings - the muted label voice, so they read
    // as structure rather than as another row with a missing swatch.
    for (QLabel* head : myGroupHeadings) {
        if (!head) continue;
        QFont f = Theme::labelFont();
        f.setBold(true);
        head->setFont(f);
        head->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                .arg(Theme::textMuted().name()));
    }

    // THE SCROLLBAR wears the app-wide sheet's bar now (Theme.cpp's install()),
    // which is the look this card used to set for itself alone - a quiet
    // chipActive handle on a transparent track. One description of a
    // scrollbar rather than this card's and a second, dithered one everywhere
    // else (the 2026-10 review's F18).
    for (const Theme::ColourToken& token : Theme::colourTokens()) {
        for (Row& row : myRows) {
            if (row.id == token.id && row.swatch) row.swatch->setColour(live.*(token.member));
        }
    }
    if (mySize) mySize->setValue(static_cast<int>(live.basePt));
    if (myGridDensity) myGridDensity->setValue(live.gridDensity);
    if (myEdgeWidth) myEdgeWidth->setValue(live.edgeWidthPx);
    if (mySketchLineWidth) mySketchLineWidth->setValue(live.sketchLineWidthPx);
    if (myGizmoScale) myGizmoScale->setValue(live.gizmoScale);
    if (myStroke) myStroke->setValue(static_cast<int>(live.chipStrokePx));
    if (myFamily) {
        int index = myFamily->findData(live.fontFamily);
        if (index < 0) index = myFamily->findText(familyDisplayName(live.fontFamily));
        if (index >= 0) myFamily->setCurrentIndex(index);
    }
    // The tab chips' own font() tracks Theme::labelFont() the same way their
    // paintEvent already does - "measure text with the font you paint it
    // with" - so a live Text size or Font edit (both reachable from this
    // very drawer, on the Text & lines tab) cannot leave a later chip->font()
    // probe reading a size that is no longer what is on screen.
    for (OptionChip* tabButton : myTabButtons) {
        if (tabButton) tabButton->setFont(Theme::labelFont());
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

    // A Text size or Font edit changes what both reset labels measure.
    fitResetWidth();

    mySyncing = false;
    update();
}

void AppearancePanel::setTokenColour(const QString& id, const QColor& colour)
{
    SettingsEdits::setTokenColour(id, colour);
}

void AppearancePanel::setBaseSize(double pt) { SettingsEdits::setNumber(QStringLiteral("basePt"), pt); }
void AppearancePanel::setChipStroke(double px) { SettingsEdits::setNumber(QStringLiteral("chipStrokePx"), px); }
void AppearancePanel::setGridDensity(double d) { SettingsEdits::setNumber(QStringLiteral("gridDensity"), d); }
void AppearancePanel::setEdgeWidth(double px) { SettingsEdits::setNumber(QStringLiteral("edgeWidthPx"), px); }
void AppearancePanel::setGizmoScale(double scale)
{
    SettingsEdits::setNumber(QStringLiteral("gizmoScale"), scale);
}
void AppearancePanel::setSketchLineWidth(double px)
{
    SettingsEdits::setNumber(QStringLiteral("sketchLineWidthPx"), px);
}
void AppearancePanel::setFontFamily(const QString& family) { SettingsEdits::setFontFamily(family); }
void AppearancePanel::reset() { SettingsEdits::reset(); }

QString AppearancePanel::resetLabel() { return SettingsEdits::resetLabel(); }
QString AppearancePanel::resetArmedLabel() { return SettingsEdits::resetArmedLabel(); }

void AppearancePanel::onResetClicked()
{
    if (resetArmed()) {
        // Disarmed BEFORE the reset: setSpec() broadcasts and applyTheme()
        // runs off it, and the label it re-measures should be the resting one.
        disarmReset();
        reset();
        return;
    }
    if (myReset) myReset->setText(resetArmedLabel());
    if (myResetArm) myResetArm->start();
}

bool AppearancePanel::resetArmed() const { return myResetArm && myResetArm->isActive(); }

int AppearancePanel::resetArmedMs() const
{
    return resetArmed() ? myResetArm->remainingTime() : -1;
}

void AppearancePanel::disarmReset()
{
    if (myResetArm) myResetArm->stop();
    if (myReset) myReset->setText(resetLabel());
}

void AppearancePanel::fitResetWidth()
{
    if (!myReset) return;
    // Measured with the button's own font - the font it paints its label
    // with - and the style's padding read off its own sizeHint() for the
    // label it is showing now, so the two labels share one width.
    const QFontMetrics metrics(myReset->font());
    const int padding = myReset->sizeHint().width() - metrics.horizontalAdvance(myReset->text());
    myReset->setMinimumWidth(std::max(metrics.horizontalAdvance(resetLabel()),
                                      metrics.horizontalAdvance(resetArmedLabel())) +
                             std::max(0, padding));
}

void AppearancePanel::hideEvent(QHideEvent* event)
{
    disarmReset();
    QWidget::hideEvent(event);
}

QString AppearancePanel::colourFileSuffix() { return SettingsEdits::colourFileSuffix(); }
QString AppearancePanel::colourFileFilter() { return SettingsEdits::colourFileFilter(); }

bool AppearancePanel::saveColoursTo(const QString& path) const
{
    return SettingsEdits::saveColoursTo(path);
}

bool AppearancePanel::loadColoursFrom(const QString& path)
{
    return SettingsEdits::loadColoursFrom(path);
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
    // SettingsEdits owns the picker (Phase 3b) - one picker for both drawers.
    SettingsEdits::openColourPicker(id, this);
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
    if (myGridDensityLabel) texts << myGridDensityLabel->text();
    if (myEdgeWidthLabel) texts << myEdgeWidthLabel->text();
    if (mySketchLineWidthLabel) texts << mySketchLineWidthLabel->text();
    if (myGizmoScaleLabel) texts << myGizmoScaleLabel->text();
    if (myStrokeLabel) texts << myStrokeLabel->text();
    if (myFamilyLabel) texts << myFamilyLabel->text();
    if (mySave) texts << mySave->text();
    if (myLoad) texts << myLoad->text();
    // Both of the reset button's labels, not only the one it shows now - the
    // armed one is up for four seconds and the sweep must cover it too.
    texts << resetLabel() << resetArmedLabel();
    // The tab words, every mirrored row's name and every choice chip's
    // label. Collected as each is built rather than listed again here, so a
    // row added later cannot slip past the sweep by being forgotten in this
    // function - the generate-don't-duplicate rule WalkthroughPanel and
    // ShortcutSheet already follow.
    texts << myExtraTexts;
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

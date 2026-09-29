#include "RenderSettingsPanel.h"

#include "IconSet.h"
#include "Measure.h"
#include "Theme.h"

#include <QAction>
#include <QColorDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

// AppearancePanel's own numbers - this card sits at the same corner and
// wears the same family, so a different width or radius between immediate
// neighbours would read as a mistake. Narrower than the 296 Appearance
// needs (that card carries a scrolling list of colour tokens; this one is
// six fixed rows). Routed through Theme::wholeDevicePixels() at the
// setFixedWidth() call site below, AppearancePanel's own idiom for a card
// that pins its own size - ViewportOverlay::relayout()'s generic
// resize(Theme::wholeDevicePixels(placed->size())) is a documented no-op on
// a fixed dimension, so a caller that pins one has to do the rounding
// itself. 260 already lands on a whole device pixel at every quarter
// Windows scale (it is a multiple of 4, wholeDevicePixels()'s own rounding
// step), so this call is a no-op today - but reading the value through the
// function rather than trusting that arithmetic by eye is what keeps a
// future editor from copying the LITERAL instead of the PATTERN and
// shipping a width that is not.
// 296, THE SAME WIDTH AS THE SETTINGS DRAWER it sits beside - which is what
// the comment above always said the rule was, while the number said 260. At
// 260 the content genuinely did not fit: with kPad either side the rows had
// 236 logical pixels for a label, a 120-wide slider AND a value, so
// "Strength" showed a truncated number, "Grain size" read "30(" and the
// export row ran its last chip off the edge. Measured rather than eyeballed -
// the clipping is plain in a composited capture of the card and invisible in
// any render of the widget on its own, which is the whole reason CLAUDE.md
// says to measure the composited window.
constexpr int kWidth = 296;
// The x on a shot row: square enough to read as a dismiss rather than as a
// second, nameless shot beside the one it belongs to.
constexpr int kShotDropWidth = 26;
constexpr int kRadius = 10;
constexpr int kPad = 12;
constexpr int kRowSpacing = 10;
constexpr int kSectionGap = 12;

constexpr int kSliderWidth = 120;

constexpr int kSwatchWidth = 46;
constexpr int kSwatchHeight = 18;
constexpr int kSwatchRadius = 4;

// The shutter's own geometry - a circle, not a rounded rect, so its radius
// IS half its side rather than a small corner cut.
constexpr int kShutterSide = 56;
constexpr int kShutterGlyph = 22;
// The wide footer shutter's own height and radius (Milestone 5).
constexpr int kShutterWideHeight = 36;
constexpr int kShutterWideRadius = 9;
constexpr int kShutterWideGlyph = 16;

// The material preset tiles - small painted thumbnails, B's own selector.
// THREE per row in a wrapping grid: however many materials the user's
// folder holds, the selector grows rows rather than clipping (the fixed
// single row clipped at four - the user's own report).
constexpr int kTileWidth = 66;
constexpr int kTileHeight = 40;
constexpr int kTileRadius = 6;
constexpr int kTileColumns = 3;
constexpr int kTileGap = 7;

// The Quality chips share the tile height's rhythm at a text size.
constexpr int kSegHeight = 26;

// Light strength's own slider convention: the integer value IS the
// multiplier times 100 (so 200 == 2.00x), the same "slider units are the
// real unit at a fixed decimal shift" idiom AppearancePanel's grid-density
// spinner uses in reverse (a QDoubleSpinBox there; a QSlider only offers
// integers, so the shift lives here instead).
constexpr int kLightStrengthMin = 20;    // 0.20x
constexpr int kLightStrengthMax = 400;   // 4.00x

// `background: transparent` on a child of this card is not decoration - the
// app-wide stylesheet paints every QWidget chrome-black, and an unqualified
// rule on a container would hand its children the same fill, costing them
// their own chrome. AppearancePanel::makeTransparent()'s own reasoning,
// copied rather than shared across two .cpp files for one four-line helper.
void makeTransparent(QWidget* widget, const QString& name)
{
    widget->setObjectName(name);
    widget->setStyleSheet(QStringLiteral("#%1 { background: transparent; border: none; }")
                              .arg(name));
}

}  // namespace

// --- Swatch -------------------------------------------------------------

// The background row's own colour chip - AppearancePanel's Swatch class,
// minus the per-token bookkeeping that class needs and this one does not
// (there is exactly one swatch here). Painted rather than a QPushButton
// stylesheet background for the same reason: a stylesheet fill is a square,
// and this is a rounded chip with the family's own 1px border.
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
        painter.fillRect(rect(), Theme::panel());

        QPainterPath path;
        path.addRoundedRect(rect(), kSwatchRadius, kSwatchRadius);
        painter.fillPath(path, myColour);
        Theme::drawCrispBorder(painter, QRectF(rect()), Theme::border(), kSwatchRadius);

    }

private:
    QColor myColour;
};

// --- MaterialTile ---------------------------------------------------------

// One preset thumbnail: a painted sphere-on-ground look whose gloss and
// metal MATCH the values the tile writes into the two sliders, so what the
// thumbnail promises is derived from the same two numbers the click sets -
// never a bitmap that could drift from them. Selection is DERIVED: the tile
// reads as current when both sliders sit within a hair of its own values.
class MaterialTile : public QAbstractButton {
public:
    MaterialTile(const QString& name, double glossiness01, double metallic01, QWidget* parent,
                 bool wood = false)
        : QAbstractButton(parent)
        , myName(name)
        , myGloss(glossiness01)
        , myMetal(metallic01)
        , myWood(wood)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_NoMousePropagation);
        Theme::makeSurfaceTransparent(this);
        setFixedSize(kTileWidth, kTileHeight);
        setToolTip(name);
    }

    // The image-backed variant: one tile per file the user dropped into the
    // materials folder. The thumbnail is decoded and scaled ONCE, here -
    // CLAUDE.md's own law: no paintEvent may decode or rescale an asset,
    // and this tile repaints on every hover.
    MaterialTile(const QString& name, const QString& filePath, QWidget* parent)
        : QAbstractButton(parent)
        , myName(name)
        , myFilePath(filePath)
        , myWood(true)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_NoMousePropagation);
        Theme::makeSurfaceTransparent(this);
        setFixedSize(kTileWidth, kTileHeight);
        setToolTip(name);
        // Decoded AT thumbnail size through QImageReader - libjpeg's own
        // DCT scaling decodes a 4K texture at ~2x tile size directly,
        // where `QImage image(filePath)` decoded every texture at native
        // resolution on the main thread at startup (hundreds of ms and
        // tens of MB per multi-megapixel file - the branch review's
        // measurement). The 2x is for crispness on scaled displays and is
        // DECLARED via setDevicePixelRatio, which is also what fixes the
        // review's other finding here: drawn with a 1x source rect, the
        // undeclared 2x pixmap showed a zoomed-in centre QUARTER of the
        // texture instead of the texture.
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        const QSize native = reader.size();
        if (native.isValid()) {
            QSize target(kTileWidth * 2, kTileHeight * 2);
            const QSize expanded =
                native.scaled(target, Qt::KeepAspectRatioByExpanding);
            reader.setScaledSize(expanded);
        }
        const QImage image = reader.read();
        if (!image.isNull()) {
            QPixmap thumb = QPixmap::fromImage(image);
            thumb.setDevicePixelRatio(2.0);
            myThumb = thumb;
        }
    }

    QString name() const { return myName; }
    QString filePath() const { return myFilePath; }
    double glossiness() const { return myGloss; }
    double metallic() const { return myMetal; }
    bool isWood() const { return myWood; }
    void setCurrent(bool current)
    {
        if (myCurrent == current) return;
        myCurrent = current;
        update();
    }

    QSize sizeHint() const override { return QSize(kTileWidth, kTileHeight); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPainterPath clip;
        const QRectF body(rect());
        clip.addRoundedRect(body, kTileRadius, kTileRadius);
        painter.save();
        painter.setClipPath(clip);

        // The studio in miniature: warm backdrop over a slightly deeper
        // floor band - the same flat-grey world render mode dresses. The
        // literals are MATERIAL DEPICTION, not shell chrome: they mimic
        // the render backdrop (itself calibrated against sampled pixels,
        // not a Theme token) the way the OCCT body materials do, and fall
        // under the same "remaining untokenised colours, by scope ruling"
        // line CLAUDE.md already draws for those.
        painter.fillRect(rect(), QColor(0xc6, 0xc3, 0xbe));
        painter.fillRect(QRect(0, int(height() * 0.66), width(), height()),
                         QColor(0xb7, 0xb4, 0xae));

        // The sphere: base tone by metal, highlight sharpness by gloss -
        // and the wood tile paints its own grain across the whole face
        // instead, the thumbnail being the material.
        if (myWood && !myThumb.isNull()) {
            // The user's own image is the thumbnail - what the tile promises
            // is literally the file the click applies.
            // The pixmap carries devicePixelRatio 2, so a plain draw at the
            // tile's own rect shows the WHOLE texture at 2x crispness -
            // the source-rect arithmetic this replaces mixed device and
            // logical units and blitted a quarter of the image.
            const QSizeF logical = QSizeF(myThumb.size()) / myThumb.devicePixelRatio();
            const QPointF at((width() - logical.width()) / 2.0,
                             (height() - logical.height()) / 2.0);
            painter.drawPixmap(QRectF(at, logical), myThumb,
                               QRectF(QPointF(0, 0), QSizeF(myThumb.size())));
            const QRect texStrip(0, height() - 13, width(), 13);
            painter.fillRect(texStrip, QColor(0, 0, 0, 150));
            painter.setPen(myCurrent ? QColor(Qt::white) : Theme::textMuted());
            painter.setFont(Theme::badgeFont());
            painter.drawText(texStrip, Qt::AlignCenter, myName);
            painter.restore();
            Theme::drawCrispBorder(painter, body,
                                   myCurrent ? Theme::accent() : Theme::border(), kTileRadius,
                                   myCurrent ? 2.0 : 1.0);
            return;
        }
        if (myWood) {
            for (int x = 0; x < width(); ++x) {
                const double band =
                    std::sin((x + 4.0 * std::sin(x * 0.10)) * 0.55) * 0.5 + 0.5;
                const QColor grain =
                    band < 0.5 ? QColor(0x6b, 0x48, 0x2a) : QColor(0x8f, 0x6a, 0x45);
                painter.fillRect(QRect(x, 0, 1, height()), grain);
            }
            const QRect woodStrip(0, height() - 13, width(), 13);
            painter.fillRect(woodStrip, QColor(0, 0, 0, 150));
            painter.setPen(myCurrent ? QColor(Qt::white) : Theme::textMuted());
            painter.setFont(Theme::badgeFont());
            painter.drawText(woodStrip, Qt::AlignCenter, myName);
            painter.restore();
            Theme::drawCrispBorder(painter, body,
                                   myCurrent ? Theme::accent() : Theme::border(), kTileRadius,
                                   myCurrent ? 2.0 : 1.0);
            return;
        }
        const QRectF ball(width() * 0.5 - height() * 0.30, height() * 0.16,
                          height() * 0.60, height() * 0.60);
        const QColor base = myMetal > 0.5 ? QColor(0x9a, 0x9c, 0xa2)
                                          : QColor(0x8f, 0x6a, 0x45);
        QRadialGradient shade(ball.center() + QPointF(-ball.width() * 0.18,
                                                      -ball.height() * 0.22),
                              ball.width() * 0.85);
        const double sharp = 0.15 + 0.5 * (1.0 - myGloss);
        shade.setColorAt(0.0, base.lighter(myMetal > 0.5 ? 175 : 145));
        shade.setColorAt(std::min(0.95, sharp), base);
        shade.setColorAt(1.0, base.darker(150));
        painter.setPen(Qt::NoPen);
        painter.setBrush(shade);
        painter.drawEllipse(ball);
        // A glossy surface carries a hard white catchlight.
        if (myGloss > 0.35) {
            painter.setBrush(QColor(255, 255, 255,
                                    int(90 + 130 * std::min(1.0, myGloss))));
            const double r = ball.width() * (0.06 + 0.06 * myGloss);
            painter.drawEllipse(QPointF(ball.center().x() - ball.width() * 0.2,
                                        ball.center().y() - ball.height() * 0.24),
                                r, r);
        }

        // The caption strip, panel-dark so the name reads on any thumbnail.
        const QRect strip(0, height() - 13, width(), 13);
        painter.fillRect(strip, QColor(0, 0, 0, 150));
        painter.setPen(myCurrent ? QColor(Qt::white) : Theme::textMuted());
        QFont f = Theme::badgeFont();
        painter.setFont(f);
        painter.drawText(strip, Qt::AlignCenter, myName);
        painter.restore();

        Theme::drawCrispBorder(painter, body,
                               myCurrent ? Theme::accent() : Theme::border(), kTileRadius,
                               myCurrent ? 2.0 : 1.0);
    }

private:
    QString myName;
    QString myFilePath;
    QPixmap myThumb;
    double myGloss = 0.0;
    double myMetal = 0.0;
    bool myWood = false;
    bool myCurrent = false;
};

// --- SegChip ----------------------------------------------------------------

// One half of the Quality pair - a small checkable-looking chip that holds
// no state of its own: the panel derives which half reads as current from
// myQuick, ToolChip's own action-mirroring discipline at panel scale.
class SegChip : public QAbstractButton {
public:
    SegChip(const QString& text, QWidget* parent) : QAbstractButton(parent)
    {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_NoMousePropagation);
        Theme::makeSurfaceTransparent(this);
        setFixedHeight(kSegHeight);
        setAttribute(Qt::WA_Hover, true);
    }

    void setCurrent(bool current)
    {
        if (myCurrent == current) return;
        myCurrent = current;
        update();
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(Theme::labelFont());
        return QSize(fm.horizontalAdvance(text()) + 22, kSegHeight);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF body(rect());
        QPainterPath path;
        path.addRoundedRect(body, 7, 7);
        QColor fill = Theme::chip();
        if (myCurrent)           fill = Theme::chipActive();
        else if (underMouse())   fill = Theme::chipHover();
        painter.fillPath(path, fill);
        Theme::drawCrispBorder(painter, body,
                               myCurrent ? Theme::accent() : Theme::border(), 7,
                               myCurrent ? 1.6 : 1.0);
        painter.setFont(Theme::labelFont());
        painter.setPen(myCurrent ? Theme::text() : Theme::textMuted());
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    bool myCurrent = false;
};

// --- ProgressLine -----------------------------------------------------------

// The footer's thin polish bar - a 3px line filled to a fraction. Hidden
// entirely when the tier has no notion of "polishing" (everything but the
// path-traced one).
class ProgressLine : public QWidget {
public:
    explicit ProgressLine(QWidget* parent) : QWidget(parent)
    {
        Theme::makeSurfaceTransparent(this);
        setFixedHeight(3);
    }
    void setFraction(double f)
    {
        f = std::clamp(f, 0.0, 1.0);
        if (std::fabs(f - myFraction) < 0.005) return;
        myFraction = f;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Theme::border());
        QRect fill = rect();
        fill.setWidth(int(std::round(width() * myFraction)));
        painter.fillRect(fill, Theme::accent());
    }

private:
    double myFraction = 0.0;
};

// --- RenderSettingsPanel --------------------------------------------------

RenderSettingsPanel::RenderSettingsPanel(QWidget* parent)
    : QWidget(parent)
{
    // The floating-surface family's mouse rule - see ItemsPanel.h/
    // AppearancePanel.h for the full reasoning: this card must swallow every
    // press and release rather than let one fall through to the viewport
    // underneath, where it would either re-pick the model or - render
    // mode's own hazard - trigger the exit gesture a plain viewport press
    // performs. Pinned by gui_smoke: a click anywhere on this card must
    // never exit render mode.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    // See Theme::makeSurfaceTransparent()'s own comment.
    Theme::makeSurfaceTransparent(this);
    setFixedWidth(Theme::wholeDevicePixels(kWidth));

    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(kPad, kPad, kPad, kPad);
    shell->setSpacing(kRowSpacing);

    myTitle = new QLabel(tr("Render settings"), this);
    shell->addWidget(myTitle);

    // The SECTIONS scroll; the footer below stays pinned. ViewportOverlay's
    // RightEdge anchor stretches this panel to the viewport's height but
    // never clamps it DOWN, and this panel had no scroll area (unlike the
    // Appearance card sharing that edge) - so a short window, or a
    // materials folder full of tiles, pushed the Quality switch and the
    // shutter below the viewport's bottom edge, unreachable (the branch
    // review's finding). AppearancePanel's own transparent-scroll idiom.
    auto* sectionScroll = new QScrollArea(this);
    sectionScroll->setWidgetResizable(true);
    sectionScroll->setFrameShape(QFrame::NoFrame);
    sectionScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // THE VERTICAL ONE APPEARS ONLY WHEN IT IS NEEDED, and that is a reversal
    // worth stating. It was turned fully off first, borrowing the Items
    // drawer's "invisible scroll bar" - which is right for a card that sizes
    // itself to its content, because there is then never anything below the
    // fold. This panel is a fixed height (the window's) with a growing list
    // of sections, and the Shots section pushed Quality, the cut-out switch
    // and every export size off the bottom: an invisible bar did not keep a
    // shot clean, it hid five controls with nothing on screen to say they
    // were there. A bar that appears only when something is genuinely out of
    // reach says the one true thing, and says it only when it is true.
    sectionScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    makeTransparent(sectionScroll, QStringLiteral("renderSettingsScroll"));
    makeTransparent(sectionScroll->viewport(),
                    QStringLiteral("renderSettingsScrollViewport"));
    auto* sectionContent = new QWidget(sectionScroll);
    makeTransparent(sectionContent, QStringLiteral("renderSettingsContent"));
    auto* outer = new QVBoxLayout(sectionContent);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(kRowSpacing);
    sectionScroll->setWidget(sectionContent);
    shell->addWidget(sectionScroll, 1);
    myScroll = sectionScroll;
    myScrollContent = sectionContent;

    // A section header - smaller and more muted than a row label, the studio
    // panel's own grouping device (mockup A). Collected for applyTheme() and
    // for paintedTexts().
    auto addSection = [&](const QString& label) {
        auto* head = new QLabel(label, this);
        makeTransparent(head, QStringLiteral("renderSettingsSection_") +
                                  QString::number(mySectionLabels.size()));
        mySectionLabels.push_back(head);
        outer->addSpacing(2);
        outer->addWidget(head);
    };

    auto addRule = [&] {
        auto* rule = new QWidget(this);
        rule->setFixedHeight(1);
        makeTransparent(rule, QStringLiteral("renderSettingsRule"));
        // The rule itself IS the border colour - painted via a per-widget
        // stylesheet on its own object name, the opaque-family rule this
        // whole card follows: a solid, fully-opaque fill, never a
        // translucent one.
        rule->setStyleSheet(rule->styleSheet() +
                            QStringLiteral(" #renderSettingsRule { background: %1; }")
                                .arg(Theme::border().name()));
        outer->addWidget(rule);
        // The extra breathing room a rule wants beyond the ordinary
        // between-row gap kRowSpacing already gives every pair of rows.
        outer->addSpacing(kSectionGap - kRowSpacing);
    };

    // `key` is a plain ASCII identifier for the object-name selectors
    // makeTransparent() writes - deliberately NOT `label` itself, which is
    // translatable and, for three of these six rows, contains a space; an
    // unescaped space in a Qt stylesheet ID selector breaks the rule
    // instead of scoping it (it reads as a descendant combinator), which
    // would have silently cost "Light angle"/"Light strength"/"Camera FOV"
    // their transparent fill - exactly the black-square-over-the-GL-surface
    // defect the opaque-paint-family rule exists to prevent, on three rows
    // out of six.
    auto addRow = [&](const QString& key, const QString& label, int min, int max, int value) {
        auto* row = new QWidget(this);
        makeTransparent(row, QStringLiteral("renderSettingsRow_") + key);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(8);

        auto* name = new QLabel(label, row);
        makeTransparent(name, QStringLiteral("renderSettingsLabel_") + key);
        line->addWidget(name, 1);
        myRowLabels.push_back(name);

        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(min, max);
        slider->setValue(value);
        slider->setFixedWidth(kSliderWidth);
        line->addWidget(slider);

        // The value readout (mockup A: every slider wears its number). The
        // TEXT is derived in syncValueLabels() so a programmatic set and a
        // drag paint through one formatter.
        auto* readout = new QLabel(row);
        makeTransparent(readout, QStringLiteral("renderSettingsValue_") + key);
        readout->setMinimumWidth(34);
        readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        line->addWidget(readout);
        myValueLabels.push_back({slider, readout});
        connect(slider, &QSlider::valueChanged, this,
                &RenderSettingsPanel::syncValueLabels);

        outer->addWidget(row);
        return slider;
    };

    // --- Light ----------------------------------------------------------
    // --- Frame ----------------------------------------------------------
    // FIRST, above everything else, because it is the first decision a
    // picture needs: what shape is it. The user asked for the picker and the
    // guides in one breath - "add a aspect ratio selected and some guides to
    // help me to put right the camera" - and they are one section for the
    // same reason, both being about where the edges of the shot are rather
    // than about what is in it.
    addSection(tr("Frame"));
    {
        auto* row = new QWidget(this);
        makeTransparent(row, QStringLiteral("renderSettingsAspectRow"));
        // Two rows of chips, the export row's own 3 + 2 grid: five of these
        // do not fit this card's content width either, and the fifth would
        // simply be drawn off the edge.
        auto* line = new QGridLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setHorizontalSpacing(5);
        line->setVerticalSpacing(5);
        const struct { const char* label; const char* tip; } shapes[] = {
            {"Free", "The window's own shape - nothing is masked off"},
            {"1:1", "A square picture"},
            {"4:5", "Taller than wide - a single piece, standing"},
            {"3:2", "A photograph's own shape"},
            {"16:9", "Wide - a room, or a run of furniture"}};
        for (std::size_t i = 0; i < myAspectChips.size(); ++i) {
            auto* chip = new SegChip(tr(shapes[i].label), row);
            chip->setToolTip(tr(shapes[i].tip));
            connect(chip, &QAbstractButton::clicked, this, [this, i] {
                const Aspect picked = static_cast<Aspect>(i);
                if (myAspect == picked) return;
                setAspect(picked);
                emit aspectChanged(picked);
            });
            myAspectChips[i] = chip;
            line->addWidget(chip, static_cast<int>(i / 3), static_cast<int>(i % 3));
        }
        line->setColumnStretch(3, 1);
        outer->addWidget(row);
    }
    {
        auto* row = new QWidget(this);
        makeTransparent(row, QStringLiteral("renderSettingsGuidesRow"));
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(5);
        const struct { const char* label; const char* tip; } kinds[] = {
            {"Off", "No lines inside the picture"},
            {"Thirds", "Two lines each way - put what matters where they cross"},
            {"Centre", "One line each way, through the middle"}};
        for (std::size_t i = 0; i < myGuideChips.size(); ++i) {
            auto* chip = new SegChip(tr(kinds[i].label), row);
            chip->setToolTip(tr(kinds[i].tip));
            connect(chip, &QAbstractButton::clicked, this, [this, i] {
                const Guides picked = static_cast<Guides>(i);
                if (myGuides == picked) return;
                setGuides(picked);
                emit guidesChanged(picked);
            });
            myGuideChips[i] = chip;
            line->addWidget(chip);
        }
        line->addStretch(1);
        outer->addWidget(row);
    }
    setAspect(Aspect::Free);
    setGuides(Guides::Thirds);

    addRule();

    // --- Shots ------------------------------------------------------------
    // "can you add a menu to save camera positions and settings? so i can do
    // multiple images using those settings and always be the same." Directly
    // under Frame, because a shot restores the frame too - the two rows above
    // are part of what one of these rows puts back.
    addSection(tr("Shots"));
    {
        auto* row = new QWidget(this);
        makeTransparent(row, QStringLiteral("renderSettingsShotSaveRow"));
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(5);
        auto* save = new SegChip(tr("Save this view"), row);
        save->setToolTip(tr("Remember where the camera is, the picture's shape, the lens "
                            "and the light — so the next image can be taken the same way"));
        connect(save, &QAbstractButton::clicked, this,
                [this] { emit shotSaveRequested(); });
        mySaveShot = save;
        line->addWidget(save);
        line->addStretch(1);
        outer->addWidget(row);

        myShotRows = new QWidget(this);
        makeTransparent(myShotRows, QStringLiteral("renderSettingsShotRows"));
        auto* rows = new QVBoxLayout(myShotRows);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(4);
        outer->addWidget(myShotRows);

        // The empty state says what the button above is for, rather than
        // leaving a heading over nothing.
        myShotEmpty = new QLabel(tr("No shots saved yet"), this);
        myShotEmpty->setObjectName(QStringLiteral("renderSettingsShotEmpty"));
        myShotEmpty->setWordWrap(true);
        outer->addWidget(myShotEmpty);
    }

    addRule();

    addSection(tr("Light"));
    myLightAngleSlider = addRow(QStringLiteral("lightAngle"), tr("Angle"), 0, 359, 0);
    connect(myLightAngleSlider, &QSlider::valueChanged, this, [this](int v) {
        if (mySyncing) return;
        emit lightAngleChanged(static_cast<double>(v));
    });
    myLightStrengthSlider = addRow(QStringLiteral("lightStrength"), tr("Strength"),
                                   kLightStrengthMin, kLightStrengthMax, 200);
    connect(myLightStrengthSlider, &QSlider::valueChanged, this, [this](int v) {
        if (mySyncing) return;
        emit lightStrengthChanged(v / 100.0);
    });

    addRule();

    // --- Material (mockup B's selector: preset tiles over the sliders) ---
    addSection(tr("Material"));
    {
        auto* grid = new QWidget(this);
        makeTransparent(grid, QStringLiteral("renderSettingsPresetGrid"));
        myTileGrid = new QGridLayout(grid);
        myTileGrid->setContentsMargins(0, 0, 0, 0);
        myTileGrid->setHorizontalSpacing(kTileGap);
        myTileGrid->setVerticalSpacing(kTileGap);
        // Left-packed: the grid's own columns stay at tile width and a
        // stretch column soaks the slack, so a row of one or two tiles
        // does not spread them across the panel.
        myTileGrid->setColumnStretch(kTileColumns, 1);
        // MATTE, SATIN AND METAL ARE GONE. The user's call, in as many words:
        // "only left the wood materials, remove the other ones". They were
        // never materials in the way a wood is - each was a gloss/metal pair
        // wearing a name - and once those two numbers moved onto the material
        // card, per material, a tile whose whole content was a pair of them
        // had nothing left to be.
        //
        // What that buys beyond the user's own reason: activeMaterialName()
        // used to DERIVE which of the three was current from the live gloss
        // and metal, so moving those values per material would have made the
        // name depend on the very values it was selecting. With woods only,
        // the live material simply names itself.
        //
        // The built-in Wood tile: a material flag, not a slider pair - see
        // setWood(). File-backed materials join the same grid through
        // addTextureMaterials().
        auto* woodTile = new MaterialTile(tr("Wood"), 0.0, 0.0, grid, /*wood=*/true);
        woodTile->setToolTip(tr("Dress every body in wood grain for the picture"));
        connect(woodTile, &QAbstractButton::clicked, this, [this, woodTile] {
            if (myWood && myWoodName == woodTile->name()) return;
            myWoodName = woodTile->name();
            setWood(true);
            emit woodTextureChosen(woodTile->name(), QString());
        });
        addTile(woodTile);
        outer->addWidget(grid);
    }
    // SURFACE, METAL, GRAIN SIZE AND GRAIN ANGLE ARE NOT HERE ANY MORE. They
    // are on the material card, per material - "the data from the image
    // should be per material, so add that into the material setting" - which
    // is where they always belonged: they describe ONE wood, and this panel
    // describes the studio the wood is standing in. Double-click a tile to
    // reach them.
    //
    // The muted note that used to sit under them, saying which tier reads
    // Surface and Metal, went with them: it was about those two rows and
    // there are no rows here for it to be about. The tier caveat itself is
    // unchanged and is stated on each dial's tooltip on the card instead,
    // which is where the user now is when it matters.
    myMaterialNote = new QLabel(
        tr("Double-click a wood to set its colour, surface and grain"), this);
    myMaterialNote->setObjectName(QStringLiteral("renderSettingsMaterialNote"));
    myMaterialNote->setWordWrap(true);
    outer->addWidget(myMaterialNote);

    addRule();

    // --- Scene ----------------------------------------------------------
    // "Studio", not "Scene": scene is the vocabulary table's Never-word
    // for the 3D area, and this section dresses the studio - backdrop,
    // floor, shadows - which is exactly what the render-mode docs call it.
    addSection(tr("Studio"));
    {
        auto* row = new QWidget(this);
        makeTransparent(row, QStringLiteral("renderSettingsBackgroundRow"));
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(8);
        auto* name = new QLabel(tr("Background"), row);
        makeTransparent(name, QStringLiteral("renderSettingsBackgroundLabel"));
        line->addWidget(name, 1);
        myRowLabels.push_back(name);

        myBackgroundSwatch = new Swatch(row);
        myBackgroundSwatch->setToolTip(tr("Pick the render-mode backdrop colour"));
        connect(myBackgroundSwatch, &QAbstractButton::clicked, this,
                &RenderSettingsPanel::openBackgroundDialog);
        line->addWidget(myBackgroundSwatch);
        outer->addWidget(row);
    }

    addRule();

    // --- Camera ----------------------------------------------------------
    addSection(tr("Camera"));
    myFovSlider = addRow(QStringLiteral("fov"), tr("FOV"), 20, 120, 45);
    connect(myFovSlider, &QSlider::valueChanged, this, [this](int v) {
        if (mySyncing) return;
        emit fovChanged(static_cast<double>(v));
    });

    addRule();

    // --- Quality ---------------------------------------------------------
    addSection(tr("Quality"));
    {
        auto* row = new QWidget(this);
        makeTransparent(row, QStringLiteral("renderSettingsQualityRow"));
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(7);
        // Ascending, left to right, so the row reads as a dial rather than as
        // three unrelated buttons.
        mySimpleChip = new SegChip(tr("Simple"), row);
        mySimpleChip->setToolTip(tr("Instant frames with real shadows — for framing a "
                                    "shot or a quicker machine"));
        myBalancedChip = new SegChip(tr("Balanced"), row);
        myBalancedChip->setToolTip(tr("Sharp the moment it appears, with no grain to wait "
                                      "out — the middle of the three"));
        myDeepChip = new SegChip(tr("Deep"), row);
        myDeepChip->setToolTip(tr("The richest picture this machine reaches — slower, "
                                  "and it keeps polishing while you watch"));
        const auto pick = [this](Quality quality) {
            if (myQuality == quality) return;
            setQuality(quality);
            emit qualityChanged(quality);
            // The two-state signal still fires for whatever only knows Quick -
            // and only when the answer to THAT question actually changed.
            emit quickChanged(quality == Quality::Simple);
        };
        connect(mySimpleChip, &QAbstractButton::clicked, this,
                [pick] { pick(Quality::Simple); });
        connect(myBalancedChip, &QAbstractButton::clicked, this,
                [pick] { pick(Quality::Balanced); });
        connect(myDeepChip, &QAbstractButton::clicked, this, [pick] { pick(Quality::Deep); });
        line->addWidget(mySimpleChip);
        line->addWidget(myBalancedChip);
        line->addWidget(myDeepChip);
        line->addStretch(1);
        outer->addWidget(row);
    }
    setQuality(Quality::Deep);

    // The cut-out switch, under Quality because that is where what an EXPORT
    // produces is decided. One chip that reads as on or off, the Quality
    // chips' own control rather than a new kind of switch on this card.
    {
        auto* row = new QWidget(this);
        makeTransparent(row, QStringLiteral("renderSettingsCutoutRow"));
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(7);
        myCutoutChip = new SegChip(tr("Cut out"), row);
        myCutoutChip->setToolTip(tr("Save Screenshot writes a PNG with no floor and no "
                                    "background — just the furniture, on transparency"));
        connect(myCutoutChip, &QAbstractButton::clicked, this, [this] {
            setCutout(!myCutout);
            emit cutoutChanged(myCutout);
        });
        line->addWidget(myCutoutChip);
        line->addStretch(1);
        outer->addWidget(row);
    }
    setCutout(false);

    // The export SIZE, beside the cut-out switch: both are about what a saved
    // file contains rather than what the viewport shows.
    {
        auto* row = new QWidget(this);
        makeTransparent(row, QStringLiteral("renderSettingsExportRow"));
        // TWO ROWS, because five chips do not fit one row of this card's
        // content width and the fifth was simply drawn off the edge. The
        // Settings drawer's tab bar already wraps for exactly this reason;
        // this is the same answer in the same shape - a grid rather than a
        // greedy pack, since these chips are near enough the same width that
        // a fixed 3 + 2 split is honest and needs no measuring pass.
        auto* line = new QGridLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setHorizontalSpacing(5);
        line->setVerticalSpacing(5);
        const struct { ExportSize size; const char* label; const char* tip; } kSizes[] = {
            {ExportSize::Viewport, QT_TR_NOOP("Window"),
             QT_TR_NOOP("Twice the viewport's own pixels — what this app has always saved")},
            {ExportSize::Height720, QT_TR_NOOP("720"), QT_TR_NOOP("1280 x 720, near enough")},
            {ExportSize::Height1080, QT_TR_NOOP("1080"), QT_TR_NOOP("Full HD")},
            {ExportSize::Height1440, QT_TR_NOOP("1440"), QT_TR_NOOP("Quad HD")},
            {ExportSize::Height2160, QT_TR_NOOP("4K"), QT_TR_NOOP("Ultra HD")},
        };
        for (std::size_t i = 0; i < myExportChips.size(); ++i) {
            auto* chip = new SegChip(tr(kSizes[i].label), row);
            chip->setToolTip(tr(kSizes[i].tip));
            const ExportSize size = kSizes[i].size;
            connect(chip, &QAbstractButton::clicked, this, [this, size] {
                if (myExportSize == size) return;
                setExportSize(size);
                emit exportSizeChanged(size);
            });
            myExportChips[i] = chip;
            line->addWidget(chip, static_cast<int>(i) / 3, static_cast<int>(i) % 3);
        }
        // The empty cell at the end of row two takes the slack, so the chips
        // stay left-aligned under their neighbours above rather than
        // stretching to fill.
        line->setColumnStretch(3, 1);
        outer->addWidget(row);

        // What those chips actually produce, in pixels, pushed in by
        // MainWindow - and what the live tier can honour.
        myExportNote = new QLabel(this);
        myExportNote->setWordWrap(true);
        makeTransparent(myExportNote, QStringLiteral("renderSettingsExportNote"));
        outer->addWidget(myExportNote);
    }
    setExportSize(ExportSize::Viewport);

    // --- footer: the live tier, the polish bar, the shutter ---------------
    // OUTSIDE the scroll, pinned to the panel's bottom edge: whatever the
    // window's height and however many material tiles the folder grew, the
    // shutter stays reachable - the sections above are what give, through
    // their own scrollbar. The stretch inside the scroll content pushes
    // short content to the top the way the old full-height stretch did.
    outer->addStretch(1);
    myTierLabel = new QLabel(this);
    makeTransparent(myTierLabel, QStringLiteral("renderSettingsTierLabel"));
    shell->addWidget(myTierLabel);
    myProgress = new ProgressLine(this);
    myProgress->hide();
    shell->addWidget(myProgress);

    syncValueLabels();
    syncPresetTiles();
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this,
            &RenderSettingsPanel::applyTheme);
}

void RenderSettingsPanel::setSurfaceGlossiness(double glossiness01)
{
    const int v = static_cast<int>(std::round(std::clamp(glossiness01, 0.0, 1.0) * 100.0));
    if (mySurfaceSlider) mySurfaceSlider->setValue(v);   // valueChanged emits for us
}

void RenderSettingsPanel::setMetal(double metallic01)
{
    const int v = static_cast<int>(std::round(std::clamp(metallic01, 0.0, 1.0) * 100.0));
    if (myMetalSlider) myMetalSlider->setValue(v);
}

void RenderSettingsPanel::setLightAngle(double azimuthDeg)
{
    double wrapped = std::fmod(azimuthDeg, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    if (myLightAngleSlider) myLightAngleSlider->setValue(static_cast<int>(std::round(wrapped)));
}

void RenderSettingsPanel::setLightStrength(double multiplier)
{
    const int v = static_cast<int>(std::round(
        std::clamp(multiplier, kLightStrengthMin / 100.0, kLightStrengthMax / 100.0) * 100.0));
    if (myLightStrengthSlider) myLightStrengthSlider->setValue(v);
}

void RenderSettingsPanel::setBackground(const QColor& colour)
{
    if (!colour.isValid()) return;
    myBackground = colour;
    if (myBackgroundSwatch) myBackgroundSwatch->setColour(colour);
    emit backgroundChanged(colour);
}

void RenderSettingsPanel::setFov(double fovyDeg)
{
    const int v = static_cast<int>(std::round(std::clamp(fovyDeg, 20.0, 120.0)));
    if (myFovSlider) myFovSlider->setValue(v);
}

void RenderSettingsPanel::setValuesSilently(double glossiness01, double metallic01,
                                            double lightAngleDeg, double lightStrength,
                                            const QColor& background, double fovyDeg)
{
    mySyncing = true;
    setSurfaceGlossiness(glossiness01);
    setMetal(metallic01);
    setLightAngle(lightAngleDeg);
    setLightStrength(lightStrength);
    if (background.isValid()) {
        myBackground = background;
        if (myBackgroundSwatch) myBackgroundSwatch->setColour(background);
    }
    setFov(fovyDeg);
    mySyncing = false;
}

double RenderSettingsPanel::surfaceGlossiness() const
{
    return mySurfaceSlider ? mySurfaceSlider->value() / 100.0 : 0.0;
}
double RenderSettingsPanel::metal() const
{
    return myMetalSlider ? myMetalSlider->value() / 100.0 : 0.0;
}
double RenderSettingsPanel::lightAngle() const
{
    return myLightAngleSlider ? static_cast<double>(myLightAngleSlider->value()) : 0.0;
}
double RenderSettingsPanel::lightStrength() const
{
    return myLightStrengthSlider ? myLightStrengthSlider->value() / 100.0 : 1.0;
}
double RenderSettingsPanel::fov() const
{
    return myFovSlider ? static_cast<double>(myFovSlider->value()) : 45.0;
}

QWidget* RenderSettingsPanel::backgroundSwatch() const { return myBackgroundSwatch; }

void RenderSettingsPanel::openBackgroundDialog()
{
    // AppearancePanel::openColourDialog()'s own shape - see that function
    // for the full reasoning on why this is show(), never open() or exec():
    // a genuinely modeless picker is the point, since the user watches the
    // studio backdrop re-dress live while choosing.
    if (myDialog) {
        myDialog->close();
        myDialog = nullptr;
    }

    auto* dialog = new QColorDialog(myBackground, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Render background colour"));
    dialog->setModal(false);

    const QColor before = myBackground;
    connect(dialog, &QColorDialog::currentColorChanged, this,
            &RenderSettingsPanel::setBackground);
    connect(dialog, &QColorDialog::rejected, this,
            [this, before] { setBackground(before); });
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (myDialog == dialog) myDialog = nullptr;
    });

    myDialog = dialog;
    dialog->show();
}

QStringList RenderSettingsPanel::paintedTexts() const
{
    QStringList texts;
    if (myTitle) texts << myTitle->text();
    for (QLabel* label : mySectionLabels) texts << label->text();
    for (QLabel* label : myRowLabels) texts << label->text();
    for (const auto& pair : myValueLabels) texts << pair.second->text();
    for (MaterialTile* tile : myPresetTiles) {
        // A file-backed tile's name IS the user's own filename - their word
        // choice, not this app's copy, so it is exempt from the banned-word
        // sweep on ItemsPanel's own isUserData terms and simply not
        // reported here (this list has no per-entry exemption channel).
        if (tile->filePath().isEmpty()) texts << tile->name();
    }
    if (myDeepChip) texts << myDeepChip->text();
    if (mySimpleChip) texts << mySimpleChip->text();
    if (myTierLabel) texts << myTierLabel->text();
    // Reported whether or not it is currently shown - see the header.
    if (myMaterialNote) texts << myMaterialNote->text();
    return texts;
}

QSize RenderSettingsPanel::sizeHint() const
{
    const int w = kWidth;
    if (!myScrollContent || !layout()) return QWidget::sizeHint();

    // Everything outside the scroll area (the title, the footer, the shell's
    // own margins) plus what the rows genuinely need - never the scroll
    // area's own answer. See the header.
    int chrome = 0;
    for (int i = 0; i < layout()->count(); ++i) {
        QLayoutItem* item = layout()->itemAt(i);
        if (!item) continue;
        if (item->widget() == myScroll) continue;
        chrome += item->sizeHint().height() + layout()->spacing();
    }
    const QMargins margins = layout()->contentsMargins();
    chrome += margins.top() + margins.bottom();

    const int rows = myScrollContent->sizeHint().height();
    return QSize(w, chrome + rows);
}

void RenderSettingsPanel::setShots(const QStringList& names)
{
    // REBUILT ONLY WHEN THE LIST ACTUALLY CHANGED. MainWindow pushes this on
    // every appStateChanged - which fires on every selection click and once
    // per mouse-move of a colour drag - and tearing down and rebuilding a
    // column of buttons at that rate would put a layout pass on the app's
    // hottest path for no visible difference. ItemsPanel::refresh()'s own
    // signature guard, in the one shape this list needs: the names ARE
    // everything a row draws.
    if (names == myShotNames) return;
    myShotNames = names;
    if (!myShotRows) return;

    auto* rows = qobject_cast<QVBoxLayout*>(myShotRows->layout());
    if (!rows) return;
    myShotApply.clear();
    myShotDrop.clear();
    while (QLayoutItem* item = rows->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    for (int i = 0; i < myShotNames.size(); ++i) {
        auto* row = new QWidget(myShotRows);
        makeTransparent(row, QStringLiteral("renderSettingsShotRow"));
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(4);

        auto* apply = new SegChip(myShotNames.at(i), row);
        apply->setToolTip(tr("Put the camera, the picture's shape, the lens and the light "
                             "back the way they were"));
        connect(apply, &QAbstractButton::clicked, this, [this, i] { emit shotApplied(i); });
        line->addWidget(apply, 1);
        myShotApply.push_back(apply);

        // A shot is FILE data, not document data, so it is deleted rather
        // than undone - the same taxonomy a version's delete follows (that
        // one asks twice on the row; this one is a single click, because a
        // shot holds no work and taking it again is one press of the button
        // above).
        auto* drop = new SegChip(QStringLiteral("\u00d7"), row);
        drop->setToolTip(tr("Forget this shot"));
        drop->setFixedWidth(kShotDropWidth);
        connect(drop, &QAbstractButton::clicked, this, [this, i] { emit shotRemoved(i); });
        line->addWidget(drop);
        myShotDrop.push_back(drop);

        rows->addWidget(row);
    }
    if (myShotEmpty) myShotEmpty->setVisible(myShotNames.isEmpty());
    myShotRows->setVisible(!myShotNames.isEmpty());
    // The card sizes itself from its content (see sizeHint()), and the
    // content just changed height by a whole row.
    updateGeometry();
}

void RenderSettingsPanel::setAspect(Aspect aspect)
{
    myAspect = aspect;
    for (std::size_t i = 0; i < myAspectChips.size(); ++i) {
        if (myAspectChips[i]) myAspectChips[i]->setCurrent(static_cast<Aspect>(i) == aspect);
    }
}

void RenderSettingsPanel::setGuides(Guides guides)
{
    myGuides = guides;
    for (std::size_t i = 0; i < myGuideChips.size(); ++i) {
        if (myGuideChips[i]) myGuideChips[i]->setCurrent(static_cast<Guides>(i) == guides);
    }
}

void RenderSettingsPanel::setExportSize(ExportSize size)
{
    myExportSize = size;
    for (std::size_t i = 0; i < myExportChips.size(); ++i) {
        if (myExportChips[i]) myExportChips[i]->setCurrent(static_cast<ExportSize>(i) == size);
    }
}

void RenderSettingsPanel::setExportNote(const QString& note)
{
    if (myExportNote) myExportNote->setText(note);
}

void RenderSettingsPanel::setCutout(bool cutout)
{
    myCutout = cutout;
    if (myCutoutChip) myCutoutChip->setCurrent(cutout);
}

void RenderSettingsPanel::setQuality(Quality quality)
{
    myQuality = quality;
    if (mySimpleChip) mySimpleChip->setCurrent(quality == Quality::Simple);
    if (myBalancedChip) myBalancedChip->setCurrent(quality == Quality::Balanced);
    if (myDeepChip) myDeepChip->setCurrent(quality == Quality::Deep);
}

void RenderSettingsPanel::setWood(bool wood)
{
    myWood = wood;
    syncPresetTiles();
}

void RenderSettingsPanel::setWoodTileMm(double mm)
{
    mySyncing = true;
    if (myWoodTileSlider)
        myWoodTileSlider->setValue(static_cast<int>(std::round(std::clamp(mm, 50.0, 1000.0))));
    mySyncing = false;
    syncValueLabels();
}

void RenderSettingsPanel::setWoodAngle(double degrees)
{
    double wrapped = std::fmod(degrees, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    mySyncing = true;
    if (myWoodAngleSlider)
        myWoodAngleSlider->setValue(static_cast<int>(std::round(wrapped)));
    mySyncing = false;
    syncValueLabels();
}

double RenderSettingsPanel::woodTileMm() const
{
    return myWoodTileSlider ? static_cast<double>(myWoodTileSlider->value()) : 300.0;
}

double RenderSettingsPanel::woodAngle() const
{
    return myWoodAngleSlider ? static_cast<double>(myWoodAngleSlider->value()) : 0.0;
}

void RenderSettingsPanel::setWoodSelection(const QString& name)
{
    myWoodName = name.isEmpty() ? QStringLiteral("Wood") : name;
    syncPresetTiles();
}

bool RenderSettingsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        // Asked of the list rather than by casting: MaterialTile is a plain
        // class in this file with no Q_OBJECT of its own (it needs none - it
        // emits nothing), so qobject_cast cannot see it. "Is this one of my
        // tiles" is also the question actually being asked.
        for (MaterialTile* tile : myPresetTiles) {
            if (tile != watched) continue;
            emit materialEditRequested(tile->name());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RenderSettingsPanel::addTile(MaterialTile* tile)
{
    // The double-click that opens this material's own editor. Watched rather
    // than subclassed: QAbstractButton has no doubleClicked() of its own, and
    // a filter on the tiles keeps every tile - preset and file-backed alike -
    // answering the same gesture from one place.
    tile->installEventFilter(this);
    const int index = static_cast<int>(myPresetTiles.size());
    myTileGrid->addWidget(tile, index / kTileColumns, index % kTileColumns);
    myPresetTiles.push_back(tile);
}

void RenderSettingsPanel::addTextureMaterials(
    const std::vector<std::pair<QString, QString>>& namesAndPaths)
{
    for (const auto& entry : namesAndPaths) {
        auto* tile = new MaterialTile(entry.first, entry.second, this);
        connect(tile, &QAbstractButton::clicked, this, [this, tile] {
            if (myWood && myWoodName == tile->name()) return;
            myWoodName = tile->name();
            setWood(true);
            emit woodTextureChosen(tile->name(), tile->filePath());
        });
        addTile(tile);
    }
    syncPresetTiles();
}

void RenderSettingsPanel::setTierStatus(const QString& tierName, double progress01)
{
    // Equality-guarded: a 500 ms ticker drives this, QLabel::setText() does
    // not early-out on equal text, and under the texture-widget repaint law
    // one dirty raster child recomposites EVERY visible overlay - so an
    // unchanged status was repainting the whole tree at 2 Hz for the length
    // of a render session (the branch review's measurement-backed finding).
    if (myTierLabel && myTierLabel->text() != tierName) myTierLabel->setText(tierName);
    if (myProgress) {
        const bool show = progress01 >= 0.0;
        if (myProgress->isVisible() != show) myProgress->setVisible(show);
        if (show) myProgress->setFraction(progress01);
    }
}

void RenderSettingsPanel::setShutterAction(QAction* action)
{
    // Loud, not silent: a null action here means a construction-order
    // regression upstream (the branch review's note), and returning quietly
    // shipped a render mode with no shutter and handed callers a null.
    if (!action) {
        qWarning("RenderSettingsPanel::setShutterAction: null action - the "
                 "shutter will be missing");
        return;
    }
    if (myShutter) return;
    myShutter = new RenderShutterButton(action, this, /*wide=*/true);
    // Straight into the outer layout's tail, after the footer status pair.
    if (auto* outer = qobject_cast<QVBoxLayout*>(layout())) outer->addWidget(myShutter);
}

void RenderSettingsPanel::syncValueLabels()
{
    for (const auto& pair : myValueLabels) {
        QSlider* slider = pair.first;
        QLabel* readout = pair.second;
        QString text;
        if (slider == myLightAngleSlider || slider == myFovSlider ||
            slider == myWoodAngleSlider)
            text = QStringLiteral("%1\u00b0").arg(slider->value());
        else if (slider == myLightStrengthSlider)
            text = QStringLiteral("%1%").arg(slider->value());
        else if (slider == myWoodTileSlider)
            // A LENGTH, so it reads through Measure like every other length
            // in this app - which is also what makes it follow the unit.
            text = QString::fromStdString(
                Measure::formatLength(static_cast<double>(slider->value())));
        else
            text = QString::number(slider->value());
        readout->setText(text);
    }
}

void RenderSettingsPanel::syncPresetTiles()
{
    // Which tile reads as current is derived - from the wood flag for the
    // wood tile, from the two sliders for the rest - so a drag that leaves
    // a preset's exact values un-marks it honestly, and wood outranks the
    // pair while it is on (the sliders still shape its roughness, but the
    // MATERIAL is wood).
    const double gloss = surfaceGlossiness();
    const double metallic = metal();
    for (MaterialTile* tile : myPresetTiles) {
        if (tile->isWood()) {
            tile->setCurrent(myWood && myWoodName == tile->name());
            continue;
        }
        tile->setCurrent(!myWood && std::fabs(tile->glossiness() - gloss) < 0.02 &&
                         std::fabs(tile->metallic() - metallic) < 0.02);
    }
}

QWidget* RenderSettingsPanel::materialNoteRow() const
{
    return myMaterialNote;
}

QAbstractButton* RenderSettingsPanel::shotControlAt(int index) const
{
    return index >= 0 && index < static_cast<int>(myShotApply.size())
               ? myShotApply[static_cast<std::size_t>(index)]
               : nullptr;
}

QAbstractButton* RenderSettingsPanel::shotRemoveAt(int index) const
{
    return index >= 0 && index < static_cast<int>(myShotDrop.size())
               ? myShotDrop[static_cast<std::size_t>(index)]
               : nullptr;
}

QStringList RenderSettingsPanel::materialTileNames() const
{
    QStringList names;
    for (MaterialTile* tile : myPresetTiles) {
        if (tile) names << tile->name();
    }
    return names;
}

void RenderSettingsPanel::setMaterialRowsApply(bool apply)
{
    myMaterialRowsApply = apply;
    // IT NO LONGER MOVES THE NOTE. This used to hide the note on the one tier
    // that does read Surface and Metal, because the note was about those two
    // rows - and those rows are on the material card now, per material, with
    // the tier caveat on each dial's own tooltip where the user actually is
    // when it matters. The panel's note says where they went, which is true
    // on every tier.
    //
    // The flag itself is kept and still answered: MainWindow pushes the
    // gate's own answer here on every appStateChanged, the suite reads it to
    // pin that the gate is one written-down copy rather than a second tier
    // list, and the next control that needs the same question has it.
}

void RenderSettingsPanel::applyTheme()
{
    if (myTitle) {
        myTitle->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                              "font-weight: 600; font-size: %2pt;")
                                   .arg(Theme::textMuted().name())
                                   .arg(Theme::titleFont().pointSizeF()));
    }
    for (QLabel* label : myRowLabels) {
        label->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                            "font-size: %2pt;")
                                 .arg(Theme::text().name())
                                 .arg(Theme::labelFont().pointSizeF()));
    }
    for (QLabel* label : mySectionLabels) {
        label->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                            "font-weight: 600; font-size: %2pt; "
                                            "letter-spacing: 1px;")
                                 .arg(Theme::textMuted().name())
                                 .arg(Theme::badgeFont().pointSizeF()));
    }
    for (const auto& pair : myValueLabels) {
        pair.second->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                                  "font-size: %2pt;")
                                       .arg(Theme::textMuted().name())
                                       .arg(Theme::badgeFont().pointSizeF()));
    }
    if (myTierLabel) {
        myTierLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                                  "font-size: %2pt;")
                                       .arg(Theme::textMuted().name())
                                       .arg(Theme::badgeFont().pointSizeF()));
    }
    // Muted, and at the badge size - the smallest step on Theme's own type
    // scale, which is where a footnote belongs and what gui_smoke's
    // font-size sweep expects to find.
    if (myMaterialNote) {
        myMaterialNote->setStyleSheet(QStringLiteral("background: transparent; color: %1; "
                                                     "font-size: %2pt;")
                                          .arg(Theme::textMuted().name())
                                          .arg(Theme::badgeFont().pointSizeF()));
    }
    update();
}

void RenderSettingsPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    Theme::paintSurface(painter, rect(), kRadius);
}

void RenderSettingsPanel::wheelEvent(QWheelEvent* event)
{
    // Accepted whether or not a slider under the cursor actually moved -
    // AppearancePanel's own reasoning: an ignored wheel here reaches the
    // viewport underneath and zooms the camera.
    event->accept();
}

// --- RenderShutterButton --------------------------------------------------

RenderShutterButton::RenderShutterButton(QAction* action, QWidget* parent, bool wide)
    : QAbstractButton(parent)
    , myAction(action)
    , myWide(wide)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // This card's own law, restated: a floating control over the viewport
    // must take its own presses and releases rather than letting either one
    // reach the viewport and trigger the render-mode exit gesture.
    setAttribute(Qt::WA_NoMousePropagation);
    // See Theme::makeSurfaceTransparent()'s own comment. QAbstractButton is a
    // QWidget subclass, so the app-wide QSS rule matches it exactly as it
    // matches any other member of this family.
    Theme::makeSurfaceTransparent(this);
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    if (myWide) {
        setFixedHeight(kShutterWideHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    } else {
        setFixedSize(kShutterSide, kShutterSide);
    }

    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this,
            &RenderShutterButton::applyTheme);

    if (myAction) {
        // Straight to the action, holding no state of its own - the
        // action-driven law every chip in this shell follows. Save
        // Screenshot is not checkable, so there is no checked state to
        // mirror, only enabled/tooltip.
        connect(this, &QAbstractButton::clicked, myAction, &QAction::trigger);
        connect(myAction, &QAction::changed, this, &RenderShutterButton::syncFromAction);
        syncFromAction();
    }
}

void RenderShutterButton::syncFromAction()
{
    setEnabled(myAction->isEnabled());
    QString tip = myAction->toolTip();
    if (tip.isEmpty()) tip = myAction->text().remove(QLatin1Char('&'));
    setToolTip(tip);
    update();
}

void RenderShutterButton::applyTheme()
{
    update();
}

QSize RenderShutterButton::sizeHint() const
{
    return myWide ? QSize(160, kShutterWideHeight) : QSize(kShutterSide, kShutterSide);
}

void RenderShutterButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF body(rect());

    if (myWide) {
        // The footer bar: accent-filled, camera glyph beside the action's
        // own words - the one control on the panel loud on purpose, being
        // the panel's whole verb.
        QColor fill = Theme::accent();
        if (!isEnabled())   fill = Theme::chip();
        else if (isDown())  fill = Theme::accent().darker(125);
        else if (myHovered) fill = Theme::accent().lighter(112);
        QPainterPath path;
        path.addRoundedRect(body, kShutterWideRadius, kShutterWideRadius);
        painter.fillPath(path, fill);
        Theme::drawCrispBorder(painter, body, Theme::border(), kShutterWideRadius);

        const QString label =
            myAction ? QString(myAction->text()).remove(QLatin1Char('&'))
                        .remove(QStringLiteral("..."))
                     : QString();
        painter.setFont(Theme::labelFont());
        const QFontMetrics fm(Theme::labelFont());
        const int textW = fm.horizontalAdvance(label);
        const int glyphAndGap = kShutterWideGlyph + 8;
        const int startX = std::max(10, (width() - textW - glyphAndGap) / 2);
        const QRect iconRect(startX,
                             (height() - kShutterWideGlyph) / 2,
                             kShutterWideGlyph, kShutterWideGlyph);
        IconSet::icon(IconSet::Glyph::Camera)
            .paint(&painter, iconRect, Qt::AlignCenter,
                  isEnabled() ? QIcon::Normal : QIcon::Disabled);
        painter.setPen(isEnabled() ? QColor(Qt::white) : Theme::textDisabled());
        painter.drawText(QRect(startX + glyphAndGap, 0, width() - startX - glyphAndGap,
                               height()),
                         Qt::AlignVCenter | Qt::AlignLeft, label);

        return;
    }

    const double radius = body.width() / 2.0;

    // No ground fill outside the circle any more - Theme::paintSurface()'s
    // own header explains why: the four corners genuinely composite through
    // to the live scene behind this button now, the same as every other
    // family member, rather than reading a flat viewport()-grey square.
    // Theme::makeSurfaceTransparent(this) in the constructor is what keeps
    // the app-wide QSS rule from painting one there first.
    QColor background = Theme::panel();
    if (!isEnabled())   background = Theme::panel().darker(115);
    else if (isDown())  background = Theme::chipActive();
    else if (myHovered) background = Theme::chipHover();

    QPainterPath disc;
    disc.addEllipse(body);
    painter.fillPath(disc, background);

    // An unconditional accent ring - this button is never checkable, so
    // "checked" is not a state that exists to gate it on, unlike ToolChip's
    // own inset ring.
    Theme::drawCrispBorder(painter, body, Theme::border(), radius, 1.0);
    Theme::drawCrispBorder(painter, body.adjusted(2.5, 2.5, -2.5, -2.5), Theme::accent(),
                           radius - 2.5, 2.0);

    const QRect iconRect(static_cast<int>(body.center().x() - kShutterGlyph / 2.0),
                         static_cast<int>(body.center().y() - kShutterGlyph / 2.0),
                         kShutterGlyph, kShutterGlyph);
    IconSet::icon(IconSet::Glyph::Camera)
        .paint(&painter, iconRect, Qt::AlignCenter,
              isEnabled() ? QIcon::Normal : QIcon::Disabled);

}

void RenderShutterButton::enterEvent(QEnterEvent*) { myHovered = true;  update(); }
void RenderShutterButton::leaveEvent(QEvent*)       { myHovered = false; update(); }

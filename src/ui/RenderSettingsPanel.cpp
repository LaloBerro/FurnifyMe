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
constexpr int kWidth = 260;
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
            if (this == window()->focusWidget()) {
                const bool active = window()->isActiveWindow();
                Theme::drawCrispBorder(painter, body.adjusted(3, 3, -3, -3),
                                       active ? Theme::focusRing() : Theme::focusRingMuted(),
                                       kTileRadius - 3, active ? 2.0 : 1.5);
            }
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
            if (this == window()->focusWidget()) {
                const bool active = window()->isActiveWindow();
                Theme::drawCrispBorder(painter, body.adjusted(3, 3, -3, -3),
                                       active ? Theme::focusRing() : Theme::focusRingMuted(),
                                       kTileRadius - 3, active ? 2.0 : 1.5);
            }
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
        if (this == window()->focusWidget()) {
            const bool active = window()->isActiveWindow();
            Theme::drawCrispBorder(painter, body.adjusted(3, 3, -3, -3),
                                   active ? Theme::focusRing() : Theme::focusRingMuted(),
                                   kTileRadius - 3, active ? 2.0 : 1.5);
        }
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
        if (this == window()->focusWidget()) {
            const bool active = window()->isActiveWindow();
            Theme::drawCrispBorder(painter, body.adjusted(2.5, 2.5, -2.5, -2.5),
                                   active ? Theme::focusRing() : Theme::focusRingMuted(),
                                   4.5, active ? 2.0 : 1.5);
        }
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
        // Each gloss/metal tile IS its two slider values - clicking writes
        // them through the same setters a drag uses, so the signals, the
        // persistence and the tier gate all come along for free. Which tile
        // reads as current is DERIVED in syncPresetTiles(), never stored.
        const struct { const char* name; double gloss; double metal; } presets[] = {
            {"Matte", 0.25, 0.0}, {"Satin", 0.65, 0.05}, {"Metal", 0.80, 1.0}};
        for (const auto& preset : presets) {
            auto* tile = new MaterialTile(tr(preset.name), preset.gloss, preset.metal, grid);
            connect(tile, &QAbstractButton::clicked, this, [this, tile] {
                // A gloss/metal pick takes wood off in the same gesture -
                // one material at a time, said once here.
                if (myWood) {
                    setWood(false);
                    emit woodChanged(false);
                }
                setSurfaceGlossiness(tile->glossiness());
                setMetal(tile->metallic());
            });
            addTile(tile);
        }
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
    mySurfaceSlider = addRow(QStringLiteral("surface"), tr("Surface"), 0, 100, 45);
    connect(mySurfaceSlider, &QSlider::valueChanged, this, [this](int v) {
        if (mySyncing) return;
        emit surfaceGlossinessChanged(v / 100.0);
    });
    myMetalSlider = addRow(QStringLiteral("metal"), tr("Metal"), 0, 100, 0);
    connect(myMetalSlider, &QSlider::valueChanged, this, [this](int v) {
        if (mySyncing) return;
        emit metalChanged(v / 100.0);
    });
    connect(mySurfaceSlider, &QSlider::valueChanged, this,
            &RenderSettingsPanel::syncPresetTiles);
    connect(myMetalSlider, &QSlider::valueChanged, this,
            &RenderSettingsPanel::syncPresetTiles);

    // The material's own two dials (user feedback on the wood): how much
    // real material one image tile covers, and which way the grain runs.
    // They shape the TEXTURED materials - the gloss/metal presets ignore
    // them - but they stay visible either way: two rows that appear and
    // vanish with the active tile would bounce the whole section.
    // DEBOUNCED, unlike every other slider here: these two rebuild every
    // body's textured overlay mesh - and on the ray-traced tiers pay the
    // rasterize-and-back round trip plus a path-tracing accumulation
    // restart - per value, and a QSlider fires per mouse-move pixel, so a
    // raw connect made one drag across the range hundreds of full scene
    // rebuilds (the branch review's finding). A short trailing timer keeps
    // the preview live at ~8 updates a second; the release flushes
    // immediately so the final value never waits.
    auto debouncedDial = [this](QSlider* slider, auto emitValue) {
        auto* debounce = new QTimer(this);
        debounce->setSingleShot(true);
        debounce->setInterval(120);
        connect(debounce, &QTimer::timeout, this,
                [slider, emitValue] { emitValue(slider->value()); });
        connect(slider, &QSlider::valueChanged, this, [this, debounce](int) {
            if (mySyncing) return;
            debounce->start();
        });
        connect(slider, &QSlider::sliderReleased, this, [this, slider, debounce, emitValue] {
            if (mySyncing) return;
            debounce->stop();
            emitValue(slider->value());
        });
    };
    myWoodTileSlider = addRow(QStringLiteral("woodTile"), tr("Grain size"), 50, 1000, 300);
    debouncedDial(myWoodTileSlider,
                  [this](int v) { emit woodTileChanged(static_cast<double>(v)); });
    myWoodAngleSlider = addRow(QStringLiteral("woodAngle"), tr("Grain angle"), 0, 359, 0);
    debouncedDial(myWoodAngleSlider,
                  [this](int v) { emit woodAngleChanged(static_cast<double>(v)); });

    // The muted note under those two rows - shown only while the active
    // tier does not read them (see setMaterialRowsApply()). Word-wrapped
    // rather than elided: the card's width is fixed, and a truncated
    // explanation explains nothing. Built here, hidden, so nothing about
    // its existence depends on which tier the session happens to probe
    // into.
    myMaterialNote = new QLabel(
        tr("Surface and Metal apply in the deepest render tier"), this);
    myMaterialNote->setObjectName(QStringLiteral("renderSettingsMaterialNote"));
    myMaterialNote->setWordWrap(true);
    myMaterialNote->hide();
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
        myDeepChip = new SegChip(tr("Deep"), row);
        myDeepChip->setToolTip(tr("The richest picture this machine reaches — slower, "
                                  "and it keeps polishing while you watch"));
        mySimpleChip = new SegChip(tr("Simple"), row);
        mySimpleChip->setToolTip(tr("Instant frames with real shadows — for framing a "
                                    "shot or a quicker machine"));
        connect(myDeepChip, &QAbstractButton::clicked, this, [this] {
            if (!myQuick) return;
            setQuick(false);
            emit quickChanged(false);
        });
        connect(mySimpleChip, &QAbstractButton::clicked, this, [this] {
            if (myQuick) return;
            setQuick(true);
            emit quickChanged(true);
        });
        line->addWidget(myDeepChip);
        line->addWidget(mySimpleChip);
        line->addStretch(1);
        outer->addWidget(row);
    }
    setQuick(false);

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

void RenderSettingsPanel::setQuick(bool quick)
{
    myQuick = quick;
    if (myDeepChip) myDeepChip->setCurrent(!quick);
    if (mySimpleChip) mySimpleChip->setCurrent(quick);
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

void RenderSettingsPanel::addTile(MaterialTile* tile)
{
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

void RenderSettingsPanel::setMaterialRowsApply(bool apply)
{
    myMaterialRowsApply = apply;
    // Derived, never a one-shot: this is called on every appStateChanged,
    // so the note's visibility follows the tier rather than remembering
    // whatever it was told once.
    if (myMaterialNote) myMaterialNote->setVisible(!apply);
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

        if (this == window()->focusWidget()) {
            const bool active = window()->isActiveWindow();
            Theme::drawCrispBorder(painter, body.adjusted(3, 3, -3, -3),
                                   active ? Theme::focusRing() : Theme::focusRingMuted(),
                                   kShutterWideRadius - 3, active ? 2.0 : 1.5);
        }
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

    if (this == window()->focusWidget()) {
        const bool active = window()->isActiveWindow();
        Theme::drawCrispBorder(painter, body.adjusted(6, 6, -6, -6),
                               active ? Theme::focusRing() : Theme::focusRingMuted(),
                               radius - 6, active ? 2.0 : 1.5);
    }
}

void RenderShutterButton::enterEvent(QEnterEvent*) { myHovered = true;  update(); }
void RenderShutterButton::leaveEvent(QEvent*)       { myHovered = false; update(); }

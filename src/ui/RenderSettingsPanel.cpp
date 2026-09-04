#include "RenderSettingsPanel.h"

#include "IconSet.h"
#include "Theme.h"

#include <QAction>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
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

// --- RenderSettingsPanel --------------------------------------------------

RenderSettingsPanel::RenderSettingsPanel(QWidget* parent)
    : QWidget(parent)
{
    // The floating-surface family's two mouse rules - see ItemsPanel.h/
    // AppearancePanel.h for the full reasoning: this card paints its own
    // whole rect opaquely (paintEvent()), and it must swallow every press
    // and release rather than let one fall through to the viewport
    // underneath, where it would either re-pick the model or - render
    // mode's own hazard - trigger the exit gesture a plain viewport press
    // performs. Pinned by gui_smoke: a click anywhere on this card must
    // never exit render mode.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    setFixedWidth(Theme::wholeDevicePixels(kWidth));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kPad, kPad, kPad, kPad);
    outer->setSpacing(kRowSpacing);

    myTitle = new QLabel(tr("Render settings"), this);
    outer->addWidget(myTitle);

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

        outer->addWidget(row);
        return slider;
    };

    // --- section 1: Surface, Metal ------------------------------------
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

    addRule();

    // --- section 2: Light angle, Light strength -------------------------
    myLightAngleSlider = addRow(QStringLiteral("lightAngle"), tr("Light angle"), 0, 359, 0);
    connect(myLightAngleSlider, &QSlider::valueChanged, this, [this](int v) {
        if (mySyncing) return;
        emit lightAngleChanged(static_cast<double>(v));
    });
    myLightStrengthSlider = addRow(QStringLiteral("lightStrength"), tr("Light strength"),
                                   kLightStrengthMin, kLightStrengthMax, 200);
    connect(myLightStrengthSlider, &QSlider::valueChanged, this, [this](int v) {
        if (mySyncing) return;
        emit lightStrengthChanged(v / 100.0);
    });

    addRule();

    // --- section 3: Background, Camera FOV -------------------------------
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

    myFovSlider = addRow(QStringLiteral("fov"), tr("Camera FOV"), 20, 120, 45);
    connect(myFovSlider, &QSlider::valueChanged, this, [this](int v) {
        if (mySyncing) return;
        emit fovChanged(static_cast<double>(v));
    });

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
    for (QLabel* label : myRowLabels) texts << label->text();
    return texts;
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

RenderShutterButton::RenderShutterButton(QAction* action, QWidget* parent)
    : QAbstractButton(parent)
    , myAction(action)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // This card's own law, restated: a floating control over the viewport
    // must take its own presses and releases rather than letting either one
    // reach the viewport and trigger the render-mode exit gesture.
    setAttribute(Qt::WA_NoMousePropagation);
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(kShutterSide, kShutterSide);

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

QSize RenderShutterButton::sizeHint() const { return QSize(kShutterSide, kShutterSide); }

void RenderShutterButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF body(rect());
    const double radius = body.width() / 2.0;

    // The floating-surface family's ground fill FIRST, across the whole
    // widget rect - Theme::paintSurface()'s own reasoning: the four corners
    // outside the circle have to read as viewport-grey, not whatever the
    // backing store held, over the GL surface underneath.
    painter.fillRect(rect(), Theme::viewport());

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

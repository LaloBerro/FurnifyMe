#include "MaterialCard.h"

#include "Theme.h"

#include <QColorDialog>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kCardWidth = 260;
constexpr int kPad = 14;
constexpr int kRowHeight = 26;
constexpr int kGap = 10;
// The four dials that moved here sit closer together than the colour row and
// the button do - they are one group, read down rather than across.
constexpr int kDialGap = 4;
constexpr int kCardRadius = 12;
constexpr int kEdgeMargin = 16;
constexpr int kSwatchWidth = 56;
constexpr int kDoneWidth = 58;
// Wide enough for the longest readout this card can produce, which is the
// grain size with its unit ("1000 mm") - measured against the label font
// rather than guessed, and the one readout that carries a unit at all. At 46
// it was legible but sat hard against the slider's right end.
constexpr int kValueWidth = 58;
constexpr int kNameWidth = 74;
// The brightness band, as the slider's own whole numbers: 25 % to 200 %, one
// step per percent, 100 being the material as it comes.
constexpr int kBrightMin = 25;
constexpr int kBrightMax = 200;
// The four that moved off the render panel keep EXACTLY the ranges they had
// there, so a value that was reachable before still is and a furniture saved
// with one renders identically.
constexpr int kGrainSizeMin = 50;
constexpr int kGrainSizeMax = 1000;

QFont titleFontBold()
{
    QFont f = Theme::bodyFont();
    f.setBold(true);
    return f;
}
}  // namespace

MaterialCard::MaterialCard(QWidget* viewport)
    : QWidget(viewport)
{
    setAttribute(Qt::WA_NoSystemBackground);
    // Every click on this card is its own - none of them reaches the model
    // behind it, which would otherwise re-pick on the release.
    setAttribute(Qt::WA_NoMousePropagation);
    Theme::makeSurfaceTransparent(this);
    setFixedWidth(kCardWidth);

    auto* swatch = new QPushButton(this);
    swatch->setCursor(Qt::PointingHandCursor);
    swatch->setFocusPolicy(Qt::NoFocus);
    swatch->setToolTip(tr("Pick this material's colour"));
    connect(swatch, &QPushButton::clicked, this, [this] { pickColour(); });
    mySwatch = swatch;

    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(kBrightMin, kBrightMax);
    slider->setValue(100);
    slider->setFocusPolicy(Qt::NoFocus);
    slider->setToolTip(tr("How bright this material is — 100% is the material as it comes"));
    connect(slider, &QSlider::valueChanged, this, [this](int value) {
        if (mySeeding) return;
        myLook.brightness = value / 100.0;
        if (myValue) myValue->setText(QStringLiteral("%1%").arg(value));
        emitChange();
    });
    mySlider = slider;

    auto* value = new QLabel(QStringLiteral("100%"), this);
    value->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    myValue = value;

    // The four dials, built by one helper so a row cannot drift from its
    // neighbours in font, alignment or focus policy. Each reports through the
    // one `changed` signal; none of them owns a value of its own.
    const auto addDial = [this](Dial& dial, const QString& label, const QString& tip, int lo,
                                int hi, int start, auto&& write) {
        auto* name = new QLabel(label, this);
        name->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        dial.name = name;
        auto* bar = new QSlider(Qt::Horizontal, this);
        bar->setRange(lo, hi);
        bar->setValue(start);
        bar->setFocusPolicy(Qt::NoFocus);
        bar->setToolTip(tip);
        dial.slider = bar;
        auto* readout = new QLabel(this);
        readout->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
        dial.value = readout;
        connect(bar, &QSlider::valueChanged, this, [this, write](int v) {
            if (mySeeding) return;
            write(v);
            syncReadouts();
            emitChange();
        });
    };

    addDial(mySurface, tr("Surface"),
            tr("Matte on the left, glossy on the right — read in the deepest render tier"),
            0, 100, 45, [this](int v) { myLook.surface = v / 100.0; });
    addDial(myMetal, tr("Metal"),
            tr("How much this material behaves like bare metal — read in the deepest "
               "render tier"),
            0, 100, 0, [this](int v) { myLook.metal = v / 100.0; });
    addDial(myGrainSize, tr("Grain size"),
            tr("How much real wood one tile of the grain image covers"),
            kGrainSizeMin, kGrainSizeMax, 300,
            [this](int v) { myLook.grainSize = static_cast<double>(v); });
    addDial(myGrainAngle, tr("Grain angle"), tr("Which way the grain runs"), 0, 359, 0,
            [this](int v) { myLook.grainAngle = static_cast<double>(v); });

    auto* done = new QPushButton(tr("Done"), this);
    done->setCursor(Qt::PointingHandCursor);
    done->setFocusPolicy(Qt::NoFocus);
    connect(done, &QPushButton::clicked, this, [this] { close(); });
    myDone = done;

    applyStyles();
    syncReadouts();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyStyles();
        update();
    });
    if (viewport) viewport->installEventFilter(this);
    hide();
}

void MaterialCard::open(const QString& material, const Look& look)
{
    myMaterial = material;
    myLook = look;
    if (!myLook.colour.isValid()) myLook.colour = QColor(178, 178, 173);
    myLook.brightness = std::clamp(myLook.brightness, kBrightMin / 100.0, kBrightMax / 100.0);
    myLook.surface = std::clamp(myLook.surface, 0.0, 1.0);
    myLook.metal = std::clamp(myLook.metal, 0.0, 1.0);
    myLook.grainSize = std::clamp(myLook.grainSize, static_cast<double>(kGrainSizeMin),
                                  static_cast<double>(kGrainSizeMax));

    // ONE guard for the whole seeding pass rather than blockSignals() per
    // slider. Seeding is not an edit, and reporting it would write the value
    // straight back to the document it just came from; with six controls to
    // seed, a flag read at the top of every handler is the spelling that
    // cannot be forgotten on the seventh.
    mySeeding = true;
    if (mySlider) mySlider->setValue(static_cast<int>(std::lround(myLook.brightness * 100.0)));
    if (mySurface.slider)
        mySurface.slider->setValue(static_cast<int>(std::lround(myLook.surface * 100.0)));
    if (myMetal.slider)
        myMetal.slider->setValue(static_cast<int>(std::lround(myLook.metal * 100.0)));
    if (myGrainSize.slider)
        myGrainSize.slider->setValue(static_cast<int>(std::lround(myLook.grainSize)));
    if (myGrainAngle.slider)
        myGrainAngle.slider->setValue(static_cast<int>(std::lround(myLook.grainAngle)) % 360);
    mySeeding = false;

    syncReadouts();
    applyStyles();
    replace();
    show();
    raise();
    update();
}

void MaterialCard::setLook(const Look& look)
{
    const QString material = myMaterial;
    // open() is the clamp-and-seed pass; reusing it keeps one copy of both.
    // It does not report, so the emit below is what makes this an edit.
    open(material, look);
    emitChange();
}

void MaterialCard::close()
{
    if (isHidden()) return;
    hide();
    emit closed();
}

void MaterialCard::syncReadouts()
{
    if (myValue) {
        myValue->setText(
            QStringLiteral("%1%").arg(static_cast<int>(std::lround(myLook.brightness * 100.0))));
    }
    if (mySurface.value)
        mySurface.value->setText(QString::number(static_cast<int>(std::lround(myLook.surface * 100.0))));
    if (myMetal.value)
        myMetal.value->setText(QString::number(static_cast<int>(std::lround(myLook.metal * 100.0))));
    // The one readout with a unit on it: a grain size is a real length of
    // wood, and a bare number would be the only figure on this card whose
    // meaning has to be guessed.
    if (myGrainSize.value) {
        myGrainSize.value->setText(
            QStringLiteral("%1 mm").arg(static_cast<int>(std::lround(myLook.grainSize))));
    }
    if (myGrainAngle.value) {
        myGrainAngle.value->setText(
            QStringLiteral("%1°").arg(static_cast<int>(std::lround(myLook.grainAngle))));
    }
}

void MaterialCard::applyStyles()
{
    if (mySwatch) {
        mySwatch->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; "
                                               "border: 1px solid %2; border-radius: 4px; }")
                                    .arg(myLook.colour.name(), Theme::border().name()));
    }
    const QString mutedLabel = QStringLiteral("background: transparent; color: %1;")
                                   .arg(Theme::textMuted().name());
    const QString plainLabel =
        QStringLiteral("background: transparent; color: %1;").arg(Theme::text().name());
    if (myValue) {
        myValue->setFont(Theme::labelFont());
        myValue->setStyleSheet(mutedLabel);
    }
    for (Dial* dial : {&mySurface, &myMetal, &myGrainSize, &myGrainAngle}) {
        if (dial->name) {
            dial->name->setFont(Theme::labelFont());
            dial->name->setStyleSheet(plainLabel);
        }
        if (dial->value) {
            dial->value->setFont(Theme::labelFont());
            dial->value->setStyleSheet(mutedLabel);
        }
    }
    if (myDone) {
        myDone->setFont(Theme::labelFont());
        myDone->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                           "border-radius: 4px; padding: 0px 8px; } "
                           "QPushButton:hover { background-color: %4; }")
                .arg(Theme::chip().name(), Theme::text().name(), Theme::border().name(),
                     Theme::chipHover().name()));
    }
}

void MaterialCard::pickColour()
{
    // show(), never open() or exec(): QDialog::open() forces window-modality
    // regardless of setModal(false), and nothing in this app blocks - the
    // same ruling the Appearance panel's own picker follows.
    auto* picker = new QColorDialog(myLook.colour, this);
    picker->setAttribute(Qt::WA_DeleteOnClose);
    picker->setOption(QColorDialog::DontUseNativeDialog, true);
    picker->setModal(false);
    connect(picker, &QColorDialog::currentColorChanged, this, [this](const QColor& colour) {
        if (!colour.isValid()) return;
        myLook.colour = colour;
        applyStyles();
        emitChange();
    });
    picker->show();
}

void MaterialCard::emitChange()
{
    if (myMaterial.isEmpty()) return;
    emit changed(myMaterial, myLook);
}

void MaterialCard::replace()
{
    if (!parentWidget()) return;
    const QFontMetrics title(titleFontBold());
    // Title, the colour row, four dial rows, then Done.
    const int height = kPad * 2 + title.height() + kGap + kRowHeight + kGap +
                       4 * (kRowHeight + kDialGap) + kGap + kRowHeight;
    setFixedSize(Theme::wholeDevicePixels(QSize(kCardWidth, height)));

    // Bottom LEFT of the viewport: the render settings are a docked panel down
    // the right edge, and the middle is the furniture this card exists to
    // watch.
    const QPoint origin = mapTo(window(), QPoint(0, 0));
    const double dpr = devicePixelRatioF();
    const int x = Theme::snapToDevicePixels(kEdgeMargin, origin.x(), dpr);
    const int y = Theme::snapToDevicePixels(
        std::max(0, parentWidget()->height() - this->height() - kEdgeMargin), origin.y(), dpr);
    move(x, y);
    layoutCard();
    if (!isHidden()) raise();
}

void MaterialCard::layoutCard()
{
    const QFontMetrics title(titleFontBold());
    int top = kPad + title.height() + kGap;

    if (mySwatch) mySwatch->setGeometry(kPad, top, kSwatchWidth, kRowHeight);
    if (mySlider) {
        const int left = kPad + kSwatchWidth + kGap;
        mySlider->setGeometry(left, top,
                              std::max(40, width() - left - kPad - kValueWidth), kRowHeight);
    }
    if (myValue) myValue->setGeometry(width() - kPad - kValueWidth, top, kValueWidth, kRowHeight);

    top += kRowHeight + kGap;
    for (Dial* dial : {&mySurface, &myMetal, &myGrainSize, &myGrainAngle}) {
        if (dial->name) dial->name->setGeometry(kPad, top, kNameWidth, kRowHeight);
        if (dial->slider) {
            const int left = kPad + kNameWidth;
            dial->slider->setGeometry(left, top,
                                      std::max(30, width() - left - kPad - kValueWidth),
                                      kRowHeight);
        }
        if (dial->value)
            dial->value->setGeometry(width() - kPad - kValueWidth, top, kValueWidth, kRowHeight);
        top += kRowHeight + kDialGap;
    }

    if (myDone) {
        myDone->setGeometry(width() - kPad - kDoneWidth, top + kGap - kDialGap, kDoneWidth,
                            kRowHeight);
    }
}

QStringList MaterialCard::paintedTexts() const
{
    // This app's own copy. The material's NAME is the app's too (its tiles
    // name it), so it rides along. Every dial's label and tooltip is listed
    // here for the same reason the card's own two strings always were -
    // painted copy the banned-word sweep would otherwise never reach.
    QStringList texts{tr("Done"), tr("Pick this material's colour"),
                      tr("How bright this material is — 100% is the material as it comes"),
                      myMaterial};
    for (const Dial* dial : {&mySurface, &myMetal, &myGrainSize, &myGrainAngle}) {
        if (dial->name) texts << dial->name->text();
        if (dial->slider) texts << dial->slider->toolTip();
    }
    return texts;
}

void MaterialCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    Theme::paintSurface(painter, rect(), kCardRadius);

    painter.setFont(titleFontBold());
    painter.setPen(Theme::text());
    const QFontMetrics title(titleFontBold());
    painter.drawText(QRect(kPad, kPad, width() - kPad * 2, title.height()),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     title.elidedText(myMaterial, Qt::ElideRight, width() - kPad * 2));
}

void MaterialCard::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!myClaimInstalled) {
        QCoreApplication::instance()->installEventFilter(this);
        myClaimInstalled = true;
    }
}

void MaterialCard::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (myClaimInstalled) {
        QCoreApplication::instance()->removeEventFilter(this);
        myClaimInstalled = false;
    }
}

bool MaterialCard::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && !isHidden()) {
        replace();
        return QWidget::eventFilter(watched, event);
    }
    // ESCAPE ONLY, and only in this card's own window. Unlike the questions
    // this app asks, this card claims nothing else: the viewport behind it
    // stays fully live - orbiting while the slider moves is exactly how a
    // material gets judged.
    if (!isHidden() && event->type() == QEvent::KeyPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget->window() == window()) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
                close();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

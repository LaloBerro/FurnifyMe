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
constexpr int kCardRadius = 12;
constexpr int kEdgeMargin = 16;
constexpr int kSwatchWidth = 56;
constexpr int kDoneWidth = 58;
// The brightness band, as the slider's own whole numbers: 25 % to 200 %, one
// step per percent, 100 being the material as it comes.
constexpr int kBrightMin = 25;
constexpr int kBrightMax = 200;

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
        myBrightness = value / 100.0;
        if (myValue) myValue->setText(QStringLiteral("%1%").arg(value));
        emitChange();
    });
    mySlider = slider;

    auto* value = new QLabel(QStringLiteral("100%"), this);
    value->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    myValue = value;

    auto* done = new QPushButton(tr("Done"), this);
    done->setCursor(Qt::PointingHandCursor);
    done->setFocusPolicy(Qt::NoFocus);
    connect(done, &QPushButton::clicked, this, [this] { close(); });
    myDone = done;

    applyStyles();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        applyStyles();
        update();
    });
    if (viewport) viewport->installEventFilter(this);
    hide();
}

void MaterialCard::open(const QString& material, const QColor& colour, double brightness)
{
    myMaterial = material;
    myColour = colour.isValid() ? colour : QColor(178, 178, 173);
    myBrightness = std::clamp(brightness, kBrightMin / 100.0, kBrightMax / 100.0);
    if (mySlider) {
        // Blocked: seeding is not an edit, and reporting it would write the
        // value back to the document it just came from.
        mySlider->blockSignals(true);
        mySlider->setValue(static_cast<int>(std::lround(myBrightness * 100.0)));
        mySlider->blockSignals(false);
    }
    if (myValue) myValue->setText(QStringLiteral("%1%").arg(mySlider ? mySlider->value() : 100));
    applyStyles();
    replace();
    show();
    raise();
    update();
}

void MaterialCard::close()
{
    if (isHidden()) return;
    hide();
    emit closed();
}

void MaterialCard::applyStyles()
{
    if (mySwatch) {
        mySwatch->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; "
                                               "border: 1px solid %2; border-radius: 4px; }")
                                    .arg(myColour.name(), Theme::border().name()));
    }
    if (myValue) {
        myValue->setFont(Theme::labelFont());
        myValue->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                                   .arg(Theme::textMuted().name()));
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
    auto* picker = new QColorDialog(myColour, this);
    picker->setAttribute(Qt::WA_DeleteOnClose);
    picker->setOption(QColorDialog::DontUseNativeDialog, true);
    picker->setModal(false);
    connect(picker, &QColorDialog::currentColorChanged, this, [this](const QColor& colour) {
        if (!colour.isValid()) return;
        myColour = colour;
        applyStyles();
        emitChange();
    });
    picker->show();
}

void MaterialCard::emitChange()
{
    if (myMaterial.isEmpty()) return;
    emit changed(myMaterial, myColour, myBrightness);
}

void MaterialCard::replace()
{
    if (!parentWidget()) return;
    const QFontMetrics title(titleFontBold());
    const int height = kPad * 2 + title.height() + kGap + kRowHeight + kGap + kRowHeight;
    setFixedSize(Theme::wholeDevicePixels(QSize(kCardWidth, height)));

    // Bottom LEFT of the viewport: the render settings card owns the right
    // edge, and the middle is the furniture this card exists to watch.
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
    const int top = kPad + title.height() + kGap;
    if (mySwatch) mySwatch->setGeometry(kPad, top, kSwatchWidth, kRowHeight);
    if (mySlider) {
        const int left = kPad + kSwatchWidth + kGap;
        mySlider->setGeometry(left, top, std::max(40, width() - left - kPad - 46), kRowHeight);
    }
    if (myValue) {
        myValue->setGeometry(width() - kPad - 42, top, 42, kRowHeight);
    }
    if (myDone) {
        myDone->setGeometry(width() - kPad - kDoneWidth, top + kRowHeight + kGap, kDoneWidth,
                            kRowHeight);
    }
}

QStringList MaterialCard::paintedTexts() const
{
    // This app's own copy. The material's NAME is the app's too (its tiles
    // name it), so it rides along.
    return {tr("Done"), tr("Pick this material's colour"),
            tr("How bright this material is — 100% is the material as it comes"), myMaterial};
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

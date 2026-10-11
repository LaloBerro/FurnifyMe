#include "ThemeDecor.h"

#include "Theme.h"
#include "Themes.h"
#include "ViewportOverlay.h"

#include <QEvent>
#include <QFont>
#include <QPainter>

ThemeDecor::ThemeDecor(QWidget* viewport, ViewportOverlay* overlay, std::function<QRect()> anchor,
                       std::function<QString()> readout)
    : QWidget(viewport)
    , myOverlay(overlay)
    , myAnchor(std::move(anchor))
    , myReadoutSource(std::move(readout))
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    Theme::makeSurfaceTransparent(this);
    // Over every card it marks: raised after each layout pass, which raises
    // the cards as it places them.
    if (myOverlay)
        connect(myOverlay, &ViewportOverlay::laidOut, this, [this] {
            if (parentWidget()) setGeometry(parentWidget()->rect());
            raise();
            refresh();
            update();
        });
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &ThemeDecor::refresh);
    if (viewport) setGeometry(viewport->rect());
    refresh();
}

void ThemeDecor::refresh()
{
    const Themes::Decor& d = Themes::shell().decor;
    setVisible(d.on);
    if (!d.on) return;
    const QString next = d.readout && myReadoutSource ? myReadoutSource() : QString();
    if (next != myReadout) {
        myReadout = next;
        update();
    }
}

void ThemeDecor::paintEvent(QPaintEvent*)
{
    const Themes::Decor& d = Themes::shell().decor;
    if (!d.on) return;
    QPainter p(this);
    if (d.marks && myOverlay) {
        // A crosshair 9 CSS px across, centred on each corner, in the theme's
        // brightest rule - the mockup's .reg, two 1 px bars.
        p.setPen(Qt::NoPen);
        p.setBrush(d.markInk.isValid() ? d.markInk : Theme::textMuted());
        const int half = d.markSize / 2;
        for (const QRect& r : myOverlay->occupiedRects()) {
            if (r.isEmpty()) continue;
            for (const QPoint c : {r.topLeft(), QPoint(r.right() + 1, r.top()), QPoint(r.left(), r.bottom() + 1),
                                   QPoint(r.right() + 1, r.bottom() + 1)}) {
                p.drawRect(QRect(c.x() - half, c.y(), d.markSize, 1));
                p.drawRect(QRect(c.x(), c.y() - half, 1, d.markSize));
            }
        }
    }
    if (d.readout && !myReadout.isEmpty() && myAnchor) {
        const QRect a = myAnchor();
        if (!a.isNull()) {
            QFont mono(d.monoFamily.isEmpty() ? QStringLiteral("monospace") : d.monoFamily);
            mono.setPixelSize(int(d.textPx));
            mono.setWeight(QFont::Medium);
            mono.setLetterSpacing(QFont::AbsoluteSpacing, d.textPx * 0.06);
            p.setFont(mono);
            p.setPen(Theme::textMuted());
            // Its parts, tab-separated, 16 px apart - the mockup's flex gap.
            const QFontMetrics fm(mono);
            int x = a.right() + 1 + 16;
            for (const QString& part : myReadout.split(QLatin1Char('\t'))) {
                p.drawText(QPoint(x, a.top() + fm.ascent()), part);
                x += fm.horizontalAdvance(part) + 16;
            }
        }
    }
}

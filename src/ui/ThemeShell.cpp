#include "ThemeShell.h"

#include <QJsonArray>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Themes {

QColor parseColour(const QString& text)
{
    const QString t = text.trimmed();
    static const QRegularExpression rgba(
        QStringLiteral("^rgba?\\(\\s*([0-9.]+)\\s*,\\s*([0-9.]+)\\s*,\\s*([0-9.]+)\\s*(?:,\\s*([0-9.]+)\\s*)?\\)$"));
    const QRegularExpressionMatch m = rgba.match(t);
    if (m.hasMatch()) {
        QColor c(int(std::lround(m.captured(1).toDouble())), int(std::lround(m.captured(2).toDouble())),
                 int(std::lround(m.captured(3).toDouble())));
        if (!m.captured(4).isEmpty()) c.setAlphaF(float(m.captured(4).toDouble()));
        return c;
    }
    return QColor(t);
}

namespace {

QColor colourOf(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? parseColour(v.toString()) : QColor();
}

double numberOf(const QJsonObject& o, const char* key, double fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : fallback;
}

std::vector<Radial> radialsOf(const QJsonArray& list)
{
    std::vector<Radial> out;
    for (const QJsonValue& v : list) {
        const QJsonObject r = v.toObject();
        Radial g;
        g.centre = QPointF(numberOf(r, "x", 0.0), numberOf(r, "y", 0.0));
        g.radius = QSizeF(numberOf(r, "rx", 100.0), numberOf(r, "ry", 100.0));
        g.colour = colourOf(r, "colour");
        g.outer = colourOf(r, "outer");
        if (!g.outer.isValid()) {
            g.outer = g.colour;
            g.outer.setAlpha(0);
        }
        g.stop = numberOf(r, "stop", 1.0);
        g.mid = colourOf(r, "mid");
        g.midStop = numberOf(r, "midStop", 0.5);
        out.push_back(g);
    }
    return out;
}

}  // namespace

Shell parseShell(const QJsonObject& o)
{
    Shell s;
    const QJsonObject b = o.value(QStringLiteral("backdrop")).toObject();
    if (!b.isEmpty()) {
        Backdrop& d = s.backdrop;
        d.on = true;
        d.frame = QSizeF(numberOf(b, "frameW", 1280.0), numberOf(b, "frameH", 772.0));
        d.base = colourOf(b, "base");
        d.baseRadials = radialsOf(b.value(QStringLiteral("baseRadials")).toArray());
        for (const QJsonValue& v : b.value(QStringLiteral("glows")).toArray()) {
            const QJsonObject g = v.toObject();
            d.glows.push_back({QRectF(numberOf(g, "x", 0.0), numberOf(g, "y", 0.0), numberOf(g, "w", 0.0),
                                      numberOf(g, "h", 0.0)),
                               colourOf(g, "colour"), numberOf(g, "opacity", 1.0)});
        }
        d.glowOffset = QPointF(numberOf(b, "glowOffsetX", 0.0), numberOf(b, "glowOffsetY", 0.0));
        d.blur = numberOf(b, "blur", 0.0);
        d.glowOpacity = numberOf(b, "glowOpacity", 1.0);
        d.saturate = numberOf(b, "saturate", 1.0);
        d.radials = radialsOf(b.value(QStringLiteral("radials")).toArray());
        d.grain = numberOf(b, "grain", 0.0);
    }
    const QJsonObject c = o.value(QStringLiteral("card")).toObject();
    if (!c.isEmpty()) {
        Card& k = s.card;
        k.on = true;
        k.radius = int(numberOf(c, "radius", 10.0));
        k.fill = colourOf(c, "fill");
        k.sheen = colourOf(c, "sheen");
        k.sheenAngle = numberOf(c, "sheenAngle", 160.0);
        k.sheenStop = numberOf(c, "sheenStop", 0.42);
        k.edge = colourOf(c, "edge");
        k.innerTop = colourOf(c, "innerTop");
        for (const QJsonValue& v : c.value(QStringLiteral("rim")).toArray()) {
            const QJsonArray stop = v.toArray();
            if (stop.size() == 2) k.rim.emplace_back(stop.at(0).toDouble(), parseColour(stop.at(1).toString()));
        }
        k.rimAngle = numberOf(c, "rimAngle", 145.0);
        k.rimWidth = numberOf(c, "rimWidth", 1.2);
        k.railFull = c.value(QStringLiteral("railFull")).toBool(false);
        if (c.contains(QStringLiteral("windowButtons"))) k.windowButtons = c.value(QStringLiteral("windowButtons")).toString();
    }
    const QJsonObject h = o.value(QStringLiteral("chip")).toObject();
    if (!h.isEmpty()) {
        Chip& p = s.chip;
        p.on = true;
        p.size = int(numberOf(h, "size", 36.0));
        p.radius = int(numberOf(h, "radius", 6.0));
        p.edgeWidth = numberOf(h, "edgeWidth", 1.0);
        p.idleFill = colourOf(h, "idleFill");
        p.idleEdge = colourOf(h, "idleEdge");
        p.idleInk = colourOf(h, "idleInk");
        p.hoverFill = colourOf(h, "hoverFill");
        p.hoverEdge = colourOf(h, "hoverEdge");
        p.hoverInk = colourOf(h, "hoverInk");
        p.onTop = colourOf(h, "onTop");
        p.onBottom = colourOf(h, "onBottom");
        if (!p.onBottom.isValid()) p.onBottom = p.onTop;
        p.onEdge = colourOf(h, "onEdge");
        p.onInk = colourOf(h, "onInk");
        p.onInnerTop = colourOf(h, "onInnerTop");
        p.offInk = colourOf(h, "offInk");
        p.offEdge = colourOf(h, "offEdge");
    }
    const QJsonObject l = o.value(QStringLiteral("list")).toObject();
    if (!l.isEmpty()) {
        List& t = s.list;
        t.on = true;
        t.rowRadius = int(numberOf(l, "rowRadius", 4));
        t.selFill = colourOf(l, "selFill");
        t.selEdge = colourOf(l, "selEdge");
        t.bar = colourOf(l, "bar");
        t.barWidth = int(numberOf(l, "barWidth", 3));
        t.barInset = int(numberOf(l, "barInset", 4));
        t.barX = int(numberOf(l, "barX", 0));
        t.hairline = colourOf(l, "hairline");
        t.nameWeight = int(numberOf(l, "nameWeight", 400));
        t.selWeight = int(numberOf(l, "selWeight", t.nameWeight));
        t.folderWeight = int(numberOf(l, "folderWeight", t.nameWeight));
        t.titleWeight = int(numberOf(l, "titleWeight", 600));
        t.titleBright = l.value(QStringLiteral("titleBright")).toBool();
        t.titlePx = numberOf(l, "titlePx", 0.0);
        t.titleFamily = l.value(QStringLiteral("titleFamily")).toString();
        t.width = int(numberOf(l, "width", 0));
        t.headings = l.value(QStringLiteral("headings")).toBool(false);
        t.rule = colourOf(l, "rule");
        t.ruleHi = colourOf(l, "ruleHi");
        t.monoFamily = l.value(QStringLiteral("monoFamily")).toString();
        t.fieldRadius = int(numberOf(l, "fieldRadius", 0));
        t.fieldFill = colourOf(l, "fieldFill");
        t.fieldEdge = colourOf(l, "fieldEdge");
        t.fieldEditEdge = colourOf(l, "fieldEditEdge");
        t.fieldSearch = l.value(QStringLiteral("fieldSearch")).toBool();
        t.badgeRadius = int(numberOf(l, "badgeRadius", 0));
        t.badgeFill = colourOf(l, "badgeFill");
        t.badgeEdge = colourOf(l, "badgeEdge");
        t.badgeInk = colourOf(l, "badgeInk");
        t.badgeWeight = int(numberOf(l, "badgeWeight", 400));
    }
    const QJsonObject z = o.value(QStringLiteral("gizmo")).toObject();
    if (!z.isEmpty()) {
        Gizmo& g = s.gizmo;
        g.on = true;
        g.boxes = z.value(QStringLiteral("style")).toString() == QLatin1String("boxes");
        g.size = QSize(int(numberOf(z, "width", 120)), int(numberOf(z, "height", 118)));
        g.arm = numberOf(z, "arm", 30.0);
        g.ball = numberOf(z, "ball", 8.5);
        g.letterInk = colourOf(z, "letterInk");
        if (z.contains(QStringLiteral("negative"))) g.negative = z.value(QStringLiteral("negative")).toString();
        g.label = z.value(QStringLiteral("label")).toBool(true);
        g.labelUpper = z.value(QStringLiteral("labelUpper")).toBool();
        g.labelRule = z.value(QStringLiteral("labelRule")).toBool();
        g.labelWeight = int(numberOf(z, "labelWeight", 600));
        g.armWidth = numberOf(z, "armWidth", 2.5);
        g.armInk = colourOf(z, "armInk");
        g.boxFill = colourOf(z, "boxFill");
        g.boxEdge = colourOf(z, "boxEdge");
        g.hubRadius = numberOf(z, "hubRadius", 4.0);
        g.hubInk = colourOf(z, "hubInk");
        g.hubSquare = z.value(QStringLiteral("hubSquare")).toBool();
        g.negRadius = numberOf(z, "negRadius", 5.0);
        g.negStroke = numberOf(z, "negStroke", 1.5);
        g.negOpacity = numberOf(z, "negOpacity", 0.6);
        g.negInk = colourOf(z, "negInk");
        g.negLine = colourOf(z, "negLine");
        g.negLineWidth = numberOf(z, "negLineWidth", 1.5);
        g.ring = numberOf(z, "ring", 0.0);
        g.ringInk = colourOf(z, "ringInk");
    }
    const QJsonObject b2 = o.value(QStringLiteral("label")).toObject();
    if (!b2.isEmpty()) {
        Label& t = s.label;
        t.on = true;
        t.radius = numberOf(b2, "radius", 0.0);
        t.fill = colourOf(b2, "fill");
        t.edgeWidth = numberOf(b2, "edgeWidth", 2.0);
        t.bold = b2.value(QStringLiteral("bold")).toBool(true);
        t.heightPx = numberOf(b2, "height", 24.0);
        t.textPx = numberOf(b2, "textPx", 13.0);
        t.padX = numberOf(b2, "padX", 9.0);
        t.fontFile = b2.value(QStringLiteral("fontFile")).toString();
        t.kpi = b2.value(QStringLiteral("kpi")).toBool();
        t.kpiTextPx = numberOf(b2, "kpiTextPx", 32.0);
        t.captionPx = numberOf(b2, "captionPx", 11.0);
        t.captionFontFile = b2.value(QStringLiteral("captionFontFile")).toString();
    }
    const QJsonObject dc = o.value(QStringLiteral("decor")).toObject();
    if (!dc.isEmpty()) {
        Decor& t = s.decor;
        t.on = true;
        t.marks = dc.value(QStringLiteral("marks")).toBool(false);
        t.markInk = colourOf(dc, "markInk");
        t.markSize = int(numberOf(dc, "markSize", 9));
        t.readout = dc.value(QStringLiteral("readout")).toBool(false);
        t.monoFamily = dc.value(QStringLiteral("monoFamily")).toString();
        t.textPx = numberOf(dc, "textPx", 11.0);
    }
    const QJsonObject m = o.value(QStringLiteral("menu")).toObject();
    if (!m.isEmpty()) {
        Menu& t = s.menu;
        t.on = true;
        t.weight = int(numberOf(m, "weight", 400));
        t.textPx = numberOf(m, "textPx", 13.0);
        t.ink = colourOf(m, "ink");
        t.hover = colourOf(m, "hover");
        t.itemRadius = int(numberOf(m, "itemRadius", 6));
        t.itemPadX = int(numberOf(m, "itemPadX", 9));
        t.wordmarkPx = numberOf(m, "wordmarkPx", 14.0);
        t.wordmarkWeight = int(numberOf(m, "wordmarkWeight", 700));
    }
    return s;
}

namespace {

// Three passes of a box blur approximate a Gaussian of `sigma` (CSS blur()
// is the Gaussian's standard deviation). Premultiplied ARGB, in place.
void boxBlur(QImage& img, double sigma)
{
    if (sigma < 0.5 || img.isNull()) return;
    const int w = img.width(), h = img.height();
    // Three equal boxes of width ~ sqrt(12 sigma^2 / 3 + 1).
    const int box = std::max(1, int(std::lround(std::sqrt(4.0 * sigma * sigma + 1.0))));
    const int r = std::max(1, box / 2);
    std::vector<uint32_t> tmp(size_t(std::max(w, h)));
    auto pass = [&](bool horizontal) {
        const int lines = horizontal ? h : w;
        const int len = horizontal ? w : h;
        for (int line = 0; line < lines; ++line) {
            auto at = [&](int i) -> uint32_t& {
                return horizontal ? reinterpret_cast<uint32_t*>(img.scanLine(line))[i]
                                  : reinterpret_cast<uint32_t*>(img.scanLine(i))[line];
            };
            long sum[4] = {0, 0, 0, 0};
            auto add = [&](uint32_t px, int sign) {
                sum[0] += sign * long((px >> 24) & 0xff);
                sum[1] += sign * long((px >> 16) & 0xff);
                sum[2] += sign * long((px >> 8) & 0xff);
                sum[3] += sign * long(px & 0xff);
            };
            auto clampAt = [&](int i) { return at(std::clamp(i, 0, len - 1)); };
            for (int i = -r; i <= r; ++i) add(clampAt(i), 1);
            const long n = 2 * r + 1;
            for (int i = 0; i < len; ++i) {
                tmp[size_t(i)] = (uint32_t(sum[0] / n) << 24) | (uint32_t(sum[1] / n) << 16) |
                                 (uint32_t(sum[2] / n) << 8) | uint32_t(sum[3] / n);
                add(clampAt(i - r), -1);
                add(clampAt(i + r + 1), 1);
            }
            for (int i = 0; i < len; ++i) at(i) = tmp[size_t(i)];
        }
    };
    for (int k = 0; k < 3; ++k) {
        pass(true);
        pass(false);
    }
}

void saturateImage(QImage& img, double amount)
{
    if (std::abs(amount - 1.0) < 1e-3) return;
    for (int y = 0; y < img.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = row[x];
            const double r = qRed(p), g = qGreen(p), b = qBlue(p);
            // The CSS saturate() matrix's luminance weights.
            const double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const auto c = [&](double v) { return int(std::clamp(l + (v - l) * amount, 0.0, double(qAlpha(p)))); };
            row[x] = qRgba(c(r), c(g), c(b), qAlpha(p));
        }
    }
}

void paintRadial(QPainter& p, const Radial& r, double sx, double sy, const QRectF& all)
{
    // An ellipse is a circle under a scale: the gradient is a circle of radius
    // rx, squashed vertically by ry/rx through the brush's own transform, and
    // padded with its outer colour everywhere past the stop - CSS's own rule.
    const double rx = r.radius.width() * sx, ry = r.radius.height() * sy;
    if (rx <= 0.0 || ry <= 0.0) return;
    QRadialGradient g(QPointF(0, 0), rx);
    g.setColorAt(0.0, r.colour);
    if (r.mid.isValid()) g.setColorAt(std::clamp(r.midStop, 0.01, 0.99), r.mid);
    g.setColorAt(std::clamp(r.stop, 0.01, 1.0), r.outer);
    if (r.stop < 1.0) g.setColorAt(1.0, r.outer);
    QBrush brush(g);
    brush.setTransform(QTransform().translate(r.centre.x() * sx, r.centre.y() * sy).scale(1.0, ry / rx));
    p.fillRect(all, brush);
}

}  // namespace

std::pair<QPointF, QPointF> cssGradientLine(const QRectF& rect, double angleDeg)
{
    const double a = angleDeg * 3.14159265358979323846 / 180.0;
    const QPointF dir(std::sin(a), -std::cos(a));
    const double half = (std::abs(rect.width() * std::sin(a)) + std::abs(rect.height() * std::cos(a))) / 2.0;
    const QPointF c = rect.center();
    return {c - dir * half, c + dir * half};
}

void paintCard(QPainter& p, const QRectF& rect, const Card& card)
{
    const double r = std::max(0.0, std::min<double>(card.radius, std::min(rect.width(), rect.height()) / 2.0));
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath shape;
    shape.addRoundedRect(rect, r, r);
    if (card.fill.isValid()) p.fillPath(shape, card.fill);
    if (card.sheen.isValid()) {
        const auto [from, to] = cssGradientLine(rect, card.sheenAngle);
        QLinearGradient g(from, to);
        QColor clear = card.sheen;
        clear.setAlpha(0);
        g.setColorAt(0.0, card.sheen);
        g.setColorAt(std::clamp(card.sheenStop, 0.01, 1.0), clear);
        p.fillPath(shape, g);
    }
    // inset 0 1px 0: the shape less itself moved down a pixel - a crescent
    // along the top edge that follows the corners.
    if (card.innerTop.isValid()) {
        QPainterPath lower;
        lower.addRoundedRect(rect.translated(0, 1.0), r, r);
        p.fillPath(shape.subtracted(lower), card.innerTop);
    }
    p.restore();
    if (card.edge.isValid() && card.edge.alpha() > 0) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath line;
        line.addRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), std::max(0.0, r - 0.5), std::max(0.0, r - 0.5));
        p.setPen(QPen(card.edge, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
        p.restore();
    }
    // The specular rim: a gradient stroked along the outline, brightest at the
    // top-left - Violet's ::before border.
    if (!card.rim.empty()) {
        const auto [from, to] = cssGradientLine(rect, card.rimAngle);
        QLinearGradient g(from, to);
        for (const auto& [at, colour] : card.rim) g.setColorAt(std::clamp(at, 0.0, 1.0), colour);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        const double w = card.rimWidth;
        QPainterPath line;
        line.addRoundedRect(rect.adjusted(w / 2, w / 2, -w / 2, -w / 2), std::max(0.0, r - w / 2),
                            std::max(0.0, r - w / 2));
        p.setPen(QPen(QBrush(g), w));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
        p.restore();
    }
}

QImage renderBackdrop(const Backdrop& b, QSize size, const QColor& viewport)
{
    if (!b.on || size.isEmpty()) return {};
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(b.base.isValid() ? b.base : viewport);
    const double sx = size.width() / b.frame.width();
    const double sy = size.height() / b.frame.height();
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (const Radial& r : b.baseRadials) paintRadial(p, r, sx, sy, QRectF(out.rect()));
    }

    // The glows, blurred at a quarter of the size - a blur of tens of pixels
    // loses nothing a quarter-size layer keeps - and scaled back up.
    if (!b.glows.empty()) {
        const double q = 0.25;
        const QSize small(std::max(1, int(size.width() * q)), std::max(1, int(size.height() * q)));
        QImage layer(small, QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        {
            QPainter p(&layer);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            for (const Glow& g : b.glows) {
                QColor c = g.colour;
                c.setAlphaF(float(std::clamp(g.opacity * c.alphaF(), 0.0, 1.0)));
                p.setBrush(c);
                const QRectF r = g.rect.translated(b.glowOffset);
                p.drawEllipse(QRectF(r.x() * sx * q, r.y() * sy * q, r.width() * sx * q, r.height() * sy * q));
            }
        }
        boxBlur(layer, b.blur * std::sqrt(sx * sy) * q);
        saturateImage(layer, b.saturate);
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setOpacity(std::clamp(b.glowOpacity, 0.0, 1.0));
        p.drawImage(QRectF(QPointF(0, 0), QSizeF(size)), layer);
    }
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing, true);
        for (const Radial& r : b.radials) paintRadial(p, r, sx, sy, QRectF(out.rect()));
    }

    // The grain: a fine grey noise blended by OVERLAY at the mockup's
    // opacity - a fixed seed, so the same theme is the same picture.
    if (b.grain > 0.0) {
        QImage noise(size, QImage::Format_ARGB32_Premultiplied);
        QRandomGenerator rng(11);
        for (int y = 0; y < noise.height(); ++y) {
            auto* row = reinterpret_cast<QRgb*>(noise.scanLine(y));
            for (int x = 0; x < noise.width(); ++x) {
                // Three draws averaged: turbulence noise sits around mid grey,
                // not evenly across the range.
                const int v = int((rng.bounded(256) + rng.bounded(256) + rng.bounded(256)) / 3);
                row[x] = qRgba(v, v, v, 255);
            }
        }
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_Overlay);
        p.setOpacity(std::clamp(b.grain, 0.0, 1.0));
        p.drawImage(0, 0, noise);
    }
    return out;
}

}  // namespace Themes

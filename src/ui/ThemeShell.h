#pragma once

// A THEME'S SHELL (themes part 2): what the app's Qt surfaces look like in a
// theme, beyond its colours - the viewport's backdrop light, the floating
// cards (fill, sheen, rim, radius) and the tool chips. The user picked each
// theme from a mockup and wants it EXACTLY so; every number here is that
// mockup's own CSS, carried as data in the theme's JSON ("shell"), so no hex
// enters src/ and the mockup stays the specification.
//
// A theme with no "shell" block - Graphite - has every part off, and every
// painter that reads this keeps exactly what it painted before: a window
// built with persistence off (gui_smoke) wears Graphite, so the suites see
// no change at all.
//
// What a Qt surface cannot do that the mockups' CSS does: a BACKDROP BLUR of
// the live 3D frame behind a card (22-26 px in the glass themes). The cards
// keep the mockups' tinted fill, sheen and rim over the backdrop's light, so
// they read as the mockups do; a body passing behind a card shows through the
// tint unblurred.

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>

#include <utility>
#include <vector>

namespace Themes {

// One soft blob of light: an ellipse in the mockup's viewport frame
// (backdrop.frame, CSS px), blurred by backdrop.blur.
struct Glow {
    QRectF rect;
    QColor colour;
    double opacity = 1.0;
};

// A CSS radial-gradient(rx ry at cx cy, colour, transparent stop).
struct Radial {
    QPointF centre;   // in frame px
    QSizeF radius;    // in frame px
    QColor colour;    // the centre's colour, alpha included
    QColor outer;     // what it fades to (transparent unless the base itself)
    double stop = 1.0;
    QColor mid;       // an optional middle stop (Aurora's calm)
    double midStop = 0.5;
};

struct Backdrop {
    bool on = false;
    QSizeF frame{1280.0, 772.0};    // the mockup's viewport, in CSS px
    QColor base;                    // invalid: the theme's viewport colour
    std::vector<Radial> baseRadials;   // painted first, over the base (Nightshade's vignette)
    std::vector<Glow> glows;
    QPointF glowOffset;             // the glow layer's own offset (Aurora's inset:-80px)
    double blur = 0.0;              // CSS blur(px) of the glow layer
    double glowOpacity = 1.0;       // the glow layer's own opacity
    double saturate = 1.0;          // the glow layer's saturate()
    std::vector<Radial> radials;    // painted over the glows (Nightshade's ambience, Aurora's calm)
    double grain = 0.0;             // noise overlay opacity
};

struct Card {
    bool on = false;
    int radius = 10;      // clamped to half the card's short side - a rail is a capsule
    QColor fill;          // the tint
    QColor sheen;         // a top-left sheen fading out at sheenStop
    double sheenAngle = 160.0;
    double sheenStop = 0.42;
    QColor edge;          // the hairline border
    QColor innerTop;      // a 1 px inner highlight along the top edge
    std::vector<std::pair<double, QColor>> rim;   // a gradient rim over the edge (Violet's specular)
    double rimAngle = 145.0;
    double rimWidth = 1.2;
};

struct Chip {
    bool on = false;
    int size = 36;
    int radius = 6;
    double edgeWidth = 1.0;
    QColor idleFill, idleEdge, idleInk;
    QColor hoverFill, hoverEdge, hoverInk;
    QColor onTop, onBottom, onEdge, onInk, onInnerTop;
    QColor offInk, offEdge;
};

// The Items drawer as the mockup draws it: the selected row's capsule, its
// bar, the names' weights, the title, the filter field and the count badge.
struct List {
    bool on = false;
    int rowRadius = 4;
    QColor selFill, selEdge;
    QColor bar;
    int barWidth = 3;
    int barInset = 4;        // from the row's top and bottom; 0 runs the full height
    int barX = 0;
    QColor hairline;         // a rule under every row (Ultraviolet)
    int nameWeight = 400, selWeight = 400, folderWeight = 400;
    int titleWeight = 600;
    bool titleBright = false;   // the title in text(), not textMuted()
    int fieldRadius = 0;
    QColor fieldFill, fieldEdge, fieldEditEdge;
    bool fieldSearch = false;   // a magnifier at the field's start
    int badgeRadius = 0;
    QColor badgeFill, badgeEdge, badgeInk;
    int badgeWeight = 400;
};

struct Shell {
    Backdrop backdrop;
    Card card;
    Chip chip;
    List list;
};

// Parses a theme's "shell" object; an empty object gives every part off.
Shell parseShell(const QJsonObject& o);
// "#rrggbb", "#aarrggbb" or "rgba(r,g,b,a)" - the mockups' own spellings.
QColor parseColour(const QString& text);

// A CSS linear-gradient's line for `rect` at `angleDeg` (0 is "to top", 90
// "to right"): start and end points, the gradient's length as CSS measures it.
std::pair<QPointF, QPointF> cssGradientLine(const QRectF& rect, double angleDeg);

// A theme's card: fill, sheen, inner top highlight, edge and gradient rim, at
// the theme's radius clamped to half the short side. Theme::paintSurface()
// calls this when the theme has a card.
void paintCard(class QPainter& p, const QRectF& rect, const Card& card);

// The backdrop at a viewport's DEVICE size: the base, the blurred glows, the
// radials and the grain, positioned in the mockup's frame scaled to `size`.
// `viewport` is the theme's viewport colour, for a backdrop with no base.
QImage renderBackdrop(const Backdrop& b, QSize size, const QColor& viewport);

}  // namespace Themes

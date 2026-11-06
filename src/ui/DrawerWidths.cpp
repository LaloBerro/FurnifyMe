#include "DrawerWidths.h"

#include "Theme.h"
#include "Themes.h"

#include <QFontMetrics>
#include <QString>

#include <algorithm>

namespace {

// The difference between what a specimen measures now and at the shipped
// scale, added to the width that scale was designed around.
int grown(int base, const QFont& now, const QFont& shipped, const QString& specimen)
{
    return std::max(base, base + QFontMetrics(now).horizontalAdvance(specimen) -
                              QFontMetrics(shipped).horizontalAdvance(specimen));
}

}   // namespace

namespace DrawerWidths {

int versions()
{
    const Theme::Spec shipped = Theme::defaultSpec();
    const QString name = QStringLiteral("A version name that runs a little long");
    const QString date = QStringLiteral("Jan 1, 2026, 12:00 PM");
    const auto measure = [&](const QFont& n, const QFont& d) {
        return std::max(QFontMetrics(n).horizontalAdvance(name), QFontMetrics(d).horizontalAdvance(date));
    };
    const int now = measure(Theme::bodyFont(), Theme::labelFont());
    const int atShipped = measure(Theme::bodyFontFor(shipped), Theme::labelFontFor(shipped));
    return std::max(240, 240 + now - atShipped);
}

int joints()
{
    const Theme::Spec shipped = Theme::defaultSpec();
    return Theme::wholeDevicePixels(grown(240, Theme::labelFont(), Theme::labelFontFor(shipped),
                                          QStringLiteral("Mortise and tenon, 6 × 30 mm")));
}

int items()
{
    const Theme::Spec shipped = Theme::defaultSpec();
    // A theme's mockup sets the drawer's width outright (themes part 2) -
    // still grown by what a larger type scale costs.
    const int base = Themes::shell().list.on && Themes::shell().list.width > 0 ? Themes::shell().list.width : 200;
    return grown(base, Theme::bodyFont(), Theme::bodyFontFor(shipped), QStringLiteral("Left side panel"));
}

QSize settings()
{
    return Theme::wholeDevicePixels(QSize(296, 380));
}

}   // namespace DrawerWidths

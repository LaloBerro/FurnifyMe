#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>

// Deliberately at global scope. Q_INIT_RESOURCE declares the initialiser as an
// extern at block scope, which binds to the innermost enclosing namespace - so
// calling it from inside `namespace Theme` would look for
// Theme::qInitResources_resources and fail to link.
static void furnifyInitResources()
{
    // A Qt resource compiled into a STATIC library is discarded by the linker
    // unless something references its initialiser. Nothing else does, so the
    // font silently would not exist at runtime.
    Q_INIT_RESOURCE(resources);
}

namespace Theme {

QColor chrome()       { return QColor("#1b1b1d"); }
QColor panel()        { return QColor("#232326"); }
QColor chip()         { return QColor("#2b2b2e"); }
QColor chipHover()    { return QColor("#34343a"); }
QColor chipActive()   { return QColor("#3d3d45"); }
QColor accent()       { return QColor("#3d7eff"); }
QColor text()         { return QColor("#f0f0f0"); }
QColor textMuted()    { return QColor("#9a9aa2"); }
QColor textDisabled() { return QColor("#5c5c64"); }
QColor border()       { return QColor("#3a3a40"); }
QColor viewport()     { return QColor("#45454b"); }
QColor gridMinor()    { return QColor("#3a3a40"); }
QColor gridMajor()    { return QColor("#4a4a52"); }
QColor axisX()        { return QColor("#7a4a4a"); }   // muted red
QColor axisY()        { return QColor("#4a7a4a"); }   // muted green
QColor danger()       { return QColor("#e0564a"); }   // invalid input, failures
QColor focusRing()    { return QColor("#ffca4a"); }   // amber - distinct from
                                                       // accent(), which already
                                                       // marks the checked state

namespace {
QString g_fontFamily;

// The whole type scale, in one place. Four sizes is all this app has needed;
// a fifth anywhere is a smell - see Theme.h.
constexpr double kBadgePt = 8.0;
constexpr double kLabelPt = 9.0;
constexpr double kBodyPt = 10.0;
constexpr double kTitlePt = 13.0;

QFont scaledFont(double pointSize, bool bold)
{
    QFont f;
    if (!g_fontFamily.isEmpty()) f.setFamily(g_fontFamily);
    f.setPointSizeF(pointSize);
    f.setBold(bold);
    return f;
}
}  // namespace

QString fontFamily() { return g_fontFamily; }

QFont titleFont() { return scaledFont(kTitlePt, /*bold=*/true); }
QFont bodyFont()  { return scaledFont(kBodyPt, /*bold=*/false); }
QFont labelFont() { return scaledFont(kLabelPt, /*bold=*/false); }
QFont badgeFont() { return scaledFont(kBadgePt, /*bold=*/false); }

int motionMs() { return 160; }
QEasingCurve motionCurve() { return QEasingCurve(QEasingCurve::OutCubic); }

void apply(QApplication& app)
{
    // DM Sans, compiled in as a Qt resource. If it cannot be loaded we keep the
    // platform default rather than falling back to something arbitrary - a
    // missing font should not change the layout in a way nobody chose.
    furnifyInitResources();

    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans.ttf"));
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) g_fontFamily = families.first();
    }

    // The application default becomes bodyFont(): every widget that never
    // calls setFont()/setStyleSheet() for itself - including Qt's own
    // internals, like a status bar's temporary message label, that this code
    // never gets a pointer to - inherits this through ordinary Qt font
    // propagation, no stylesheet cascade involved. Widgets that need a
    // different scale size (titleFont(), labelFont(), badgeFont()) set it
    // explicitly at their own call site.
    app.setFont(bodyFont());

    QPalette palette;
    palette.setColor(QPalette::Window, chrome());
    palette.setColor(QPalette::WindowText, text());
    palette.setColor(QPalette::Base, panel());
    palette.setColor(QPalette::AlternateBase, chip());
    palette.setColor(QPalette::Text, text());
    palette.setColor(QPalette::Button, chip());
    palette.setColor(QPalette::ButtonText, text());
    palette.setColor(QPalette::Highlight, accent());
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, textDisabled());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled());
    palette.setColor(QPalette::Disabled, QPalette::Text, textDisabled());
    app.setPalette(palette);

    // Compiled in as a raw string rather than a .qrc file: it avoids adding
    // AUTORCC and a resource tree for one asset, and is just as much "in the
    // binary" as a resource would be.
    //
    // font-size on QStatusBar/QStatusBar QLabel is the one place this
    // stylesheet carries a type-scale size: it is how labelFont() reaches
    // QStatusBar's own internal message label, which showMessage() creates
    // privately and this code never gets a pointer to. Read from labelFont()
    // rather than hand-typed, so it cannot drift from the scale in Theme.h.
    app.setStyleSheet(QStringLiteral(R"(
QMainWindow, QWidget       { background-color: #1b1b1d; color: #f0f0f0; }
QMenuBar                   { background-color: #1b1b1d; color: #f0f0f0;
                             border-bottom: 1px solid #3a3a40; padding: 2px; }
QMenuBar::item             { background: transparent; padding: 6px 12px;
                             border-radius: 4px; }
QMenuBar::item:selected    { background-color: #34343a; }
QMenu                      { background-color: #232326; color: #f0f0f0;
                             border: 1px solid #3a3a40; padding: 4px; }
QMenu::item                { padding: 6px 24px 6px 12px; border-radius: 4px; }
QMenu::item:selected       { background-color: #34343a; }
QMenu::item:disabled       { color: #5c5c64; }
QMenu::separator           { height: 1px; background: #3a3a40; margin: 4px 8px; }
QStatusBar                 { background-color: #1b1b1d; color: #9a9aa2;
                             border-top: 1px solid #3a3a40; font-size: %1pt; }
QStatusBar QLabel          { color: #9a9aa2; font-size: %1pt; }
QSplitter::handle          { background-color: #3a3a40; width: 1px; }
QToolTip                   { background-color: #232326; color: #f0f0f0;
                             border: 1px solid #3a3a40; padding: 4px; }
QScrollBar:vertical        { background: #232326; width: 10px; margin: 0; }
QScrollBar::handle:vertical{ background: #3a3a40; border-radius: 5px;
                             min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)").arg(labelFont().pointSizeF()));
}

}  // namespace Theme

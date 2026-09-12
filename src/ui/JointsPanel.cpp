#include "JointsPanel.h"

#include "DocumentModel.h"
#include "IconSet.h"
#include "Joinery.h"
#include "MainWindow.h"
#include "Measure.h"
#include "OcctViewWidget.h"
#include "Theme.h"

#include <QFontMetrics>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStaticText>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
// The drawer's own width at the shipped type scale - the mockup's 240, the
// same as Items and Versions beside it - grown only by what a larger base
// size costs, measured against specimen text (VersionsPanel::cardWidth()'s
// reasoning, unchanged).
constexpr int kBaseWidth = 240;
constexpr int kRadius = 10;
constexpr int kHeaderSide = 11;
constexpr int kPadTop = 10;
constexpr int kPadBottom = 8;
constexpr int kMinHeight = 60;
constexpr int kScrollCap = 420;

// A row, in its own coordinates. The row widget sits 1px inside the card's
// border on each side, so these are the mockup's 14/12 less that pixel.
constexpr double kRowPadL = 13.0;
constexpr double kRowPadR = 11.0;
constexpr double kRowPadT = 6.0;
constexpr double kRowPadB = 7.0;

// The ruler.
constexpr double kStripGap = 6.0;      // above each piece's strip
constexpr double kBarInset = 6.0;      // zero sits this far in from the text edge
constexpr double kBarHeight = 8.0;
constexpr double kTickOverhang = 2.0;  // an item tick past the bar, each side
constexpr double kZeroOverhang = 3.0;  // the zero tick, a little longer
constexpr double kLabelGap = 3.0;      // the least room between two labels on a line
constexpr int kDeleteSize = 20;

QString specimen() { return QStringLiteral("Mortise and tenon, 6 × 30 mm"); }

QString lengthText(double mm) { return QString::fromStdString(Measure::formatLength(mm)); }

// Measure's own number with its unit taken off - a tick label, whose unit the
// shared line names once. Still Measure's rounding, separator and display unit:
// this removes the suffix Measure appended, it formats nothing itself.
QString numberOnly(double mm)
{
    QString text = lengthText(mm);
    const QString suffix = QLatin1Char(' ') + QString::fromStdString(Measure::unitSuffix());
    if (text.endsWith(suffix)) text.chop(suffix.size());
    return text;
}

// The PAINTED form of a string: a number never wraps away from its unit
// ("9 mm"), "inset" never wraps away from its number, and a "W × D" pair stays
// one piece. Non-breaking spaces, in the prepared QStaticText only - the raw
// string every reader compares against keeps plain spaces.
QString bindUnits(const QString& raw)
{
    static const QRegularExpression unitAfterNumber(QStringLiteral("(\\d) (mm|cm)\\b"));
    static const QRegularExpression insetBeforeNumber(QStringLiteral("\\binset (?=\\d)"));
    static const QRegularExpression timesBetweenNumbers(QStringLiteral("(\\d) × (?=\\d)"));
    const QString nbsp(QChar(0x00A0));
    QString painted = raw;
    painted.replace(unitAfterNumber, QStringLiteral("\\1") + nbsp + QStringLiteral("\\2"));
    painted.replace(insetBeforeNumber, QStringLiteral("inset") + nbsp);
    painted.replace(timesBetweenNumbers,
                    QStringLiteral("\\1") + nbsp + QStringLiteral("×") + nbsp);
    return painted;
}

QString number(double value) { return QString::number(value, 'g', 10); }

QString deleteTooltip()
{
    return JointsPanel::tr("Delete this joint\nOne undo brings it back.");
}

QString emptyText()
{
    return JointsPanel::tr("No joints yet.\n\nSelect two pieces that meet and press J to "
                           "plan one.");
}

QString titleCss()
{
    return QStringLiteral("background: transparent; color: %1; font-weight: 600; font-size: %2pt;")
        .arg(Theme::text().name())
        .arg(Theme::titleFont().pointSizeF());
}

QString countCss()
{
    return QStringLiteral("background: transparent; color: %1; font-size: %2pt;")
        .arg(Theme::textMuted().name())
        .arg(Theme::labelFont().pointSizeF());
}

QString emptyCss()
{
    return QStringLiteral("background: transparent; color: %1; font-size: %2pt;")
        .arg(Theme::textMuted().name())
        .arg(Theme::bodyFont().pointSizeF());
}

QString deleteCss()
{
    return QStringLiteral("QPushButton { background: transparent; border: none; "
                          "border-radius: 4px; padding: 0px; } "
                          "QPushButton:hover { background-color: %1; } "
                          "QPushButton:disabled { background: transparent; }")
        .arg(Theme::chipHover().name());
}

// What the prepared rows depend on besides their content: the four fonts and
// the card's width. A Theme broadcast that leaves this alone moved only colours.
QString layoutKey(int cardWidth)
{
    return Theme::bodyFont().toString() + QLatin1Char('|') + Theme::labelFont().toString() +
           QLatin1Char('|') + Theme::badgeFont().toString() + QLatin1Char('|') +
           Theme::titleFont().toString() + QLatin1Char('|') + QString::number(cardWidth);
}

// A derivation's refusal, as the row says it. Joinery's own reasons are written
// for the user; a kernel exception's text is written for a log, and CLAUDE.md
// never shows one - it gets the plain sentence instead.
QString reasonText(const std::string& error)
{
    if (error.rfind("contact:", 0) == 0)
        return JointsPanel::tr("these two pieces can't be measured");
    return QString::fromStdString(error);
}
}  // namespace

// Everything one row paints, decided in refresh() and never changed after:
// the row widget holds it by shared_ptr-to-const and its paintEvent() only
// draws what is in here.
struct JointsPanel::RowModel {
    enum FontRole { Body, Label, LabelBold, Badge, FontRoleCount };
    enum Ink { InkText, InkMuted, InkDanger };

    struct Painted {
        QStaticText text;
        QString raw;
        QRectF rect;
        FontRole font = Body;
        Ink ink = InkText;
    };
    struct Piece {
        QString name;          // user text
        QString depthText;     // "drill 13.5 mm", "18 × 6 mm"
        std::vector<double> marks;
        bool band = false;
        double bandStart = 0.0;
        double bandEnd = 0.0;
    };
    struct Strip {
        QRectF bar;
        QRectF band;
        std::vector<double> ticks;
        std::vector<Painted> labels;   // tick labels only, left to right
        Painted edge;                  // the edge word, when edgeLine >= 0
        int edgeLine = -1;
        bool staggered = false;
        QString depthText;
    };

    // --- content --------------------------------------------------------------
    int jointId = 0;
    int kind = 0;
    bool broken = false;
    bool selected = false;
    bool expanded = false;
    bool fastener = true;
    QString nameA, nameB;
    QString kindLine;
    QString caveat;
    QString edge;
    bool edgeNamed = false;
    double run = 0.0;
    double inset = 0.0;
    double depthA = 0.0;
    double depthB = 0.0;
    double width = 0.0;
    QString unit;
    std::vector<Piece> pieces;
    QString readoutText;

    // --- layout ---------------------------------------------------------------
    QFont fonts[FontRoleCount];
    int rowWidth = 0;
    int height = 0;
    std::vector<Painted> painted;
    QRectF nameARect;
    bool hasCaveatGlyph = false;
    QRectF caveatGlyph;
    std::vector<Strip> strips;
    bool fellBack = false;
    bool staggered = false;
    QPointF chevron;
    QPoint deleteAt;
    QStringList appCopy;
};

namespace {
using RowModel = JointsPanel::RowModel;

RowModel::Painted prepared(const RowModel& m, const QString& raw, RowModel::FontRole font,
                           RowModel::Ink ink, double x, double top, double wrapWidth = -1.0)
{
    RowModel::Painted p;
    p.raw = raw;
    p.font = font;
    p.ink = ink;
    p.text.setTextFormat(Qt::PlainText);   // a name like "<b>Top" is text, not markup
    p.text.setText(bindUnits(raw));
    if (wrapWidth > 0.0) p.text.setTextWidth(wrapWidth);
    p.text.setPerformanceHint(QStaticText::AggressiveCaching);
    p.text.prepare(QTransform(), m.fonts[font]);
    p.rect = QRectF(QPointF(x, top), p.text.size());
    return p;
}

std::shared_ptr<RowModel> contentFor(const DocumentModel& doc, const DocumentModel::Joint& joint,
                                     const Joinery::Derivation& d, int selectedJoint,
                                     bool expanded)
{
    auto m = std::make_shared<RowModel>();
    m->jointId = joint.id;
    m->kind = static_cast<int>(joint.kind);
    m->broken = !d.ok;
    m->selected = joint.id == selectedJoint;
    m->expanded = expanded;
    const Joinery::Family family = Joinery::familyOf(joint.kind);
    m->fastener = family == Joinery::Family::Fasteners;
    m->nameA = QString::fromStdString(doc.nameOf(joint.bodyA));
    m->nameB = QString::fromStdString(doc.nameOf(joint.bodyB));
    m->unit = QString::fromStdString(Measure::unitSuffix());
    const QString kind = QString::fromStdString(Joinery::kindName(joint.kind));

    if (!d.ok) {
        // No readout, no numbers, no caveat - a derivation that refused carries
        // none of them, and nothing here reaches back for the last good one.
        m->kindLine = JointsPanel::tr("Broken — %1").arg(reasonText(d.error));
        return m;
    }

    const Joinery::Readout& r = d.readout;
    m->run = d.contact.runLength();
    m->inset = r.insetMm;
    m->depthA = r.depthAMm;
    m->depthB = r.depthBMm;
    m->width = r.widthMm;
    m->edge = QString::fromStdString(r.referenceEdgeA);
    m->edgeNamed = r.referenceEdgeNamed;
    // THE caveat channel, kind and contact together - not regionShortfallCaveat()
    // directly: a housing or a mortise on a contact that names no end-on piece
    // carries a caveat of its own, and asking the one composed function is what
    // keeps this row and the placement message showing the same set.
    m->caveat = QString::fromStdString(Joinery::caveatsFor(joint.kind, d.contact));

    if (m->fastener) {
        const double size = d.items.empty() ? joint.params.sizeMm : d.items.front().sizeMm;
        m->kindLine = JointsPanel::tr("%1, %2 × %3")
                          .arg(kind, QString::number(d.items.size()), lengthText(size));
        // Both pieces are drilled; a piece with no depth has nothing to mark.
        if (r.depthAMm > 0.0)
            m->pieces.push_back({m->nameA, JointsPanel::tr("drill %1").arg(lengthText(r.depthAMm)),
                                 r.alongMm, false, 0.0, 0.0});
        if (r.depthBMm > 0.0)
            m->pieces.push_back({m->nameB, JointsPanel::tr("drill %1").arg(lengthText(r.depthBMm)),
                                 r.alongMm, false, 0.0, 0.0});
    } else {
        m->kindLine = JointsPanel::tr("%1, %2 × %3")
                          .arg(kind, numberOnly(r.widthMm), lengthText(r.depthAMm));
        // The band is the item's own span along the run: the channel for a
        // housing, the tenon between its shoulders, the whole lap.
        const bool alongU = d.contact.runsAlongU();
        const double centre = r.alongMm.empty() ? m->run / 2.0 : r.alongMm.front();
        const double span = d.items.empty()
                                ? m->run
                                : (alongU ? d.items.front().spanUMm : d.items.front().spanVMm);
        const double start = centre - span / 2.0;
        const double end = centre + span / 2.0;
        if (r.depthAMm > 0.0)
            m->pieces.push_back({m->nameA,
                                 JointsPanel::tr("%1 × %2").arg(numberOnly(r.widthMm),
                                                                lengthText(r.depthAMm)),
                                 {}, true, start, end});
        // A housing's housed piece is not cut (depthB 0) and gets no strip.
        if (r.depthBMm > 0.0)
            m->pieces.push_back({m->nameB,
                                 JointsPanel::tr("%1 × %2").arg(numberOnly(r.widthMm),
                                                                lengthText(r.depthBMm)),
                                 {}, true, start, end});
    }

    // The numbers this row carries, whether or not it is open - the suite's
    // "a broken joint has none" reads this, so it must not depend on a click.
    QStringList readout;
    for (const RowModel::Piece& piece : m->pieces) {
        readout << piece.depthText;
        if (piece.band) {
            readout << JointsPanel::tr("%1 to %2").arg(numberOnly(piece.bandStart),
                                                      lengthText(piece.bandEnd));
        } else {
            QStringList parts;
            for (double mark : piece.marks) parts << numberOnly(mark);
            readout << parts.join(QStringLiteral(" · ")) + QLatin1Char(' ') + m->unit;
        }
    }
    readout << m->edge;
    if (m->fastener) readout << JointsPanel::tr("inset %1 from the face").arg(lengthText(m->inset));
    m->readoutText = readout.join(QLatin1Char('\n'));
    return m;
}

// The early-out signature of one row: EVERY field the row displays, and only
// those. A field shown but missing here stops updating - the suite pins the
// names, the selected state and the caveat by name for that reason.
QString signatureOf(const RowModel& m)
{
    const QChar sep(0x1f);
    QString s = number(m.jointId) + sep + number(m.kind) + sep + m.nameA + sep + m.nameB + sep +
                (m.broken ? QStringLiteral("B") : QStringLiteral("b")) +
                (m.selected ? QStringLiteral("S") : QStringLiteral("s")) +
                (m.expanded ? QStringLiteral("E") : QStringLiteral("e")) + sep + m.kindLine + sep +
                m.caveat + sep + m.edge + sep + (m.edgeNamed ? QStringLiteral("N") : QStringLiteral("n")) +
                sep + number(m.run) + sep + number(m.inset) + sep + m.unit + sep + number(m.depthA) +
                sep + number(m.depthB) + sep + number(m.width) + sep;
    for (const RowModel::Piece& piece : m.pieces) {
        s += piece.name + sep + piece.depthText + sep + (piece.band ? QStringLiteral("1") : QStringLiteral("0")) +
             sep + number(piece.bandStart) + sep + number(piece.bandEnd) + sep;
        for (double mark : piece.marks) s += number(mark) + QLatin1Char(',');
        s += sep;
    }
    return s;
}

// One piece's ruler. False when its tick labels cannot be kept apart on two
// lines at the font they are painted in - the caller then writes the numbers.
bool layoutStrip(const RowModel& m, const RowModel::Piece& piece, double x0, double x1,
                 double& y, RowModel::Strip& out, std::vector<RowModel::Painted>& texts)
{
    y += kStripGap;
    RowModel::Painted depth = prepared(m, piece.depthText, RowModel::Label, RowModel::InkMuted, 0.0, y);
    depth.rect.moveLeft(x1 - depth.rect.width());
    const QFontMetricsF bold(m.fonts[RowModel::LabelBold]);
    const QString name = bold.elidedText(piece.name, Qt::ElideRight,
                                         std::max(12.0, depth.rect.left() - 8.0 - x0));
    RowModel::Painted who = prepared(m, name, RowModel::LabelBold, RowModel::InkText, x0, y);
    const double whoHeight = std::max(who.rect.height(), depth.rect.height());
    texts.push_back(who);
    texts.push_back(depth);
    y += whoHeight + 1.0;

    const double barLeft = x0 + kBarInset;
    const double barRight = x1 - kBarInset;
    const double barSpan = std::max(1.0, barRight - barLeft);
    // TO SCALE: the named edge at zero, the joint's run as the bar's length.
    const auto xAt = [&](double mm) {
        if (m.run <= 0.0) return barLeft;
        return barLeft + std::clamp(mm, 0.0, m.run) / m.run * barSpan;
    };

    struct Candidate {
        QString text;
        double anchor;
    };
    std::vector<Candidate> candidates;
    std::vector<double> ticks;
    if (piece.band) {
        ticks = {xAt(piece.bandStart), xAt(piece.bandEnd)};
        candidates.push_back({numberOnly(piece.bandStart), ticks[0]});
        candidates.push_back({numberOnly(piece.bandEnd), ticks[1]});
    } else {
        for (double mark : piece.marks) {
            ticks.push_back(xAt(mark));
            candidates.push_back({numberOnly(mark), ticks.back()});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.anchor < b.anchor; });

    const double labelHeight = QFontMetricsF(m.fonts[RowModel::Badge]).height();
    const double lineTop0 = y;
    const double barTop = lineTop0 + labelHeight + 3.0;
    const double barBottom = barTop + kBarHeight;
    const double lineTop1 = barBottom + kZeroOverhang + 1.0;

    // TICK LABELS FIRST, and above their ticks - the picked design. Greedy,
    // left to right: a label takes the line above the bar when it clears the
    // last label there, the line below when it clears the last one THERE, and
    // otherwise the ruler cannot be drawn honestly at all.
    double lastRight[2] = {-1.0e9, -1.0e9};
    double firstLeft[2] = {1.0e9, 1.0e9};
    bool staggered = false;
    std::vector<RowModel::Painted> labels;
    for (const Candidate& c : candidates) {
        RowModel::Painted label =
            prepared(m, c.text, RowModel::Badge, RowModel::InkText, 0.0, 0.0);
        const double w = label.rect.width();
        const double left = std::clamp(c.anchor - w / 2.0, x0, std::max(x0, x1 - w));
        int line = -1;
        if (left >= lastRight[0] + kLabelGap) {
            line = 0;
        } else if (left >= lastRight[1] + kLabelGap) {
            line = 1;
        } else {
            return false;
        }
        firstLeft[line] = std::min(firstLeft[line], left);
        lastRight[line] = left + w;
        if (line == 1) staggered = true;
        label.rect.moveTopLeft(QPointF(left, line == 0 ? lineTop0 : lineTop1));
        labels.push_back(label);
    }

    // THEN the edge word, where it is genuinely clear: at zero above the bar
    // when it ends before the first label there, under zero below the bar when
    // it ends before the first label on THAT line, and otherwise not on the
    // ruler at all - the shared line already names the edge, so nothing is lost.
    RowModel::Painted edge = prepared(m, m.edge, RowModel::Badge, RowModel::InkMuted, 0.0, 0.0);
    int edgeLine = -1;
    if (x0 + edge.rect.width() + kLabelGap <= firstLeft[0]) {
        edgeLine = 0;
    } else if (x0 + edge.rect.width() + kLabelGap <= firstLeft[1]) {
        edgeLine = 1;
    }
    if (edgeLine >= 0) edge.rect.moveTopLeft(QPointF(x0, edgeLine == 0 ? lineTop0 : lineTop1));

    out.bar = QRectF(barLeft, barTop, barSpan, kBarHeight);
    out.ticks = ticks;
    out.labels = labels;
    out.edge = edge;
    out.edgeLine = edgeLine;
    out.staggered = staggered;
    out.depthText = piece.depthText;
    if (piece.band && ticks.size() == 2)
        out.band = QRectF(QPointF(ticks[0], barTop), QPointF(ticks[1], barBottom));
    const bool usesLineBelow = staggered || edgeLine == 1;
    y = usesLineBelow ? lineTop1 + labelHeight : barBottom + kZeroOverhang;
    return true;
}

void layoutRow(RowModel& m, double width)
{
    m.fonts[RowModel::Body] = Theme::bodyFont();
    m.fonts[RowModel::Label] = Theme::labelFont();
    QFont bold = Theme::labelFont();
    bold.setWeight(QFont::DemiBold);
    m.fonts[RowModel::LabelBold] = bold;
    m.fonts[RowModel::Badge] = Theme::badgeFont();

    m.rowWidth = static_cast<int>(width);
    m.painted.clear();
    m.strips.clear();
    m.appCopy.clear();
    m.fellBack = false;
    m.staggered = false;
    m.hasCaveatGlyph = false;

    const double x0 = kRowPadL;
    const double x1 = width - kRowPadR;
    double y = kRowPadT;

    // --- "<A> ↔ <B>", names elided to share the line -------------------------
    const double reserve = m.expanded ? kDeleteSize + 16.0 : 0.0;
    const QFontMetricsF body(m.fonts[RowModel::Body]);
    const QString arrow = QStringLiteral(" ↔ ");
    const double arrowWidth = body.horizontalAdvance(arrow);
    const double available = std::max(24.0, x1 - x0 - reserve - arrowWidth);
    double widthA = body.horizontalAdvance(m.nameA);
    double widthB = body.horizontalAdvance(m.nameB);
    if (widthA + widthB > available) {
        const double half = available / 2.0;
        if (widthA <= half) {
            widthB = available - widthA;
        } else if (widthB <= half) {
            widthA = available - widthB;
        } else {
            widthA = half;
            widthB = half;
        }
    }
    const RowModel::Ink nameInk = m.broken ? RowModel::InkDanger : RowModel::InkText;
    RowModel::Painted a = prepared(m, body.elidedText(m.nameA, Qt::ElideRight, widthA),
                                   RowModel::Body, nameInk, x0, y);
    RowModel::Painted mid = prepared(m, arrow, RowModel::Body, RowModel::InkMuted, a.rect.right(), y);
    RowModel::Painted b = prepared(m, body.elidedText(m.nameB, Qt::ElideRight, widthB),
                                   RowModel::Body, nameInk, mid.rect.right(), y);
    const double pairHeight = std::max({a.rect.height(), mid.rect.height(), b.rect.height()});
    m.nameARect = a.rect;
    m.chevron = QPointF(x1 - kDeleteSize - 8.0, y + pairHeight / 2.0);
    m.deleteAt = QPoint(static_cast<int>(width - kRowPadR - kDeleteSize + 6.0),
                        static_cast<int>(kRowPadT - 3.0));
    m.painted.push_back(a);
    m.painted.push_back(mid);
    m.painted.push_back(b);
    m.appCopy << arrow.trimmed();
    y += pairHeight + 1.0;

    // --- the kind line, or the reason ----------------------------------------
    RowModel::Painted kind = prepared(m, m.kindLine, RowModel::Label,
                                      m.broken ? RowModel::InkText : RowModel::InkMuted, x0, y,
                                      x1 - x0);
    m.painted.push_back(kind);
    m.appCopy << m.kindLine;
    y += kind.rect.height();

    // --- the caveat ------------------------------------------------------------
    if (!m.caveat.isEmpty()) {
        y += 4.0;
        const double glyph = 12.0;
        m.hasCaveatGlyph = true;
        m.caveatGlyph = QRectF(x0, y + 1.0, glyph, glyph);
        RowModel::Painted note = prepared(m, m.caveat, RowModel::Badge, RowModel::InkMuted,
                                          x0 + glyph + 6.0, y, x1 - x0 - glyph - 6.0);
        m.painted.push_back(note);
        m.appCopy << m.caveat;
        y += std::max(note.rect.height(), glyph + 1.0);
    }

    // --- open: the mark-out ----------------------------------------------------
    if (m.expanded && !m.broken && !m.pieces.empty()) {
        bool drawn = m.edgeNamed;
        double rulerY = y;
        std::vector<RowModel::Strip> strips;
        std::vector<RowModel::Painted> stripTexts;
        for (std::size_t i = 0; drawn && i < m.pieces.size(); ++i) {
            RowModel::Strip strip;
            drawn = layoutStrip(m, m.pieces[i], x0, x1, rulerY, strip, stripTexts);
            if (drawn) strips.push_back(strip);
        }

        if (drawn) {
            y = rulerY;
            m.strips = strips;
            for (const RowModel::Strip& strip : m.strips) m.staggered = m.staggered || strip.staggered;
            for (const RowModel::Painted& t : stripTexts) {
                m.painted.push_back(t);
                // The bold name is the piece's own (user) name; the depth is ours.
                if (t.font != RowModel::LabelBold) m.appCopy << t.raw;
            }
            for (const RowModel::Strip& strip : m.strips) {
                for (const RowModel::Painted& label : strip.labels) m.appCopy << label.raw;
                if (strip.edgeLine >= 0) m.appCopy << strip.edge.raw;
            }
        } else {
            // WRITTEN, not drawn: no single edge to put at zero, or labels that
            // collide even on two lines. An unreadable ruler is not a ruler.
            m.fellBack = true;
            // No single edge: the sentence saying so comes FIRST, so every
            // number below it is read already knowing why no edge is named.
            if (!m.edgeNamed) {
                y += 5.0;
                RowModel::Painted why =
                    prepared(m, m.edge, RowModel::Label, RowModel::InkMuted, x0, y, x1 - x0);
                m.painted.push_back(why);
                m.appCopy << m.edge;
                y += why.rect.height();
            }
            for (const RowModel::Piece& piece : m.pieces) {
                y += kStripGap;
                RowModel::Painted depth =
                    prepared(m, piece.depthText, RowModel::Label, RowModel::InkMuted, 0.0, y);
                depth.rect.moveLeft(x1 - depth.rect.width());
                const QFontMetricsF boldMetrics(m.fonts[RowModel::LabelBold]);
                RowModel::Painted who = prepared(
                    m,
                    boldMetrics.elidedText(piece.name, Qt::ElideRight,
                                           std::max(12.0, depth.rect.left() - 8.0 - x0)),
                    RowModel::LabelBold, RowModel::InkText, x0, y);
                m.painted.push_back(who);
                m.painted.push_back(depth);
                m.appCopy << piece.depthText;
                y += std::max(who.rect.height(), depth.rect.height()) + 1.0;

                QString numbers;
                if (piece.band) {
                    numbers = JointsPanel::tr("%1 to %2").arg(numberOnly(piece.bandStart),
                                                             lengthText(piece.bandEnd));
                } else {
                    QStringList parts;
                    for (double mark : piece.marks) parts << numberOnly(mark);
                    numbers = parts.join(QStringLiteral(" · ")) + QLatin1Char(' ') + m.unit;
                }
                const QString line =
                    m.edgeNamed ? JointsPanel::tr("from the %1 edge: %2").arg(m.edge, numbers)
                                : numbers;
                RowModel::Painted written =
                    prepared(m, line, RowModel::Label, RowModel::InkText, x0, y, x1 - x0);
                m.painted.push_back(written);
                m.appCopy << line;
                y += written.rect.height();
            }
        }

        // The one line shared by both pieces.
        const QString inset = JointsPanel::tr("inset %1 from the face").arg(lengthText(m.inset));
        QString shared;
        if (!m.fellBack) {
            shared = m.fastener
                         ? JointsPanel::tr("%1 from the %2 edge · %3").arg(m.unit, m.edge, inset)
                         : JointsPanel::tr("%1 from the %2 edge").arg(m.unit, m.edge);
        } else {
            // Written: the edge is already named on every numbers line, or its
            // absence already explained above them - only the inset is left.
            shared = m.fastener ? inset : QString();
        }
        if (!shared.isEmpty()) {
            y += 5.0;
            RowModel::Painted line =
                prepared(m, shared, RowModel::Label, RowModel::InkMuted, x0, y, x1 - x0);
            m.painted.push_back(line);
            m.appCopy << shared;
            y += line.rect.height();
        }
    }

    y += kRowPadB;
    m.height = Theme::wholeDevicePixels(static_cast<int>(std::ceil(y)));
}

QColor inkColour(RowModel::Ink ink)
{
    switch (ink) {
        case RowModel::InkText: return Theme::text();
        case RowModel::InkMuted: return Theme::textMuted();
        case RowModel::InkDanger: return Theme::danger();
    }
    return Theme::text();
}

// One row. Paints its model and nothing else: every rectangle and every
// prepared string was decided in refresh(). Colours are read HERE, at paint
// time, which is what lets a colour edit repaint a row without rebuilding it.
class JointRowWidget : public QWidget {
public:
    JointRowWidget(JointsPanel* panel, std::shared_ptr<const RowModel> model, QWidget* parent)
        : QWidget(parent)
        , myPanel(panel)
        , myModel(std::move(model))
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_NoMousePropagation);
        Theme::makeSurfaceTransparent(this);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const RowModel& m = *myModel;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // The mockup's two row states: the open joint in the accent, a broken
        // one in danger, each a soft ground and a 3px bar at the left edge.
        if (m.broken || m.selected) {
            QColor ground = m.broken ? Theme::danger() : Theme::accent();
            ground.setAlphaF(m.broken ? 0.13 : 0.22);
            p.fillRect(rect(), ground);
            const QColor bar = m.broken ? Theme::danger() : Theme::accent().lighter(160);
            p.fillRect(QRectF(0.0, 6.0, 3.0, std::max(0, height() - 12)), bar);
        }
        Theme::drawCrispRule(p, QPointF(0.0, 0.0), QPointF(width(), 0.0), Theme::border());

        for (const RowModel::Painted& t : m.painted) {
            p.setFont(m.fonts[t.font]);
            p.setPen(inkColour(t.ink));
            p.drawStaticText(t.rect.topLeft(), t.text);
        }

        if (m.hasCaveatGlyph) {
            const QRectF g = m.caveatGlyph;
            QPainterPath triangle;
            triangle.moveTo(g.center().x(), g.top());
            triangle.lineTo(g.right(), g.bottom());
            triangle.lineTo(g.left(), g.bottom());
            triangle.closeSubpath();
            QPen caution(Theme::caution(), 1.4);
            caution.setJoinStyle(Qt::RoundJoin);
            caution.setCapStyle(Qt::RoundCap);
            p.setPen(caution);
            p.setBrush(Qt::NoBrush);
            p.drawPath(triangle);
            p.drawLine(QPointF(g.center().x(), g.top() + g.height() * 0.40),
                       QPointF(g.center().x(), g.top() + g.height() * 0.66));
            p.drawPoint(QPointF(g.center().x(), g.top() + g.height() * 0.84));
        }

        const QColor tickInk = Theme::accent().lighter(160);
        for (const RowModel::Strip& strip : m.strips) {
            p.setPen(QPen(Theme::border(), 1.0));
            p.setBrush(Theme::chipHover());
            p.drawRoundedRect(strip.bar, 1.5, 1.5);
            if (!strip.band.isNull()) {
                QColor band = Theme::accent();
                band.setAlphaF(0.55);
                p.fillRect(strip.band, band);
            }
            p.setPen(QPen(Theme::textMuted(), 1.4));
            p.drawLine(QPointF(strip.bar.left(), strip.bar.top() - kZeroOverhang),
                       QPointF(strip.bar.left(), strip.bar.bottom() + kZeroOverhang));
            p.setPen(QPen(tickInk, 1.6));
            for (double x : strip.ticks)
                p.drawLine(QPointF(x, strip.bar.top() - kTickOverhang),
                           QPointF(x, strip.bar.bottom() + kTickOverhang));
            for (const RowModel::Painted& label : strip.labels) {
                p.setFont(m.fonts[label.font]);
                p.setPen(inkColour(label.ink));
                p.drawStaticText(label.rect.topLeft(), label.text);
            }
            if (strip.edgeLine >= 0) {
                p.setFont(m.fonts[strip.edge.font]);
                p.setPen(inkColour(strip.edge.ink));
                p.drawStaticText(strip.edge.rect.topLeft(), strip.edge.text);
            }
        }

        if (m.expanded) {
            QPen chevron(Theme::textMuted(), 1.4);
            chevron.setCapStyle(Qt::RoundCap);
            chevron.setJoinStyle(Qt::RoundJoin);
            p.setPen(chevron);
            const QPointF c = m.chevron;
            p.drawPolyline(QPolygonF{c + QPointF(-3.0, -1.5), c + QPointF(0.0, 1.5),
                                     c + QPointF(3.0, -1.5)});
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        event->accept();
        // The trailing release of a double-click belongs to the double-click,
        // whose first press/release pair has already toggled the row - see
        // mouseDoubleClickEvent(). The gesture that started owns the release
        // that ends it.
        if (mySwallowNextRelease) {
            mySwallowNextRelease = false;
            return;
        }
        if (!rect().contains(event->position().toPoint())) return;
        // Copied out before the call: activateRow() can rebuild every row, this
        // one included (deleteLater - so this frame survives), and nothing below
        // the call may touch this widget again.
        const QPointer<JointsPanel> panel = myPanel;
        const int jointId = myModel->jointId;
        if (panel) panel->activateRow(jointId);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        event->accept();
        // Qt delivers a double-click as press, release, DblClick, release. The
        // first pair toggled the row - often REBUILDING it, so this may be the
        // new row that now sits under the cursor - and the release still to
        // come is this double-click's own, not a second click.
        if (event->button() == Qt::LeftButton) mySwallowNextRelease = true;
    }

private:
    QPointer<JointsPanel> myPanel;
    std::shared_ptr<const RowModel> myModel;
    bool mySwallowNextRelease = false;
};
}  // namespace

JointsPanel::JointsPanel(MainWindow* window, OcctViewWidget* view, QWidget* parent)
    : QWidget(parent)
    , myWindow(window)
    , myView(view)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_NoMousePropagation);
    Theme::makeSurfaceTransparent(this);
    setFixedWidth(cardWidth());

    myOuter = new QVBoxLayout(this);
    // 1px in on each side so a row's ground never paints over the border.
    myOuter->setContentsMargins(1, kPadTop, 1, kPadBottom);
    myOuter->setSpacing(0);

    auto* header = new QWidget(this);
    header->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(kHeaderSide, 0, kHeaderSide, 8);
    headerLayout->setSpacing(8);
    myTitle = new QLabel(tr("Joints"), header);
    myTitle->setStyleSheet(titleCss());
    headerLayout->addWidget(myTitle, 1);
    myCount = new QLabel(header);
    myCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    myCount->setStyleSheet(countCss());
    headerLayout->addWidget(myCount);
    myOuter->addWidget(header);

    // VersionsPanel's transparent scroll idiom, the viewport rule included -
    // QStyleSheetStyle paints a scroll viewport opaque without it, so the
    // stylesheet goes on BOTH the area and its viewport.
    myRowScroll = new QScrollArea(this);
    myRowScroll->setWidgetResizable(true);
    myRowScroll->setFrameShape(QFrame::NoFrame);
    myRowScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    myRowScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    myRowScroll->setAttribute(Qt::WA_NoSystemBackground);
    myRowScroll->viewport()->setAttribute(Qt::WA_NoSystemBackground);
    myRowScroll->setStyleSheet(QStringLiteral("background: transparent;"));
    myRowScroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    myRowScroll->setMaximumHeight(Theme::wholeDevicePixels(kScrollCap));
    auto* rowsHost = new QWidget(myRowScroll);
    rowsHost->setAttribute(Qt::WA_NoSystemBackground);
    rowsHost->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* hostLayout = new QVBoxLayout(rowsHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);
    myRowsLayout = new QVBoxLayout();
    myRowsLayout->setContentsMargins(0, 0, 0, 0);
    myRowsLayout->setSpacing(0);
    hostLayout->addLayout(myRowsLayout);
    hostLayout->addStretch(1);
    myRowScroll->setWidget(rowsHost);
    myOuter->addWidget(myRowScroll);
    myOuter->addStretch(1);

    // Constructed hidden (MainWindow hides it before anchoring), so this only
    // marks the theme pending; the first show settles it and builds the rows.
    applyTheme();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &JointsPanel::applyTheme);
}

JointsPanel::~JointsPanel() = default;

int JointsPanel::cardWidth()
{
    const Theme::Spec shipped = Theme::defaultSpec();
    const int now = QFontMetrics(Theme::labelFont()).horizontalAdvance(specimen());
    const int atShippedScale =
        QFontMetrics(Theme::labelFontFor(shipped)).horizontalAdvance(specimen());
    return Theme::wholeDevicePixels(std::max(kBaseWidth, kBaseWidth + now - atShippedScale));
}

void JointsPanel::applyTheme()
{
    // A Theme broadcast fires per mouse move of an Appearance colour drag. A
    // hidden drawer answers it with one flag, and showEvent() settles it.
    if (!isVisible()) {
        myThemePending = true;
        return;
    }
    myThemePending = false;

    const auto restyle = [](QWidget* widget, const QString& css) {
        if (widget != nullptr && widget->styleSheet() != css) widget->setStyleSheet(css);
    };
    restyle(myTitle, titleCss());
    restyle(myCount, countCss());
    restyle(myEmpty, emptyCss());
    const int width = cardWidth();
    if (minimumWidth() != width || maximumWidth() != width) setFixedWidth(width);

    // Fonts and metrics are baked into every prepared string, so a TYPE change
    // is a rebuild. A colour is read at paint time, so a colour edit restyles
    // the delete controls only if their own colours moved, and repaints.
    const QString key = layoutKey(width);
    if (key != myLayoutKey) {
        myLayoutKey = key;
        myRowsBuilt = false;
        refresh();
        return;
    }
    const QString css = deleteCss();
    const QString iconKey = Theme::text().name() + Theme::textDisabled().name();
    const bool restyleButtons = css != myDeleteCss;
    const bool reicon = iconKey != myIconKey;
    if (reicon) {
        myDeleteIcon = IconSet::icon(IconSet::Glyph::Delete);
        myIconKey = iconKey;
    }
    myDeleteCss = css;
    for (const Row& row : myRows) {
        if (row.remove && restyleButtons) row.remove->setStyleSheet(css);
        if (row.remove && reicon) row.remove->setIcon(myDeleteIcon);
        if (row.widget) row.widget->update();
    }
    update();
}

void JointsPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Everything that happened while hidden is caught up here, so an opened
    // drawer is always current.
    if (myThemePending)
        applyTheme();
    else
        refresh();
}

QSize JointsPanel::sizeHint() const
{
    const int cw = width() > 0 ? width() : cardWidth();
    if (!myOuter) return QSize(cw, kMinHeight);
    const int wrapped = myOuter->hasHeightForWidth() ? myOuter->heightForWidth(cw)
                                                     : myOuter->totalSizeHint().height();
    return QSize(cw, std::max(std::max(wrapped, myOuter->totalMinimumSize().height()), kMinHeight));
}

void JointsPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    Theme::paintSurface(painter, rect(), kRadius);
}

QString JointsPanel::rowSignature(const DocumentModel& document, const DocumentModel::Joint& joint,
                                  const Joinery::Derivation& derivation, int selectedJointId,
                                  bool expanded)
{
    return signatureOf(*contentFor(document, joint, derivation, selectedJointId, expanded));
}

void JointsPanel::refresh()
{
    // A hidden drawer does NO work on a state change - showEvent() refreshes on
    // the way back, so nothing it would have shown is lost by skipping.
    if (!isVisible()) return;
    ++myContentPasses;

    std::vector<std::shared_ptr<RowModel>> models;
    const bool live = myWindow != nullptr && !myWindow->isShowingInitScreen();
    if (live) {
        const DocumentModel& doc = myWindow->document();
        // COPIES, both - see MainWindow::jointDerivations(). Nothing built below
        // holds a reference into either across a later document change.
        const std::vector<DocumentModel::Joint> joints = doc.joints();
        const std::vector<Joinery::Derivation> derivations = myWindow->jointDerivations();
        const std::size_t count = std::min(joints.size(), derivations.size());
        const int selected = myWindow->selectedJointId();
        QSet<int> alive;
        for (std::size_t i = 0; i < count; ++i) {
            alive.insert(joints[i].id);
            models.push_back(contentFor(doc, joints[i], derivations[i], selected,
                                        myExpanded.contains(joints[i].id)));
        }
        myExpanded.intersect(alive);
    } else {
        myExpanded.clear();
    }
    // Broken joints first, each group in document order.
    std::stable_partition(models.begin(), models.end(),
                          [](const std::shared_ptr<RowModel>& m) { return m->broken; });

    const bool enabled =
        live && !myWindow->isSketching() && !myWindow->renderModeEnabled();
    myCount->setText(QString::number(models.size()));

    // Whether a furniture is open is displayed too - the empty state says so -
    // so it is in the signature: the library's empty list and an empty
    // furniture's are both "0 rows" and must not early-out into each other.
    QString signature = (live ? QStringLiteral("L") : QStringLiteral("l")) +
                        QString::number(models.size()) + QLatin1Char('\x1e');
    for (const auto& m : models) signature += signatureOf(*m) + QLatin1Char('\x1e');

    if (myRowsBuilt && signature == myRowSignature) {
        for (const Row& row : myRows)
            if (row.remove) row.remove->setEnabled(enabled);
        return;
    }
    myRowSignature = signature;
    myRowsBuilt = true;
    ++myRowBuilds;

    const int scrolledTo = myRowScroll->verticalScrollBar()->value();
    while (QLayoutItem* item = myRowsLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // hide() is what a caller observes synchronously; the delete waits
            // for the event loop, because this can run from inside a row's own
            // click handler.
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    myRows.clear();
    myEmpty = nullptr;

    // Every position computed HERE, once per change - never per paint. Laid out
    // at the full width first; if the rows overflow the cap, again at the width
    // a scrollbar leaves, with the bar forced on, so a bar appearing can never
    // leave a ruler drawn for a width the row no longer has.
    const int cap = Theme::wholeDevicePixels(kScrollCap);
    double rowWidth = cardWidth() - 2.0;
    int total = 0;
    for (const auto& m : models) {
        layoutRow(*m, rowWidth);
        total += m->height;
    }
    const bool overflow = total > cap;
    if (overflow) {
        rowWidth -= myRowScroll->verticalScrollBar()->sizeHint().width();
        for (const auto& m : models) layoutRow(*m, rowWidth);
    }
    myRowScroll->setVerticalScrollBarPolicy(overflow ? Qt::ScrollBarAlwaysOn
                                                     : Qt::ScrollBarAlwaysOff);

    const QString iconKey = Theme::text().name() + Theme::textDisabled().name();
    if (iconKey != myIconKey || myDeleteIcon.isNull()) {
        myDeleteIcon = IconSet::icon(IconSet::Glyph::Delete);
        myIconKey = iconKey;
    }
    myDeleteCss = deleteCss();
    QWidget* host = myRowScroll->widget();
    for (const auto& m : models) {
        auto* widget = new JointRowWidget(this, m, host);
        widget->setFixedSize(m->rowWidth, m->height);
        auto* remove = new QPushButton(widget);
        remove->setFixedSize(Theme::wholeDevicePixels(QSize(kDeleteSize, kDeleteSize)));
        remove->move(m->deleteAt);
        remove->setIcon(myDeleteIcon);
        remove->setIconSize(QSize(14, 14));
        remove->setToolTip(deleteTooltip());
        remove->setStyleSheet(myDeleteCss);
        remove->setEnabled(enabled);
        remove->setVisible(m->expanded);   // an OPEN row offers it, as mocked
        const int jointId = m->jointId;
        connect(remove, &QPushButton::clicked, this, [this, jointId] {
            // ONE checkpoint with Undo, owned by the window. Nothing after this
            // call may touch the row - the delete rebuilds every one of them.
            if (myWindow) myWindow->deleteJoint(jointId);
        });
        myRowsLayout->addWidget(widget);
        widget->show();
        myRows.push_back({widget, remove, m});
    }

    if (models.empty() && live) {
        myEmpty = new QLabel(emptyText(), host);
        myEmpty->setWordWrap(true);
        myEmpty->setContentsMargins(kHeaderSide, 4, kHeaderSide, 4);
        myEmpty->setStyleSheet(emptyCss());
        myRowsLayout->addWidget(myEmpty);
        myEmpty->show();
    }

    myRowsLayout->invalidate();
    myOuter->invalidate();
    myOuter->activate();
    if (host) {
        const int want = host->sizeHint().height();
        myRowScroll->setMinimumHeight(std::min(want, cap));
    }
    updateGeometry();
    adjustSize();
    myRowScroll->verticalScrollBar()->setValue(scrolledTo);
    update();
}

void JointsPanel::activateRow(int jointId)
{
    if (myExpanded.contains(jointId))
        myExpanded.remove(jointId);
    else
        myExpanded.insert(jointId);
    // Selecting runs updateActions(), whose appStateChanged refreshes this
    // panel; the direct refresh covers a joint that was already selected, and
    // the signature early-out absorbs the second call.
    if (myWindow) myWindow->setSelectedJoint(jointId);
    refresh();
}

const JointsPanel::Row* JointsPanel::rowAt(int index) const
{
    return index >= 0 && index < static_cast<int>(myRows.size()) ? &myRows[index] : nullptr;
}

QString JointsPanel::rowTextAt(int index) const
{
    const Row* row = rowAt(index);
    if (!row) return QString();
    const RowModel& m = *row->model;
    QString text = m.nameA + QStringLiteral(" ↔ ") + m.nameB + QLatin1Char('\n') + m.kindLine;
    if (!m.caveat.isEmpty()) text += QLatin1Char('\n') + m.caveat;
    return text;
}

void JointsPanel::expandRow(int index)
{
    const Row* row = rowAt(index);
    if (!row) return;
    myExpanded.insert(row->model->jointId);
    refresh();
}

void JointsPanel::collapseRow(int index)
{
    const Row* row = rowAt(index);
    if (!row) return;
    myExpanded.remove(row->model->jointId);
    refresh();
}

bool JointsPanel::isExpandedAt(int index) const
{
    const Row* row = rowAt(index);
    return row && row->model->expanded;
}

bool JointsPanel::isSelectedAt(int index) const
{
    const Row* row = rowAt(index);
    return row && row->model->selected;
}

bool JointsPanel::isBrokenAt(int index) const
{
    const Row* row = rowAt(index);
    return row && row->model->broken;
}

int JointsPanel::jointIdAt(int index) const
{
    const Row* row = rowAt(index);
    return row ? row->model->jointId : 0;
}

QString JointsPanel::readoutTextAt(int index) const
{
    const Row* row = rowAt(index);
    return row ? row->model->readoutText : QString();
}

QString JointsPanel::caveatTextAt(int index) const
{
    const Row* row = rowAt(index);
    return row ? row->model->caveat : QString();
}

QStringList JointsPanel::rowAppCopyAt(int index) const
{
    const Row* row = rowAt(index);
    return row ? row->model->appCopy : QStringList();
}

QStringList JointsPanel::paintedStringsAt(int index) const
{
    const Row* row = rowAt(index);
    if (!row) return {};
    std::vector<std::pair<QPointF, QString>> placed;
    for (const RowModel::Painted& t : row->model->painted)
        placed.emplace_back(t.rect.topLeft(), t.text.text());
    for (const RowModel::Strip& strip : row->model->strips) {
        for (const RowModel::Painted& label : strip.labels)
            placed.emplace_back(label.rect.topLeft(), label.text.text());
        if (strip.edgeLine >= 0) placed.emplace_back(strip.edge.rect.topLeft(), strip.edge.text.text());
    }
    std::stable_sort(placed.begin(), placed.end(), [](const auto& a, const auto& b) {
        if (std::fabs(a.first.y() - b.first.y()) > 0.5) return a.first.y() < b.first.y();
        return a.first.x() < b.first.x();
    });
    QStringList strings;
    for (const auto& entry : placed) strings << entry.second;
    return strings;
}

QWidget* JointsPanel::rowWidgetAt(int index) const
{
    const Row* row = rowAt(index);
    return row ? row->widget : nullptr;
}

QPushButton* JointsPanel::deleteButtonAt(int index) const
{
    const Row* row = rowAt(index);
    return row ? row->remove : nullptr;
}

void JointsPanel::ensureRowVisible(int index)
{
    QWidget* widget = rowWidgetAt(index);
    if (widget && myRowScroll) myRowScroll->ensureWidgetVisible(widget, 0, 8);
}

bool JointsPanel::rulerFellBackAt(int index) const
{
    const Row* row = rowAt(index);
    return row && row->model->fellBack;
}

int JointsPanel::stripCountAt(int index) const
{
    const Row* row = rowAt(index);
    return row ? static_cast<int>(row->model->strips.size()) : 0;
}

namespace {
const RowModel::Strip* stripOf(const std::shared_ptr<const RowModel>& model, int strip)
{
    if (!model || strip < 0 || strip >= static_cast<int>(model->strips.size())) return nullptr;
    return &model->strips[static_cast<std::size_t>(strip)];
}
}  // namespace

QRectF JointsPanel::rulerBarAt(int index, int strip) const
{
    const Row* row = rowAt(index);
    const RowModel::Strip* s = row ? stripOf(row->model, strip) : nullptr;
    return s ? s->bar : QRectF();
}

std::vector<double> JointsPanel::tickXAt(int index, int strip) const
{
    const Row* row = rowAt(index);
    const RowModel::Strip* s = row ? stripOf(row->model, strip) : nullptr;
    return s ? s->ticks : std::vector<double>();
}

QRectF JointsPanel::bandAt(int index, int strip) const
{
    const Row* row = rowAt(index);
    const RowModel::Strip* s = row ? stripOf(row->model, strip) : nullptr;
    return s ? s->band : QRectF();
}

std::vector<QRectF> JointsPanel::labelRectsAt(int index, int strip) const
{
    std::vector<QRectF> rects;
    const Row* row = rowAt(index);
    const RowModel::Strip* s = row ? stripOf(row->model, strip) : nullptr;
    if (s)
        for (const RowModel::Painted& label : s->labels) rects.push_back(label.rect);
    return rects;
}

QStringList JointsPanel::labelTextsAt(int index, int strip) const
{
    QStringList texts;
    const Row* row = rowAt(index);
    const RowModel::Strip* s = row ? stripOf(row->model, strip) : nullptr;
    if (s)
        for (const RowModel::Painted& label : s->labels) texts << label.raw;
    return texts;
}

QString JointsPanel::edgeWordAt(int index, int strip) const
{
    const Row* row = rowAt(index);
    const RowModel::Strip* s = row ? stripOf(row->model, strip) : nullptr;
    return s && s->edgeLine >= 0 ? s->edge.raw : QString();
}

QRectF JointsPanel::edgeWordRectAt(int index, int strip) const
{
    const Row* row = rowAt(index);
    const RowModel::Strip* s = row ? stripOf(row->model, strip) : nullptr;
    return s && s->edgeLine >= 0 ? s->edge.rect : QRectF();
}

int JointsPanel::edgeWordLineAt(int index, int strip) const
{
    const Row* row = rowAt(index);
    const RowModel::Strip* s = row ? stripOf(row->model, strip) : nullptr;
    return s ? s->edgeLine : -1;
}

QString JointsPanel::depthTextAt(int index, int strip) const
{
    const Row* row = rowAt(index);
    const RowModel::Strip* s = row ? stripOf(row->model, strip) : nullptr;
    return s ? s->depthText : QString();
}

bool JointsPanel::isStaggeredAt(int index) const
{
    const Row* row = rowAt(index);
    return row && row->model->staggered;
}

QRectF JointsPanel::nameRectAt(int index) const
{
    const Row* row = rowAt(index);
    return row ? row->model->nameARect : QRectF();
}

QRectF JointsPanel::caveatGlyphRectAt(int index) const
{
    const Row* row = rowAt(index);
    return row && row->model->hasCaveatGlyph ? row->model->caveatGlyph : QRectF();
}

bool JointsPanel::emptyStateShown() const
{
    return myEmpty != nullptr && myEmpty->isVisible();
}

QStringList JointsPanel::paintedTexts() const
{
    QStringList texts{tr("Joints"), emptyText(), deleteTooltip()};
    for (const Row& row : myRows) texts << row.model->appCopy;
    return texts;
}

QStringList JointsPanel::paintedNames() const
{
    QStringList names;
    for (const Row& row : myRows) names << row.model->nameA << row.model->nameB;
    return names;
}

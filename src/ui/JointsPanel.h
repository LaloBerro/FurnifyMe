#pragma once
//
// The joints drawer (joinery, Task 12): every planned wood joint in the
// furniture, one row each, and - opened in place - the mark-out numbers a
// woodworker transfers to the wood with a pencil and a square.
//
// The user's pick from the drawer mockup round ("B - the row opens in place,
// with a ruler per piece") is the contract:
//
//   - a row opens IN PLACE on a click and closes on a second one;
//   - an open row shows each piece's name with `drill <depth>` beside it and a
//     TO-SCALE ruler: the named reference edge at zero, the joint's run as the
//     ruler's length, one tick per item with its distance printed ABOVE it, and
//     one shared line under the pieces - `mm from the front edge · inset 9 mm
//     from the face`;
//   - housings and interlocks draw the same ruler with a shaded BAND over the
//     item's span instead of ticks, labelled at both ends, and `<width> ×
//     <depth>` where a fastener says `drill <depth>`;
//   - a BROKEN joint sorts to the top, paints its names in Theme::danger() and
//     shows its reason with no ruler and no numbers - a stale measurement is
//     worse than none;
//   - a board angled so no single edge applies has no zero point, and its row
//     writes its numbers instead of drawing them, under the sentence saying
//     why; so does a row whose tick labels cannot be kept apart even on two
//     staggered lines;
//   - the region-shortfall caveat sits in the row with a Theme::caution() glyph;
//   - deleting a joint from its row is ONE checkpoint with Undo - a joint is
//     document content, so it takes MainWindow::deleteJoint(), never the
//     two-click confirm VersionsPanel keeps for file data.
//
// VersionsPanel's shape (a paintSurface() card parented to the viewport,
// anchored ViewportOverlay::Anchor::TopLeft, rows in a transparent scroll area,
// visibility derived from its View action alone) with one difference that is
// a budget rather than a style: the viewport is a QOpenGLWidget, so ONE dirty
// overlay repaints EVERY visible overlay on every orbit step (CLAUDE.md, "One
// dirty card repaints all of them"). Everything a row paints - every string as
// a prepared QStaticText, every tick, label, band and rectangle - is computed
// in refresh() when the row's signature changes and handed to the row as an
// immutable model. paintEvent() draws plain lines, rects and prepared text and
// does no measuring, no layout and no lookup.
//
// And it is a per-GESTURE budget too (CLAUDE.md's rule on cheap-looking work
// moved onto a hot path): a HIDDEN drawer does no work on appStateChanged or
// on a Theme broadcast - showEvent() catches it up - and a colour edit, which
// an Appearance drag broadcasts per mouse move, repaints and rebuilds nothing.
// OCCT first, by the Handle() rule - DocumentModel.h carries Joinery.h.
#include "DocumentModel.h"

#include <QIcon>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <memory>
#include <vector>

class MainWindow;
class OcctViewWidget;
class QLabel;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QVBoxLayout;

class JointsPanel : public QWidget {
    Q_OBJECT

public:
    JointsPanel(MainWindow* window, OcctViewWidget* view, QWidget* parent = nullptr);
    ~JointsPanel() override;

    // Re-reads MainWindow::document().joints() and MainWindow::jointDerivations()
    // (a COPY - nothing here holds a reference across a document change) and
    // rebuilds the rows only when their signature moved. The signature is built
    // from EVERYTHING a row displays - kind, both names, broken state and
    // reason, every readout number, the run, the caveat, the unit, and the
    // row's own expanded and selected state - plus whether a furniture is open
    // at all (the empty state), because a field a row shows but the early-out
    // does not compare is a field that stops updating. A no-op while hidden:
    // showEvent() refreshes, so an opened drawer is always current.
    void refresh();

    int rowCount() const { return static_cast<int>(myRows.size()); }
    // What the closed row says: "<A> ↔ <B>", the kind line (or the reason when
    // broken) and the caveat, newline-separated. Names included - this is the
    // row as read, not a sweep channel (see paintedTexts()/paintedNames()).
    QString rowTextAt(int index) const;
    // Opens the row in place. Idempotent - unlike a click, which toggles and
    // also selects the joint.
    void expandRow(int index);
    // Closes it again. Idempotent, and selects nothing.
    void collapseRow(int index);
    bool isExpandedAt(int index) const;
    bool isSelectedAt(int index) const;
    bool isBrokenAt(int index) const;
    int jointIdAt(int index) const;
    // Every string the row's mark-out section carries - depths, tick and band
    // labels, the written numbers, the shared line - WITHOUT the pieces' names.
    // Empty for a broken joint: it carries no numbers at all. Computed whether
    // or not the row is open, so an empty answer is the contract, not a closed
    // row.
    QString readoutTextAt(int index) const;
    QString caveatTextAt(int index) const;
    // This app's own copy on the row, names excluded - what a broken row must
    // carry no digit in.
    QStringList rowAppCopyAt(int index) const;
    // Every string the row PAINTS, exactly as prepared - non-breaking spaces
    // and all - in reading order (top to bottom, then left to right). Names
    // included. The raw strings the sweep and readoutTextAt() read keep plain
    // spaces; this is what actually reaches the QStaticText.
    QStringList paintedStringsAt(int index) const;

    QWidget* rowWidgetAt(int index) const;
    // The row's delete control. Shown on an OPEN row, as mocked.
    QPushButton* deleteButtonAt(int index) const;
    void ensureRowVisible(int index);

    // --- the ruler, for the suite: every rectangle in rowWidgetAt()'s own
    // coordinates, exactly what the row paints ---------------------------------
    // True when the open row WRITES its numbers rather than drawing a ruler:
    // no single edge to put at zero, or labels that collide even staggered.
    bool rulerFellBackAt(int index) const;
    // How many ruler strips the open row draws - one per piece that is cut
    // (a housing's housed piece is not). 0 when it fell back or is broken.
    int stripCountAt(int index) const;
    QRectF rulerBarAt(int index, int strip) const;
    std::vector<double> tickXAt(int index, int strip) const;
    // The shaded band of a housing or interlock; a null rect for a fastener.
    QRectF bandAt(int index, int strip) const;
    // The TICK labels, left to right - never the edge word, which sits apart.
    std::vector<QRectF> labelRectsAt(int index, int strip) const;
    QStringList labelTextsAt(int index, int strip) const;
    // The edge word at zero: its text and rect when the ruler found it a clear
    // place, and its line - 0 above the bar, 1 below it, -1 left off (the shared
    // line already names the edge).
    QString edgeWordAt(int index, int strip) const;
    QRectF edgeWordRectAt(int index, int strip) const;
    int edgeWordLineAt(int index, int strip) const;
    QString depthTextAt(int index, int strip) const;
    // True when a TICK label had to take the line below the bar.
    bool isStaggeredAt(int index) const;
    // Where the closed row's first name is painted - the danger-ink probe.
    QRectF nameRectAt(int index) const;
    // The caution glyph beside a caveat; a null rect when the row has none.
    QRectF caveatGlyphRectAt(int index) const;
    // The "No joints yet" state is on screen.
    bool emptyStateShown() const;

    // How many times refresh() has actually read the document, and how many
    // times it rebuilt the rows - the clock-free pins for "a hidden drawer does
    // no work" and "a colour edit rebuilds nothing".
    int contentPassCount() const { return myContentPasses; }
    int rowBuildCount() const { return myRowBuilds; }

    // A row's early-out signature, from the same content refresh() compares.
    // A documented TEST SEAM for the one field no gesture can move alone: a
    // contact becoming non-rectangular (the caveat) while its joint survives -
    // a boolean replaces both bodies and the joint dies with them. The suite
    // hands in a derivation whose region falls short and asserts the signature
    // moves; the other fields are pinned through the real paths.
    static QString rowSignature(const DocumentModel& document, const DocumentModel::Joint& joint,
                                const Joinery::Derivation& derivation, int selectedJointId,
                                bool expanded);

    // Every string THIS APP wrote that the drawer paints: the title, the empty
    // state, the delete tooltip, and each row's kind line, reason, caveat,
    // depth texts, ruler labels, written numbers and shared line. NEVER a
    // piece's name - see paintedNames().
    QStringList paintedTexts() const;
    // The user's own words the drawer paints: the pieces' names. The
    // vocabulary law governs this app's copy, never the user's, so the suite
    // exempts exactly this channel and sweeps the other one plainly - the
    // exemption lives at the surface that knows which substring is whose.
    QStringList paintedNames() const;

    // A row's click: toggles it open or closed and selects its joint through
    // MainWindow::setSelectedJoint(). Called by the row widget.
    void activateRow(int jointId);

    struct RowModel;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    QSize sizeHint() const override;

private:
    void applyTheme();
    static int cardWidth();

    struct Row {
        QWidget* widget = nullptr;
        QPushButton* remove = nullptr;
        std::shared_ptr<const RowModel> model;
    };
    const Row* rowAt(int index) const;

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;
    QLabel* myTitle = nullptr;
    QLabel* myCount = nullptr;
    QLabel* myEmpty = nullptr;
    QVBoxLayout* myOuter = nullptr;
    QVBoxLayout* myRowsLayout = nullptr;
    QScrollArea* myRowScroll = nullptr;
    std::vector<Row> myRows;
    // Open rows, by JOINT id - so a row stays open across the rebuild a
    // document change causes, and the sort that moves a broken joint to the
    // top cannot open a different one.
    QSet<int> myExpanded;
    QString myRowSignature;
    bool myRowsBuilt = false;

    // A Theme broadcast that arrived while hidden, settled on the next show.
    bool myThemePending = false;
    // What the prepared rows were laid out against - the four fonts and the
    // card width. A broadcast that leaves this unchanged changed only colours,
    // which every row reads at paint time.
    QString myLayoutKey;
    QString myDeleteCss;
    QString myIconKey;
    QIcon myDeleteIcon;
    int myContentPasses = 0;
    int myRowBuilds = 0;
};

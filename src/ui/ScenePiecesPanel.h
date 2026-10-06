#pragma once
//
// THE PIECES IN A SCENE, listed. ItemsPanel's shape at a fraction of its size:
// one row per piece, each with the piece's name and an eye.
//
// What it deliberately is NOT is ItemsPanel with a flag. That drawer carries
// folders, outlines, bodies, drag-and-drop, multi-select, a grain mark and a
// right-click menu of six entries, every one of which is about the inside of
// ONE furniture. A scene has none of those: it has a flat list of whole
// furniture standing somewhere. Reusing the drawer would have meant a second
// meaning for every row kind it already has, which is the failure CLAUDE.md
// records for a ten-thousand-line class gaining a second document.
//
// It knows nothing about SceneModel. Rows are pushed in and gestures are
// reported out, exactly as RenderSettingsPanel mirrors enums it never sees -
// so this class stays a view and SceneWindow stays the one place a scene is
// changed.
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QAction;
class QLabel;
class QMouseEvent;
class QScrollArea;
class QPushButton;
class QVBoxLayout;

class ScenePiecesPanel : public QWidget {
    Q_OBJECT

public:
    // One row's worth of what this panel draws. `reason` is non-empty when the
    // piece's furniture could not be resolved - deleted, or unreadable - and
    // it is printed WHERE THE NAME GOES rather than beside it, because a row
    // that reads like an ordinary piece and renders nothing is worse than one
    // that visibly cannot be drawn.
    struct Row {
        int pieceId = 0;
        QString name;
        bool visible = true;
        QString reason;
    };

    // The remove control's tooltip, written once - see the .cpp for why.
    static QString removeTipText();
    // What the ARMED remove control reads, on the control itself, and its
    // tooltip. The armed state used to be a typed "!" that explained itself
    // only on hover; it says what the next click does now.
    static QString armedRemoveText();
    static QString armedRemoveTipText();
    // The line an empty card shows above its Add piece row. One author, for
    // the paint and the banned-word sweep alike.
    static QString emptyText();

    explicit ScenePiecesPanel(QWidget* parent = nullptr);

    // THE ON-SCREEN WAY TO ADD A PIECE (the 2026-10 review's F2). The empty
    // card used to say "add a furniture" over nothing to click, and the only
    // route was a menu entry under File with a different verb. This builds a
    // full-width labelled ToolChip from the window's own Add piece action, at
    // the foot of the card, present whether the list is empty or not - and,
    // being a ToolChip, it triggers that action, follows its enabled state and
    // holds no state of its own. Called once, by the window.
    void setAddAction(QAction* action);
    class ToolChip* addChip() const { return myAddChip; }

    // THE SCROLL CUE (F10). The list scrolls by wheel with no bar drawn, so a
    // long one used to cut its last row in half and say nothing. A fade at the
    // list's bottom edge stands while rows are below, and one at the top while
    // rows are above - derived from the scroll position on every move, never
    // remembered.
    bool topFadeShown() const;
    bool bottomFadeShown() const;
    QWidget* topFade() const { return myTopFade; }
    QWidget* bottomFade() const { return myBottomFade; }
    QScrollArea* rowsScrollArea() const { return myScroll; }

    // Rebuilt only when the rows would actually LOOK different. This is driven
    // from the host's appStateChanged, which fires on every selection click,
    // and rebuilding a column of widgets at that rate changes nothing anybody
    // can see - ItemsPanel::refresh()'s own signature guard, in the smallest
    // shape this list needs.
    void setRows(const QVector<Row>& rows);
    void setSelected(int pieceId);

    int rowCount() const { return static_cast<int>(myRows.size()); }
    int rowIdAt(int index) const;
    QWidget* rowWidgetAt(int index) const;
    QString rowTextAt(int index) const;
    QPushButton* rowEyeAt(int index) const;
    QPushButton* rowRemoveAt(int index) const;

    // Everything this panel paints, for the banned-word sweep. The piece NAMES
    // are the user's own words and are swept as user data; this list carries
    // only the copy this app wrote.
    QStringList paintedTexts() const;

    // Its own height, capped so a busy scene cannot run the card off the
    // bottom of the viewport. Added up FROM THE PARTS and never asked of the
    // scroll area, which does not answer for its widget - the trap this
    // project has now paid for in two other cards.
    QSize sizeHint() const override;

signals:
    void selectionRequested(int pieceId);
    // A piece carries its own name, independent of the furniture it points
    // at and independent of any other piece pointing at the same one.
    void renameCommitted(int pieceId, const QString& name);
    void visibilityToggled(int pieceId, bool visible);
    void removeRequested(int pieceId);

protected:
    void paintEvent(QPaintEvent* event) override;
    // A press on a row SELECTS that piece. selectionRequested was declared
    // from the start and never emitted, so this list could show the selection
    // and not set it - the viewport was the only way to pick a piece, which is
    // exactly backwards for a list whose rows are the handles.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    // Re-places the two fades whenever the scroll area moves or resizes.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void applyTheme();
    // Places both fades over the scroll area's edges and shows each one only
    // while there are rows past that edge.
    void syncFades();

    struct BuiltRow {
        Row data;
        QWidget* widget = nullptr;
        QLabel* name = nullptr;
        QPushButton* eye = nullptr;
        QPushButton* remove = nullptr;
    };

    QVector<Row> myWanted;
    QVector<BuiltRow> myRows;
    QString mySignature;
    // Whether rebuild() has ever run. Without it the early-out needs two
    // clauses to tell "no rows yet" from "no rows, and that is already what
    // is on screen" - correct, but a reader has to work it out.
    bool myBuilt = false;
    QVBoxLayout* myColumn = nullptr;
    QScrollArea* myScroll = nullptr;
    QWidget* myRowsHost = nullptr;
    QVBoxLayout* myRowsColumn = nullptr;
    QLabel* myTitle = nullptr;
    QLabel* myEmpty = nullptr;
    class ToolChip* myAddChip = nullptr;
    QWidget* myTopFade = nullptr;
    QWidget* myBottomFade = nullptr;
    int mySelected = 0;
    // Which row's remove control is ARMED. The deletion asks once first -
    // VersionsPanel's own two-click confirm. It was written when a scene had
    // no undo at all; it has one now, and a removal reports a Note with Undo,
    // but the second click stays: a piece is a whole furniture, and the
    // control sits one row's width from the eye.
    int myArmedRemove = 0;
};

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

    explicit ScenePiecesPanel(QWidget* parent = nullptr);

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
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void rebuild();
    void applyTheme();

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
    int mySelected = 0;
    // Which row's remove control is ARMED. A scene has no undo at all, so a
    // deletion asks once first - VersionsPanel's own two-click confirm, which
    // is this app's answer wherever a change cannot be taken back.
    int myArmedRemove = 0;
};

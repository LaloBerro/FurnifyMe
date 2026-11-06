#pragma once

// The scene window's two cards for the chrome page (migration Phase 5b): the
// Pieces card (`pieces`) and the Add piece card (`addPiece`). Each holds what
// its card shows and the little state a card owns (which Remove is armed,
// whether the add card stands), in the Qt cards' own words, and sends every
// request as the SAME signal the Qt card sends - SceneWindow wires both cards
// to one handler each. No WebEngine here: both presets, tested offscreen
// (scene_cards_tests).

#include "FurnitureStore.h"
#include "ScenePiecesPanel.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QVector>

class QAction;

class ScenePiecesModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
public:
    using QObject::QObject;
    // {title, empty, add: {text, enabled}, words: {removeTip, armed, armedTip,
    //  eyeTip}, rows: [{id, name, visible, reason, selected, armed}]}.
    QVariantMap state() const;

    void setRows(const QVector<ScenePiecesPanel::Row>& rows);
    void setSelected(int pieceId);
    void setAddAction(QAction* action);

signals:
    // Carries the state WITH it - a property's NOTIFY arrives a batch late.
    void stateChanged(const QVariantMap& state);
    void selectionRequested(int pieceId);
    void renameCommitted(int pieceId, const QString& name);
    void visibilityToggled(int pieceId, bool visible);
    void removeRequested(int pieceId);

public slots:
    void select(int pieceId);
    void rename(int pieceId, const QString& name);
    void toggleVisible(int pieceId, bool visible);
    // Two clicks, as the Qt card: the first arms (and disarms any other row),
    // the second on the armed row takes the piece out.
    void remove(int pieceId);
    void disarm();
    void addPiece();

private:
    bool hasRow(int pieceId) const;
    void announce();

    QVector<ScenePiecesPanel::Row> myRows;
    int mySelected = 0;
    int myArmed = 0;
    QPointer<QAction> myAdd;
    // The state last sent: the scene refreshes often, and an unchanged card
    // is not announced again.
    QVariantMap myLast;
};

class AddPieceModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
public:
    using QObject::QObject;
    // {shown, seed, title, empty, library, keyHint, entries: [{id, name}]}.
    QVariantMap state() const;

    void showFor(const QVector<FurnitureStore::FurnitureInfo>& furniture);
    void setLibraryAction(QAction* action);
    bool isShown() const { return myShown; }

signals:
    void stateChanged(const QVariantMap& state);
    void chosen(const QString& furnitureId);

public slots:
    void choose(const QString& furnitureId);
    void goToLibrary();
    void close();

private:
    void announce();

    bool myShown = false;
    int mySeed = 0;
    QVector<FurnitureStore::FurnitureInfo> myEntries;
    QPointer<QAction> myLibrary;
    QVariantMap myLast;
};

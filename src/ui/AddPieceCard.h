#pragma once
//
// WHICH FURNITURE GOES INTO THIS SCENE. A card over the scene's viewport
// listing the library, one row per furniture; picking one reports it and
// closes.
//
// It is NOT a dialog, and that is this app's own law rather than a style
// preference: nothing here blocks, because nothing it has to say requires an
// answer before the user may do anything else. So it is a plain widget over
// the viewport with a key claim while it is up, exactly as UnsavedCloseCard
// and NameFurnitureCard are.
//
// It knows nothing about SceneModel and nothing about the store. It is handed
// a list and it reports an id, so the one place a scene actually changes stays
// SceneWindow - the same arrangement ScenePiecesPanel keeps.
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "FurnitureStore.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class AddPieceCard : public QWidget {
    Q_OBJECT

public:
    explicit AddPieceCard(QWidget* parent = nullptr);

    // Shows the card over whatever the library currently holds. Re-read on
    // every open rather than cached at construction: a furniture made since
    // this window opened is one the user expects to find here.
    void showFor(const QVector<FurnitureStore::FurnitureInfo>& furniture);

    int entryCount() const { return static_cast<int>(myRows.size()); }
    QString entryIdAt(int index) const;
    QPushButton* entryButtonAt(int index) const;

    // This app's own copy only - a furniture's NAME is the owner's word
    // choice and is swept as user data by the caller, never by this list.
    QStringList paintedTexts() const;

signals:
    void chosen(const QString& furnitureId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuild(const QVector<FurnitureStore::FurnitureInfo>& furniture);
    void applyTheme();

    struct Row {
        QString id;
        QPushButton* button = nullptr;
    };

    QVector<Row> myRows;
    QVBoxLayout* myColumn = nullptr;
    QLabel* myTitle = nullptr;
    QLabel* myEmpty = nullptr;
    QScrollArea* myScroll = nullptr;
    QWidget* myList = nullptr;
    QVBoxLayout* myListColumn = nullptr;
};

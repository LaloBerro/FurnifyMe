#pragma once

// The library hub's logic (migration Phase 2), with no window: what the hub
// lists, in which order, what a search keeps, what creating, renaming and
// deleting do and what they say. The HTML hub's page calls the slots below
// through the web channel and renders the lists; SelectorWindow (the Qt hub,
// kept until Phase 6) still carries its own copy of the same rules, which is
// the specification this file was ported from - sentence for sentence.
//
// QtCore + QtGui only (furnify_session); headless-tested by hub_model_tests.

#include "FurnitureStore.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <vector>

struct HubEntry
{
    QString id;
    QString name;
    QString thumbPath;
    qint64 thumbStamp = 0;   // thumb.png's last write, ms; 0 when there is none
    QDateTime lastEdited;
    bool scene = false;
};

class HubModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList furniture READ furnitureList NOTIFY changed)
    Q_PROPERTY(QVariantList scenes READ sceneList NOTIFY changed)

public:
    explicit HubModel(FurnitureStore& store, QObject* parent = nullptr);

    // (The order and the search are the PAGE's - it owns that view state, as
    // the Qt hub did; web/src/hub/order.ts. This class lists the store's own
    // enumeration and does the work.)
    // How old a save is, in the terms somebody thinks in - today and
    // yesterday by name with the time, inside a week the weekday, beyond that
    // the date, with the year only once it is not this one.
    static QString editedText(const QDateTime& when, const QDateTime& now);
    static QString deletedNoteText(const QString& name, int scenes);

    // Per entry: id, name, thumbUrl (a file URL carrying the picture's stamp,
    // so a rewritten picture is never shown from the page's cache; empty when
    // there is none), editedText, edited (ms since epoch, 0 never saved),
    // initial (the placeholder's letter).
    QVariantList furnitureList() const;
    QVariantList sceneList() const;

public slots:
    void refresh();
    QString suggestedName() const;
    // The names of the scenes that would lose this furniture - asked when a
    // Delete arms, never per paint: it reads every scene file.
    QStringList usedIn(const QString& furnitureId) const;
    void createFurniture(const QString& name);
    void createScene();
    void rename(const QString& id, bool scene, const QString& name);
    void remove(const QString& id, bool scene);
    void choose(const QString& id, bool scene);

signals:
    void changed();
    void furnitureChosen(const QString& id);
    void sceneChosen(const QString& id);
    void createRequested();
    void createSceneRequested();
    // A refusal, in the hub's own words - the page shows it in its banner.
    void failure(const QString& text);
    // A success report - the page shows it as a Note toast.
    void note(const QString& text);

private:
    QVariantList listOf(const std::vector<HubEntry>& entries) const;

    FurnitureStore& myStore;
    std::vector<HubEntry> myFurniture;
    std::vector<HubEntry> myScenes;
};

#pragma once

// THE VERSIONS DRAWER'S MODEL (migration Phase 3c): what the drawer lists
// for the open furniture, the name a new version is offered, the drawer's
// words, and the four things a row can ask for. The HTML card calls the
// slots below through the web channel; VersionsPanel (the Qt drawer, kept
// until Phase 6) carries its own copy of the same rules, which is the
// specification this file was ported from.
//
// Saving, restoring, comparing and deleting stay MainWindow's: they reach it
// through Hooks, so this class needs no window. A gate is the model's as well
// as the page's - a request arriving while it is shut asks nothing.
//
// QtCore + QtGui only (furnify_session); headless-tested by versions_model_tests.

#include "FurnitureStore.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class VersionsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed)
    Q_PROPERTY(bool rowsEnabled READ rowsEnabled NOTIFY changed)
    Q_PROPERTY(QVariantMap words READ words CONSTANT)

public:
    struct Hooks
    {
        std::function<bool(const QString&)> save;
        std::function<bool(const QString&)> restore;
        std::function<bool(const QString&)> compare;
        std::function<bool(const QString&)> remove;
        std::function<void()> close;
    };
    // VersionsPanel::kDeleteConfirmMs - the second click's window.
    static constexpr int kDeleteConfirmMs = 4000;

    VersionsModel(FurnitureStore& store, Hooks hooks, QObject* parent = nullptr);

    // Per version, in the store's order: name, savedText (the Qt drawer's
    // short local date), saved (ms since epoch), thumbUrl (a file URL with
    // the picture's stamp; empty when there is none).
    QVariantList versions() const { return myVersions; }
    bool canSave() const { return myCanSave; }
    bool rowsEnabled() const { return myRowsEnabled; }
    QVariantMap words() const;

    // The open furniture ('' at the library) and the two gates MainWindow
    // derives (canOpenSaveVersion(), and not at the library nor sketching),
    // and whether the drawer is shown. Re-reads the store while it is; says
    // changed() only when something moved.
    void setState(const QString& furnitureId, bool canSave, bool rowsEnabled, bool shown = true);
    // File -> Save version...: the card opens its pending version.
    void requestNew();

public slots:
    void refresh();
    QString defaultName() const;
    bool save(const QString& name);
    bool restore(const QString& name);
    bool compare(const QString& name);
    bool remove(const QString& name);
    void close();

signals:
    void changed();
    void newRequested();

private:
    QVariantList readList() const;

    FurnitureStore& myStore;
    Hooks myHooks;
    QString myFurnitureId;
    bool myCanSave = false;
    bool myRowsEnabled = false;
    // Whether the drawer is on screen: a hidden one reads nothing and saves
    // nothing (review I1, C2).
    bool myShown = true;
    QVariantList myVersions;
};

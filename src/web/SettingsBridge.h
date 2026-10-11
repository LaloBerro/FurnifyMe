#pragma once

// THE SETTINGS DRAWER FOR A PAGE (migration Phase 3b): the editor chrome
// page's `settings` object. It describes the drawer - tabs, number fields,
// colour tokens with their names and group headings, the font list, the rows
// MainWindow composes, and the drawer's own words - once, as `layout`; it
// carries the live Theme::Spec as `spec`, re-sent on every Theme broadcast;
// and it carries the rows' actions as `actions`, through an ActionWatch, so
// the page mirrors them exactly as the Qt drawer's chips do.
//
// Every edit lands in SettingsEdits - the same functions the Qt drawer calls
// - so a value cannot mean two things depending on which drawer set it. A
// trigger obeys the registry's rule (a disabled action refuses) and one more:
// the page may trigger only the actions its rows name, plus any id passed to
// allowAction() (the Settings action, for the card's own close x).
//
// No WebEngine here: a QObject over QtCore types, compiled and tested in
// both presets.

#include "SettingsEdits.h"

#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantMap>

#include <vector>

class ActionRegistry;
class ActionWatch;
class QWidget;

class SettingsBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap layout READ layout CONSTANT)
    Q_PROPERTY(QVariantMap spec READ spec NOTIFY specChanged)
    Q_PROPERTY(QJsonArray actions READ actions NOTIFY actionsChanged)
public:
    SettingsBridge(ActionRegistry* registry, const std::vector<SettingsEdits::Row>& rows,
                   QObject* parent = nullptr);

    QVariantMap layout() const { return myLayout; }
    QVariantMap spec() const;
    QJsonArray actions() const;
    // An action outside the rows the page may trigger too.
    void allowAction(const QString& id);
    // What the colour picker and the file dialogs stand over.
    void setDialogParent(QWidget* parent) { myDialogParent = parent; }

public slots:
    void setNumber(const QString& id, double value);
    void setColour(const QString& id, const QString& hex);
    void setFontFamily(const QString& family);
    void pickColour(const QString& id);
    void reset();
    // Native file dialogs, then SettingsEdits - the Qt drawer's two buttons.
    void saveColours();
    void loadColours();
    bool trigger(const QString& actionId);

signals:
    void specChanged();
    void actionsChanged();
    // MainWindow says these, with the sentences the Qt drawer's signals get.
    void colourSaveFailed(const QString& path);
    void colourLoadRefused(const QString& path);

private:
    ActionRegistry* myRegistry = nullptr;
    ActionWatch* myWatch = nullptr;
    QVariantMap myLayout;
    QSet<QString> myAllowed;
    QPointer<QWidget> myDialogParent;
};

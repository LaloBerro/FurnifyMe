#pragma once

// THE RENDER SETTINGS PANEL FOR A PAGE (migration Phase 3e): the render page's
// `render` object. The Qt RenderSettingsPanel stays the state and the one edit
// path (the plan's R1): this reads every row off it and sends every edit to
// the panel's own edit slots, so RenderStudio's wiring and each window's
// listeners never learn a second panel exists, and a value cannot mean two
// things depending on which panel set it.
//
//   layout - the words (RenderSettingsCopy, through the panel's tr context),
//            each dial's range, and the wood tiles with their thumbnails as
//            PNG data URLs made from the pixmaps the panel decoded once - the
//            page never decodes a texture. Re-announced when tiles are added.
//   state  - every current value, the dials' readouts as the panel formats
//            them, the shot names, the tier line, the shutter's words and
//            whether Save Screenshot is enabled.
//
// No WebEngine here: a QObject over the panel, compiled and tested in both
// presets.

#include <QObject>
#include <QPointer>
#include <QVariantMap>

class RenderSettingsPanel;

class RenderBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap layout READ layout NOTIFY layoutChanged)
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
public:
    explicit RenderBridge(RenderSettingsPanel* panel, QObject* parent = nullptr);

    QVariantMap layout() const { return myLayout; }
    QVariantMap state() const;
    // The page has said it is listening (ready()).
    bool isReady() const { return myReady; }

public slots:
    // Each by the panel's own rules - a current choice says nothing, an index
    // with no chip or row is refused.
    void aspect(int index);
    void guides(int index);
    void exportSize(int index);
    void quality(int index);
    void cutout();
    // In SLIDER units: degrees for lightAngle and fov, percent for
    // lightStrength. An id that names no dial is refused.
    void setDial(const QString& id, int value);
    void wood(const QString& name);
    void editMaterial(const QString& name);
    void saveShot();
    void applyShot(int index);
    void removeShot(int index);
    void pickBackground();
    void resetBackground();
    // Save Screenshot, only while it is enabled - the shutter's own rule.
    void shoot();
    // The page has every signal connected.
    void ready();

signals:
    void layoutChanged();
    void stateChanged();
    void readyChanged(bool ready);

private:
    void rebuildLayout();

    QPointer<RenderSettingsPanel> myPanel;
    QVariantMap myLayout;
    bool myReady = false;
};

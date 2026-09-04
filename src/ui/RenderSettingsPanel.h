#pragma once
// The render-mode-only settings card and its camera shutter (Task 7.2) -
// Option A from the picked mockup ("one floating card"): a single card at
// the viewport's right edge, in AppearancePanel's own visual language
// (paintSurface() family, same radius/pad, Theme tokens throughout), plus a
// separate round shutter control at the bottom-right corner.
//
// Both classes are pure VIEWS, exactly as AppearancePanel is a view of
// Theme::spec() - but there is no Theme-shaped global these six render
// settings live in, so where AppearancePanel writes straight into Theme and
// lets its own broadcast repaint the swatches, this card SIGNALS every edit
// and holds nothing but what a QSlider/the swatch button need to paint.
// MainWindow is the one listener: it reaches OcctViewWidget's six matching
// setters and QSettings, so this file knows about neither. setValuesSilently()
// is the return path - MainWindow calls it once at construction (from
// whatever QSettings/the viewport's own defaults already say) and again
// whenever a value moves for a reason that did not originate here (a
// persisted value loaded, an Appearance edit moving the background swatch's
// own derived default while no override is set) - AppearancePanel's
// applyTheme() shape, pushed IN rather than pulled from a singleton.
//
// Anchored TopRight by MainWindow::buildOverlay(), the exact slot the axis
// gizmo card and AppearancePanel already occupy - safe because all three
// are mutually exclusive by construction: the gizmo and AppearancePanel
// both hide unconditionally while render mode is on (CLAUDE.md's "the
// viewport is the furniture alone" reaches them too), and this card is
// visible ONLY while render mode is on, so no two of them are ever anchored
// there at once. Visibility is DERIVED off MainWindow's own myRenderModeOn
// on every appStateChanged, the sibling-visibility law every other overlay
// card in this shell already follows - never a one-shot show()/hide() at
// the toggle site.
//
// The card carries WA_NoMousePropagation, AppearancePanel's own law: a
// press or release that reached the viewport underneath would either
// re-pick whatever is there or - worse, here - trigger the render-mode
// exit gesture a plain viewport press performs. A click anywhere on this
// card, or on the shutter below, must never exit render mode; only a press
// that actually lands on the bare viewport does. Pinned by gui_smoke.
#include <QAbstractButton>
#include <QColor>
#include <QStringList>
#include <QWidget>

#include <vector>

class QAction;
class QColorDialog;
class QLabel;
class QSlider;

class RenderSettingsPanel : public QWidget {
    Q_OBJECT

public:
    explicit RenderSettingsPanel(QWidget* parent = nullptr);

    // Six edit paths, one per control - each is BOTH what a user drives
    // (the matching QSlider/the swatch's colour dialog land here) and what
    // the suite drives, AppearancePanel's own "what a test drives is what a
    // user drives" rule. Each clamps into its own control's range and emits
    // the matching signal below; it does NOT talk to OcctViewWidget or
    // QSettings directly - MainWindow is the one listener for both.
    //
    // `glossiness01` is the SLIDER's own convention - 0 matte, 1 glossy,
    // dragging RIGHT reads as glossier, the mockup's own requirement - and
    // is the inverse of the kernel-facing "roughness" OcctViewWidget deals
    // in (a physically-based material's roughness, not a UI slider's own
    // sense of "how glossy does this look"). The inversion is MainWindow's,
    // at the one wiring site that turns this panel's signal into
    // OcctViewWidget::setRenderSurfaceRoughness(1.0 - glossiness01) - this
    // class only ever speaks the mockup's own word, "glossy".
    void setSurfaceGlossiness(double glossiness01);
    void setMetal(double metallic01);
    // Azimuth in degrees, wrapped rather than clamped - a compass heading,
    // not a bounded quantity.
    void setLightAngle(double azimuthDeg);
    // A multiplier - see the slider's own range in the .cpp for the legal
    // band.
    void setLightStrength(double multiplier);
    void setBackground(const QColor& colour);
    void setFov(double fovyDeg);

    // Re-reads every control WITHOUT emitting a signal - see the header's
    // own note on why this exists and AppearancePanel::applyTheme() for the
    // shape it follows. `glossiness01` is this panel's own slider
    // convention, exactly as setSurfaceGlossiness()'s own parameter is -
    // MainWindow converts OcctViewWidget's roughness into it before calling
    // this, the same inversion the other direction performs.
    void setValuesSilently(double glossiness01, double metallic01, double lightAngleDeg,
                            double lightStrength, const QColor& background, double fovyDeg);

    double surfaceGlossiness() const;
    double metal() const;
    double lightAngle() const;
    double lightStrength() const;
    QColor background() const { return myBackground; }
    double fov() const;

    // Every string this card paints - the title, the six row labels - swept
    // by gui_smoke's vocabulary check on AppearancePanel::paintedTexts()'s
    // own terms.
    QStringList paintedTexts() const;

    // The six controls, for childAt() hit tests and direct suite drives -
    // AppearancePanel's own sizeControl()/swatchFor() shape.
    QSlider* surfaceControl() const { return mySurfaceSlider; }
    QSlider* metalControl() const { return myMetalSlider; }
    QSlider* lightAngleControl() const { return myLightAngleSlider; }
    QSlider* lightStrengthControl() const { return myLightStrengthSlider; }
    QWidget* backgroundSwatch() const;
    QSlider* fovControl() const { return myFovSlider; }
    // The live modeless picker, or null when none is open -
    // AppearancePanel::activeColourDialog()'s own reason to exist: the
    // suite can assert it is modeless and close it, so the "no QDialog
    // after an outcome" checks elsewhere in the suite stay meaningful.
    QColorDialog* activeColourDialog() const { return myDialog; }

signals:
    void surfaceGlossinessChanged(double glossiness01);
    void metalChanged(double metallic01);
    void lightAngleChanged(double azimuthDeg);
    void lightStrengthChanged(double multiplier);
    void backgroundChanged(const QColor& colour);
    void fovChanged(double fovyDeg);

protected:
    void paintEvent(QPaintEvent* event) override;
    // AppearancePanel's own reason: an unhandled wheel over a slider row
    // would propagate to the viewport underneath and zoom the camera - the
    // wheel-shaped half of WA_NoMousePropagation, which only covers presses
    // and releases.
    void wheelEvent(QWheelEvent* event) override;

private:
    void openBackgroundDialog();
    // Restyles every row's font/colour off Theme - AppearancePanel's own
    // applyTheme(), hooked to the same Theme::notifier() broadcast.
    void applyTheme();

    QLabel* myTitle = nullptr;
    QSlider* mySurfaceSlider = nullptr;
    QSlider* myMetalSlider = nullptr;
    QSlider* myLightAngleSlider = nullptr;
    QSlider* myLightStrengthSlider = nullptr;
    QSlider* myFovSlider = nullptr;
    class Swatch* myBackgroundSwatch = nullptr;
    QColor myBackground;
    QColorDialog* myDialog = nullptr;
    // Every QLabel this card paints, in the order built - applyTheme() and
    // paintedTexts() both walk it rather than naming six pointers apiece.
    std::vector<QLabel*> myRowLabels;
    // True while setValuesSilently()/applyTheme() is writing the controls,
    // so a slider's own valueChanged does not re-emit straight back out -
    // AppearancePanel's mySyncing, the standard guard against a two-way
    // binding oscillating.
    bool mySyncing = false;
};

// The round camera shutter (Task 7.2) - a SIBLING concept to ToolChip
// (action-driven, holds no state of its own: enabled state, tooltip and the
// click itself all mirror the QAction it is built with) but a different
// shape, deliberately: this is not a rail chip, it is a standalone floating
// control at the viewport's bottom-right corner, round rather than
// rounded-rectangular, with an unconditional accent ring rather than one
// that only appears when checked (this button is never checkable - Save
// Screenshot is a one-shot action, not a mode).
class RenderShutterButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit RenderShutterButton(QAction* action, QWidget* parent = nullptr);

    // ToolChip's own accessor, on ToolChip's own terms: this button stores
    // nothing of its own, so proving it is wired to a particular action is
    // a pointer comparison against this rather than a behavioural probe -
    // gui_smoke's own reason to expose it (Save Screenshot opens a native,
    // blocking file dialog, so the suite proves the WIRING here and proves
    // the CLICK mechanism generically against a throwaway action with no
    // dialog attached, rather than ever triggering the real one).
    QAction* action() const { return myAction; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void syncFromAction();
    void applyTheme();

    QAction* myAction = nullptr;
    bool myHovered = false;
};

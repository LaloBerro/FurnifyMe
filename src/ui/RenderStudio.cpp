#include "RenderStudio.h"

#include "AppBar.h"
#include "CardSlide.h"
#include "IconSet.h"
#include "OcctViewWidget.h"
#include "RenderFrameGuides.h"
#include "RenderSettingsPanel.h"
#include "ToolChip.h"
#include "ViewportOverlay.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QTimer>
#include <QWidget>

namespace {
// How often the polish reading is refreshed while render mode is on. The
// accumulation deepens frame by frame with no appStateChanged to ride, so
// this is the one piece of live data in the panel that needs a clock.
constexpr int kTierTickMs = 500;
}  // namespace

RenderStudio::RenderStudio(OcctViewWidget* view, QMainWindow* host, ViewportOverlay* overlay,
                           AppBar* bar, QAction* renderModeAction, QObject* parent)
    : QObject(parent ? parent : host), myView(view), myHost(host), myOverlay(overlay), myBar(bar)
{
    // The app bar leaves upward when render mode takes the viewport. The user
    // asked for the bar HIDDEN rather than tinted ("Dont put white the top
    // bar, just hide it (with animation)"), which is why there is a slide here
    // and no ground colour. A host with no bar simply has no slide.
    if (myBar && myView && myOverlay) {
        myBarSlide = new CardSlide(myBar, CardSlide::From::Top, myView, myOverlay, this);
    }

    // The way back out of render mode once the menu has slid away.
    //
    // Icon-only, like every other chip over this viewport - a rule gui_smoke
    // enforces, and one a Labelled chip here would have been the sole
    // exception to. IconSet::Glyph::Back was added for it: a left chevron
    // reads as "back" without a word, and the words are in the tooltip,
    // which mirrors the action exactly as every other chip's does.
    if (renderModeAction && myView) {
        myExitChip = new ToolChip(renderModeAction, IconSet::Glyph::Back,
                                  ToolChip::ChipMode::IconOnly, myView);
        myExitChip->hide();
        // Added LAST in the TopLeft column by the host, so it takes the corner
        // only when every drawer above it is hidden - which is exactly and
        // only render mode.
        if (myOverlay) myOverlay->addWidget(myExitChip, ViewportOverlay::Anchor::TopLeft);
    }

    // The picture's edges. Deliberately NOT a ViewportOverlay entry: the
    // overlay anchors cards to edges and this covers the viewport edge to
    // edge, and it must never appear in occupiedRects() either - a toast
    // stepping around the frame would be stepping around the whole viewport.
    if (myView) {
        myFrame = new RenderFrameGuides(myView);
        connect(myView, &OcctViewWidget::renderFrameChanged, this, [this] {
            if (myFrame) myFrame->refresh();
        });
    }

    // Hidden before it is anywhere: its visibility belongs to the derived
    // refresh() alone, never to a default show(). NOT anchored to the overlay
    // - it is docked beside the viewport now (setDockOpen()), so the overlay
    // has no business placing it. Leaving the entry registered was not
    // harmless either: relayout() went on move()ing it, and since the
    // reparent had changed what those coordinates MEAN, the panel landed back
    // over the viewport inside its new container.
    if (myView) {
        myPanel = new RenderSettingsPanel(myView);
        myPanel->hide();
    wireViewportControls();

        connect(myPanel, &RenderSettingsPanel::shotSaveRequested, this,
                [this] { emit shotSaveRequested(); });
        connect(myPanel, &RenderSettingsPanel::shotApplied, this,
                [this](int index) { emit shotApplied(index); });
        connect(myPanel, &RenderSettingsPanel::shotRemoved, this,
                [this](int index) { emit shotRemoved(index); });
    }

    // The polish ticker - see refresh() for start and stop.
    myTierTicker = new QTimer(this);
    connect(myTierTicker, &QTimer::timeout, this, [this] { emit tierTick(); });
}

void RenderStudio::wireViewportControls()
{
    if (!myPanel || !myView) return;
    OcctViewWidget* view = myView;

    connect(myPanel, &RenderSettingsPanel::surfaceGlossinessChanged, this,
            [this, view](double glossiness01) {
                view->setRenderSurfaceRoughness(1.0 - glossiness01);
                emit settingsChanged();
            });
    connect(myPanel, &RenderSettingsPanel::metalChanged, this, [this, view](double metallic01) {
        view->setRenderMetal(metallic01);
        emit settingsChanged();
    });
    connect(myPanel, &RenderSettingsPanel::lightAngleChanged, this,
            [this, view](double azimuthDeg) {
                view->setRenderLightAngleDeg(azimuthDeg);
                emit settingsChanged();
            });
    connect(myPanel, &RenderSettingsPanel::lightStrengthChanged, this,
            [this, view](double multiplier) {
                view->setRenderLightStrength(multiplier);
                emit settingsChanged();
            });
    connect(myPanel, &RenderSettingsPanel::backgroundChanged, this,
            [this, view](const QColor& colour) {
                view->setRenderBackgroundOverride(colour);
                emit settingsChanged();
            });
    connect(myPanel, &RenderSettingsPanel::aspectChanged, this,
            [this, view](RenderSettingsPanel::Aspect aspect) {
                view->setRenderAspect(static_cast<OcctViewWidget::RenderAspect>(aspect));
                emit settingsChanged();
            });
    connect(myPanel, &RenderSettingsPanel::guidesChanged, this,
            [this, view](RenderSettingsPanel::Guides guides) {
                view->setRenderGuides(static_cast<OcctViewWidget::RenderGuides>(guides));
                emit settingsChanged();
            });
    connect(myPanel, &RenderSettingsPanel::fovChanged, this, [this, view](double fovyDeg) {
        view->setRenderFov(fovyDeg);
        emit settingsChanged();
    });
    connect(myPanel, &RenderSettingsPanel::woodChanged, this, [this, view](bool wood) {
        view->setRenderWood(wood);
        emit settingsChanged();
    });
    connect(myPanel, &RenderSettingsPanel::woodTextureChosen, this,
            [this, view](const QString& name, const QString& path) {
                Q_UNUSED(name);
                view->setRenderTextureFile(path);
                view->setRenderWood(true);
                emit settingsChanged();
            });
    connect(myPanel, &RenderSettingsPanel::woodTileChanged, this, [this, view](double mm) {
        view->setRenderWoodTileMm(mm);
        emit settingsChanged();
    });
    connect(myPanel, &RenderSettingsPanel::woodAngleChanged, this,
            [this, view](double degrees) {
                view->setRenderWoodAngleDeg(degrees);
                emit settingsChanged();
            });
    connect(myPanel, &RenderSettingsPanel::exportSizeChanged, this,
            [this, view](RenderSettingsPanel::ExportSize size) {
                view->setRenderExportSize(static_cast<OcctViewWidget::ExportSize>(size));
                emit settingsChanged();
            });
    connect(myPanel, &RenderSettingsPanel::cutoutChanged, this, [this, view](bool cutout) {
        view->setRenderCutout(cutout);
        emit settingsChanged();
    });
    connect(myPanel, &RenderSettingsPanel::qualityChanged, this,
            [this, view](RenderSettingsPanel::Quality quality) {
                view->setRenderQuality(
                    quality == RenderSettingsPanel::Quality::Simple
                        ? OcctViewWidget::RenderQuality::Simple
                        : (quality == RenderSettingsPanel::Quality::Balanced
                               ? OcctViewWidget::RenderQuality::Balanced
                               : OcctViewWidget::RenderQuality::Deep));
                // A live material/quality change does not reach a ray-traced
                // frame on this build; the round trip is what applies it.
                if (myEnabled) {
                    setEnabled(false);
                    setEnabled(true);
                }
                emit settingsChanged();
            });
}

DocumentModel::Shot RenderStudio::shotFrom(const OcctViewWidget* view, const QString& name)
{
    DocumentModel::Shot shot;
    shot.name = name.toStdString();
    if (!view) return shot;
    shot.camera = view->camera().state();
    shot.orthographic =
        view->camera().baseProjection() == CameraController::Projection::Orthographic;
    shot.fovDeg = view->renderFov();
    shot.aspect = static_cast<int>(view->renderAspect());
    shot.lightAngleDeg = view->renderLightAngleDeg();
    shot.lightStrength = view->renderLightStrength();
    return shot;
}

void RenderStudio::applyShotTo(OcctViewWidget* view, const DocumentModel::Shot& shot)
{
    if (!view) return;
    // The projection BEFORE the pose: setBaseProjection() is the user-facing
    // route that also hands back a temporary-ortho loan (see CameraController's
    // own note), and applying it after the pose would drop a loan the pose
    // never took while leaving the camera where it already was. Order settled
    // by that rule rather than by taste.
    view->setBaseProjection(shot.orthographic ? CameraController::Projection::Orthographic
                                              : CameraController::Projection::Perspective);
    view->setCameraStateNow(shot.camera);
    view->setRenderFov(shot.fovDeg);
    view->setRenderAspect(static_cast<OcctViewWidget::RenderAspect>(shot.aspect));
    view->setRenderLightAngleDeg(shot.lightAngleDeg);
    view->setRenderLightStrength(shot.lightStrength);
}

void RenderStudio::setEnabled(bool on)
{
    if (myEnabled == on) return;
    myEnabled = on;
    if (myView) myView->setRenderMode(on);
    // The panel takes its own column beside the viewport rather than floating
    // over it. Done AFTER setRenderMode() so the viewport is already in its
    // studio state when it is resized into the narrower half, rather than
    // rendering one modeling frame at the new width first.
    setDockOpen(on);
    emit enabledChanged(on);
}

void RenderStudio::refresh()
{
    // Derived on every state change, never set once at the toggle - a one-shot
    // hide is not a state, and QWidget::showChildren() on a window's first
    // show will happily undo one.
    if (myBarSlide) myBarSlide->setShown(!myEnabled);
    if (myExitChip) myExitChip->setVisible(myEnabled);
    if (myPanel) myPanel->setVisible(myEnabled);
    if (myFrame) {
        myFrame->setVisible(myEnabled);
        if (myEnabled) {
            // Re-read on every state change, not only when the aspect moves:
            // the viewport resizes when the panel docks and when the window
            // does, and the frame is a fraction of the viewport either way.
            myFrame->refresh();
            // LOWERED among the viewport's siblings, not raised. The mask dims
            // the SCENE, and every sibling over this viewport is a control
            // which has to stay crisp on top of it.
            myFrame->lower();
        }
    }
    if (myTierTicker) {
        if (myEnabled && !myTierTicker->isActive()) myTierTicker->start(kTierTickMs);
        else if (!myEnabled) myTierTicker->stop();
    }
}

void RenderStudio::setDockOpen(bool open)
{
    if (!myPanel || !myHost || !myView) return;
    if (open == (myDock != nullptr)) return;

    if (open) {
        // The size the viewport has RIGHT NOW is the size the container is
        // about to be handed, and the compare pane's finding says to set it
        // explicitly rather than trust a layout pass to happen: captured
        // before anything below moves a widget.
        const QSize centralSize = myView->size();

        myDock = new QWidget(myHost);
        auto* row = new QHBoxLayout(myDock);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        // The viewport takes every pixel the panel does not, and the panel
        // takes its own fixed width at full height - which is the whole of
        // what "a panel, not a card" means here.
        row->addWidget(myView, 1);
        row->addWidget(myPanel, 0);
        myHost->setCentralWidget(myDock);
        myDock->resize(centralSize);
    } else {
        const QSize centralSize = myDock ? myDock->size() : myView->size();
        // The panel is parented back to the WINDOW before the container goes,
        // or deleting the container would delete the panel with it - every
        // later refresh() reads it, and a dangling pointer would read as "no
        // panel" rather than as a crash, which is worse.
        myPanel->setParent(myHost);
        myPanel->hide();
        myHost->setCentralWidget(myView);
        // The compare pane's own lesson, applied at the second site that
        // needs it: setCentralWidget() leaves myView at the width it had as
        // ONE CHILD of the container until something forces
        // QMainWindowLayout to run again, and neither invalidate() nor
        // activate() is that something. It was found there by a composited
        // capture rather than by any geometry check, because the view
        // rendered and picked correctly inside its own wrong rect.
        myView->resize(centralSize);
        myDock->deleteLater();
        myDock = nullptr;
    }
}

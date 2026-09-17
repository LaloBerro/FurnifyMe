#pragma once
// The render-mode-only STUDIO PANEL and its camera shutter - Milestone 5's
// render-UI rework (mockup pick: "A, with B's material selector, plus a
// quality switch"): a full-height panel pinned to the viewport's right edge
// (ViewportOverlay::Anchor::RightEdge), sectioned Light / Material / Scene /
// Camera, with visual material preset tiles, a Quality (Deep/Simple)
// segmented pair, and a footer carrying the live tier, the path-tracing
// polish progress and a WIDE shutter - all in AppearancePanel's own visual
// language (paintSurface() family, Theme tokens throughout).
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

#include <array>

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

    // Whether the active render tier actually applies Surface and Metal.
    // False raises a muted one-line note under those two rows saying where
    // they do apply, and nothing else - the sliders stay live and enabled,
    // because the value they hold is real and takes effect the moment the
    // session lands on a tier that reads it. CLAUDE.md's "a disabled control
    // that will not say why reads as broken" applies at least as strongly to
    // an ENABLED control that silently does nothing, which is what these two
    // were on three of the four tiers. Derived by MainWindow from
    // OcctViewWidget::renderMaterialControlsApply() on every appStateChanged
    // - never a one-shot at the toggle site.
    void setMaterialRowsApply(bool apply);
    bool materialRowsApply() const { return myMaterialRowsApply; }

    // The Quality pair (Milestone 5): Deep = the session's probed-best tier,
    // Simple = the shadow-mapped raster tier, instant frames. setQuick() is
    // the silent return path (MainWindow pushes the persisted/live value
    // in); a user click emits quickChanged() and nothing else - MainWindow
    // owns what a flip actually does (OcctViewWidget::setRenderQuick() plus
    // a render-mode re-entry).
    // THREE now, not two (improvements item 16): Simple, Balanced, Deep.
    // The panel keeps its own enum rather than reaching for
    // OcctViewWidget::RenderQuality - this card knows nothing about tiers or
    // GPUs, only about which of three chips is lit, and MainWindow maps the
    // one to the other exactly as it does for every other row here.
    enum class Quality { Simple, Balanced, Deep };
    // The cut-out checkbox: with it on, Save Screenshot writes a PNG with a
    // real alpha channel - no floor, no backdrop. setCutout() is the silent
    // return path (MainWindow pushes the persisted value in); a click emits
    // cutoutChanged() and nothing else.
    // How big an exported render is. Five chips, and a line under them that
    // prints the pixels it would actually produce - a size picker whose
    // answer you only learn after exporting is a size picker that has to be
    // tried. setExportSize()/setExportNote() are the silent return paths;
    // MainWindow owns what a click does and what the note says, because the
    // pixels depend on the viewport and the live tier, neither of which this
    // card knows anything about.
    // THE PICTURE'S SHAPE and the lines drawn inside it. Mirrors of
    // OcctViewWidget::RenderAspect and ::RenderGuides, declared here so this
    // card still knows nothing about the viewport - the Quality enum's own
    // arrangement, and for the same reason: MainWindow maps the one onto the
    // other at the single wiring site.
    //
    // Free is first and is the default: a user who has not chosen a shape has
    // the window's shape, which is what this app did before the picker
    // existed, so nothing changes for anybody who never touches it.
    enum class Aspect { Free, Square, FourFive, ThreeTwo, SixteenNine };
    void setAspect(Aspect aspect);
    Aspect aspect() const { return myAspect; }

    enum class Guides { Off, Thirds, Centre };
    void setGuides(Guides guides);
    Guides guides() const { return myGuides; }

    enum class ExportSize { Viewport, Height720, Height1080, Height1440, Height2160 };
    void setExportSize(ExportSize size);
    ExportSize exportSize() const { return myExportSize; }
    void setExportNote(const QString& note);

    void setCutout(bool cutout);
    bool cutout() const { return myCutout; }

    void setQuality(Quality quality);
    Quality quality() const { return myQuality; }
    // The two-state spelling the rest of this file used before the middle
    // chip existed, kept so nothing that only knows Quick has to change.
    void setQuick(bool quick) { setQuality(quick ? Quality::Simple : Quality::Deep); }
    bool quick() const { return myQuality == Quality::Simple; }

    // The Wood preset (Milestone 5): a material of its own rather than a
    // gloss/metal pair, so it is a flag beside the sliders, not a write into
    // them. setWood() is the silent return path; a tile click emits
    // woodChanged() and MainWindow owns what it does
    // (OcctViewWidget::setRenderWood() plus persistence). Clicking any of
    // the three gloss/metal tiles takes wood off in the same gesture.
    void setWood(bool wood);
    bool wood() const { return myWood; }
    // Which textured material is the active one while wood() is on - "Wood"
    // for the built-in grain, otherwise the tile's own name (derived from
    // the image's filename). Silent, persistence's return path.
    void setWoodSelection(const QString& name);
    QString woodSelection() const { return myWoodName; }
    // The material's two dials, silent return paths - see the signals.
    void setWoodTileMm(double mm);
    void setWoodAngle(double degrees);
    double woodTileMm() const;
    double woodAngle() const;

    // One tile per image the user dropped into the materials folder -
    // MainWindow scans and calls this once, right after construction. The
    // selector lays every tile (the three gloss/metal presets, the built-in
    // Wood, and however many of these) in a WRAPPING grid, so more
    // materials grow rows rather than clipping - the user's own ask.
    void addTextureMaterials(const std::vector<std::pair<QString, QString>>& namesAndPaths);

    // The footer's live line: the active tier's name and - for the
    // path-traced tier - how polished the on-screen picture is right now
    // (progress01 in [0,1]; anything negative hides the bar). Pushed by
    // MainWindow on a timer while render mode is on; this panel stores and
    // paints, nothing more.
    void setTierStatus(const QString& tierName, double progress01);

    // Builds the WIDE shutter into the footer, wired to `action` - Save
    // Screenshot - on RenderShutterButton's own action-driven terms. Called
    // once by MainWindow::buildOverlay(); the button is a child of this
    // panel, so its visibility rides the panel's own.
    void setShutterAction(QAction* action);
    class RenderShutterButton* shutter() const { return myShutter; }

    double surfaceGlossiness() const;
    double metal() const;
    double lightAngle() const;
    double lightStrength() const;
    QColor background() const { return myBackground; }
    double fov() const;

    // Every string this card paints - the title, the six row labels and the
    // material note - swept by gui_smoke's vocabulary check on
    // AppearancePanel::paintedTexts()'s own terms. The note is reported
    // whether or not it is currently SHOWN: a sweep that only saw the copy
    // on the tier that happens to be running would be a coin toss, which is
    // Toast::paintedTexts()'s own recorded lesson.
    QStringList paintedTexts() const;
    // The note row itself, so the suite can assert it appears and
    // disappears rather than trusting the flag.
    QWidget* materialNoteRow() const;
    // Every material tile's own name, in the order the grid holds them - for
    // the suite, which has to be able to say that the three non-wood tiles
    // really went rather than that some tile or other is present.
    QStringList materialTileNames() const;

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
    void quickChanged(bool quick);
    void qualityChanged(Quality quality);
    void cutoutChanged(bool cutout);
    void aspectChanged(Aspect aspect);
    void guidesChanged(Guides guides);
    void exportSizeChanged(ExportSize size);
    // A tile was DOUBLE-clicked: open that material's own colour and
    // brightness editor. The first click of the gesture has already made it
    // the live material, which is what makes the editor's changes visible
    // the moment they are made.
    void materialEditRequested(QString material);
    void woodChanged(bool wood);
    // A textured tile was clicked: `path` is the image file, empty for the
    // built-in procedural grain. MainWindow routes it into
    // OcctViewWidget::setRenderTextureFile() + setRenderWood(true).
    void woodTextureChosen(const QString& name, const QString& path);
    // Grain size (how much real material one tile of the image covers) and
    // grain angle (which way it runs).
    void woodTileChanged(double mm);
    void woodAngleChanged(double degrees);
    void surfaceGlossinessChanged(double glossiness01);
    void metalChanged(double metallic01);
    void lightAngleChanged(double azimuthDeg);
    void lightStrengthChanged(double multiplier);
    void backgroundChanged(const QColor& colour);
    void fovChanged(double fovyDeg);

protected:
    // THE CARD'S OWN HEIGHT, added up from its parts rather than asked of the
    // layout. The rows live in a widgetResizable QScrollArea, and
    // QScrollArea::sizeHint() answers with a cached, line-bounded box of its
    // own - a layout containing one reports whatever that says, which is not
    // how tall this card's content actually is. ItemsPanel learned exactly
    // this (see its own sizeHint()), and it cost three wrong fixes there
    // before the cause was found; the same trap bit this card the moment it
    // stopped being stretched to the viewport's full height and had to answer
    // for itself.
    //
    // DECLARED HERE, beside the other QWidget overrides, and not one line
    // higher: the section above is `signals:`, and moc read an override put
    // there as a SIGNAL - generating a definition of its own, which the
    // linker then reported as a duplicate of the real one. The same trap this
    // project already hit once with a QAction in a slots section.
    QSize sizeHint() const override;

    void paintEvent(QPaintEvent* event) override;
    // AppearancePanel's own reason: an unhandled wheel over a slider row
    // would propagate to the viewport underneath and zoom the camera - the
    // wheel-shaped half of WA_NoMousePropagation, which only covers presses
    // and releases.
    void wheelEvent(QWheelEvent* event) override;

private:
    // The widget inside the scroll area - the only thing that knows how tall
    // the rows are. See sizeHint().
    QWidget* myScrollContent = nullptr;
    class QScrollArea* myScroll = nullptr;
    void openBackgroundDialog();
    // Restyles every row's font/colour off Theme - AppearancePanel's own
    // applyTheme(), hooked to the same Theme::notifier() broadcast.
    void applyTheme();

    QLabel* myTitle = nullptr;
    // The footer's live status pair - see setTierStatus().
    QLabel* myTierLabel = nullptr;
    class ProgressLine* myProgress = nullptr;
    // The Quality chips and their state - see setQuality().
    class SegChip* myDeepChip = nullptr;
    class SegChip* mySimpleChip = nullptr;
    Quality myQuality = Quality::Deep;
    class SegChip* myBalancedChip = nullptr;
    class SegChip* myCutoutChip = nullptr;
    bool myCutout = false;
    ExportSize myExportSize = ExportSize::Viewport;
    std::array<class SegChip*, 5> myExportChips{};
    std::array<class SegChip*, 5> myAspectChips{};
    std::array<class SegChip*, 3> myGuideChips{};
    Aspect myAspect = Aspect::Free;
    Guides myGuides = Guides::Thirds;
    class QLabel* myExportNote = nullptr;
    bool myWood = false;
    QString myWoodName = QStringLiteral("Wood");
    class QGridLayout* myTileGrid = nullptr;
    // Watches the material tiles for a double-click - see addTile().
    bool eventFilter(QObject* watched, QEvent* event) override;
    void addTile(class MaterialTile* tile);
    // The three material preset tiles (the mockup's "B material selector").
    std::vector<class MaterialTile*> myPresetTiles;
    class RenderShutterButton* myShutter = nullptr;
    // Per-slider value readouts, keyed by the slider they follow.
    std::vector<std::pair<QSlider*, QLabel*>> myValueLabels;
    // Section headers, styled apart from row labels by applyTheme().
    std::vector<QLabel*> mySectionLabels;
    void syncValueLabels();
    void syncPresetTiles();
    QSlider* myWoodTileSlider = nullptr;
    QSlider* myWoodAngleSlider = nullptr;
    QSlider* mySurfaceSlider = nullptr;
    QSlider* myMetalSlider = nullptr;
    QSlider* myLightAngleSlider = nullptr;
    QSlider* myLightStrengthSlider = nullptr;
    QSlider* myFovSlider = nullptr;
    // The muted one-liner under Surface/Metal, and the flag it is derived
    // from - see setMaterialRowsApply().
    QLabel* myMaterialNote = nullptr;
    bool myMaterialRowsApply = true;
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
    // `wide` (Milestone 5's render-UI rework) trades the round floating
    // shutter for a full-width bar living inside the studio panel's footer -
    // same action-driven contract, same accent, camera glyph plus the
    // action's own text.
    explicit RenderShutterButton(QAction* action, QWidget* parent = nullptr,
                                 bool wide = false);

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
    bool myWide = false;
};

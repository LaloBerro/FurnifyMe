#pragma once
// SETTINGS - the app's one settings drawer, in four tabs.
//
// THE CLASS AND THE FILE ARE STILL CALLED AppearancePanel, and that is a
// deliberate keep rather than an oversight. This card began as the colours-
// and-fonts panel and grew (improvements item 10, mockup pick B) into the
// drawer that also holds the viewport switches, the unit and the autosave
// mode; renaming the type would churn MainWindow, gui_smoke and CMake for
// roughly a hundred references and buy nothing a reader of this comment does
// not already have. What the class IS, is the Settings drawer: the title says
// Settings, the action says Settings..., and nothing in here claims to be
// about appearance alone.
//
// Four tabs, in this order, and the split is by MEANING rather than by
// mechanism:
//
//   Colours  - every colour token, the type scale, the three line widths,
//              the font, Save/Load colours and Reset. Theme::Spec values.
//   Viewport - the grid switch, Grid detail, Show sizes, Projection,
//              Gizmo size, Show notifications. A mix of QActions and two
//              Theme::Spec values, which is the point: Grid detail and Gizmo
//              size are SPEC values that belong beside the grid and the
//              gizmos they describe, not beside the colour swatches they
//              merely share a persistence mechanism with.
//   Units    - Millimetres/Centimetres, Snap to Grid, Magnet.
//   Files    - Autosave (Off / After every change / Every minute / 5 / 15).
//
// THE LAW EVERY NON-COLOUR ROW FOLLOWS: a control here is built from the
// window's existing QAction and MIRRORS it. It holds no checked state of its
// own - addToggleRow()/addChoiceRow() read isChecked() and isEnabled() off
// the action on every QAction::changed, and a click on the control triggers
// the action rather than writing anything. So the View menu, the File menu
// and this drawer cannot disagree, updateActions() stays the single place
// availability is decided, and the vocabulary sweep keeps covering these
// settings through the actions it already finds by text. A row added here
// that kept its own bool would be a second source of truth for a setting the
// menus also show - which is the oldest mistake this shell has a rule
// against.
//
// The Theme::Spec rows keep the mechanism they already had, unchanged: an
// edit writes Theme::setSpec(), the notifier broadcasts, applyTheme()
// re-reads every control, and MainWindow's 400 ms debounce stores it. No
// widget caches a colour across the broadcast.
//
// WHICH TAB IS OPEN IS SESSION STATE, held in myTab and nowhere else. It is
// deliberately not persisted: it is not a Theme::Spec value, so storing it
// would mean a second persistence mechanism on this card for something that
// is not a preference at all but where the user happened to be looking last
// time. Reopening on Colours is also the honest default - it is the tab this
// drawer has always opened on.
//
// A floating card of the Theme::paintSurface() family, parented to the
// viewport and anchored by ViewportOverlay exactly as the items drawer is -
// which is also what puts it in occupiedRects(), so the toast, the hint
// balloon and the guide step around it for free rather than this file naming
// any of them. It is anchored TopRight, stacking under the axis gizmo:
//
//   - Right, because the whole left side is spoken for - the rail is a spine
//     down the full left edge and the items drawer sits beside it.
//   - TopRight rather than RightCenter, because a RightCenter entry is
//     centred on the viewport's height, and a card this tall centred there
//     runs into the axis gizmo the moment the window is short. Stacking under
//     the gizmo is deterministic at every viewport size: relayout()'s
//     TopRight cursor puts this card exactly one gap below whatever is
//     already anchored there.
//
// It holds NO appearance state of its own. Every swatch reads Theme::spec()
// and every edit writes back through Theme::setSpec(), so the panel is a view
// of the live spec rather than a second copy of it that has to be kept in
// step - and a change made from anywhere else (a reset, a persisted spec
// installed at startup) shows up here through the same Theme::notifier()
// broadcast every other widget listens to.
//
// The colour picker is MODELESS, and getting there takes show() rather than
// either of the two calls that look right. exec() spins a nested event loop
// and freezes the application outright; QDialog::open() returns immediately
// but forces Qt::WindowModal on the way past, which still locks the rail, the
// viewport and the toast's Undo pill while a colour is being chosen -
// setModal(false) does not survive it. The app's no-modal law is that nothing
// blocks, and a picker with a live preview has a second reason to obey it:
// the whole point is that the user watches their model re-dress while they
// choose, which they cannot do if they cannot orbit it.
#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <vector>

class QAbstractButton;
class QAction;
// Both live in AppearancePanel.cpp: Swatch is one token's colour as a button,
// OptionChip is the small checkable-looking chip the tab bar, the toggle
// pills and the choice rows are all built from.
class Swatch;
class OptionChip;
class QComboBox;
class QHBoxLayout;
class QColorDialog;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class QEvent;
class QObject;

class AppearancePanel : public QWidget {
    Q_OBJECT

public:
    // The four tabs, in the order they are drawn. Used as an index into
    // myPages, so the order of the enumerators IS the order on screen.
    enum class Tab { Colours = 0, Viewport = 1, Units = 2, Files = 3 };
    static constexpr int kTabCount = 4;

    explicit AppearancePanel(QWidget* parent = nullptr);

    // --- the mirrored rows --------------------------------------------------
    //
    // Both take an action the WINDOW already owns and build a control that is
    // a view of it: enabled follows isEnabled(), current follows isChecked(),
    // and a click calls trigger(). Neither stores a bool. Called by
    // MainWindow::buildOverlay() once, after the actions exist.
    //
    // addToggleRow() is the on/off pill - one checkable action.
    // addChoiceRow() is the segmented pair or stack - a set of mutually
    // exclusive checkable actions, `stacked` laying them out one per line for
    // a set too wide to sit in a row (autosave's five modes).
    void addToggleRow(Tab tab, const QString& label, QAction* action);
    void addChoiceRow(Tab tab, const QString& label, const QVector<QAction*>& options,
                      const QStringList& optionLabels, bool stacked = false);
    // The two-state segmented row built from ONE checkable action: the left
    // chip is the action unchecked, the right chip is it checked. Projection
    // is the case - the app has an Orthographic action and no Perspective
    // one, because perspective is simply Orthographic off - and a row that
    // painted only the word "Orthographic" beside a pill would make the user
    // work that out. Still a pure mirror: both chips read isChecked().
    void addBinaryChoiceRow(Tab tab, const QString& label, QAction* action,
                            const QString& offLabel, const QString& onLabel);

    // --- this drawer's OWN Theme::Spec rows, placed by the caller -----------
    //
    // Grid detail and Gizmo size are Theme::Spec values that belong beside
    // the grid and the gizmos they describe rather than beside the colour
    // swatches they merely share a persistence mechanism with. They are built
    // here, not in the constructor, so MainWindow can interleave them with
    // the mirrored rows and the Viewport tab reads in the order the mockup
    // set. Persisted exactly as before - Theme::setSpec() and MainWindow's
    // 400 ms debounce, untouched by the move.
    void addGridDetailRow(Tab tab);
    void addGizmoSizeRow(Tab tab);

    // Which tab is showing. Session state - see the header comment.
    Tab currentTab() const { return myTab; }
    void setCurrentTab(Tab tab);
    // The tab's own button, for a childAt() hit test and for driving a real
    // click the way a user does.
    QAbstractButton* tabButton(Tab tab) const;
    // The page a tab shows. Exposed so the suite can assert that switching
    // tabs actually swaps the rows rather than merely repainting a heading.
    QWidget* pageFor(Tab tab) const;
    // The control mirroring `action`, or null for an action this drawer has
    // no row for. One lookup for both kinds of row: a toggle's pill and a
    // choice's option chip are both "the control that stands for this action".
    QAbstractButton* controlFor(QAction* action) const;
    // The OFF half of a binary choice row - the chip that is current exactly
    // when `action` is not checked. Null for anything else.
    QAbstractButton* alternateControlFor(QAction* action) const;
    // What a mirrored control is actually PAINTING, read off the control's
    // own state rather than out of the action again. That distinction is the
    // whole point of the mirror law: a probe that asked the action twice
    // would pass against a control that had silently gone stale.
    bool controlIsCurrent(const QAbstractButton* control) const;

    // The panel's own edit path. A swatch click, the size spinner, the family
    // combo and the reset button all land here, and so does the suite - so
    // what a test drives is what a user drives, not a parallel entry point.
    void setTokenColour(const QString& id, const QColor& colour);
    void setBaseSize(double pt);
    // ToolChip's border width - Spec::chipStrokePx, clamped to Theme's own
    // range. 0 is legal and means the chips draw no ring at all.
    void setChipStroke(double px);
    // The work-plane grid's line density - Spec::gridDensity, clamped to
    // Theme's own range. Above 1.0 packs more lines into the same view;
    // below 1.0 thins it out. See GridRenderer::minorStepFor() for the exact
    // mapping this drives.
    void setGridDensity(double density);
    // A body's face-boundary line width - Spec::edgeWidthPx, clamped to
    // Theme's own range. 0 draws no boundary lines at all.
    void setEdgeWidth(double px);
    // The body gizmos' drawn-size multiplier - Spec::gizmoScale, clamped to
    // Theme's own range. Both the Move tool and the Rotate/Scale gizmo read it.
    void setGizmoScale(double scale);
    // The in-progress and closed outline's own line width -
    // Spec::sketchLineWidthPx, clamped to Theme's own range.
    void setSketchLineWidth(double px);
    void setFontFamily(const QString& family);
    // Back to Theme::defaultSpec() - Graphite, and the bundled family at 10pt.
    void reset();

    // --- a look, written to a file and read back ----------------------------
    //
    // The FILE HALF of Save colours / Load colours, split off from the two
    // buttons deliberately. QFileDialog's native dialogs cannot be driven from
    // inside the event loop that raised them, so a suite that could only reach
    // this through the buttons could not reach it at all; these two are what
    // the buttons call and what the suite calls, so what is tested is what
    // ships rather than a parallel path.
    //
    // saveColoursTo() writes Theme::serializeSpec() - the same string
    // QSettings stores - and answers false for a path it cannot open or write.
    //
    // loadColoursFrom() routes the file's contents through
    // Theme::deserializeSpec(), whose contract is that it leaves its output
    // UNTOUCHED when it refuses (see Theme.h). So a garbage file cannot
    // half-apply: either every token in it lands through Theme::setSpec() -
    // live, and persisted by MainWindow's existing debounce, since this goes
    // through the same broadcast every other edit does - or nothing does and
    // this answers false with the live spec exactly as it was.
    bool saveColoursTo(const QString& path) const;
    bool loadColoursFrom(const QString& path);
    // The extension and the file dialog's filter, in one place so the two
    // buttons and the suite cannot disagree about what a colour file is called.
    static QString colourFileSuffix();   // ".furnifytheme"
    static QString colourFileFilter();

    // The popup-preview pair behind the font combo (item 10).
    //
    // The family applies on `highlighted` rather than only on `activated`, so
    // arrowing down the open list re-dresses the whole app as the user moves -
    // the same live-preview rule the colour picker already follows, and for the
    // same reason: this is a look, and a look is judged by looking at it.
    // Escape must therefore be able to put back what was there BEFORE the
    // popup opened, which is what these two remember and restore. Public
    // because the popup itself cannot be opened from inside the event loop
    // driving it, exactly as the file pair above cannot.
    void beginFamilyPreview();
    void cancelFamilyPreview();
    // The family recorded when the popup opened, or empty when no preview is
    // in flight.
    QString familyBeforePreview() const { return myFamilyBeforePreview; }

    // The user's word for a token id (`viewport` -> "Viewport"). Empty for an
    // id this panel has no name for, which is the case gui_smoke asserts can
    // never happen: a token in Theme::colourTokens() with no name here would
    // be an editable colour with no row.
    static QString nameForToken(const QString& id);

    // Every string this panel puts on screen - the title, each row's name,
    // the two control labels and the reset button. All of it is QLabel and
    // button text rather than QAction text or a tooltip, so the vocabulary
    // sweep cannot see any of it without this.
    QStringList paintedTexts() const;

    int colourRowCount() const { return static_cast<int>(myRows.size()); }
    // The swatch control for a token, for a childAt() hit test. Null for an
    // unknown id.
    QWidget* swatchFor(const QString& id) const;
    QWidget* resetButton() const;
    QWidget* saveButton() const;
    QWidget* loadButton() const;
    QSpinBox* sizeControl() const { return mySize; }
    QSpinBox* strokeControl() const { return myStroke; }
    QDoubleSpinBox* gridDensityControl() const { return myGridDensity; }
    QDoubleSpinBox* edgeWidthControl() const { return myEdgeWidth; }
    QDoubleSpinBox* sketchLineWidthControl() const { return mySketchLineWidth; }
    QDoubleSpinBox* gizmoScaleControl() const { return myGizmoScale; }
    QComboBox* familyControl() const { return myFamily; }
    // The live modeless picker, or null when none is open. Exposed so the
    // suite can assert it is modeless and close it - a dialog left open would
    // trip the "no QDialog after an outcome" checks elsewhere in the suite,
    // and that check has to stay meaningful.
    QColorDialog* activeColourDialog() const { return myDialog; }

signals:
    // A colour file that could not be written, and one that was not a colour
    // file at all. The COPY lives in MainWindow, with every other outcome the
    // app reports, rather than here: this card owns a look, not the toast
    // host, and a refusal that reported through a second surface would be a
    // second set of rules for how this app says no.
    void colourSaveFailed(const QString& path);
    void colourLoadRefused(const QString& path);

protected:
    void paintEvent(QPaintEvent* event) override;
    // Watches the family combo's popup: Show records the family to fall back
    // to, Escape puts it back, Hide ends the preview. A filter rather than a
    // QComboBox subclass because showPopup() is the only hook a subclass would
    // add and Escape is handled inside the popup's own window, which a
    // subclass of the combo never sees.
    bool eventFilter(QObject* watched, QEvent* event) override;
    // The scroll area swallows the wheel while it has somewhere to scroll,
    // but not once it is at an end - and an unhandled wheel over this card
    // would reach the viewport underneath and zoom the camera. Accepting it
    // here is the wheel-shaped half of the WA_NoMousePropagation this widget
    // already carries for presses and releases.
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Row {
        QString id;
        QString name;
        class Swatch* swatch = nullptr;
    };

    // One mirrored control and the action it is a view of. `option` is the
    // action a choice chip stands for; for a toggle it is the same action the
    // pill watches. Nothing here holds a checked state - see syncMirrors().
    struct Mirror {
        QAction* action = nullptr;
        OptionChip* control = nullptr;
    };

    // Puts one built row at the bottom of a tab's page - before the trailing
    // stretch on the three pages that have one, so a short tab keeps its rows
    // against the top edge however many are added later.
    void appendRow(Tab tab, QWidget* row);
    // A row shell: a transparent container, a left-aligned name label that
    // goes into paintedTexts(), and the horizontal layout to put the control
    // in. One builder, so a mirrored row and a spec row cannot drift apart
    // in spacing or in what the sweep can see.
    QHBoxLayout* makeRow(Tab tab, const QString& key, const QString& label);
    // Re-reads every mirrored control from its action - checked and enabled
    // both. Hooked to each action's QAction::changed, which Qt emits for a
    // setChecked(), a setEnabled() and a text change alike, so there is no
    // list of signals to keep in step with updateActions().
    void syncMirrors();

    void openColourDialog(const QString& id);
    // The two buttons' own handlers: a native file dialog, then the matching
    // function above. Native dialogs are the one modal surface this app does
    // use, and deliberately - see the comment at each call site.
    void chooseSaveFile();
    void chooseLoadFile();
    // Re-reads every swatch, the spinner and the combo from the live spec.
    // Hooked to Theme::notifier(), so the panel follows a change it did not
    // make - a reset, or the persisted spec installed at startup - without
    // any caller having to remember to refresh it.
    void applyTheme();

    std::vector<Row> myRows;
    std::vector<Mirror> myMirrors;
    // The OFF half of a binary choice row - a chip that is current exactly
    // when its action is NOT checked. Kept apart from myMirrors so the
    // ordinary "current == isChecked()" rule stays one line rather than
    // growing a polarity flag every other row would have to carry.
    QHash<OptionChip*, QAction*> myOffMirrors;
    // Every label this drawer paints that is not already a member below -
    // the mirrored rows' names and the tab buttons' words. Collected as they
    // are built so paintedTexts() cannot fall behind a row somebody adds.
    QStringList myExtraTexts;
    QStackedWidget* myPages = nullptr;
    OptionChip* myTabButtons[kTabCount] = {};
    QWidget* myTabPages[kTabCount] = {};
    QVBoxLayout* myPageLayouts[kTabCount] = {};
    Tab myTab = Tab::Colours;
    QLabel* myTitle = nullptr;
    QLabel* mySizeLabel = nullptr;
    QLabel* myGridDensityLabel = nullptr;
    QLabel* myEdgeWidthLabel = nullptr;
    QLabel* mySketchLineWidthLabel = nullptr;
    QLabel* myGizmoScaleLabel = nullptr;
    QLabel* myStrokeLabel = nullptr;
    QLabel* myFamilyLabel = nullptr;
    QSpinBox* mySize = nullptr;
    QDoubleSpinBox* myGridDensity = nullptr;
    QDoubleSpinBox* myEdgeWidth = nullptr;
    QDoubleSpinBox* mySketchLineWidth = nullptr;
    QDoubleSpinBox* myGizmoScale = nullptr;
    QSpinBox* myStroke = nullptr;
    QComboBox* myFamily = nullptr;
    class QPushButton* myReset = nullptr;
    class QPushButton* mySave = nullptr;
    class QPushButton* myLoad = nullptr;
    QColorDialog* myDialog = nullptr;
    // The family the combo's popup opened over, so Escape can put it back.
    // Empty when no popup preview is in flight - which is also the flag, so
    // there is no second boolean to keep in step with it.
    QString myFamilyBeforePreview;
    // True while applyTheme() is writing the controls, so a control's own
    // valueChanged/currentTextChanged does not write straight back into
    // Theme. Not a cursor and not state: it is the standard guard against a
    // two-way binding oscillating, and it is false everywhere outside that
    // one function.
    bool mySyncing = false;
};

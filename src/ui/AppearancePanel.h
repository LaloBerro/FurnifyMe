#pragma once
// Every colour token and the type scale, opened to the user.
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
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class QComboBox;
class QColorDialog;
class QLabel;
class QSpinBox;
class QVBoxLayout;
class QEvent;
class QObject;

class AppearancePanel : public QWidget {
    Q_OBJECT

public:
    explicit AppearancePanel(QWidget* parent = nullptr);

    // The panel's own edit path. A swatch click, the size spinner, the family
    // combo and the reset button all land here, and so does the suite - so
    // what a test drives is what a user drives, not a parallel entry point.
    void setTokenColour(const QString& id, const QColor& colour);
    void setBaseSize(double pt);
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
    QLabel* myTitle = nullptr;
    QLabel* mySizeLabel = nullptr;
    QLabel* myFamilyLabel = nullptr;
    QSpinBox* mySize = nullptr;
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

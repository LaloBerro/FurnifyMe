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
// The colour picker is MODELESS - QColorDialog::open(), never exec(). The
// app's no-modal law is about never blocking the user to say something, and a
// picker that froze the application would also make the live preview - the
// entire point of picking a colour here - impossible to see.
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
    QSpinBox* sizeControl() const { return mySize; }
    QComboBox* familyControl() const { return myFamily; }
    // The live modeless picker, or null when none is open. Exposed so the
    // suite can assert it is modeless and close it - a dialog left open would
    // trip the "no QDialog after an outcome" checks elsewhere in the suite,
    // and that check has to stay meaningful.
    QColorDialog* activeColourDialog() const { return myDialog; }

protected:
    void paintEvent(QPaintEvent* event) override;
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
    QColorDialog* myDialog = nullptr;
    // True while applyTheme() is writing the controls, so a control's own
    // valueChanged/currentTextChanged does not write straight back into
    // Theme. Not a cursor and not state: it is the standard guard against a
    // two-way binding oscillating, and it is false everywhere outside that
    // one function.
    bool mySyncing = false;
};

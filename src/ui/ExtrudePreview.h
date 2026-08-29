#pragma once
// OCCT first: Handle() is a macro that collides with some Windows headers Qt
// drags in.
#include <TopoDS_Face.hxx>

// Replaces the old QInputDialog::getDouble() for extrude height: a small
// field docked over the viewport, whose every edit rebuilds a transparent
// preview through the SAME ModelingOps::extrude() call the commit uses. A
// preview built by a different path than the commit is a lie - this is the
// one place a user judges a number by what it looks like.
//
// The field is a real focusable QLineEdit, and follows Toast::UndoControl /
// WalkthroughPanel::SkipControl exactly (see WalkthroughPanel.cpp for the
// full reasoning): this panel paints its own background and label and is
// transparent to mouse events over its own body, so the field cannot be a
// child of it - Qt::WA_TransparentForMouseEvents excludes a widget's ENTIRE
// SUBTREE from hit-testing, not just the widget carrying it. The field is
// therefore a SIBLING, parented to the same viewport, with its geometry and
// visibility derived from this panel's own move/show/hide rather than
// trusted to an event that might never arrive, and Qt::WA_NoMousePropagation
// so a click into it cannot bubble to the viewport underneath and re-pick.
#include <QPointer>
#include <QStringList>
#include <QWidget>

class MainWindow;
class OcctViewWidget;
class QHideEvent;
class QLineEdit;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;

class ExtrudePreview : public QWidget {
    Q_OBJECT

public:
    ExtrudePreview(MainWindow* window, OcctViewWidget* view);
    ~ExtrudePreview() override;

    // Shows the panel, focuses the field, and previews `face` at the default
    // 10 mm - unconditionally, regardless of whatever height a previous use
    // left in the field.
    void begin(const TopoDS_Face& face);

    // Removes the preview shape from the viewport and hides the panel. The
    // pending face itself is untouched - MainWindow still owns it, so the
    // user can press Extrude again and try another height.
    void cancel();

    double height() const;
    bool hasPreview() const { return myHasPreview; }
    // Defined in the .cpp, not inline here: QPointer<QLineEdit>'s converting
    // operator needs QLineEdit to be a complete type, and this header only
    // forward-declares it (the same reason Toast.h keeps its own sibling
    // control behind a QWidget* rather than its concrete UndoControl type).
    QLineEdit* field() const;

    // Every string this panel paints, for gui_smoke's banned-word sweep -
    // the same reason WalkthroughPanel, HintBalloon and Toast each expose
    // one.
    QStringList paintedTexts() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QRect fieldRect() const;
    void syncFieldGeometry();
    // Centred along the top edge, at ViewportOverlay's own edge margin -
    // clear of the bottom strip where the toast, the walkthrough guide and
    // the hint balloon all live, and of the chip clusters docked in the top
    // corners. See ExtrudePreview.cpp for why that placement was chosen over
    // tracking the other three at runtime.
    void reposition();
    // Rebuilds the preview from the field's current text through
    // ModelingOps::extrude(). Leaves the last good preview alone - and marks
    // the field - on anything that does not parse or that extrude rejects.
    void updatePreview();
    void commit();
    void markInvalid(bool invalid);

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;
    TopoDS_Face myFace;
    bool myHasPreview = false;
    bool myInvalid = false;
    double myHeight = 10.0;   // last successfully previewed height
    QPointer<QLineEdit> myField;   // sibling, not a child - see class comment
};

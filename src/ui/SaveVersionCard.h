#pragma once
//
// File -> Save version...'s panel: one field, a name, over the viewport
// instead of a modal QInputDialog. Built on ExtrudePreview's exact key-claim
// contract (see ExtrudePreview.h for the full reasoning, repeated only in
// summary here): a real focusable QLineEdit that is a SIBLING of this panel
// rather than a child of it (this panel paints its own background and is
// transparent to mouse events over its own body, and
// Qt::WA_TransparentForMouseEvents excludes a widget's entire subtree from
// hit-testing), and an application-wide Enter/Escape claim - a
// QEvent::ShortcutOverride grab plus a KeyPress filter, installed on show()
// and removed on hide() - so the two keys reach this panel regardless of
// what holds focus, the same reason an RMB orbit must not silently strand a
// live ExtrudePreview.
//
// Disjointness: MainWindow gates the "Save version..." action on
// !hasPendingFace() && !canPullSelectedFace() && !canBevelSelectedEdge() &&
// !canTransformSelectedBody() - every OTHER app-wide key claim this app can
// have live at once. This panel can therefore never be open while
// ExtrudePreview, the pull arrow or the bevel arrow are, so their four
// application-wide filters can never collide - see MainWindow::canOpenSaveVersion().
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

class SaveVersionCard : public QWidget {
    Q_OBJECT

public:
    SaveVersionCard(MainWindow* window, OcctViewWidget* view);
    ~SaveVersionCard() override;

    // Shows the panel with an empty field, focused - unconditionally, same
    // as ExtrudePreview::begin() always seeding its own default.
    void begin();
    // Hides the panel; the typed name, if any, is simply discarded.
    void cancel();

    void replace();
    QLineEdit* field() const;

    // Every string this panel paints - the label and the key hint. NOT the
    // typed name: that is the field's own live text, and ExtrudePreview's
    // own paintedTexts() already excludes its numeric field's text on
    // exactly the same reasoning - an in-progress field value is not
    // painted copy, it is either nothing yet or the user's own words.
    QStringList paintedTexts() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyTheme();
    QRect fieldRect() const;
    QRect hintRect() const;
    QString labelText() const;
    QString hintText() const;
    void syncFieldGeometry();
    void reposition();
    void commit();
    void markInvalid(bool invalid);
    // Mirrors ExtrudePreview::onAppStateChanged()'s reasoning: this panel's
    // own predicate (MainWindow::canOpenSaveVersion()) can go false out from
    // under it while it is open - a sketch started with Ctrl+K while this
    // panel sits over the viewport, say - and nothing else would tell it to
    // close. One slot on the one signal that already fires after every
    // route that could do that, rather than trusting each present and
    // future route to remember to call cancel() individually.
    void onAppStateChanged();

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;
    bool myInvalid = false;
    QPointer<QLineEdit> myField;   // sibling, not a child - see class comment
};

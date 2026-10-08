#pragma once
// The Items drawer's permanent "Filter by name" field (T-063). A typed field
// keeps its focus and marks itself with an accentText underline while it holds
// a query or has focus - no focus ring, this app's rule for every field.
//
// It owns its keys while focused: QLineEdit already claims every printable
// key's ShortcutOverride, so a letter typed here never fires E, S, L or M,
// and this claims Escape the same way so Cancel Sketch cannot take it either -
// but only while there is a query to clear. Escape is ANNOUNCED, not acted
// on: the drawer's Clear filter action is the one thing that clears,
// whichever route asks. On an EMPTY field Escape is not claimed at all: the
// field hands the focus back and the key goes on to whatever owns it.
//
// Down and Enter hand over to the LIST (ruling D2): Tab passes the list by
// (it is Qt::ClickFocus), so without them the keyboard could reach the field
// and never the rows under it. Which keys do that is the drawer's one key
// table's answer (ItemsPanel::listKeys()), never a second list written here.
#include <QLineEdit>

class ItemsFilterField : public QLineEdit {
    Q_OBJECT

public:
    explicit ItemsFilterField(QWidget* parent = nullptr);
    // Re-reads every token. Called on a theme change and whenever the field
    // goes in or out of use (a query appears or empties, focus comes or goes).
    void applyTheme();
    static QString placeholder();

signals:
    void escapePressed();
    // Down or Enter: the drawer moves the focus into its rows.
    void listRequested();

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    bool inUse() const { return hasFocus() || !text().isEmpty(); }
    // Escape on an EMPTY field: focus goes back to the nearest enclosing
    // widget that takes it (the Items list, which is Qt::ClickFocus), never
    // stays parked here.
    void releaseFocus();
    bool myInUse = false;
};

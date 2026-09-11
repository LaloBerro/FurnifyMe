#pragma once
// The joint's chip (joinery, Task 13): the small card beside a selected joint
// where its kind is switched, its numbers are edited, and the piece it is cut
// into is chosen. The last piece of joinery UI.
//
// Two design picks are its contract (the user's own, one decision per round):
//
//   Round 1, B - J places a joint at once and the kind is switched AFTERWARDS,
//   from a kind button on the chip's top line. Its menu lists all ten kinds,
//   grouped fasteners / housings / interlocks, the current one ticked, a kind
//   the contact refuses greyed with Joinery::validityOf()'s real reason on a
//   line beneath it. Picking one is ONE checkpoint.
//
//   Round 3, C - the chip opens SMALL, carrying the family's one defining
//   number (a fastener's count, a housing's depth, a tenon's length, the depth
//   a half-lap takes out of each piece), with a More line that opens the rest
//   inside the same card and a Fewer line that folds them. More does not
//   persist: the chip folds whenever its joint stops being the selected one.
//
// BevelArrow's shape and its rules, one widget further along: the
// application-wide Enter/Escape claim (KeyClaim, installed on show and removed
// on hide, answering ShortcutOverride), GestureChip's frame and placement
// beside a projected anchor, a size through Theme::wholeDevicePixels() and a
// position through Theme::snapToDevicePixels(), fields read in the DISPLAYED
// unit through Measure::parseLength(), and every painted string exposed for
// the vocabulary sweep. Enter applies every changed field as one checkpoint;
// Escape puts the fields back and leaves the joint exactly as it was.
//
// What is different is that this card is INTERACTIVE all over - a kind button,
// up to nine fields, toggles, two segment pairs - so, unlike BevelArrow (whose
// card is transparent to the mouse and whose one field is a sibling), the card
// takes the mouse itself and its controls are real children. It carries
// WA_NoMousePropagation and accepts press, release and double-click alike, so
// no click on its padding ever reaches the viewport and re-picks.
//
// Visibility is DERIVED, never stored: MainWindow::jointChipJointId(), read on
// every appStateChanged. That predicate requires the joint's own two pieces to
// be the whole-body selection, which keeps this card disjoint from the face
// pull (a face), the bevel arrow (an edge) and the transform gizmo (exactly
// one body) by construction, and from ExtrudePreview and a live Mirror
// placement by explicit terms - so at most one application-wide Enter/Escape
// claim is ever installed (CLAUDE.md, "Direct modeling").
#include "DocumentModel.h"
#include "Joinery.h"

#include <gp_Pnt.hxx>

#include <QPointer>
#include <QStaticText>
#include <QStringList>
#include <QWidget>

#include <array>
#include <vector>

class JointKindMenu;
class MainWindow;
class OcctViewWidget;
class QAbstractButton;
class QHideEvent;
class QLineEdit;
class QPushButton;
class QShowEvent;

class JointChip : public QWidget {
    Q_OBJECT

public:
    // Every number the chip can carry. Which of them a joint shows is its
    // family's (see familySlots() in the .cpp); the rest stay hidden.
    enum class Slot { Count, Size, Inset, DepthA, DepthB, Width, Stop, Thickness, Length };
    static constexpr int kSlotCount = 9;

    JointChip(MainWindow* window, OcctViewWidget* view);
    ~JointChip() override;

    // THE predicate's mirror, connected to MainWindow::appStateChanged(). Shows
    // for MainWindow::jointChipJointId(), hides otherwise, and re-seeds the
    // fields whenever the joint itself changed underneath them (an undo, a
    // commit, a kind switch, the display unit).
    void refresh();
    // Beside the joint's anchor (its first item, projected). Driven by
    // OcctViewWidget::cameraChanged.
    void reposition();
    // Re-places AND re-raises, from ViewportOverlay::laidOut().
    void replace();

    int jointId() const { return myJointId; }

    // The main field - the first of a half-lap's pair.
    QLineEdit* field() const;
    // The field for `slot`, or null when this joint's kind does not carry it.
    // Hidden behind a folded More is still "carried": the pointer is returned
    // and isVisible() says whether it is on screen.
    QLineEdit* fieldFor(Slot slot) const;
    bool isFieldInvalid(Slot slot) const;

    QPushButton* kindButton() const { return myKindButton; }
    QPushButton* moreButton() const { return myMoreButton; }
    QPushButton* stoppedButton() const { return myStoppedButton; }
    QPushButton* haunchedButton() const { return myHaunchedButton; }
    // "Cut into" - the segment for `bodyId`, or null when the kind has no host
    // choice (fasteners, a half-lap) or `bodyId` is not one of its pieces.
    QPushButton* hostButtonFor(int bodyId) const;
    // "Drilled from" - a pocket screw's two faces, or null for any other kind.
    QPushButton* drilledFromButton(Joinery::DrilledFrom from) const;

    // Whether this kind has anything behind More at all (a half-lap does not).
    bool hasMore() const;
    bool moreOpen() const { return myMoreOpen; }
    void setMoreOpen(bool open);

    bool kindMenuOpen() const;
    void openKindMenu();
    void closeKindMenu();
    QWidget* kindMenu() const;
    // The menu's entry for `kind`, and the reason shown beneath it when the
    // contact refuses it (empty when it is offered).
    QAbstractButton* kindMenuEntry(Joinery::Kind kind) const;
    QString kindMenuReason(Joinery::Kind kind) const;

    // The kind, in the user's words - the kind button's own word.
    QString labelText() const;
    // The main field's text as it stands (both of a half-lap's, joined).
    QString valueText() const;
    QString hintText() const;

    // This app's copy, for the banned-word sweep - never a piece name.
    QStringList paintedTexts() const;
    // The piece names painted on the card, for the sweep's user-data channel.
    QStringList paintedNames() const;

    // Enter: every changed field, parsed in the displayed unit, as ONE
    // MainWindow::editJointParameters() call - one checkpoint. A field that
    // does not parse is marked and the refusal is said, and nothing is written.
    void commit();
    // Escape: the fields go back to the joint's own values; the joint is not
    // touched.
    void cancel();

    // How many times the card has been laid out - the suite's proof that a
    // camera move re-places the card without re-measuring it.
    int layoutCount() const { return myLayoutCount; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    // One piece of painted copy, laid out once in relayout() and drawn as-is by
    // paintEvent() - the per-frame budget: no measuring, eliding or shaping in
    // a paint this card is charged for on every orbit frame.
    struct PaintedText {
        QStaticText text;
        QPointF at;
        QFont font;
        bool muted = true;
        bool userData = false;
        QString raw;
    };

    void applyTheme();
    void end();
    void reseed(const DocumentModel::Joint& joint);
    void relayout();
    void restyleField(Slot slot);
    void syncToggleTexts();
    void updateAnchor();
    void placeKindMenu();
    void onKindPicked(Joinery::Kind kind);
    void onFieldEdited(Slot slot);
    QString seedSignature(const DocumentModel::Joint& joint) const;
    QString textFor(Slot slot, const Joinery::Parameters& params) const;
    QString kindButtonText() const;
    QString slotLabel(Slot slot) const;
    // Parses `slot`'s field. False (with `why` set) when the text is not a
    // number in this slot's grammar or is outside what the slot can hold.
    bool parseSlot(Slot slot, Joinery::Parameters& into, QString& why) const;
    std::vector<Slot> familySlots() const;
    std::vector<QLineEdit*> tabOrder() const;

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;

    // The joint on show, as it was when the fields were last seeded - what
    // Escape returns to and what Enter compares against.
    int myJointId = 0;
    DocumentModel::Joint myJoint;
    QString myNameA;
    QString myNameB;
    QString mySeed;
    gp_Pnt myAnchor;

    bool myMoreOpen = false;
    int myLayoutCount = 0;
    int myRuleY = -1;
    // An outside press that closed the kind menu owns its release too -
    // ShapeFlyout's one-event-longer rule.
    bool mySwallowNextRelease = false;

    QPushButton* myKindButton = nullptr;
    QPushButton* myMoreButton = nullptr;
    std::array<QLineEdit*, kSlotCount> myFields{};
    std::array<QString, kSlotCount> mySeededText{};
    std::array<bool, kSlotCount> myInvalid{};
    QPushButton* myStoppedButton = nullptr;
    QPushButton* myHaunchedButton = nullptr;
    // "Cut into", ordered by body id so a swap never trades the two places.
    std::array<QPushButton*, 2> myHostButtons{};
    std::array<int, 2> myHostBodies{};
    // "Drilled from", index = static_cast<int>(DrilledFrom).
    std::array<QPushButton*, 2> myDrilledFromButtons{};
    Joinery::DrilledFrom myPendingDrilledFrom = Joinery::DrilledFrom::InsetFace;

    std::vector<PaintedText> myTexts;
    QPointer<JointKindMenu> myKindMenu;   // sibling in the viewport, not a child
};

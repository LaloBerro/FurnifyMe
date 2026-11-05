#pragma once

// WHAT A GESTURE CHIP SAYS, AS DATA (migration Phase 4a). The editor's chrome
// page draws the chips - Boolean and Slats first - while the Qt chip stays the
// chip: its place, its Enter/Escape claim, its preview and its commit. This is
// the seam between the two.
//
// A chip registers its REAL controls (its QLineEdits and buttons) by id and
// describes itself as rows of parts read off them - words, fields, buttons,
// toggles (ruling R1: content in C++, layout in the page). An edit from the
// page lands IN those controls - a field's setText(), a button's click() - so
// the chip's own signals run exactly as they do for a Qt click, and a commit
// writes the page's values in before it runs, because a mirrored edit can lag
// one message behind the Enter that follows it (ruling R3).
//
// Drawn elsewhere, the chip paints nothing, hides its controls and lets the
// pointer through to the page; its size is the page's measurement, so its own
// placement code runs unchanged (ruling R2).
//
// No WebEngine here: an ordinary QObject over widgets, built in both presets
// and tested offscreen (chip_face_tests).

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QWidget>

#include <functional>

class QAbstractButton;
class QLineEdit;

class ChipFace : public QObject
{
    Q_OBJECT
public:
    // `name` is the chip's card name on the page; `chip` the Qt chip itself.
    ChipFace(const QString& name, QWidget* chip);

    QString name() const { return myName; }
    QWidget* chip() const { return myChip; }

    // The controls the page may write, by id. Their own change signals are
    // announced (notify()).
    void addField(const QString& id, QLineEdit* field);
    void addButton(const QString& id, QAbstractButton* button);
    // Rows of parts, in order, read fresh on every face().
    void setDescriber(std::function<QVariantList()> rows);
    // Enter's and Escape's routes - the chip's own commit() and cancel().
    void setActions(std::function<void()> commit, std::function<void()> cancel);

    // Parts. text(): words in a tone - "title", "label", "hint", "value",
    // "muted" or "danger". field(): a registered field with its label.
    // button(): a registered button; a checkable one is a "toggle".
    static QVariantMap text(const QString& text, const QString& tone = QStringLiteral("label"));
    QVariantMap field(const QString& id, const QString& label) const;
    QVariantMap button(const QString& id) const;

    // {rows, invalid, seed, focus}.
    QVariantMap face() const;
    // The chip's input does not read: the page outlines the card in danger.
    void setInvalid(bool invalid);
    // A new gesture: the page re-seeds its fields and focuses `focusId`
    // (ruling R5).
    void reseed(const QString& focusId = QString());

    // Announces a change - once per event-loop turn, however many arrive.
    void notify();

    // From the page. Each refuses while the chip is hidden, and an id that is
    // not of the right kind; edit/press/commit write through the controls.
    bool edit(const QString& id, const QString& text);
    bool press(const QString& id);
    bool commit(const QVariantMap& values);
    bool cancel();

    // Drawn by the page (ruling R2): the chip paints nothing and takes no
    // pointer, its controls hide. The chip reads controlsShown() wherever it
    // shows them, and re-syncs on drawnChanged().
    void setDrawnElsewhere(bool drawnElsewhere);
    bool drawnElsewhere() const { return myDrawnElsewhere; }
    bool controlsShown() const { return !myDrawnElsewhere; }
    // The page measured the card; invalid until it has (and once it is drawn
    // here again).
    void setFaceSize(QSize size);
    QSize faceSize() const { return myFaceSize; }

    // Keys (ruling R3): `view` is the page's view; `holds` says whether a
    // field of THIS chip on the page holds the keyboard. While both hold, a
    // key aimed at the page passes the chip's claim to the page's field.
    void setPageKeys(QWidget* view, std::function<bool()> holds);
    bool keyBelongsToPage(QObject* watched) const;

signals:
    void changed();
    void drawnChanged(bool drawnElsewhere);
    void faceSizeChanged();

private:
    QString myName;
    QPointer<QWidget> myChip;
    QHash<QString, QPointer<QLineEdit>> myFields;
    QHash<QString, QPointer<QAbstractButton>> myButtons;
    std::function<QVariantList()> myDescriber;
    std::function<void()> myCommit;
    std::function<void()> myCancel;
    bool myInvalid = false;
    int mySeed = 0;
    QString myFocus;
    bool myNotifyPending = false;
    bool myDrawnElsewhere = false;
    bool myWasMouseTransparent = false;
    QSize myFaceSize;
    QPointer<QWidget> myPageView;
    std::function<bool()> myPageHolds;
};

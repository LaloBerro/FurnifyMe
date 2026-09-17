#pragma once
// The little editor a material tile opens on a double-click: everything about
// what this material looks like on THIS furniture - its colour, how bright it
// is, how glossy, how metallic, and how its grain is scaled and turned.
//
// The last four moved here from the render panel, where they were ONE GLOBAL
// SET. The user's ask: "the data from the image should be per material, so
// add that into the material setting". They were right that it is a defect
// and not a preference - oak and walnut do not share a grain size any more
// than they share a colour, and choosing a second wood used to inherit the
// first one's numbers with no way to tell that had happened.
//
// It is not a QDialog - this app has none, and gui_smoke asserts as much.
// UnsavedCloseCard's shape, minus the scrim: this one does NOT cover the
// viewport, because the whole point of it is to watch the furniture change
// while the slider moves. It floats over the viewport's bottom left, takes
// its own clicks (WA_NoMousePropagation, so none of them reach the model
// behind it), and closes on Done, on Escape, or when the material it is about
// stops being the live one.
//
// It holds NO state of its own beyond what is being edited right now: every
// change is reported at once (MainWindow writes it to the document and pushes
// it to the viewport), so what is on screen and what is in the file are never
// two different answers.
#include <QColor>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class QAbstractButton;
class QHideEvent;
class QLabel;
class QShowEvent;
class QSlider;

class MaterialCard : public QWidget {
    Q_OBJECT

public:
    explicit MaterialCard(QWidget* viewport);

    // EVERYTHING one material looks like, in the card's own terms. A POD
    // rather than DocumentModel::MaterialLook: this is a ui class and has no
    // business including the document, which is the same line every other
    // card here draws (RenderSettingsPanel's Quality and ExportSize mirrors
    // are the precedent). MainWindow maps the two at its one wiring site.
    //
    // `surface` is the SLIDER's own sense - 0 matte, 1 glossy, dragging right
    // reads as glossier - and is the inverse of the kernel-facing roughness
    // OcctViewWidget deals in. The inversion stays where it always was, at
    // that one wiring site; nothing here says "roughness".
    struct Look {
        QColor colour{178, 178, 173};
        double brightness = 1.0;
        double surface = 0.45;
        double metal = 0.0;
        double grainSize = 300.0;
        double grainAngle = 0.0;
    };

    // Opens the card for `material`, seeded with everything it currently
    // looks like. Opening it again for another material re-seeds rather than
    // stacking a second card.
    void open(const QString& material, const Look& look);
    // THE ONE EDIT PATH, and it is both what a drag lands in and what the
    // suite drives - RenderSettingsPanel's own rule, one card over. It
    // clamps, pushes every control and reports the change, so a test that
    // calls it exercises exactly what a user moving a slider exercises.
    // open() deliberately does NOT report: seeding is not an edit.
    void setLook(const Look& look);
    void close();
    bool isOpen() const { return !isHidden(); }
    QString material() const { return myMaterial; }

    Look look() const { return myLook; }
    QColor colour() const { return myLook.colour; }
    double brightness() const { return myLook.brightness; }

    // Re-places the card against the viewport's bottom left.
    void replace();

    QStringList paintedTexts() const;

signals:
    // Live, on every move of any slider and every colour picked: the render
    // is the preview, so there is nothing to apply afterwards.
    //
    // ONE signal carrying the whole look rather than one per dial. The
    // listener writes a single record and pushes a single set of values at
    // the viewport, so splitting this would have been six ways to say the
    // same sentence and six chances for two of them to disagree.
    void changed(QString material, MaterialCard::Look look);
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void layoutCard();
    // Every readout re-derived from myLook in one place, so a number on the
    // card and the value it describes cannot be two different answers.
    void syncReadouts();
    void applyStyles();
    void pickColour();
    void emitChange();

    QString myMaterial;
    Look myLook;

    QPointer<QAbstractButton> mySwatch;
    QPointer<QSlider> mySlider;
    QPointer<QAbstractButton> myDone;
    QPointer<QLabel> myValue;
    // The four that moved here. Each is a slider and a readout, built by the
    // same helper, and each reports through the one `changed` signal above.
    struct Dial {
        QPointer<QSlider> slider;
        QPointer<QLabel> value;
        QPointer<QLabel> name;
    };
    Dial mySurface;
    Dial myMetal;
    Dial myGrainSize;
    Dial myGrainAngle;
    // Guards the seeding pass in open(), so pushing a value into a slider
    // does not report it straight back out as a user edit - RenderSettingsPanel's
    // mySyncing, the same problem one card over.
    bool mySeeding = false;
    bool myClaimInstalled = false;
};

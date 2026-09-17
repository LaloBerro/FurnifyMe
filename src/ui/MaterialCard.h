#pragma once
// The little editor a material tile opens on a double-click: what colour this
// material is on THIS furniture, and how bright.
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

    // Opens the card for `material`, seeded with the colour and brightness it
    // currently has. Opening it again for another material re-seeds rather
    // than stacking a second card.
    void open(const QString& material, const QColor& colour, double brightness);
    void close();
    bool isOpen() const { return !isHidden(); }
    QString material() const { return myMaterial; }

    QColor colour() const { return myColour; }
    double brightness() const { return myBrightness; }

    // Re-places the card against the viewport's bottom left.
    void replace();

    QStringList paintedTexts() const;

signals:
    // Live, on every move of the slider and every colour picked: the render
    // is the preview, so there is nothing to apply afterwards.
    void changed(QString material, QColor colour, double brightness);
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void layoutCard();
    void applyStyles();
    void pickColour();
    void emitChange();

    QString myMaterial;
    QColor myColour{178, 178, 173};
    double myBrightness = 1.0;

    QPointer<QAbstractButton> mySwatch;
    QPointer<QSlider> mySlider;
    QPointer<QAbstractButton> myDone;
    QPointer<QLabel> myValue;
    bool myClaimInstalled = false;
};

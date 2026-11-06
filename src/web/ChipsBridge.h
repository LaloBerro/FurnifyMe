#pragma once

// The page's `chips` object (migration Phase 4a): every gesture chip's face,
// by name, and the page's requests back into them. It decides nothing - each
// request lands in the chip's own controls through ChipFace, which refuses
// while the chip is hidden. No WebEngine here, so it builds and is tested in
// both presets.

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

class ChipFace;

class ChipsBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap faces READ faces NOTIFY facesChanged)
public:
    using QObject::QObject;
    void add(ChipFace* face);
    QVariantMap faces() const;

signals:
    // Carries the faces WITH it: a property's NOTIFY is batched by
    // QWebChannel on a timer, and a chip's words must arrive with its change.
    void facesChanged(const QVariantMap& faces);

public slots:
    void edit(const QString& chip, const QString& id, const QString& text);
    void press(const QString& chip, const QString& id);
    // A slider part dragged on the page (Phase 7a): lands in the QSlider.
    void slide(const QString& chip, const QString& id, int value);
    // Enter on the page: the values its fields hold ride WITH it (ruling R3).
    void commit(const QString& chip, const QVariantMap& values);
    void cancel(const QString& chip);
    // The page measured the chip's card; the Qt chip takes the size.
    void reportSize(const QString& chip, int width, int height);

private:
    ChipFace* faceNamed(const QString& chip) const;
    QHash<QString, QPointer<ChipFace>> myFaces;
};

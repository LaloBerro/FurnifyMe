#include "ChipsBridge.h"

#include "ChipFace.h"

void ChipsBridge::add(ChipFace* face)
{
    if (!face) return;
    myFaces.insert(face->name(), face);
    connect(face, &ChipFace::changed, this, [this] { emit facesChanged(faces()); });
}

QVariantMap ChipsBridge::faces() const
{
    QVariantMap map;
    for (auto it = myFaces.cbegin(); it != myFaces.cend(); ++it)
        if (it.value()) map.insert(it.key(), it.value()->face());
    return map;
}

ChipFace* ChipsBridge::faceNamed(const QString& chip) const
{
    return myFaces.value(chip);
}

void ChipsBridge::edit(const QString& chip, const QString& id, const QString& text)
{
    if (ChipFace* face = faceNamed(chip)) face->edit(id, text);
}

void ChipsBridge::press(const QString& chip, const QString& id)
{
    if (ChipFace* face = faceNamed(chip)) face->press(id);
}

void ChipsBridge::commit(const QString& chip, const QVariantMap& values)
{
    if (ChipFace* face = faceNamed(chip)) face->commit(values);
}

void ChipsBridge::cancel(const QString& chip)
{
    if (ChipFace* face = faceNamed(chip)) face->cancel();
}

void ChipsBridge::reportSize(const QString& chip, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (ChipFace* face = faceNamed(chip)) face->setFaceSize(QSize(width, height));
}

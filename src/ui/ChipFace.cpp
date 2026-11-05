#include "ChipFace.h"

#include <QAbstractButton>
#include <QLineEdit>
#include <QTimer>

ChipFace::ChipFace(const QString& name, QWidget* chip)
    : QObject(chip)
    , myName(name)
    , myChip(chip)
{
}

void ChipFace::addField(const QString& id, QLineEdit* field)
{
    if (!field) return;
    myFields.insert(id, field);
    connect(field, &QLineEdit::textChanged, this, &ChipFace::notify);
}

void ChipFace::addButton(const QString& id, QAbstractButton* button)
{
    if (!button) return;
    myButtons.insert(id, button);
    connect(button, &QAbstractButton::toggled, this, &ChipFace::notify);
}

void ChipFace::setDescriber(std::function<QVariantList()> rows)
{
    myDescriber = std::move(rows);
}

void ChipFace::setActions(std::function<void()> commit, std::function<void()> cancel)
{
    myCommit = std::move(commit);
    myCancel = std::move(cancel);
}

QVariantMap ChipFace::text(const QString& text, const QString& tone)
{
    return {{QStringLiteral("kind"), QStringLiteral("text")},
            {QStringLiteral("text"), text},
            {QStringLiteral("tone"), tone}};
}

QVariantMap ChipFace::field(const QString& id, const QString& label) const
{
    const QLineEdit* field = myFields.value(id);
    if (!field) return {};
    return {{QStringLiteral("kind"), QStringLiteral("field")},
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), label},
            {QStringLiteral("value"), field->text()},
            {QStringLiteral("tip"), field->toolTip()},
            {QStringLiteral("enabled"), field->isEnabled()}};
}

QVariantMap ChipFace::button(const QString& id) const
{
    const QAbstractButton* button = myButtons.value(id);
    if (!button) return {};
    QString text = button->text();
    text.remove(QLatin1Char('&'));
    return {{QStringLiteral("kind"), button->isCheckable() ? QStringLiteral("toggle") : QStringLiteral("button")},
            {QStringLiteral("id"), id},
            {QStringLiteral("text"), text},
            {QStringLiteral("tip"), button->toolTip()},
            {QStringLiteral("enabled"), button->isEnabled()},
            {QStringLiteral("checked"), button->isChecked()}};
}

QVariantMap ChipFace::face() const
{
    return {{QStringLiteral("rows"), myDescriber ? myDescriber() : QVariantList()},
            {QStringLiteral("invalid"), myInvalid},
            {QStringLiteral("seed"), mySeed},
            {QStringLiteral("focus"), myFocus}};
}

void ChipFace::setInvalid(bool invalid)
{
    if (myInvalid == invalid) return;
    myInvalid = invalid;
    notify();
}

void ChipFace::reseed(const QString& focusId)
{
    ++mySeed;
    myFocus = focusId;
    notify();
}

void ChipFace::notify()
{
    // One announcement per turn: a gesture's begin() seeds three fields and
    // re-previews, and the page wants the face once it has settled.
    if (myNotifyPending) return;
    myNotifyPending = true;
    QTimer::singleShot(0, this, [this] {
        myNotifyPending = false;
        emit changed();
    });
}

bool ChipFace::edit(const QString& id, const QString& text)
{
    if (!myChip || myChip->isHidden()) return false;
    QLineEdit* field = myFields.value(id);
    if (!field) return false;
    if (field->text() != text) field->setText(text);
    return true;
}

bool ChipFace::press(const QString& id)
{
    if (!myChip || myChip->isHidden()) return false;
    QAbstractButton* button = myButtons.value(id);
    if (!button || !button->isEnabled()) return false;
    button->click();
    return true;
}

bool ChipFace::commit(const QVariantMap& values)
{
    if (!myChip || myChip->isHidden()) return false;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (!myFields.value(it.key())) return false;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        QLineEdit* field = myFields.value(it.key());
        const QString text = it.value().toString();
        if (field->text() != text) field->setText(text);
    }
    if (myCommit) myCommit();
    return true;
}

bool ChipFace::cancel()
{
    if (!myChip || myChip->isHidden()) return false;
    if (myCancel) myCancel();
    return true;
}

void ChipFace::setDrawnElsewhere(bool drawnElsewhere)
{
    if (myDrawnElsewhere == drawnElsewhere || !myChip) return;
    myDrawnElsewhere = drawnElsewhere;
    if (drawnElsewhere) {
        myWasMouseTransparent = myChip->testAttribute(Qt::WA_TransparentForMouseEvents);
        myChip->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        for (const QPointer<QLineEdit>& field : std::as_const(myFields))
            if (field) field->hide();
        for (const QPointer<QAbstractButton>& button : std::as_const(myButtons))
            if (button) button->hide();
    } else {
        myChip->setAttribute(Qt::WA_TransparentForMouseEvents, myWasMouseTransparent);
        // A button that is the chip's own child shows with it again; a sibling
        // control is the chip's to show (drawnChanged).
        for (const QPointer<QAbstractButton>& button : std::as_const(myButtons))
            if (button && button->parentWidget() == myChip) button->show();
        myFaceSize = QSize();
    }
    myChip->update();
    emit drawnChanged(drawnElsewhere);
    notify();
}

void ChipFace::setFaceSize(QSize size)
{
    if (!size.isValid() || size.isEmpty() || size == myFaceSize) return;
    myFaceSize = size;
    emit faceSizeChanged();
}

void ChipFace::setPageKeys(QWidget* view, std::function<bool()> holds)
{
    myPageView = view;
    myPageHolds = std::move(holds);
}

bool ChipFace::keyBelongsToPage(QObject* watched) const
{
    if (!myDrawnElsewhere || !myPageView || !myPageHolds || !myPageHolds()) return false;
    auto* widget = qobject_cast<QWidget*>(watched);
    return widget && (widget == myPageView || myPageView->isAncestorOf(widget));
}

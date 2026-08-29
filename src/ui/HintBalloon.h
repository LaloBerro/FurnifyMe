#pragma once
//
// A small balloon that teaches one capability the first time it becomes
// available, and never again once the user has done it three times. It owns
// both the widget and the policy: which hint is due is decided here, from
// UserProgress plus live application state.
//
#include <QString>
#include <QWidget>

#include <set>

class MainWindow;

class HintBalloon : public QWidget {
    Q_OBJECT

public:
    HintBalloon(MainWindow* window, QWidget* parent);

    // The text currently shown, or empty when no hint is up.
    QString currentHint() const { return myText; }

    // Every string this balloon paints, so gui_smoke's banned-word sweep can
    // reach it - the sweep only walks action text and widget tooltips
    // otherwise, and this widget paints its own copy rather than exposing it
    // through either of those.
    QStringList paintedTexts() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void reconsider();
    void showHint(const QString& event, const QString& text);
    void dismiss();

    MainWindow* myWindow = nullptr;
    QString myText;
    QString myEvent;     // the progress event this hint teaches
    // Per-hint, not global: a single flag would mean the first balloon shown
    // silenced the other two for the rest of the session.
    std::set<QString> myShownThisSession;
};

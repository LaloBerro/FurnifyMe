#pragma once
//
// A centred overlay listing every keyboard shortcut. Its rows are generated
// from the window's own QActions rather than written by hand, so it cannot go
// stale the moment somebody adds a binding - the same principle as the
// vocabulary test: make the documentation executable. The grouping is
// generated too: rows sit under the menu that owns them, read off the menu
// bar, so a heading cannot drift from the menu it names either.
//
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class QAction;

class ShortcutSheet : public QWidget {
    Q_OBJECT

public:
    explicit ShortcutSheet(QWidget* parent);

    // Rebuilds from the parent window's actions, then shows and centres itself.
    void showSheet();

    // Total shortcut rows, headings excluded - one per bound action.
    int rowCount() const;

    // Every string this sheet paints: the title, the group headings, and each
    // row's label and keys. Painted copy is invisible to gui_smoke's
    // banned-word sweep otherwise, the same blind spot WalkthroughPanel and
    // HintBalloon close the same way. Const, and independent of whether the
    // sheet has ever been opened, because it rebuilds the same groups from
    // the same actions the paint path does rather than keeping a second copy
    // that only the sweep sees.
    QStringList paintedTexts() const;

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Row {
        QString label;
        QString keys;
    };
    struct Group {
        QString title;
        std::vector<Row> rows;
    };

    // Reads the parent window's menu bar and returns one group per menu that
    // owns at least one bound action, plus a trailing catch-all for any bound
    // action that lives outside the menus - a sheet that silently omitted one
    // would be exactly the stale documentation this class exists to prevent.
    std::vector<Group> buildGroups() const;
    void rebuild();
    void recentre();

    std::vector<Group> myGroups;
    // A click outside is swallowed so it cannot also pick in the viewport
    // behind an apparently-modal sheet; its release has to go the same way,
    // or the viewport picks on the release instead.
    bool mySwallowRelease = false;
};

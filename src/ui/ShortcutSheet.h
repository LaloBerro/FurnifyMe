#pragma once
//
// A centred overlay listing every keyboard shortcut. Its rows are generated
// from the window's own QActions rather than written by hand, so it cannot go
// stale the moment somebody adds a binding - the same principle as the
// vocabulary test: make the documentation executable.
//
#include <QWidget>

#include <vector>

class QAction;

class ShortcutSheet : public QWidget {
    Q_OBJECT

public:
    explicit ShortcutSheet(QWidget* parent);

    // Rebuilds from the parent window's actions, then shows and centres itself.
    void showSheet();

    int rowCount() const { return static_cast<int>(myRows.size()); }

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct Row {
        QString label;
        QString keys;
    };

    void rebuild();

    std::vector<Row> myRows;
};

// PHASE 0 SPIKE - isolation probe. A bare QWebEngineView, then the app's
// settings added one at a time, printing the page's own innerWidth each time:
//   web_probe.exe [--format] [--share] [--glparent | --glsibling] [--transparent]
#include "OcctViewWidget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QWebEnginePage>
#include <QWebEngineView>

#include <cstdio>

int main(int argc, char* argv[])
{
    QStringList args;
    for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    if (args.contains(QStringLiteral("--format")))
        QSurfaceFormat::setDefaultFormat(OcctViewWidget::surfaceFormat());
    if (args.contains(QStringLiteral("--share")))
        QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);

    QWidget top;
    top.setAttribute(Qt::WA_ShowWithoutActivating);
    top.resize(800, 600);
    QWidget* host = &top;
    if (args.contains(QStringLiteral("--glparent"))) {
        auto* gl = new QOpenGLWidget(&top);
        gl->setGeometry(0, 0, 800, 600);
        host = gl;
    }
    if (args.contains(QStringLiteral("--glsibling"))) {
        auto* gl = new QOpenGLWidget(&top);
        gl->setGeometry(0, 0, 800, 600);
    }
    auto* view = new QWebEngineView(host);
    view->raise();
    view->setGeometry(0, 0, 800, 600);
    if (args.contains(QStringLiteral("--transparent")))
        view->page()->setBackgroundColor(Qt::transparent);
    view->setHtml(QStringLiteral("<html><body style='background:transparent'>"
                                 "<div style='width:200px;height:100px;background:#232326'></div></body></html>"));
    top.show();

    bool loaded = false;
    QObject::connect(view, &QWebEngineView::loadFinished, [&](bool) { loaded = true; });
    QElapsedTimer t;
    t.start();
    while (!loaded && t.elapsed() < 10000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    while (t.elapsed() < 1500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QString answer;
    bool done = false;
    view->page()->runJavaScript(QStringLiteral("innerWidth + 'x' + innerHeight"), [&](const QVariant& v) {
        answer = v.toString();
        done = true;
    });
    t.restart();
    while (!done && t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    std::printf("%s -> loaded %d, page %s, render widget %s\n", qPrintable(args.join(' ')), loaded,
                qPrintable(answer),
                view->focusProxy() ? view->focusProxy()->metaObject()->className() : "none");
    return 0;
}

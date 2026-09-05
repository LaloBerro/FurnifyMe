#include "MainWindow.h"

#include "Measure.h"
#include "ModelingOps.h"
#include "OcctViewWidget.h"

#include "AppBar.h"
#include "AppearancePanel.h"
#include "RenderSettingsPanel.h"
#include "AxisGizmo.h"
#include "BevelArrow.h"
#include "ExtrudePreview.h"
#include "HintBalloon.h"
#include "IconSet.h"
#include "ItemsPanel.h"
#include "PullArrow.h"
#include "ShortcutSheet.h"
#include "Theme.h"
#include "Toast.h"
#include "ToolChip.h"
#include "ToolCluster.h"
#include "ViewportOverlay.h"
#include "VersionsPanel.h"
#include "WalkthroughPanel.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <ElSLib.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Vec.hxx>

#include <QAction>
#include <QSignalBlocker>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QCloseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

QString MainWindow::defaultLibraryRoot()
{
    // A SENTINEL, not the real path - resolveLibraryRoot() below is the one
    // place that actually asks QStandardPaths, so there is exactly one
    // implementation of "where the real library lives". Deliberately not
    // the empty string: see this method's declaration in MainWindow.h for
    // why an empty libraryRoot has to mean something else entirely (a
    // refusal, not "use the default"). The leading \x01 makes collision
    // with any string a caller could type or a path could resolve to
    // essentially impossible.
    static const QString sentinel =
        QStringLiteral("\x01__FurnifyMe_default_library_root__");
    return sentinel;
}

namespace {
// The real library location - QStandardPaths::DocumentsLocation +
// "/FurnifyMe" - is resolved here rather than inline in the initializer
// list below, purely so the constructor's own comment can stay next to the
// member it explains rather than a one-liner buried in a mem-initializer.
//
// Structural, not advisory: an EMPTY (or all-whitespace) libraryRoot
// reaching this function is refused outright with qFatal() rather than
// quietly treated as "use the default". That default is asked for through
// MainWindow::defaultLibraryRoot()'s own sentinel - the constructor's
// header default, so main.cpp's plain `MainWindow window;` still gets it
// for free - so an empty string here can only mean a caller passed one
// explicitly, most concretely a QTemporaryDir that failed to create and
// handed back "". Silently falling through to the real library in that
// case is exactly the failure mode this refusal exists to close off; the
// test suite's own RequiredTempDir (gui_smoke.cpp) is the other half of
// the same fix, at the point such a failure would actually originate.
QString resolveLibraryRoot(const QString& injected)
{
    if (injected == MainWindow::defaultLibraryRoot()) {
        return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
              QStringLiteral("/FurnifyMe");
    }
    if (injected.trimmed().isEmpty()) {
        qFatal("MainWindow: an empty library root was passed explicitly - refusing to "
              "silently fall back to the real furniture library. Pass "
              "MainWindow::defaultLibraryRoot() to ask for that on purpose, or a real "
              "path otherwise.");
    }
    return injected;
}

// The no-recursion camera sync's own epsilon compare - see
// MainWindow::syncCamera()'s declaration for the whole argument. Tight on
// purpose: the two widgets are meant to be pushed to EXACTLY the same
// CameraState by that function, so this only has to absorb floating-point
// noise, not genuine disagreement, and a loose epsilon would let a real
// follow (an orbit that moved the camera by less than the epsilon) go
// silently unsynced. CameraController.h gains nothing new for this - every
// field it needs is already public on CameraState.
bool camerasApproximatelyEqual(const CameraState& a, const CameraState& b)
{
    constexpr double kPosEps = 1.0e-6;     // mm
    constexpr double kAngleEps = 1.0e-6;   // degrees
    constexpr double kDistEps = 1.0e-6;    // mm
    return a.target.Distance(b.target) < kPosEps &&
           std::abs(a.azimuthDeg - b.azimuthDeg) < kAngleEps &&
           std::abs(a.elevationDeg - b.elevationDeg) < kAngleEps &&
           std::abs(a.distance - b.distance) < kDistEps;
}

// The compare view's corner badge: a family card (Theme::paintSurface,
// opaque, WA_NoMousePropagation) naming the version being compared, with a
// Close-compare control on it - ItemsPanel's own composition, not the
// sibling-widget trick ExtrudePreview/Toast need. Those two exist because
// their cards must let a click through to the model everywhere EXCEPT one
// small interactive area; this badge has no such requirement - it is a
// small, bounded card in a corner, exactly like ItemsPanel's own drawer -
// so its Close button can be an ordinary CHILD, hit-tested by Qt the normal
// way, with nothing to route around.
//
// No Q_OBJECT: it declares no signals of its own, and connecting an
// existing Qt signal (QPushButton::clicked, Theme::Notifier::changed) to a
// lambda needs no moc on the RECEIVING object - only on a class that
// declares its own signals or slots. MainWindow reads the button back
// through closeButton() and wires its own connection to closeCompare(),
// rather than this class knowing MainWindow exists.
class CompareBadge : public QWidget {
public:
    explicit CompareBadge(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_NoMousePropagation);
        // See Theme::makeSurfaceTransparent()'s own comment: without this,
        // the app-wide QSS background rule would stamp this card's corners -
        // which paintSurface() below leaves genuinely unpainted now - with a
        // flat chrome() square instead of letting the real compare view show
        // through them.
        Theme::makeSurfaceTransparent(this);

        myName = new QLabel(this);
        myClose = new QPushButton(MainWindow::compareBadgeCloseLabel(), this);
        myClose->setFixedHeight(22);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(kPad, kPad, kPad, kPad);
        layout->setSpacing(10);
        layout->addWidget(myName, 1);
        layout->addWidget(myClose);

        // Tracks the PARENT's own resize, not this widget's - the badge sits
        // at a fixed offset from myCompareView's own top-left corner, and
        // that offset's snapped device-pixel value depends on where
        // myCompareView itself lands inside the window (see reposition()),
        // which moves whenever the splitter handle is dragged. No Q_OBJECT
        // needed: eventFilter() overrides a plain virtual QObject already
        // declares.
        if (parent) parent->installEventFilter(this);

        applyTheme();
        connect(Theme::notifier(), &Theme::Notifier::changed, this,
                [this] { applyTheme(); });
    }

    // The version's name is USER TEXT - painted here raw and unmangled
    // (never truncated to a fixed banned-word-safe alphabet - CLAUDE.md's
    // point is that the user's own words are not this app's copy to
    // police), and deliberately not exposed through any paintedTexts()-style
    // accessor the vocabulary sweep would walk. See VersionsPanel.h's class
    // comment for the same rule applied to a row.
    void setVersionName(const QString& name)
    {
        const QFontMetrics fm(Theme::bodyFont());
        myName->setText(fm.elidedText(name, Qt::ElideRight, kMaxNameWidth));
        myName->setToolTip(name);
        growAndReposition();
    }

    QPushButton* closeButton() const { return myClose; }

    // Snaps this card's position off its parent's own placement inside the
    // window - Theme.h's position half of the whole-device-pixel rule, the
    // same one ViewportOverlay::relayout() applies to every anchored card.
    // Public so MainWindow can call it once right after construction
    // (before the first paint, when this card is not yet parent-resized) as
    // well as from the event filter below.
    void reposition()
    {
        QWidget* host = parentWidget();
        if (!host) return;
        const QPoint origin = host->mapTo(host->window(), QPoint(0, 0));
        const double dpr = host->devicePixelRatioF();
        move(Theme::snapToDevicePixels(kMargin, origin.x(), dpr),
             Theme::snapToDevicePixels(kMargin, origin.y(), dpr));
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        Theme::paintSurface(painter, rect(), 8);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == parentWidget() && event->type() == QEvent::Resize) reposition();
        return QWidget::eventFilter(watched, event);
    }

private:
    static constexpr int kPad = 10;
    static constexpr int kMaxNameWidth = 160;
    static constexpr int kMargin = 16;   // the same corner margin every other floating card uses

    // Theme.h's SIZE half of the whole-device-pixel rule: adjustSize() first,
    // to get this card's natural size from its own layout (the name label's
    // width changed), then grown to a whole number of device pixels so a
    // fractional display scale cannot leave an unpainted row along its far
    // edge over OCCT's GL surface - see ExtrudePreview::applyTheme() and
    // ViewportOverlay::relayout() for the same rule applied to a
    // setFixedSize() card and an anchored one respectively; this is the
    // adjustSize()-driven variant of the identical rule. reposition() runs
    // afterward too, since a font or padding change can, in principle, move
    // where this card's content wants to sit relative to its own top-left.
    void growAndReposition()
    {
        adjustSize();
        resize(Theme::wholeDevicePixels(size()));
        reposition();
    }

    void applyTheme()
    {
        myName->setStyleSheet(QStringLiteral("background: transparent; color: %1; font-size: %2pt;")
                                  .arg(Theme::text().name())
                                  .arg(Theme::bodyFont().pointSizeF()));
        myClose->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; color: %2; border: none; "
                          "border-radius: 4px; font-size: %3pt; padding: 2px 8px; } "
                          "QPushButton:hover { background-color: %4; }")
                .arg(Theme::chip().name(), Theme::text().name())
                .arg(Theme::labelFont().pointSizeF())
                .arg(Theme::chipHover().name()));
        growAndReposition();
        update();
    }

    QLabel* myName = nullptr;
    QPushButton* myClose = nullptr;
};

}  // namespace

namespace {

// The mirror-placement gesture's floating value chip (Milestone 4, Phase
// 3) - PullArrow's shape, pared down to what this gesture actually needs.
// There is nothing here to TYPE - the plane moves by dragging the handle
// OcctViewWidget draws in the scene, and jumps between the three
// axis-aligned presets on X/Y/Z - so this paints a label, a live offset
// readout and the hint line CLAUDE.md's own rule requires for a modeless
// gesture with keyboard verbs ("a modeless panel with invisible verbs went
// unnoticed for a whole branch" - ExtrudePreview's own lesson). Application-
// wide Enter/Escape/X/Y/Z claim, installed on show and removed on hide -
// ExtrudePreview's and PullArrow's own shape, and provably disjoint from
// theirs: MainWindow::canBeginMirrorPlacement() requires body-selection
// mode with no pending face, which is exactly what keeps ExtrudePreview,
// the pull arrow (face mode) and the bevel arrow (edge mode) from ever
// being live at the same time this is.
//
// No Q_OBJECT - CompareBadge's own reasoning above this class: every
// connection below is either an existing Qt signal into a lambda, or a
// plain member-function pointer, neither of which needs moc on the
// receiving object. A proper CHILD of the viewport (QWidget(view) below),
// so Qt's parent-child cascade destroys it with no manual teardown.
class MirrorPlacementChip : public QWidget {
public:
    MirrorPlacementChip(MainWindow* window, OcctViewWidget* view)
        : QWidget(view)
        , myWindow(window)
        , myView(view)
    {
        // Paints its own card and must never eat a click meant for the
        // model or the handle behind it - there is no interactive control
        // on this widget at all, unlike PullArrow's field.
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        // See Theme::makeSurfaceTransparent()'s own comment.
        Theme::makeSurfaceTransparent(this);
        applySize();
        hide();

        connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
            applySize();
            update();
        });
        if (myWindow)
            connect(myWindow, &MainWindow::appStateChanged, this, &MirrorPlacementChip::refresh);
        if (myView) {
            // The plane is drawn in the scene, so a camera move changes both
            // where the chip belongs on screen and (through worldPerPixel())
            // how big the plane rectangle itself is drawn - the latter is
            // OcctViewWidget's own concern (applyCameraState() already calls
            // updateMirrorPlacementIndicator()); this only has to follow the
            // handle's projected position.
            connect(myView, &OcctViewWidget::cameraChanged, this,
                    &MirrorPlacementChip::reposition);
            connect(myView, &OcctViewWidget::mirrorPlaneDragged, this,
                    [this](double) { onPlaneChanged(); });
        }
    }

    // THE predicate's own mirror, in one place, used to show and to hide -
    // connected to MainWindow::appStateChanged(), never driven from an
    // event, PullArrow::refresh()'s own rule. Reads
    // OcctViewWidget::mirrorPlacementActive() directly rather than a second
    // copy of MainWindow::canBeginMirrorPlacement(): that predicate answers
    // "can a gesture BEGIN", which stops being true about a body the moment
    // the gesture actually starts (transformableBodyId()'s own added term),
    // while this widget's own visibility has to track the gesture that is
    // already running.
    void refresh()
    {
        if (!myView) return;
        if (myView->mirrorPlacementActive()) {
            if (!isVisible()) {
                onPlaneChanged();
                reposition();
                show();
                raise();
            } else {
                onPlaneChanged();
            }
        } else if (isVisible()) {
            hide();
        }
    }

    // Re-places the chip against the handle's projected position. Driven by
    // OcctViewWidget::cameraChanged - PullArrow::reposition()'s own rule.
    void reposition()
    {
        if (!myView) return;
        gp_Pnt handlePoint;
        QPoint at;
        if (!myView->mirrorPlacementHandle(handlePoint) ||
            !myView->projectToScreen(handlePoint, at))
            return;

        // Beside the handle, flipped to the other side rather than clamped
        // when that would run off the right edge - PullArrow::reposition()'s
        // own layout, for the same reason: a chip that walked away from the
        // handle it labels would stop labelling it.
        int x = at.x() + kChipGap;
        if (x + width() > myView->width() - kEdgeInset) x = at.x() - kChipGap - width();
        x = std::clamp(x, kEdgeInset, std::max(kEdgeInset, myView->width() - width() - kEdgeInset));
        int y = at.y() - height() / 2;
        y = std::clamp(y, kEdgeInset,
                       std::max(kEdgeInset, myView->height() - height() - kEdgeInset));

        // Whole DEVICE pixels, Theme's position rule - PullArrow's own
        // closing lines.
        const QPoint origin = myView->mapTo(myView->window(), QPoint(0, 0));
        const double dpr = devicePixelRatioF();
        x = Theme::snapToDevicePixels(x, origin.x(), dpr);
        y = Theme::snapToDevicePixels(y, origin.y(), dpr);
        move(x, y);
    }

    // Re-places AND re-raises, from ViewportOverlay::laidOut() -
    // PullArrow::replace()'s own reason.
    void replace()
    {
        reposition();
        if (isVisible()) raise();
    }

    // Every string this chip paints, for gui_smoke's banned-word sweep -
    // MainWindow::mirrorPlacementPaintedTexts() is the reachable copy of
    // this same list (see labelText()/hintText()'s own comment).
    QStringList paintedTexts() const { return {labelText(), hintText()}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const int margin = Theme::surfaceShadowMargin();
        const QRect body = rect().adjusted(margin, margin, -margin, -margin);
        Theme::paintSurface(painter, body, 8);

        painter.setFont(Theme::labelFont());
        painter.setPen(Theme::text());
        painter.drawText(
            QRect(body.left() + kPad, body.top(), body.width() - kPad * 2, kLabelHeight),
            Qt::AlignVCenter | Qt::AlignLeft, labelText());

        painter.setFont(Theme::bodyFont());
        painter.setPen(Theme::accent());
        painter.drawText(QRect(body.left() + kPad, body.top() + kLabelHeight,
                               body.width() - kPad * 2, kValueHeight),
                         Qt::AlignVCenter | Qt::AlignLeft, valueText());

        painter.setFont(Theme::badgeFont());
        painter.setPen(Theme::textMuted());
        painter.drawText(
            QRect(body.left() + kPad, body.top() + kLabelHeight + kValueHeight + kHintGap,
                 body.width() - kPad * 2, kHintHeight),
            Qt::AlignVCenter | Qt::AlignLeft, hintText());
    }

    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        // Installed for exactly as long as the chip is up -
        // ExtrudePreview's and PullArrow's own lifetime rule for an
        // application-wide filter.
        QCoreApplication::instance()->installEventFilter(this);
    }

    void hideEvent(QHideEvent* event) override
    {
        QWidget::hideEvent(event);
        QCoreApplication::instance()->removeEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        // Enter, Escape and X/Y/Z belong to this chip for as long as it is
        // VISIBLE, whatever holds focus - PullArrow::eventFilter()'s own
        // reasoning: the whole reason a live gesture exists is that the
        // user orbits to see the plane from another angle, and an orbit is
        // a press in the viewport that takes focus off of everything else.
        if (!isVisible()) return QWidget::eventFilter(watched, event);

        const QEvent::Type type = event->type();
        if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress)
            return QWidget::eventFilter(watched, event);

        // Application-wide means every window in this process - gui_smoke
        // builds several at once - so only keys headed for this chip's own
        // window count.
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget || widget->window() != window())
            return QWidget::eventFilter(watched, event);

        // Fix round 1 (Task 3.2 review, Finding 2): never steal a keystroke
        // out of a focused text field. This chip owns no QLineEdit of its
        // own - unlike PullArrow/BevelArrow/ExtrudePreview, there is nothing
        // here to type - so `widget` being a QLineEdit means some OTHER
        // gesture's field somehow has focus while this one is live. Rename
        // and the versions-create field are now both excluded from opening
        // during an active gesture at their own source (canRename and
        // canOpenSaveVersion(), both `&& !mirrorPlacementActive()`, plus
        // ItemsPanel::beginRenameForItem()'s own belt-and-suspenders guard),
        // but this is the independent, structural backstop: X, Y and Z are
        // ordinary letters any typed name can contain, and a filter that
        // only trusts its OWN callers to have gated correctly is one
        // unguarded call site away from silently eating one again.
        if (qobject_cast<QLineEdit*>(widget)) return QWidget::eventFilter(watched, event);

        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
        if (mods != Qt::NoModifier) return QWidget::eventFilter(watched, event);

        const int key = keyEvent->key();
        const bool commits = key == Qt::Key_Return || key == Qt::Key_Enter;
        const bool cancels = key == Qt::Key_Escape;
        const bool axisX = key == Qt::Key_X;
        const bool axisY = key == Qt::Key_Y;
        const bool axisZ = key == Qt::Key_Z;
        if (!commits && !cancels && !axisX && !axisY && !axisZ)
            return QWidget::eventFilter(watched, event);

        if (type == QEvent::ShortcutOverride) {
            event->accept();   // claims the key back from QShortcutMap
            return true;
        }

        if (commits) {
            if (myWindow) myWindow->confirmMirrorPlacement();
        } else if (cancels) {
            if (myWindow) myWindow->cancelMirrorPlacement();
        } else if (myView) {
            myView->setMirrorPlacementAxis(axisX ? 0 : axisY ? 1 : 2);
            onPlaneChanged();
        }
        return true;
    }

private:
    // Rebuilds the live twin ghost preview and repaints the offset readout -
    // called on every drag step and every orientation flip, PullArrow's
    // updatePreview()'s own trigger points.
    void onPlaneChanged()
    {
        updatePreview();
        update();
    }

    // The SAME ModelingOps::mirrorShape() DocumentModel::pairWithMirror()
    // itself uses, one per selected id, combined into one compound so the
    // dedicated modeling-preview channel's single-shape contract still
    // holds. A preview built by a different path is a lie - CLAUDE.md's own
    // rule for every gizmo preview in this app - and this is the one place
    // the user judges where the twins will land by what they look like.
    // `replacesSolidId` stays -1 throughout: this gesture ADDS twins, it
    // does not stand in for any one of the bodies being paired the way a
    // pull or a bevel preview stands in for the body it edits.
    void updatePreview()
    {
        if (!myWindow || !myView || !myView->mirrorPlacementActive()) return;

        const gp_Pln plane = myView->mirrorPlacementPlane();
        const std::vector<int> ids = myView->mirrorPlacementIds();

        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        bool any = false;
        for (int id : ids) {
            const TopoDS_Shape shape = myWindow->document().shapeOf(id);
            if (shape.IsNull()) continue;
            const ModelingOps::BooleanResult mirrored = ModelingOps::mirrorShape(shape, plane);
            if (!mirrored.ok) continue;
            builder.Add(compound, mirrored.shape);
            any = true;
        }
        if (any) myView->setModelingPreview(compound, -1);
        else     myView->clearModelingPreview();
    }

    // This card's SIZE is measured with the fonts it paints its three
    // strings with - ExtrudePreview's own rule, and for the same reason: a
    // fixed guess at the hint line's width ("X Y Z aim - drag to move -
    // Enter mirror - Esc cancel" is the longest string any chip in this app
    // paints) is exactly the kind of number that silently clips the day the
    // copy changes.
    void applySize()
    {
        const int margin = Theme::surfaceShadowMargin();
        const QFontMetrics hintMetrics(Theme::badgeFont());
        const QFontMetrics labelMetrics(Theme::labelFont());
        const QFontMetrics bodyMetrics(Theme::bodyFont());
        const int textWidth =
            std::max({hintMetrics.horizontalAdvance(hintText()),
                      labelMetrics.horizontalAdvance(labelText()),
                      bodyMetrics.horizontalAdvance(valueText())});
        const int cardWidth = std::max(kMinWidth, textWidth + kPad * 2);
        setFixedSize(Theme::wholeDevicePixels(
            QSize(cardWidth + margin * 2,
                 kPad * 2 + kLabelHeight + kValueHeight + kHintGap + kHintHeight + margin * 2)));
    }

    // The one place each painted string is spelled out - paintEvent() draws
    // through these and paintedTexts() reports them, PullArrow's own rule.
    // PUBLIC and STATIC, unlike PullArrow's own (instance, private), and
    // pass straight through to MainWindow::mirrorPlacementLabelText()/
    // mirrorPlacementHintText() - CompareBadge's own precedent, one call
    // site up: this class is never reachable from outside MainWindow.cpp
    // (no header of its own - see myMirrorChip's field comment), so
    // gui_smoke's banned-word sweep reads the strings through MainWindow's
    // static accessors instead of a live instance's paintedTexts(). Neither
    // takes gesture state, unlike valueText() below, which stays
    // instance-level and out of the sweep - BevelArrow's own
    // kindText()/valueText() split, for the same reason: a number cannot
    // carry a banned word.
    static QString labelText() { return MainWindow::mirrorPlacementLabelText(); }
    static QString hintText() { return MainWindow::mirrorPlacementHintText(); }
    QString valueText() const
    {
        const double offset = myView ? myView->mirrorPlacementOffset() : 0.0;
        return tr("Plane %1").arg(QString::fromStdString(Measure::formatLength(offset)));
    }

    MainWindow* myWindow = nullptr;
    OcctViewWidget* myView = nullptr;

    static constexpr int kPad = 10;
    static constexpr int kMinWidth = 176;
    static constexpr int kLabelHeight = 16;
    static constexpr int kValueHeight = 22;
    static constexpr int kHintGap = 4;
    static constexpr int kHintHeight = 14;
    static constexpr int kChipGap = 18;
    static constexpr int kEdgeInset = 8;
};

}  // namespace

MainWindow::MainWindow(QWidget* parent, bool persistProgress, const QString& libraryRoot)
    : QMainWindow(parent)
    , myStore(resolveLibraryRoot(libraryRoot))
    , myPersistProgress(persistProgress)
{
    if (myPersistProgress) {
        const QSettings settings;
        myProgress.deserialize(
            settings.value(QStringLiteral("progress")).toString().toStdString());
        // Read before buildActions() so the Units menu's initial checked
        // state, and the readout label built in buildOverlay(), both agree
        // with what was last chosen - same guard as the learning progress,
        // so the suite (persistProgress=false) can never read the
        // developer's real store.
        if (settings.value(QStringLiteral("displayUnit")).toString() ==
            QStringLiteral("cm"))
            Measure::setDisplayUnit(Measure::Unit::Centimetres);

        // The base projection, on the same terms as the unit above: a
        // preference, read once, under the same guard, so the suite can never
        // see the developer's own choice. It cannot be applied here - the
        // viewport does not exist yet - so it is held until it does.
        myStartOrthographic = settings.value(QStringLiteral("projection")).toString() ==
                              QStringLiteral("ortho");

        // Whether the app announces the things that went right. Same guard,
        // same "read once before buildActions()" reason as the two above: the
        // View menu entry is built with its checked state already correct
        // rather than corrected afterwards. Defaults to ON - an app that
        // started silent would look broken to a first-time user.
        myShowNotifications =
            settings.value(QStringLiteral("showNotifications"), true).toBool();

        // View -> Show bottom bar. Same guard, same "read before
        // buildActions()" reason as the notifications preference just above:
        // the View entry's initial checked state has to agree with what was
        // last chosen. Default ON, for the same reason.
        myShowBottomBar = settings.value(QStringLiteral("showBottomBar"), true).toBool();

        // File -> Save automatically. Same guard, same "read before
        // buildActions()" reason: the menu entry's initial checked state has
        // to agree with what was last chosen rather than being corrected
        // afterwards. Defaults to ON - see the member's own comment in the
        // header for why the safer default wins for a user who has not found
        // the toggle yet.
        myAutosaveOn = settings.value(QStringLiteral("autosave"), true).toBool();

        // Before a single widget exists, for the same reason as the unit
        // above: every card measures itself with the type scale in its own
        // constructor, so installing the spec afterwards would leave the
        // shell laid out for a size it is no longer wearing. Theme::apply()
        // has already installed defaultSpec() by now (main.cpp calls it
        // before this window is built), so a garbage or absent setting simply
        // leaves the app at its shipped appearance - deserializeSpec()
        // guarantees `stored` is untouched when it refuses.
        Theme::Spec stored;
        if (Theme::deserializeSpec(
                settings.value(QStringLiteral("appearance")).toString(), stored))
            Theme::setSpec(stored);

        // The six render-settings values (Task 7.2), on myStartOrthographic's
        // own terms - read here, before the viewport exists, applied to it
        // the moment it does (a few lines down). Render mode ITSELF stays
        // session-only per CLAUDE.md; these six are not that flag and DO
        // persist, the same way autosave and the unit choice do. Absent
        // keys fall back to exactly OcctViewWidget's own defaults, so a
        // first-ever run changes nothing about what render mode already
        // looks like. Slash-separated keys rather than beginGroup()/
        // endGroup() - `settings` is a `const QSettings` (the same guard
        // every preference above already reads through), and beginGroup()
        // is not a const member.
        myStartRenderRoughness =
            settings.value(QStringLiteral("renderMode/roughness"), myStartRenderRoughness)
                .toDouble();
        myStartRenderMetallic =
            settings.value(QStringLiteral("renderMode/metallic"), myStartRenderMetallic)
                .toDouble();
        myStartRenderLightAngleDeg =
            settings
                .value(QStringLiteral("renderMode/lightAngleDeg"), myStartRenderLightAngleDeg)
                .toDouble();
        myStartRenderLightStrength =
            settings
                .value(QStringLiteral("renderMode/lightStrength"), myStartRenderLightStrength)
                .toDouble();
        // Empty string (the absent-key default too) means "no stored
        // override" - OcctViewWidget::renderBackgroundOverride()'s own
        // invalid-QColor convention, carried across the QSettings boundary
        // as an empty vs. non-empty name rather than a second bool key.
        const QString bg = settings.value(QStringLiteral("renderMode/background")).toString();
        if (!bg.isEmpty()) {
            const QColor colour(bg);
            if (colour.isValid()) myStartRenderBackground = colour;
        }
        myStartRenderFov =
            settings.value(QStringLiteral("renderMode/fov"), myStartRenderFov).toDouble();
    }

    // The title bar's and the taskbar's mark, painted rather than loaded - see
    // IconSet::appIcon(). Set on the WINDOW rather than only on the
    // application, so a window built by the suite (which never runs main.cpp)
    // carries it too; QWidget::windowIcon() would otherwise fall back to an
    // application icon nothing had set.
    setWindowIcon(IconSet::appIcon());

    myView = new OcctViewWidget(this);
    // Straight onto the camera rather than through setBaseProjection(): that
    // one persists and calls updateActions(), and neither the settings store
    // nor half the shell is ready to be asked anything yet. The viewport has
    // not initialized its OCCT view either - it does that lazily on its first
    // paint - and applyCameraState() reads this state then, so the very first
    // frame is already drawn in the mode the user left.
    if (myStartOrthographic)
        myView->camera().setBaseProjection(CameraController::Projection::Orthographic);
    // The six render-settings values, applied the moment the viewport
    // exists - myStartOrthographic's own two-step pattern just above.
    // Harmless before render mode has ever been entered: every setter
    // simply records the value as session state (see OcctViewWidget.h),
    // and only the light-angle default is worth a word here - a light
    // angle of exactly -1.0 (myStartRenderLightAngleDeg's own sentinel)
    // means "nothing was ever persisted," so this leaves the viewport's own
    // constructor-computed default (which reproduces the Milestone-3
    // studio key exactly) rather than overwriting it with a bogus negative
    // angle.
    myView->setRenderSurfaceRoughness(myStartRenderRoughness);
    myView->setRenderMetal(myStartRenderMetallic);
    if (myStartRenderLightAngleDeg >= 0.0)
        myView->setRenderLightAngleDeg(myStartRenderLightAngleDeg);
    myView->setRenderLightStrength(myStartRenderLightStrength);
    if (myStartRenderBackground.isValid())
        myView->setRenderBackgroundOverride(myStartRenderBackground);
    myView->setRenderFov(myStartRenderFov);
    // Full bleed: the central widget is the viewport and nothing else. The
    // items panel used to take a splitter pane out of the window's width;
    // it is a floating drawer over the viewport now (see buildOverlay()),
    // which is why there is no longer anything to split.
    setCentralWidget(myView);

    // Parented to the viewport from birth - buildOverlay() anchors it, and
    // ViewportOverlay would reparent it anyway, but a card that is a child of
    // the window until then would flash in the wrong place on the first show.
    myItemsPanel = new ItemsPanel(&myDocument, myView, myView);

    connect(myView, &OcctViewWidget::sketchPointPicked, this, &MainWindow::onSketchPointPicked);
    connect(myView, &OcctViewWidget::sketchCursorMoved, this, &MainWindow::onSketchCursorMoved);
    connect(myView, &OcctViewWidget::selectionChanged, this, &MainWindow::onSelectionChanged);
    // The second route to Lock to Face. The viewport reports the gesture; this
    // window decides what it means, and both routes land in the same
    // lockToFace() - including its refusal - rather than one of them growing
    // its own copy of the rule.
    connect(myView, &OcctViewWidget::faceDoubleClicked, this, &MainWindow::lockToFace);
    // The other double-click route: a plain one on a body while faces or edges
    // are what is being picked means "select the whole body". The viewport
    // reports the gesture; this window performs it, because the selection mode
    // is a QAction's checked state and updateActions() is the single place that
    // decides what is available.
    connect(myView, &OcctViewWidget::bodyDoubleClicked, this, &MainWindow::onBodyDoubleClicked);
    // The transform gizmo reports the end of a drag; this window decides what
    // it means, exactly as it does for the face-pull arrow above.
    connect(myView, &OcctViewWidget::gizmoReleased, this, &MainWindow::onGizmoReleased);
    // Render mode's own exit gesture - "a pick press in the viewport". The
    // viewport already swallowed the press (see its own mousePressEvent()),
    // so this window's only job is to turn the mode off through the single
    // authority every other exit routes through.
    connect(myView, &OcctViewWidget::renderModeExitRequested, this,
            [this] { setRenderModeEnabled(false); });

    buildActions();
    buildAppBar(buildMenus());

    // The mirror-placement gesture's own self-cancel (fix round 1) - see
    // refreshMirrorPlacement()'s own comment. Connected HERE, genuinely
    // before buildOverlay() constructs the gesture's value chip and connects
    // ITS refresh() to the same signal, so Qt's connected-in-order guarantee
    // puts this slot's cancel ahead of the chip's own read of
    // mirrorPlacementActive() within any one appStateChanged emission.
    //
    // It used to sit with the other appStateChanged connections BELOW
    // buildOverlay(), while two comments (here and at
    // refreshMirrorPlacement()) both claimed this ordering - so the chip
    // refreshed FIRST, saw a still-active gesture, stayed visible, and the
    // cancel landed second, leaving a visible chip with a live
    // application-wide Enter/Escape/X/Y/Z filter over a gesture that had
    // already ended. It was cleared only by accident, because the one
    // reachable self-cancel trigger happens to emit appStateChanged twice.
    // Moved rather than re-documented: the ordering is the mechanism, and
    // an accident is not one.
    connect(this, &MainWindow::appStateChanged, this, &MainWindow::refreshMirrorPlacement);

    buildOverlay();

    myShortcutSheet = new ShortcutSheet(this);
    connect(myShortcutsAction, &QAction::triggered, myShortcutSheet, &ShortcutSheet::showSheet);

    // appStateChanged alone, not documentChanged too: every document edit
    // already calls updateActions() (and so emits appStateChanged) before it
    // emits documentChanged, so a second connection here only rebuilt the
    // same rows twice per edit. appStateChanged also covers the case
    // documentChanged never fires for - switching the display unit, which
    // touches no document but still has to reread every dimension the panel
    // shows (see setDisplayUnit()).
    connect(this, &MainWindow::appStateChanged, myItemsPanel, &ItemsPanel::refresh);
    // The versions drawer, on the same terms - built in buildOverlay(),
    // which has already run by this point in the constructor.
    if (myVersionsPanel)
        connect(this, &MainWindow::appStateChanged, myVersionsPanel, &VersionsPanel::refresh);

    // Connected AFTER the refresh above, so it runs after it: a row added or
    // removed changes the drawer's height, and the drawer's rectangle is one
    // of the obstacles the toast, the balloon and the guide place themselves
    // against - re-laying out here is what keeps that set current between
    // resizes.
    //
    // Its visibility is re-DERIVED from the action here rather than only
    // being set when the action is toggled. CLAUDE.md's rule, learned twice
    // already on this viewport (WalkthroughPanel's skip control, Toast's undo
    // pill): a one-shot hide is not a state, and anything that shows a
    // widget's siblings wholesale - QWidget::showChildren() on the window's
    // first show, for one - will happily undo it. Reading it off the action
    // on every state change means the two cannot drift.
    //
    // Only reads state and moves geometry, so it cannot recurse back into
    // updateActions().
    connect(this, &MainWindow::appStateChanged, this, [this] {
        // Render mode (Milestone 3, item 5) hides every one of these outright,
        // regardless of its own action's checked state - "&& !myRenderModeOn"
        // on each line below rather than a separate branch, so turning render
        // mode off needs no restore logic of its own: the very next
        // appStateChanged (updateActions() ends by emitting it) re-derives
        // every line from the SAME action states it always has, which is
        // CLAUDE.md's sibling-visibility law applied to a fourth surface
        // rather than a new mechanism.
        const bool hiddenForRenderMode = myRenderModeOn;
        myItemsPanel->setVisible(myItemsPanelAction->isChecked() && !hiddenForRenderMode);
        // Derived on every state change from the action, exactly as the
        // drawer above is and for exactly the same reason - a one-shot hide
        // is not a state, and QWidget::showChildren() on the window's first
        // show will happily undo one.
        if (myAppearancePanel)
            myAppearancePanel->setVisible(myAppearanceAction->isChecked() && !hiddenForRenderMode);
        if (myVersionsPanel)
            myVersionsPanel->setVisible(myVersionsPanelAction->isChecked() && !hiddenForRenderMode);
        // The status bar's own shown state, on the same derived-not-stored
        // terms - View -> Show bottom bar's checked state IS the answer,
        // never a one-shot hide()/show() called from the toggle handler
        // alone.
        if (myBottomBarAction)
            statusBar()->setVisible(myBottomBarAction->isChecked() && !hiddenForRenderMode);
        // The rail and the axis gizmo card have no action of their own to be
        // derived FROM - they are always on outside render mode - so this is
        // simply their whole predicate rather than one term of it.
        if (myRail) myRail->setVisible(!hiddenForRenderMode);
        if (myAxisGizmo) myAxisGizmo->setVisible(!hiddenForRenderMode);
        // The render settings card and the shutter (Task 7.2) - the exact
        // opposite predicate: neither has a QAction of its own either, and
        // both exist ONLY while render mode is on, so `hiddenForRenderMode`
        // is read as their whole visibility rather than negated into it.
        if (myRenderSettingsPanel) {
            myRenderSettingsPanel->setVisible(hiddenForRenderMode);
            // Two of the six controls are read only by the deepest tier
            // (OcctViewWidget::renderMaterialControlsApply(), which IS the
            // gate the setters are wrapped in, not a second copy of the
            // tier list). Derived here, on every appStateChanged, so the
            // note follows the tier the session actually probed into rather
            // than being set once at the toggle site.
            myRenderSettingsPanel->setMaterialRowsApply(myView->renderMaterialControlsApply());
        }
        if (myRenderShutter) myRenderShutter->setVisible(hiddenForRenderMode);
        if (myOverlay) myOverlay->relayout();
    });

    // A dimension label reads through Measure too, so it has to follow a unit
    // switch the way the items panel and the status bar do. DimensionRenderer
    // is not a QObject - it draws, it does not listen - so the window drives
    // it from the one signal every unit-following surface already refreshes
    // on, rather than setDisplayUnit() growing a private list of everything
    // that shows a length. refreshDimension() redraws only what is already on
    // screen and is a no-op otherwise, so this cannot make an annotation
    // appear; and it only reads and repaints, so it cannot recurse back into
    // updateActions().
    connect(this, &MainWindow::appStateChanged, myView, &OcctViewWidget::refreshDimension);

    // The transform gizmo's visibility, derived on every state change from the
    // one predicate that decides it - never set from the event that happened
    // to make it true. Only reads state and attaches or detaches an AIS
    // object, so it cannot recurse back into updateActions().
    connect(this, &MainWindow::appStateChanged, this, &MainWindow::refreshTransformGizmo);

    // And the edge annotation's, from the bevel arrow's predicate - two
    // annotations on one edge is noise, so the length label stands down for as
    // long as the arrow's own value chip is up. Only reads state and moves AIS
    // objects, so it cannot recurse back into updateActions().
    connect(this, &MainWindow::appStateChanged, this, &MainWindow::refreshEdgeAnnotation);

    // Selection syncs both ways.
    connect(myItemsPanel, &ItemsPanel::solidActivated, this,
            [this](int id) { myView->setSelectedSolids({id}); });
    // An outline row is the outline's only handle - it is not pickable in the
    // viewport - so clicking one is what chooses which outline Extrude
    // consumes. Routed through MainWindow rather than the panel writing the
    // state itself: selectOutline() calls updateActions(), which is the single
    // place that decides what is available.
    connect(myItemsPanel, &ItemsPanel::outlineActivated, this,
            [this](int id) { selectOutline(id); });
    connect(myView, &OcctViewWidget::selectionChanged, this,
            [this] { myItemsPanel->showSelection(myView->selectedSolidIds()); });
    // A row's rename gesture (double-click, or F2 through onRenameSelected())
    // committed. MainWindow does the checkpoint/setItemName/toast, exactly as
    // the panel's own header says - see onItemRenameCommitted().
    connect(myItemsPanel, &ItemsPanel::renameCommitted, this,
            &MainWindow::onItemRenameCommitted);

    // The Items rail button, the menu entry and Ctrl+Alt+S all drive the one
    // action, and the drawer's shown state is read off that action rather
    // than stored - exactly as every chip mirrors an action rather than
    // remembering a mode.
    //
    // One other thing does call setVisible() on it: ViewportOverlay::addWidget()
    // show()s whatever it anchors, which is right for every other entry it
    // takes. The derivation wins rather than the initial show, because the
    // appStateChanged slot below re-reads the action on every state change -
    // so an overlay that shows a drawer whose action is unchecked is
    // corrected before the window is ever on screen. That is the point of
    // deriving it repeatedly instead of only on toggle.
    //
    // Opening or closing it also re-lays the overlay out, because the drawer
    // is one of the rectangles ViewportOverlay::occupiedRects() reports and
    // the toast, the balloon and the guide place themselves against that set.
    // Without this, opening the drawer would leave a live toast sitting
    // underneath it until the next resize. relayout() only reads and moves
    // geometry, so it cannot recurse back into updateActions().
    connect(myItemsPanelAction, &QAction::toggled, this, [this](bool shown) {
        myItemsPanel->setVisible(shown);
        if (myOverlay) myOverlay->relayout();
    });

    // The versions drawer, on the same terms - see the items drawer's
    // toggle above.
    connect(myVersionsPanelAction, &QAction::toggled, this, [this](bool shown) {
        if (myVersionsPanel) myVersionsPanel->setVisible(shown);
        if (myOverlay) myOverlay->relayout();
    });

    // The Appearance card on the same terms - see the drawer's toggle above.
    connect(myAppearanceAction, &QAction::toggled, this, [this](bool shown) {
        if (myAppearancePanel) myAppearancePanel->setVisible(shown);
        if (myOverlay) myOverlay->relayout();
    });

    // Theme's broadcast, relayed into this window. Connected to the
    // application-wide notifier rather than to the panel: a spec can also be
    // installed with no panel involved (the persisted one at startup, or a
    // reset), and a relay hung off the panel would miss both.
    connect(Theme::notifier(), &Theme::Notifier::changed, this,
            &MainWindow::onThemeChanged);

    updateActions();

    // Theme.cpp's stylesheet reaches the status bar's own internal message
    // label through the QStatusBar/QStatusBar QLabel selectors (it is
    // created privately by showMessage() and this code never gets a pointer
    // to it) - this sets the same size directly, so a plain QStatusBar with
    // no matching stylesheet rule would still be correct.
    statusBar()->setFont(Theme::labelFont());

    // Permanent widget so it survives transient showMessage() calls: the left
    // side reports what just happened, the right side always says where you are.
    myStateLabel = new QLabel(this);
    statusBar()->addPermanentWidget(myStateLabel);
    updateStateLabel();
    syncChromeHeights();

    setWindowTitle(tr("FurnifyMe"));
    resize(1280, 800);
    statusBar()->showMessage(tr("Right-drag to orbit, middle-drag to pan, wheel to zoom"));

    // On launch nothing is open - myShowingInitScreen already starts true -
    // and this establishes that data state (a fresh, empty document) and
    // writes its own status message over the generic one two lines up. It
    // also hides this window and emits returnedToSelector() (see the
    // header), which nobody has connected to yet at this point in the
    // constructor - main.cpp's own handoff wiring is what actually shows
    // SelectorWindow, unconditionally, as the real app's boot state.
    showInitScreen();
}

void MainWindow::buildActions()
{
    myStartSketchAction = new QAction(tr("&Start Sketch"), this);
    myStartSketchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(myStartSketchAction, &QAction::triggered, this, &MainWindow::onStartSketch);

    myFinishSketchAction = new QAction(tr("&Finish Sketch"), this);
    myFinishSketchAction->setShortcut(QKeySequence(Qt::Key_Return));
    connect(myFinishSketchAction, &QAction::triggered, this, &MainWindow::onFinishSketch);

    myUndoPointAction = new QAction(tr("&Undo Last Point"), this);
    myUndoPointAction->setShortcut(QKeySequence(Qt::Key_Backspace));
    connect(myUndoPointAction, &QAction::triggered, this, &MainWindow::onUndoSketchPoint);

    myCancelSketchAction = new QAction(tr("&Cancel Sketch"), this);
    myCancelSketchAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(myCancelSketchAction, &QAction::triggered, this, &MainWindow::onCancelSketch);

    myExtrudeAction = new QAction(tr("&Extrude..."), this);
    myExtrudeAction->setShortcut(QKeySequence(Qt::Key_E));
    connect(myExtrudeAction, &QAction::triggered, this, &MainWindow::onExtrude);

    myUnionAction = new QAction(tr("&Union"), this);
    connect(myUnionAction, &QAction::triggered, this, &MainWindow::onUnion);

    mySubtractAction = new QAction(tr("&Subtract"), this);
    connect(mySubtractAction, &QAction::triggered, this, &MainWindow::onSubtract);

    myIntersectAction = new QAction(tr("&Intersect"), this);
    connect(myIntersectAction, &QAction::triggered, this, &MainWindow::onIntersect);

    myLockFaceAction = new QAction(tr("&Lock to Face"), this);
    myLockFaceAction->setShortcut(QKeySequence(Qt::Key_L));
    myLockFaceAction->setToolTip(lockTooltipText());
    connect(myLockFaceAction, &QAction::triggered, this, &MainWindow::onLockToFace);

    myUnlockFaceAction = new QAction(tr("U&nlock Face"), this);
    myUnlockFaceAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_L));
    myUnlockFaceAction->setToolTip(unlockTooltipText());
    connect(myUnlockFaceAction, &QAction::triggered, this, &MainWindow::unlockFace);

    // Mirror (Milestone 3, rebound in Milestone 4 Phase 3, renamed and
    // re-scoped in the final fix wave). The user-facing word for this whole
    // concept is Mirror - the kernel-facing symmetryOn()/setSymmetry() names
    // and every private member keep their own spelling, but nothing painted
    // says "symmetry" any more, and the vocabulary sweep enforces that
    // (CLAUDE.md's table has the row). The old name was the one direction
    // that matters most: the word on the control the user must press was
    // not the word any of its own feedback used.
    //
    // Still checkable, and its checked state is STILL document().symmetryOn()
    // - updateActions() reads that back onto it, never the reverse - but
    // TRIGGERING it always means "begin a placement" now. It used to mean
    // "turn mirroring off" whenever mirroring was already on, which made
    // pairing ADDITIONAL bodies impossible without first unpairing
    // everything: two presses, the first silently destroying every pairing
    // in the document. DocumentModel::pairWithMirror() already skips a body
    // that is already paired and reports it, so beginning a placement over
    // live mirroring is honest work, not a second meaning. Turning mirroring
    // OFF is its own menu entry below, deliberately without a shortcut - a
    // destructive unpair-everything should not share a key with the gesture
    // people reach for constantly. Qt has already flipped isChecked() by the
    // time triggered() fires; updateActions() at the end of every branch
    // resyncs it to whatever document().symmetryOn() genuinely is.
    mySymmetryAction = new QAction(tr("&Mirror"), this);
    mySymmetryAction->setCheckable(true);
    // No "(S)" here - the banned-word sweep matches "(s)" as a bare
    // substring, case-insensitive, for the vocabulary rule against a typed
    // plural marker, and this shortcut's own letter collides with it.
    mySymmetryAction->setToolTip(
        tr("Place a plane and pair the selected bodies with mirrored twins — "
          "shortcut S\n"
          "Select one or more bodies first — Enter mirrors them, Esc cancels."));
    mySymmetryAction->setShortcut(QKeySequence(Qt::Key_S));
    connect(mySymmetryAction, &QAction::triggered, this,
            &MainWindow::onSymmetryActionTriggered);

    // The off switch, on its own. No shortcut by design (see the comment
    // above), and enabled only while there is something to turn off.
    mySymmetryOffAction = new QAction(tr("Turn Mirroring O&ff"), this);
    mySymmetryOffAction->setToolTip(tr("Stop pairing bodies with mirrored twins\n"
                                       "Every existing pairing is dropped — the bodies "
                                       "themselves stay."));
    connect(mySymmetryOffAction, &QAction::triggered, this,
            [this] { setSymmetryEnabled(false); });

    mySetSymmetryPlaneAction = new QAction(tr("Set Mirror &Plane"), this);
    mySetSymmetryPlaneAction->setToolTip(tr("Mirror across this face instead of the middle\n"
                                            "Pick one flat face - the plane it lies on "
                                            "becomes the mirror."));
    connect(mySetSymmetryPlaneAction, &QAction::triggered, this, &MainWindow::onSetSymmetryPlane);

    // Linked copies (Milestone 4, Task 4.2) - see the header's own "linked
    // copies" section for what each one does and refuses on. Ctrl+D was
    // free (checked against every other binding in this function); the
    // other two carry no shortcut of their own, the same as Union/Subtract/
    // Intersect just below.
    myDuplicateLinkedAction = new QAction(tr("Duplicate &linked"), this);
    myDuplicateLinkedAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(myDuplicateLinkedAction, &QAction::triggered, this,
            [this] { duplicateLinkedCopy(); });

    myLinkSelectedAction = new QAction(tr("&Link selected"), this);
    connect(myLinkSelectedAction, &QAction::triggered, this,
            [this] { linkSelectedBodies(); });

    myUnlinkAction = new QAction(tr("&Unlink"), this);
    connect(myUnlinkAction, &QAction::triggered, this, [this] { unlinkSelectedBody(); });

    myExportStepAction = new QAction(tr("Export &STEP..."), this);
    // Ctrl+S is Save's now - the platform standard key and a furniture SAVE
    // is what it should mean the moment a library exists to save into.
    // Export keeps a mnemonic of its own rather than losing a binding
    // outright.
    myExportStepAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(myExportStepAction, &QAction::triggered, this, &MainWindow::onExportStep);

    myFileSaveAction = new QAction(tr("&Save"), this);
    myFileSaveAction->setShortcut(QKeySequence::Save);
    myFileSaveAction->setToolTip(tr("Save this furniture (Ctrl+S)"));
    connect(myFileSaveAction, &QAction::triggered, this,
            [this] { saveCurrentFurniture(); });

    myAutosaveAction = new QAction(tr("Save &automatically"), this);
    myAutosaveAction->setCheckable(true);
    myAutosaveAction->setChecked(myAutosaveOn);
    myAutosaveAction->setToolTip(tr("Save a moment after every change\n"
                                    "Off, Ctrl+S is how a change reaches disk."));
    connect(myAutosaveAction, &QAction::toggled, this, &MainWindow::setAutosaveEnabled);

    myCloseFurnitureAction = new QAction(tr("&Close furniture"), this);
    myCloseFurnitureAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    myCloseFurnitureAction->setToolTip(tr("Return to your furniture library (Ctrl+W)\n"
                                          "With Save automatically off, this saves first."));
    connect(myCloseFurnitureAction, &QAction::triggered, this,
            &MainWindow::closeCurrentFurniture);

    // File -> Save version... Enabled state is canOpenSaveVersion() - see
    // its own declaration for the disjointness this buys against
    // ExtrudePreview and the two drag gizmos' own application-wide key
    // claims.
    mySaveVersionAction = new QAction(tr("Save &version..."), this);
    mySaveVersionAction->setToolTip(tr("Keep a named snapshot of this furniture\n"
                                       "Come back to it later with Restore, or open it "
                                       "beside the live one with Compare."));
    connect(mySaveVersionAction, &QAction::triggered, this, &MainWindow::onSaveVersion);

    mySolidSelectAction = new QAction(tr("Select &Bodies"), this);
    mySolidSelectAction->setCheckable(true);
    mySolidSelectAction->setChecked(true);
    myFaceSelectAction = new QAction(tr("Select F&aces"), this);
    myFaceSelectAction->setCheckable(true);
    myEdgeSelectAction = new QAction(tr("Select &Edges"), this);
    myEdgeSelectAction->setCheckable(true);
    myEdgeSelectAction->setToolTip(tr("Pick one edge at a time\n"
                                      "Hovering shows its length."));

    myDeleteAction = new QAction(tr("&Delete Selected"), this);
    myDeleteAction->setShortcut(QKeySequence::Delete);
    myDeleteAction->setToolTip(tr("Delete the selected bodies (Del)"));
    connect(myDeleteAction, &QAction::triggered, this, &MainWindow::onDeleteSelected);

    // Renames the Items drawer's selected row - see the header for why this
    // is exactly one item, never a multi-body selection. F2 is the row's
    // keyboard route; double-click on a row is its mouse route, wired
    // straight into ItemsPanel rather than through this action (see
    // buildOverlay()'s connection to renameCommitted()).
    // No ellipsis: the convention elsewhere in this menu is that "..."
    // promises a further dialog (Appearance..., Save version...), and this
    // app has none - Rename opens an inline edit directly over the row, the
    // same immediate contract Delete Selected's own unadorned label keeps.
    myRenameAction = new QAction(tr("Re&name"), this);
    myRenameAction->setShortcut(QKeySequence(Qt::Key_F2));
    connect(myRenameAction, &QAction::triggered, this, &MainWindow::onRenameSelected);

    myUndoAction = new QAction(tr("&Undo"), this);
    myUndoAction->setShortcut(QKeySequence::Undo);
    myUndoAction->setToolTip(tr("Undo the last change to your bodies (Ctrl+Z)"));
    connect(myUndoAction, &QAction::triggered, this, &MainWindow::onUndo);

    myRedoAction = new QAction(tr("&Redo"), this);
    myRedoAction->setShortcut(QKeySequence::Redo);
    myRedoAction->setToolTip(tr("Redo the change you just undid (Ctrl+Y)"));
    connect(myRedoAction, &QAction::triggered, this, &MainWindow::onRedo);

    mySnapAction = new QAction(tr("Snap to &Grid"), this);
    mySnapAction->setCheckable(true);
    mySnapAction->setChecked(true);
    mySnapAction->setToolTip(snapTooltipText());
    connect(mySnapAction, &QAction::toggled, this, &MainWindow::onSnapToggled);

    myItemsPanelAction = new QAction(tr("Items"), this);
    myItemsPanelAction->setCheckable(true);
    myItemsPanelAction->setChecked(true);
    myItemsPanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+S")));
    myItemsPanelAction->setToolTip(tr("Show or hide the list of bodies (Ctrl+Alt+S)"));

    // View -> Versions - the drawer's visibility is DERIVED from this
    // action's checked state, both directions, exactly as the items
    // drawer's is from myItemsPanelAction. Starts UNCHECKED, unlike Items:
    // most furniture never has a saved version at all, and a second drawer
    // open by default beside one that is almost always empty is clutter the
    // Items drawer does not have to earn.
    myVersionsPanelAction = new QAction(tr("Versions"), this);
    myVersionsPanelAction->setCheckable(true);
    myVersionsPanelAction->setChecked(false);
    myVersionsPanelAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+V")));
    myVersionsPanelAction->setToolTip(tr("Show or hide this furniture's saved versions "
                                         "(Ctrl+Alt+V)"));

    // Menu only, and deliberately: the rail stays at thirteen tools. Choosing
    // colours is not a modelling tool and does not belong in the spine the
    // user's hand lives on. Checkable, because the panel's visibility is
    // DERIVED from it in both directions - the same contract the items drawer
    // has, and the reason nothing else in this file shows or hides the panel.
    myAppearanceAction = new QAction(tr("Appearance..."), this);
    myAppearanceAction->setCheckable(true);
    myAppearanceAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+A")));
    myAppearanceAction->setToolTip(tr("Choose the app's colours and text size (Ctrl+Alt+A)\n"
                                      "Every change is applied as you make it."));

    // Whether the app says so when something goes RIGHT. Checkable and
    // persisted, on the same terms as the projection and the unit below; its
    // initial state is the one the constructor read from the store (default
    // on), and updateActions() is what pushes it onto the toast host, so this
    // preference obeys the same single authority every other one does.
    //
    // It cannot silence a refusal, by construction rather than by care here:
    // ToastHost::show() drops Kind::Note only, and the label says notifications
    // rather than messages for exactly that reason.
    myNotificationsAction = new QAction(tr("Show &notifications"), this);
    myNotificationsAction->setCheckable(true);
    myNotificationsAction->setChecked(myShowNotifications);
    myNotificationsAction->setToolTip(tr("Report the things that went right\n"
                                         "Off, only refusals appear. Undo stays on the "
                                         "Edit menu and on Ctrl+Z either way."));
    connect(myNotificationsAction, &QAction::toggled, this, &MainWindow::setShowNotifications);

    // Whether the status bar along the bottom edge is shown at all. Checkable
    // and persisted on the same terms as the preference above; a Failure
    // toast is unrelated chrome (parented to OcctViewWidget, not to the
    // status bar) and stays reachable with this off - the tooltip says so,
    // because a control that hides a place messages appear has to say what
    // still gets through.
    myBottomBarAction = new QAction(tr("Show &bottom bar"), this);
    myBottomBarAction->setCheckable(true);
    myBottomBarAction->setChecked(myShowBottomBar);
    myBottomBarAction->setToolTip(tr("Show or hide the status bar along the bottom edge\n"
                                     "Off, a Failure toast still reaches you - only the bar hides."));
    connect(myBottomBarAction, &QAction::toggled, this, &MainWindow::setShowBottomBar);

    // Render mode (Milestone 3, item 5) - strips the viewport to the
    // furniture alone. Checkable, but deliberately NOT initialised from
    // QSettings the way every toggle above it is: CLAUDE.md's own words for
    // this one are "the app always starts in modeling", so it always starts
    // unchecked regardless of how a previous session left it.
    //
    // toggled(bool) connects straight to the public setRenderModeEnabled(),
    // exactly as myAutosaveAction connects to setAutosaveEnabled() - but
    // this is also the one action in this file that gets un-checked from
    // CODE as often as from the user, since every exit gesture calls
    // setRenderModeEnabled(false) directly (see its own declaration).
    myRenderModeAction = new QAction(tr("&Render mode"), this);
    myRenderModeAction->setCheckable(true);
    myRenderModeAction->setChecked(false);
    connect(myRenderModeAction, &QAction::toggled, this, &MainWindow::setRenderModeEnabled);

    // The projection toggle. Checkable, because the mode is state the user
    // chose and comes back next session; a QAction rather than a button that
    // decides for itself, because the bar's readout, the View menu entry and
    // the O shortcut all have to say the same thing - and because the
    // generated shortcut sheet lists it for free the moment it carries a
    // binding.
    //
    // Reads its initial state from the camera, which the constructor has
    // already set from the stored preference, rather than from that
    // preference a second time.
    myOrthographicAction = new QAction(tr("&Orthographic"), this);
    myOrthographicAction->setCheckable(true);
    myOrthographicAction->setChecked(myView->camera().baseProjection() ==
                                     CameraController::Projection::Orthographic);
    myOrthographicAction->setShortcut(QKeySequence(Qt::Key_O));
    myOrthographicAction->setToolTip(tr("Draw without perspective (O)\n"
                                        "Parallel edges stay parallel, so a face seen "
                                        "straight on reads at its true shape."));
    connect(myOrthographicAction, &QAction::toggled, this, &MainWindow::setBaseProjection);

    myDisplayModeAction = new QAction(tr("Wireframe"), this);
    myDisplayModeAction->setCheckable(true);
    myDisplayModeAction->setToolTip(tr("Draw bodies as edges only\n"
                                       "Useful for seeing through to what is behind."));
    connect(myDisplayModeAction, &QAction::toggled, this,
            [this](bool on) { myView->setWireframe(on); });

    myFitAction = new QAction(tr("&Fit All"), this);
    myFitAction->setShortcut(QKeySequence(Qt::Key_F));
    myFitAction->setToolTip(tr("Frame every body in the viewport (F)"));
    connect(myFitAction, &QAction::triggered, myView, &OcctViewWidget::fitAll);

    myScreenshotAction = new QAction(tr("Save S&creenshot..."), this);
    myScreenshotAction->setToolTip(tr("Save the viewport as a PNG image"));
    connect(myScreenshotAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Save Screenshot"),
                                                          QString(), tr("PNG image (*.png)"));
        if (!path.isEmpty() && !myView->saveSnapshot(path)) {
            myToasts->show(tr("Screenshot failed — Couldn't save the image to %1 — "
                              "Check that the folder exists and isn't read-only")
                              .arg(path),
                          Toast::Kind::Failure, false);
        }
    });

    myStartSketchAction->setToolTip(tr("Draw an outline on the ground (Ctrl+K)\n"
                                       "Click to place points; close it to make a face."));
    myFinishSketchAction->setToolTip(tr("Close the outline into a face (Enter)\n"
                                        "Needs at least three points."));
    // NOT "pull the face up": Pull is the face-dragging operation's own name
    // now (see CLAUDE.md's vocabulary table), and one word for two operations
    // is the thing that table exists to stop. Extrude raises a closed outline;
    // Pull moves a face of a body that already exists.
    myExtrudeAction->setToolTip(tr("Raise the face into a body (E)\n"
                                   "The outline's shape becomes the body's footprint."));
    myUnionAction->setToolTip(tr("Combine two bodies into one\n"
                                 "Overlapping material is kept once, not twice."));
    mySubtractAction->setToolTip(tr("Cut the second body out of the first\n"
                                    "Like a chisel removing waste. The body you made "
                                    "first is the one that keeps its shape."));
    myIntersectAction->setToolTip(tr("Keep only where two bodies overlap\n"
                                     "Everything outside the shared volume is discarded."));

    // Linked copies (Milestone 4, Task 4.2). These are the ENABLED
    // tooltips; updateActions() swaps each for a reason-specific one while
    // disabled, the same "why not" contract Lock to Face and Set Symmetry
    // Plane keep - see duplicateLinkedTooltipText() and its two neighbours.
    myDuplicateLinkedAction->setToolTip(duplicateLinkedTooltipText());
    myLinkSelectedAction->setToolTip(linkSelectedTooltipText());
    myUnlinkAction->setToolTip(unlinkBodyTooltipText());

    auto* selectionGroup = new QActionGroup(this);
    selectionGroup->addAction(mySolidSelectAction);
    selectionGroup->addAction(myFaceSelectAction);
    selectionGroup->addAction(myEdgeSelectAction);
    selectionGroup->setExclusive(true);
    connect(mySolidSelectAction, &QAction::triggered, this, &MainWindow::onSelectionModeChanged);
    connect(myFaceSelectAction, &QAction::triggered, this, &MainWindow::onSelectionModeChanged);
    connect(myEdgeSelectAction, &QAction::triggered, this, &MainWindow::onSelectionModeChanged);

    myUnitsMillimetresAction = new QAction(tr("Millimetres"), this);
    myUnitsMillimetresAction->setCheckable(true);
    myUnitsCentimetresAction = new QAction(tr("Centimetres"), this);
    myUnitsCentimetresAction->setCheckable(true);

    auto* unitsGroup = new QActionGroup(this);
    unitsGroup->addAction(myUnitsMillimetresAction);
    unitsGroup->addAction(myUnitsCentimetresAction);
    unitsGroup->setExclusive(true);

    // Reflects whatever setDisplayUnit() the constructor already applied from
    // the persisted setting (or the Millimetres default), before this action
    // group exists at all.
    const bool startsInCentimetres = Measure::displayUnit() == Measure::Unit::Centimetres;
    myUnitsMillimetresAction->setChecked(!startsInCentimetres);
    myUnitsCentimetresAction->setChecked(startsInCentimetres);

    connect(myUnitsMillimetresAction, &QAction::triggered, this,
            [this] { setDisplayUnit(Measure::Unit::Millimetres); });
    connect(myUnitsCentimetresAction, &QAction::triggered, this,
            [this] { setDisplayUnit(Measure::Unit::Centimetres); });
}

QMenuBar* MainWindow::buildMenus()
{
    // Ours from the start, never the window's auto-created one - see the
    // declaration in MainWindow.h and the trap at the top of AppBar.h. It is
    // parented to the window only so it is never briefly a top-level widget;
    // the app bar's layout adopts it a moment later.
    auto* bar = new QMenuBar(this);

    QMenu* fileMenu = bar->addMenu(tr("&File"));
    fileMenu->addAction(myFileSaveAction);
    fileMenu->addAction(myAutosaveAction);
    fileMenu->addAction(mySaveVersionAction);
    fileMenu->addAction(myCloseFurnitureAction);
    fileMenu->addSeparator();
    fileMenu->addAction(myExportStepAction);
    fileMenu->addAction(myScreenshotAction);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    QMenu* sketchMenu = bar->addMenu(tr("&Sketch"));
    sketchMenu->addAction(myStartSketchAction);
    sketchMenu->addAction(myFinishSketchAction);
    sketchMenu->addAction(myUndoPointAction);
    sketchMenu->addAction(myCancelSketchAction);
    sketchMenu->addSeparator();
    sketchMenu->addAction(myLockFaceAction);
    sketchMenu->addAction(myUnlockFaceAction);

    QMenu* editMenu = bar->addMenu(tr("&Edit"));
    editMenu->addAction(myUndoAction);
    editMenu->addAction(myRedoAction);
    editMenu->addSeparator();
    editMenu->addAction(myDeleteAction);
    editMenu->addAction(myRenameAction);

    QMenu* modelMenu = bar->addMenu(tr("&Model"));
    modelMenu->addAction(myExtrudeAction);
    modelMenu->addSeparator();
    modelMenu->addAction(myUnionAction);
    modelMenu->addAction(mySubtractAction);
    modelMenu->addAction(myIntersectAction);
    modelMenu->addSeparator();
    // Menu-only - see mySymmetryAction's own declaration for why no rail
    // chip.
    modelMenu->addAction(mySymmetryAction);
    modelMenu->addAction(mySetSymmetryPlaneAction);
    modelMenu->addAction(mySymmetryOffAction);
    modelMenu->addSeparator();
    // Linked copies (Milestone 4, Task 4.2) - menu-only, same reason.
    modelMenu->addAction(myDuplicateLinkedAction);
    modelMenu->addAction(myLinkSelectedAction);
    modelMenu->addAction(myUnlinkAction);

    QMenu* viewMenu = bar->addMenu(tr("&View"));
    viewMenu->addAction(myFitAction);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Axonometric"), QKeySequence(Qt::Key_0), this,
                        &MainWindow::goAxonometric);
    // These three name a DIRECTION and nothing else: unlike a click on a gizmo
    // tip, they deliberately do not borrow an orthographic look, so they leave
    // the projection exactly as the user set it. The asymmetry is the brief's,
    // and it is a real distinction rather than an oversight - the gizmo is a
    // direct-manipulation gesture aimed at a face of a cube, where convergence
    // is the thing being complained about, while these are a menu entry and a
    // number key that mean "look from the top" and make no claim about how the
    // scene should be drawn once you get there.
    viewMenu->addAction(tr("&Top"), QKeySequence(Qt::Key_1), this, [this] {
        myView->setViewTop();
        recordViewChanged();
    });
    viewMenu->addAction(tr("F&ront"), QKeySequence(Qt::Key_2), this, [this] {
        myView->setViewFront();
        recordViewChanged();
    });
    viewMenu->addAction(tr("&Right"), QKeySequence(Qt::Key_3), this, [this] {
        myView->setViewRight();
        recordViewChanged();
    });
    viewMenu->addSeparator();
    // Beside the named views, because it is the other half of "what am I
    // looking at" - but below the separator, because it changes how the scene
    // is drawn rather than where the camera stands.
    viewMenu->addAction(myOrthographicAction);
    viewMenu->addSeparator();
    viewMenu->addAction(mySnapAction);
    viewMenu->addSeparator();
    viewMenu->addAction(mySolidSelectAction);
    viewMenu->addAction(myFaceSelectAction);
    viewMenu->addAction(myEdgeSelectAction);
    viewMenu->addAction(myItemsPanelAction);
    viewMenu->addAction(myVersionsPanelAction);
    viewMenu->addAction(myNotificationsAction);
    viewMenu->addAction(myBottomBarAction);
    viewMenu->addAction(myRenderModeAction);
    viewMenu->addSeparator();
    QMenu* unitsMenu = viewMenu->addMenu(tr("Units"));
    unitsMenu->addAction(myUnitsMillimetresAction);
    unitsMenu->addAction(myUnitsCentimetresAction);
    viewMenu->addAction(myAppearanceAction);

    QMenu* helpMenu = bar->addMenu(tr("&Help"));

    myShortcutsAction = new QAction(tr("Keyboard Shortcuts"), this);
    // Both bindings the design calls for. F1 is what people reach for without
    // being told; ? is what the sheet itself is worth advertising.
    myShortcutsAction->setShortcuts(
        {QKeySequence(Qt::Key_Question), QKeySequence(Qt::Key_F1)});
    myShortcutsAction->setToolTip(tr("List every keyboard shortcut (? or F1)"));
    helpMenu->addAction(myShortcutsAction);

    helpMenu->addAction(tr("Show tips again"), this, [this] {
        myProgress.reset();
        if (myPersistProgress) {
            QSettings settings;
            settings.setValue(QStringLiteral("progress"), QString());
        }
        statusBar()->showMessage(tr("Tips reset — the guide and hints will appear again"));
        // Before appStateChanged, not after: a surface that remembers what it
        // already showed this session has to forget that first, or the
        // reconsider() this emission drives would find every hint still
        // marked as spent and put none of them back. Emptying the store is
        // only half of what "show tips again" means.
        emit progressReset();
        emit appStateChanged();
        // The guide has just reappeared, somewhere in the middle of that
        // emission. Anything that places itself against it - the toast, the
        // hint balloon - has to be told, and which slot ran first on
        // appStateChanged is not something to rely on. relayout() re-places
        // every anchored widget and then emits laidOut(), which is the one
        // ordering guarantee in this file: the dependents re-place after the
        // guide is at its final rectangle, not before. HintBalloon already
        // handled this case for itself inside reconsider(); ToastHost did
        // not, so a Show tips again under a live toast left the restored
        // guide sitting on top of it.
        if (myOverlay) myOverlay->relayout();
    });

    return bar;
}

void MainWindow::goAxonometric()
{
    // The one way back to the angled view. Its entry points - the View menu
    // and its 0 shortcut - call this, so the pose and the recorded event
    // cannot drift apart the way they would if each site re-derived the
    // camera state for itself. The app bar's button used to be a third; that
    // seat is the projection toggle now.
    //
    // The angled view is the opposite of a face-on one, so it hands back any
    // borrowed orthographic look rather than carrying it into a pose nothing
    // squared up for. A user who CHOSE Ortho keeps it - this clears the loan,
    // not the mode.
    myView->camera().setTemporaryOrtho(false);
    myView->setViewAxonometric();
    recordViewChanged();
}

void MainWindow::buildAppBar(QMenuBar* menus)
{
    myAppBar = new AppBar(menus, myDisplayModeAction, myFitAction);
    // The window takes ownership. Nothing may call menuBar() from here on.
    setMenuWidget(myAppBar);

    myAppBar->setOrthographic(myOrthographicAction->isChecked());
    myAppBar->setUnitLabel(QString::fromStdString(Measure::unitSuffix()));

    // The button triggers the action rather than flipping anything itself -
    // the same contract the unit chip has, and the reason the menu entry, the
    // O shortcut and this button can never disagree. It no longer snaps to
    // the axonometric pose: that is the gizmo's job, and the View menu's, and
    // this seat now belongs to the projection.
    connect(myAppBar, &AppBar::projectionClicked, this,
            [this] { myOrthographicAction->trigger(); });

    // The button triggers the OTHER unit's existing action rather than
    // writing the unit itself: persistence, the items panel, the status bar
    // and the extrude field's own label then all follow the single path
    // setDisplayUnit() already owns, and updateActions() stays the one place
    // that decides anything.
    connect(myAppBar, &AppBar::unitClicked, this, [this] {
        if (Measure::displayUnit() == Measure::Unit::Millimetres)
            myUnitsCentimetresAction->trigger();
        else
            myUnitsMillimetresAction->trigger();
    });

    // The readout follows the one signal every unit-following surface already
    // refreshes on. It only reads and sets a string, so it cannot recurse
    // back into updateActions().
    connect(this, &MainWindow::appStateChanged, myAppBar, [this] {
        myAppBar->setUnitLabel(QString::fromStdString(Measure::unitSuffix()));
        // The BASE mode, off the action that owns it - not the camera's
        // effective one. A gizmo arm or a face lock borrows orthographic for
        // one orbit, and a readout that followed the loan would tell the user
        // they had changed a setting they never touched.
        myAppBar->setOrthographic(myOrthographicAction->isChecked());
    });
}

void MainWindow::buildOverlay()
{
    myOverlay = new ViewportOverlay(myView);

    // ONE rail, pinned to the viewport's left edge, in place of the four
    // chip clusters that used to float in three corners and one edge centre.
    // Every button is an existing QAction rendered icon-only; nothing here
    // creates an action, and nothing here decides whether a button is
    // enabled or checked - updateActions() remains the single place that
    // does. The groups read top to bottom as the order of work: what to look
    // at, what to draw, what to build, what to pick, and - pushed to the
    // bottom by the stretch - what to take back.
    //
    // ViewportOverlay::relayout()'s LeftEdge case deliberately keeps every
    // button its designed size on a too-short viewport and lets the last one
    // run off the bottom edge - Redo first, then Undo - rather than squeezing
    // fixed-size buttons into a space they do not fit, which Qt resolves by
    // overlapping them. See that comment for why the clip is legible rather
    // than fixed there. It is fixed HERE instead, a few lines down, by never
    // letting the viewport get that short in the first place - the minimum
    // height is DERIVED from the rail's own sizeHint() rather than a measured
    // literal, so it cannot go stale the day a fourteenth button is added
    // (see CLAUDE.md's warning that the rail wants a rework well before a
    // screen's own height becomes the real ceiling this derivation cannot
    // push past).
    auto* rail = new ToolCluster(myView);
    // Kept as a member - render mode's own visibility lambda (see the
    // constructor) needs to reach it, and findChild<>() on every
    // appStateChanged is a lookup this class already has a real pointer for.
    myRail = rail;
    auto tool = [rail](QAction* action, IconSet::Glyph glyph) {
        rail->addChip(new ToolChip(action, glyph, ToolChip::ChipMode::IconOnly));
    };

    tool(myItemsPanelAction, IconSet::Glyph::Items);
    rail->addSeparator();
    tool(myStartSketchAction, IconSet::Glyph::Sketch);
    tool(myExtrudeAction,     IconSet::Glyph::Extrude);
    rail->addSeparator();
    tool(myUnionAction,       IconSet::Glyph::Fuse);
    tool(mySubtractAction,    IconSet::Glyph::Cut);
    tool(myIntersectAction,   IconSet::Glyph::Intersect);
    tool(myDeleteAction,      IconSet::Glyph::Delete);
    rail->addSeparator();
    tool(mySnapAction,        IconSet::Glyph::Snap);
    tool(mySolidSelectAction, IconSet::Glyph::SelectSolid);
    tool(myFaceSelectAction,  IconSet::Glyph::SelectFace);
    tool(myEdgeSelectAction,  IconSet::Glyph::SelectEdge);
    rail->addStretch();
    tool(myUndoAction,        IconSet::Glyph::Undo);
    tool(myRedoAction,        IconSet::Glyph::Redo);

    // The viewport must never be able to shrink shorter than the rail needs.
    // rail->sizeHint() is the rail's own natural stack height - every chip,
    // separator and gap, plus the card's own top/bottom padding - with the
    // stretch between Select Edges and Undo contributing nothing, the same
    // number ViewportOverlay::relayout() calls `ch` for a LeftEdge entry.
    // ViewportOverlay pins that entry kEdgeMargin px off BOTH the top and the
    // bottom of the viewport (see relayout()'s LeftEdge case), so the
    // viewport needs at least the rail's height plus twice that margin.
    // Read from ViewportOverlay itself rather than repeated here, so the two
    // cannot silently disagree about what the rail is pinned against.
    myView->setMinimumHeight(rail->sizeHint().height() + 2 * ViewportOverlay::kEdgeMargin);

    myOverlay->addWidget(rail, ViewportOverlay::Anchor::LeftEdge);

    // The items drawer, beside the rail rather than under it - see
    // ViewportOverlay's Anchor comment for why that is the layout's business
    // and not a hard-coded offset here. Anchoring it is also the whole of
    // what puts it in occupiedRects(), so the toast, the balloon and the
    // guide step around it without any of them naming this widget.
    myOverlay->addWidget(myItemsPanel, ViewportOverlay::Anchor::TopLeft);

    // The versions drawer, same anchor - TopLeft entries stack downward in
    // the order they are added (see ViewportOverlay::relayout()), so this
    // lands beside the rail and below the items drawer for free. Starts
    // hidden: myVersionsPanelAction starts unchecked (see buildActions()),
    // and addWidget() shows whatever it anchors UNLESS the widget has
    // already made its own explicit hide decision - hide() here, before
    // adding it, is what makes this one of those.
    myVersionsPanel = new VersionsPanel(this, myView, myView);
    myVersionsPanel->hide();
    myOverlay->addWidget(myVersionsPanel, ViewportOverlay::Anchor::TopLeft);

    // Wireframe and Fit All are buttons in the app bar, and Save Screenshot -
    // the least used of the three, and absent from the design's bar and rail
    // alike - is reachable from the File menu.

    // The orientation gizmo. Its own label chip and the unit readout that sat
    // under it are in the app bar; only the axes stay over the viewport.
    auto* gizmo = new AxisGizmo(myView, myView);
    // Kept as a member on the same terms as myRail above - render mode
    // hides this card too.
    myAxisGizmo = gizmo;
    // Clicking an arm of the gizmo is the other way to look from a named
    // direction, and the hint that teaches the gizmo is retired by
    // view.changed - so a user who only ever used the gizmo used to dismiss
    // that hint every session and never cross the threshold. The gizmo
    // announces the snap and this window decides what it means; giving the
    // gizmo a MainWindow just to record an event would hand a painted
    // overlay a dependency on the whole application.
    connect(gizmo, &AxisGizmo::viewSnapped, this, &MainWindow::recordViewChanged);
    myOverlay->addWidget(gizmo, ViewportOverlay::Anchor::TopRight);

    // The Appearance card, anchored at the same corner so relayout() stacks
    // it one gap under the gizmo - see AppearancePanel.h for why TopRight and
    // not RightCenter. Hidden BEFORE it is added: ViewportOverlay::addWidget()
    // shows whatever it anchors unless the widget has already made an
    // explicit hide decision of its own, and this card's visibility belongs to
    // myAppearanceAction alone.
    myAppearancePanel = new AppearancePanel(myView);
    myAppearancePanel->hide();
    myOverlay->addWidget(myAppearancePanel, ViewportOverlay::Anchor::TopRight);

    // The render settings card (Task 7.2, Option A - "one floating card"),
    // anchored at the SAME TopRight slot the gizmo and the Appearance card
    // already stack under - safe because the three are mutually exclusive
    // by construction: the gizmo and the Appearance card both hide
    // unconditionally the instant render mode turns on (see the
    // appStateChanged-driven visibility lambda below, extended by this
    // task to derive this card's own visibility the opposite way - visible
    // ONLY while render mode is on), so no two of them are ever anchored
    // there at once. Hidden before it is added, on the Appearance
    // card's own terms: its visibility belongs to the render-mode-derived
    // lambda alone, never to addWidget()'s default show().
    myRenderSettingsPanel = new RenderSettingsPanel(myView);
    myRenderSettingsPanel->hide();
    myOverlay->addWidget(myRenderSettingsPanel, ViewportOverlay::Anchor::TopRight);
    // Seeded from whatever the constructor already applied to the viewport
    // (QSettings, or OcctViewWidget's own shipped defaults) - setValuesSilently()
    // so this first sync does not immediately re-emit six signals and
    // persist six values nothing actually changed. renderSurfaceRoughness()
    // is inverted into this panel's own "glossiness" convention - see
    // RenderSettingsPanel.h's own note on why the inversion lives at this
    // wiring site and nowhere else.
    myRenderSettingsPanel->setValuesSilently(
        1.0 - myView->renderSurfaceRoughness(), myView->renderMetal(),
        myView->renderLightAngleDeg(), myView->renderLightStrength(),
        myView->renderBackdropColour(), myView->renderFov());
    connect(myRenderSettingsPanel, &RenderSettingsPanel::surfaceGlossinessChanged, this,
            [this](double glossiness01) {
                myView->setRenderSurfaceRoughness(1.0 - glossiness01);
                persistRenderSettings();
            });
    connect(myRenderSettingsPanel, &RenderSettingsPanel::metalChanged, this,
            [this](double metallic01) {
                myView->setRenderMetal(metallic01);
                persistRenderSettings();
            });
    connect(myRenderSettingsPanel, &RenderSettingsPanel::lightAngleChanged, this,
            [this](double azimuthDeg) {
                myView->setRenderLightAngleDeg(azimuthDeg);
                persistRenderSettings();
            });
    connect(myRenderSettingsPanel, &RenderSettingsPanel::lightStrengthChanged, this,
            [this](double multiplier) {
                myView->setRenderLightStrength(multiplier);
                persistRenderSettings();
            });
    connect(myRenderSettingsPanel, &RenderSettingsPanel::backgroundChanged, this,
            [this](const QColor& colour) {
                myView->setRenderBackgroundOverride(colour);
                persistRenderSettings();
            });
    connect(myRenderSettingsPanel, &RenderSettingsPanel::fovChanged, this,
            [this](double fovyDeg) {
                myView->setRenderFov(fovyDeg);
                persistRenderSettings();
            });

    // The camera shutter - a round, standalone control at the viewport's
    // bottom-right corner, holding no state of its own: it triggers the
    // EXISTING Save Screenshot action, exactly as the mockup calls for,
    // rather than growing a second export path. Same hidden-before-added,
    // render-mode-derived visibility as the settings card above.
    myRenderShutter = new RenderShutterButton(myScreenshotAction, myView);
    myRenderShutter->hide();
    myOverlay->addWidget(myRenderShutter, ViewportOverlay::Anchor::BottomRight);

    // Every outcome the app reports - success or failure - goes through this
    // one host rather than a modal dialog. It parents itself (and its Toast)
    // to the viewport and positions itself, so it needs no overlay anchor of
    // its own; raise()ing on every show() keeps it above whatever cluster
    // happens to be on top. Undo, when a message offers it, replays through
    // the same onUndo() the Undo action itself uses.
    myToasts = new ToastHost(myView, this);
    // Through the ACTION, never straight to onUndo(). The menu entry, the
    // chip and the Ctrl+Z binding all obey myUndoAction's enabled state -
    // "!mySketching && canUndo()", set in updateActions(), which CLAUDE.md
    // makes the single place that decides what is available - and a toast
    // that called the slot directly was a fourth entry point that obeyed
    // none of it: delete a body, start a sketch inside the four-second
    // window, click Undo, and the document was resynced and the selection
    // cleared while the user was still placing points. updateActions() also
    // pushes that same enabled state onto the toast (see setUndoEnabled), so
    // the pill is dimmed and out of hit-testing rather than merely inert.
    connect(myToasts, &ToastHost::undoRequested, this, [this] {
        // Two guards saying two different things. The first is scope: a toast
        // names a change to the DOCUMENT, and since Ctrl+Z gained its
        // mid-sketch meaning the action would otherwise take back a point
        // under a message about a body. The second is availability, which
        // updateActions() owns. The pill is already dimmed and out of
        // hit-testing in both cases; this is the backstop for an event
        // delivered straight at it.
        if (mySketching) return;
        if (myUndoAction->isEnabled()) myUndoAction->trigger();
    });
    // The Appearance card's two file outcomes. The card owns a look, not the
    // way this app reports things, so it announces and the copy lives here
    // with every other outcome - in cause-and-fix form, like each of them.
    // Failures, so they are shown whether or not the user has notifications on
    // (see ToastHost::show): a Load that changed nothing and said nothing would
    // be indistinguishable from a colour file with nothing in it.
    connect(myAppearancePanel, &AppearancePanel::colourSaveFailed, this,
            [this](const QString& path) {
                myToasts->show(tr("Couldn't write the colours to %1 — Check that the "
                                  "folder exists and isn't read-only").arg(path),
                              Toast::Kind::Failure, false);
            });
    connect(myAppearancePanel, &AppearancePanel::colourLoadRefused, this,
            [this](const QString& path) {
                myToasts->show(tr("%1 doesn't hold a look this app can read, so nothing "
                                  "changed — Pick a file made with Save colours")
                                   .arg(path),
                              Toast::Kind::Failure, false);
            });

    // A toast that offers to undo one operation must not survive that
    // operation - see ToastHost::documentMovedTo().
    connect(this, &MainWindow::documentChanged, this,
            [this] { myToasts->documentMovedTo(myDocument.revision()); });

    // File -> Save automatically's arm: documentChanged fires after every
    // committed change to the document (every commit path checkpoints THEN
    // mutates THEN emits this), which is functionally "after every
    // checkpoint" without a second signal only this feature would need.
    // Restarted on every call, exactly like the appearance debounce - a
    // burst of edits inside the 400ms window lands one write, not one per
    // edit. Skips entirely while no furniture is open or the toggle is off,
    // so this never fires for the seeded startup document a test builds
    // before opening anything.
    connect(this, &MainWindow::documentChanged, this, [this] {
        if (myShowingInitScreen || myFurnitureId.isEmpty() || !myAutosaveOn) return;
        // openFurniture() emits this too, for a freshly loaded document that
        // is clean by construction (mySavedRevision is set to its revision
        // in the same call) - guarded here so opening a furniture cannot
        // itself arm a spurious autosave write.
        if (!isFurnitureDirty()) return;
        armAutosaveTimer();
    });
    // A guide can appear UNDERNEATH a toast that is already up (Show tips
    // again does exactly that), and nothing told the toast to step aside
    // when it did - HintBalloon::reconsider() already handled that case for
    // itself. appStateChanged is when it happens - but the connection that
    // used to sit here, straight from appStateChanged to replace(), was
    // REDUNDANT with the one below on ViewportOverlay::laidOut(), not a
    // second necessary route: the lambda a few lines up
    // (myItemsPanel->setVisible(...)) already calls myOverlay->relayout() on
    // every appStateChanged, and relayout() itself emits laidOut() once every
    // anchored widget is at its final rectangle - so replace() was already
    // running once, in the right order, before this line ran it a second
    // time. Removed rather than kept as a belt-and-braces call: two
    // connections that fire from the same event and do the same thing is the
    // sort of drift this file's own rule against a second refresh path warns
    // about, and the survivor is the one ordered correctly - see the comment
    // on the laidOut() connection below.

    // Replaces the old QInputDialog::getDouble() for extrude height. Parents
    // itself to the viewport and positions itself (top-center, clear of the
    // toast/guide/balloon bottom strip - see ExtrudePreview::reposition()),
    // so it needs no overlay anchor of its own either.
    myExtrudePreview = new ExtrudePreview(this, myView);

    // The face-pull gizmo. Like the extrude panel it parents itself to the
    // viewport and places itself - beside the arrow's projected head rather
    // than against a viewport edge, so it needs no overlay anchor. It decides
    // its own visibility from MainWindow::canPullSelectedFace() on every
    // appStateChanged; nothing here shows or hides it.
    myPullArrow = new PullArrow(this, myView);

    // The bevel gizmo, on exactly the same terms: it parents itself to the
    // viewport, places itself beside its arrow's projected head, and decides
    // its own visibility from MainWindow::bevelTarget() on every
    // appStateChanged. Nothing here shows or hides it.
    myBevelArrow = new BevelArrow(this, myView);

    // The mirror-placement gesture's own value chip (Milestone 4, Phase 3),
    // on the same terms as the two arrows just above: it parents itself to
    // the viewport, places itself beside the handle's projected position,
    // and decides its own visibility from
    // OcctViewWidget::mirrorPlacementActive() on every appStateChanged.
    // Nothing here shows or hides it - only beginMirrorPlacement()/
    // confirmMirrorPlacement()/cancelMirrorPlacement() ever change that
    // state, and this widget's own refresh() is the only thing that reads
    // it back into a visible/hidden card. myMirrorChip is stored as a plain
    // QWidget* (see its own field comment in MainWindow.h) purely so this
    // window can reach it once more, immediately below, for the laidOut
    // connection every anchored/self-placing panel in this file gets.
    auto* mirrorChip = new MirrorPlacementChip(this, myView);
    myMirrorChip = mirrorChip;

    // The live view's half of the compare camera sync - see syncCamera()'s
    // declaration. A no-op for as long as myCompareView is null, which is
    // most of this window's life; wired once, here, rather than re-wired
    // every time compare opens.
    connect(myView, &OcctViewWidget::cameraChanged, this, [this] {
        if (myCompareView) syncCamera(myView, myCompareView);
    });

    // Always built, even for a user who has already learned this - it
    // decides its own visibility in its constructor (see WalkthroughPanel's
    // refresh()) and hides itself immediately in that case. Gating
    // construction on hasLearned() here instead would mean a returning
    // user's window has no panel to bring back when Show tips again resets
    // their progress, and the guide would stay gone until the app is
    // restarted - exactly the case Show tips again exists for.
    myOverlay->addWidget(new WalkthroughPanel(this, myView),
                         ViewportOverlay::Anchor::BottomRight);

    // Built last, after the walkthrough, so the guide is never competing with
    // a hint on first run. It parents itself to the viewport and positions
    // itself, centred near the bottom rather than pinned to an edge, so it
    // needs no overlay anchor of its own.
    auto* hints = new HintBalloon(this, myView);

    // The three surfaces that place themselves against widgets the overlay
    // owns re-place themselves HERE, from the overlay's own "I have finished
    // laying out" signal - not from their own filters on the viewport's
    // resize event. Qt runs event filters last-installed-first, and the
    // overlay installs its own first, so those filters all ran BEFORE the
    // guide and the chip clusters had moved: on a shrink the toast and the
    // balloon stepped aside from where the guide used to be and the guide
    // then landed on top of them, and the extrude panel was raised before
    // relayout() raised the top-left cluster back over it. Ordering off the
    // signal makes "after the anchored widgets have moved" a property of the
    // code rather than an accident of construction order.
    //
    // This is myToasts's ONE connection to replace() - appStateChanged
    // reaches it too, but only by relaying through relayout()'s own laidOut()
    // emission (see the comment further up, where a second direct connection
    // to appStateChanged used to sit and double-call this).
    connect(myOverlay, &ViewportOverlay::laidOut, myToasts, &ToastHost::replace);
    connect(myOverlay, &ViewportOverlay::laidOut, hints, &HintBalloon::reposition);
    connect(myOverlay, &ViewportOverlay::laidOut, myExtrudePreview, &ExtrudePreview::replace);
    connect(myOverlay, &ViewportOverlay::laidOut, myPullArrow, &PullArrow::replace);
    connect(myOverlay, &ViewportOverlay::laidOut, myBevelArrow, &BevelArrow::replace);
    connect(myOverlay, &ViewportOverlay::laidOut, mirrorChip, &MirrorPlacementChip::replace);
}

void MainWindow::updateActions()
{
    // The init screen's own gate. Every modeling action is disabled while
    // it shows - not merely painted over. A fresh, empty DocumentModel
    // (showInitScreen() replaces it, never just clears it) already makes
    // most of the predicates below false on their own - there is nothing to
    // select, nothing to undo, no outline waiting - but Start Sketch's own
    // predicate is `!mySketching` alone, which an empty document does not
    // touch: without this a shortcut typed over the gallery would start
    // drawing an outline nobody can see. Kept as one explicit clause here
    // rather than trusted to the document being empty, because "empty" and
    // "no furniture is open" are two different facts that only happen to
    // coincide right now.
    const bool atInit = myShowingInitScreen;

    const std::size_t selectedCount = myView->selectedSolidIds().size();
    const bool booleanReady = !mySketching && !atInit && selectedCount == 2;

    myStartSketchAction->setEnabled(!mySketching && !atInit);
    myFinishSketchAction->setEnabled(mySketching && mySketch.canClose());
    myUndoPointAction->setEnabled(mySketching && mySketch.pointCount() > 0);
    myCancelSketchAction->setEnabled(mySketching);

    myExtrudeAction->setEnabled(!mySketching && !atInit && hasPendingFace());

    // Exactly one face, and a flat one: an outline needs a single plane to
    // live on, and a cylinder's side has no such plane. Both halves are
    // checked again inside lockToFace(), because the double-click route can
    // reach a curved face this enabled state never sees.
    //
    // Not while sketching: the points already placed live on the plane that is
    // about to be swapped, and an outline with points on two planes is not an
    // outline. Not while a closed outline is waiting either - see
    // canChangeSketchPlane() for what moving the plane out from under it does.
    // That is exactly canPullSelectedFace()'s rule too, so the two read the
    // same function rather than each carrying a copy of it.
    const bool planeCanMove = !mySketching && !atInit && !hasPendingFace();
    const bool flatFaceSelected = !atInit && canPullSelectedFace();
    myLockFaceAction->setEnabled(flatFaceSelected);
    myUnlockFaceAction->setEnabled(myFaceLocked && planeCanMove);
    // A disabled control that does not say why is a control the user reads as
    // broken. Same idea as snapTooltipText(): recomputed here rather than
    // frozen at buildActions() time, so the reason is current.
    //
    // Two reasons, and they are asked in the order planeCanMove combines
    // them. hasPendingFace() is TRUE THROUGHOUT A SKETCH now - the waiting
    // outline is a document item and the new sketch does not discard it - so
    // an unguarded swap blamed the waiting outline while the sketch in
    // progress was the actual blocker, and told the user to press E, which is
    // disabled mid-sketch. The sketch takes precedence because it is the
    // condition the user can act on first, and because it is the one that is
    // true even with no outline anywhere.
    //
    // The outline reason names the remedies that WORK. "Ctrl+K to start a new
    // one" was one of them until this phase - see canChangeSketchPlane() for
    // why it stopped being one and why advice that does nothing is worse than
    // none. "Ctrl+Z to take it back" went the same way for the same reason,
    // one review later: it is only the outline's undo while the outline is
    // the TOP of the stack, and nothing gates the operations that push onto
    // it - close an outline, Union two bodies, and Ctrl+Z means the Union.
    // Delete is the remedy that is always the outline's, whatever has
    // happened since (see onDeleteSelected()).
    const QString sketchReason =
        tr("Unavailable while you're drawing — press Enter to close this outline, "
           "or Esc to cancel it");
    const QString pendingReason =
        tr("Unavailable while an outline is waiting — press E to extrude it, "
           "or Delete to discard it");
    const QString planeReason = mySketching ? sketchReason : pendingReason;
    myLockFaceAction->setToolTip(planeCanMove ? lockTooltipText() : planeReason);
    myUnlockFaceAction->setToolTip(planeCanMove ? unlockTooltipText() : planeReason);

    // Symmetry (Milestone 3, rebound in Milestone 4 Phase 3). The checked
    // state is STILL document state - undo, redo, opening a different
    // furniture, restoring a version, and now Enter confirming a mirror
    // placement can all change myDocument.symmetryOn() without this action's
    // own click - so it is resynced here rather than trusted to stay in step
    // on its own, the way the pure UI preferences (autosave, projection) are.
    // Blocked defensively (setChecked() does not itself emit triggered(),
    // only toggled(), and onSymmetryActionTriggered() is wired to the
    // former - but a resync that could re-enter its own handler is exactly
    // the class of bug this project has paid for once already).
    //
    // Always enabled whenever a furniture is open: unlike the flat-face
    // pick below, "no bodies selected" is not a reason to grey this out -
    // this task's own ruling is that S REFUSES with a reason at trigger
    // time rather than going dark, since going dark would say nothing about
    // why.
    if (mySymmetryAction) {
        const QSignalBlocker blocker(mySymmetryAction);
        mySymmetryAction->setChecked(myDocument.symmetryOn());
    }
    if (mySymmetryAction) mySymmetryAction->setEnabled(!atInit);
    // The off switch has something to do exactly while mirroring is on.
    if (mySymmetryOffAction) mySymmetryOffAction->setEnabled(!atInit && myDocument.symmetryOn());
    // The same pick as Lock to Face - one flat face, no sketch, no pending
    // outline.
    if (mySetSymmetryPlaneAction) mySetSymmetryPlaneAction->setEnabled(flatFaceSelected);

    // Linked copies (Milestone 4, Task 4.2). Each ENABLED state is read
    // straight from the header's own accessor (canDuplicateLinked() and
    // friends) rather than recomputed here, so the checkbox and the reason
    // shown while disabled can never drift into different ideas of what is
    // possible - canPullSelectedFace()'s own "one function, three readers"
    // rule, one gizmo over. The reason cascade asks the same terms
    // linkGestureEnvironmentOk() does, in the same order, for the same
    // reason mirrorPlacementRefusalText() does.
    {
        const std::vector<int> linkIds = myView->selectedSolidIds();
        const bool linkWrongMode =
            !mySketching && !atInit && !hasPendingFace() &&
            myView->selectionMode() != OcctViewWidget::SelectionMode::Solid;

        if (myDuplicateLinkedAction) {
            const bool enabled = canDuplicateLinked();
            myDuplicateLinkedAction->setEnabled(enabled);
            myDuplicateLinkedAction->setToolTip(
                enabled                    ? duplicateLinkedTooltipText()
                : mySketching               ? sketchReason
                : hasPendingFace()          ? pendingReason
                : linkWrongMode             ? tr("Switch to body selection, then select "
                                                  "the body to duplicate")
                : linkIds.size() != 1       ? tr("Select exactly one body to duplicate")
                                            : tr("This body is already mirrored — duplicate "
                                                 "its twin instead, or turn mirroring off "
                                                 "first"));
        }

        if (myLinkSelectedAction) {
            const bool enabled = canLinkSelected();
            myLinkSelectedAction->setEnabled(enabled);
            myLinkSelectedAction->setToolTip(
                enabled                    ? linkSelectedTooltipText()
                : mySketching               ? sketchReason
                : hasPendingFace()          ? pendingReason
                : linkWrongMode             ? tr("Switch to body selection, then select two "
                                                  "or more bodies")
                : linkIds.size() < 2        ? tr("Select two or more bodies to link")
                                            : tr("One of the selected bodies is already "
                                                 "linked or already mirrored — unlink it or "
                                                 "turn mirroring off first"));
        }

        if (myUnlinkAction) {
            const bool enabled = canUnlink();
            myUnlinkAction->setEnabled(enabled);
            myUnlinkAction->setToolTip(
                enabled                    ? unlinkBodyTooltipText()
                : mySketching               ? sketchReason
                : hasPendingFace()          ? pendingReason
                : linkWrongMode             ? tr("Switch to body selection, then select a "
                                                  "linked body")
                : linkIds.size() != 1       ? tr("Select exactly one linked body")
                                            : tr("This body isn't linked to anything"));
        }
    }

    myUnionAction->setEnabled(booleanReady);
    mySubtractAction->setEnabled(booleanReady);
    myIntersectAction->setEnabled(booleanReady);

    myExportStepAction->setEnabled(!atInit && myDocument.count() > 0);
    // Delete has TWO meanings and one of them is new: bodies when bodies are
    // selected, and the waiting outline when nothing is. It is the outline's
    // only exit besides Extrude, and the whole reason it needed one is in
    // onDeleteSelected() - the operations that push onto the undo stack are
    // not gated on a waiting outline, so "Ctrl+Z to take it back" stops being
    // true the moment the user does anything else. Which meaning applies is
    // decided HERE, in the one place that decides what is available, and
    // onDeleteSelected() asks the same question the same way.
    const bool deleteTargetsOutline = selectedCount == 0 && hasPendingFace();
    myDeleteAction->setEnabled(!mySketching && !atInit && (selectedCount > 0 || hasPendingFace()));
    // A control whose meaning moves has to say which meaning is live, or the
    // user reads one label and gets the other - the same argument the Lock to
    // Face tooltip above makes for a control that is disabled.
    myDeleteAction->setToolTip(
        deleteTargetsOutline
            ? tr("Discard the outline that's waiting (Del) — nothing is selected, "
                 "so Delete takes the outline instead of a body")
            : tr("Delete the selected bodies (Del)"));
    // Rename has the same two targets Delete does, but never falls back to a
    // "whichever, in bulk" meaning: InlineRename edits exactly one name, so
    // this is live for exactly one selected body, or for the waiting outline
    // when nothing is selected - never for two or more bodies, where Delete
    // stays enabled and this does not.
    //
    // Fix round 1 (review): the drawer must be VISIBLE too. F2's whole
    // gesture is opening a QLineEdit over a row that lives inside
    // myItemsPanel, and with View -> Items off that row is a real widget in
    // a HIDDEN hierarchy - InlineRename's setFocus() never actually takes
    // focus there (Qt does not focus a widget with a hidden ancestor), so
    // none of Enter/Escape/focus-out can ever fire and the stray editor sits
    // there forever. Worse, ItemsPanel::beginRenameForItem()'s own
    // re-entrancy guard (see its header) then reads that stray editor as "a
    // rename is already open" and refuses every LATER rename too, drawer
    // shown or not, until an unrelated document change rebuilds the rows out
    // from under it. Gating here is the disabled-control-explains-itself law
    // CLAUDE.md names elsewhere; ItemsPanel::beginRenameForItem() below
    // additionally guards itself, because this action's enabled state does
    // not stop a caller from invoking trigger() directly (Qt actions ignore
    // isEnabled() for programmatic trigger()s, only for real shortcut/menu
    // input) - the ONE place the gesture actually opens is where the wedge
    // has to be structurally impossible, not just discouraged.
    const bool drawerVisible = myItemsPanelAction && myItemsPanelAction->isChecked();
    const bool renameTargetsOutline = selectedCount == 0 && hasPendingFace();
    // Fix round 1 (Task 3.2 review, Finding 2): a live mirror-placement
    // gesture also has to exclude Rename, the same way canOpenSaveVersion()
    // already excludes the pull/bevel/extrude claims - one selected body is
    // exactly what both this and canBeginMirrorPlacement() want, so the
    // single most ordinary case (one body, S pressed) left Rename fully
    // reachable and typing a name containing x/y/z silently reoriented the
    // plane instead of reaching the field. ItemsPanel::beginRenameForItem()
    // carries the same guard directly, for the reason its own comment gives
    // (a disabled action does not stop a programmatic trigger()).
    const bool canRename = !mySketching && !atInit && drawerVisible &&
                           !myView->mirrorPlacementActive() &&
                           (selectedCount == 1 || renameTargetsOutline);
    myRenameAction->setEnabled(canRename);
    myRenameAction->setToolTip(
        !drawerVisible
            ? tr("Show the Items drawer to rename a row (F2, View → Items)")
            : myView->mirrorPlacementActive()
                  ? tr("Unavailable while placing a mirror plane — Enter mirrors, "
                      "Esc cancels")
                  : renameTargetsOutline
                        ? tr("Rename the outline that's waiting (F2)")
                        : selectedCount == 1
                              ? tr("Rename the selected body (F2)")
                              : tr("Select exactly one body to rename it (F2)"));
    // Mid-sketch, Undo removes the last placed point (onUndo() reroutes to
    // onUndoSketchPoint); outside a sketch it undoes a document change. The
    // menu text stays "Undo" either way - the user's word for "take that
    // back" does not change with the mode, and a menu entry whose label moved
    // under them would be worse than one whose scope did.
    //
    // Redo has no mid-sketch counterpart - a removed point is gone, not
    // parked on a stack - so it stays disabled while sketching. That
    // asymmetry is deliberate: it is better than a Redo that silently means
    // "redo a document change" while the user is looking at an outline.
    myUndoAction->setEnabled(mySketching ? mySketch.pointCount() > 0
                                         : (!atInit && myDocument.canUndo()));
    myUndoAction->setToolTip(mySketching
                                 ? tr("Take back the last point you placed (Ctrl+Z)")
                                 : tr("Undo the last change to your bodies (Ctrl+Z)"));
    // The toast's Undo pill still triggers the action, but its availability
    // is the DOCUMENT half of that predicate, not the action's whole enabled
    // state. Until the reroute above the two were the same expression and
    // this line could just read the action; they are not any more, and the
    // pill has to keep the narrower one. A pill under "Deleted Body 02" that
    // quietly took back a sketch point instead would be the label describing
    // one change while the control performed another - the exact defect the
    // revision guard in ToastHost was added to end. Still decided here, in
    // the one place that decides what is available, and still pushed out
    // rather than re-derived at the toast.
    if (myToasts) myToasts->setUndoEnabled(!mySketching && !atInit && myDocument.canUndo());
    // View -> Show notifications, pushed the same way and for the same reason:
    // this is the one place that decides it, and the host reads it rather than
    // re-deriving it from an action it would otherwise have to know about.
    // Note that Undo remains reachable with notifications off - the menu entry,
    // the rail chip and Ctrl+Z are untouched by this; only the toast that would
    // have offered a shortcut to it goes away.
    if (myToasts && myNotificationsAction)
        myToasts->setNotesEnabled(myNotificationsAction->isChecked());
    // Which outline Extrude would consume, pushed onto the drawer the same
    // way and for the same reason: this is the one place that decides it, and
    // the drawer row is the only handle the user has on the choice, so a
    // choice with no mark on it is a choice they cannot see. Refreshed here
    // rather than at the click, so an undo or a redo that moves the pending
    // outline moves the highlight with it.
    if (myItemsPanel) myItemsPanel->showPendingOutline(pendingOutlineId());
    myRedoAction->setEnabled(!mySketching && !atInit && myDocument.canRedo());

    // Not a slot on appStateChanged - part of updateActions() itself, same
    // as updateStateLabel(), so it recomputes on every unit switch too
    // rather than freezing whatever unit was active when the tooltip was
    // first built in buildActions().
    mySnapAction->setToolTip(snapTooltipText());

    // Selection mode and snap are meaningless with nothing to select or
    // snap - part of the same "every modeling action" gate atInit closes,
    // even though an empty document already leaves them harmless.
    mySnapAction->setEnabled(!atInit);
    mySolidSelectAction->setEnabled(!atInit);
    myFaceSelectAction->setEnabled(!atInit);
    myEdgeSelectAction->setEnabled(!atInit);

    // File -> Save / Save automatically / Close furniture: available only
    // with a furniture actually open.
    if (myFileSaveAction) myFileSaveAction->setEnabled(!atInit);
    if (myAutosaveAction) myAutosaveAction->setEnabled(!atInit);
    if (myCloseFurnitureAction) myCloseFurnitureAction->setEnabled(!atInit);
    // File -> Save version...: see canOpenSaveVersion()'s own declaration for
    // the full predicate - a furniture open, no sketch, no render mode, and
    // none of the three OTHER application-wide key claims live. The tooltip
    // only names the render-mode reason specifically (fix round 1, Important
    // 2) - the other four are pre-existing refusals this action already
    // disabled itself for silently, and adding a full disjunction here for
    // all five would be new copy for four reasons this task did not touch.
    if (mySaveVersionAction) {
        const bool canSaveVersion = canOpenSaveVersion();
        mySaveVersionAction->setEnabled(canSaveVersion);
        mySaveVersionAction->setToolTip(
            !canSaveVersion && myRenderModeOn
                ? tr("Unavailable in render mode — exit it first (a viewport "
                     "click, or the View menu)")
                : tr("Keep a named snapshot of this furniture\n"
                     "Come back to it later with Restore, or open it beside "
                     "the live one with Compare."));
    }
    if (myVersionsPanelAction) myVersionsPanelAction->setEnabled(!atInit);

    // Render mode (Milestone 3, item 5). "|| myRenderModeOn" is what keeps a
    // control whose entire subject is this mode from ever being outvoted by
    // a state that changed underneath it - the same rule the Persp/Ortho
    // toggle's own comment makes; without it a stray state change while the
    // mode was already on could disable the one control that turns it back
    // off. In practice every one of canOpenRenderMode()'s four conditions is
    // kept true for as long as myRenderModeOn is (every route that could
    // make one false forces the mode off FIRST - see checkpointDocument(),
    // onStartSketch(), openCompare()), so this is defence in depth rather
    // than a state this file expects to actually reach.
    if (myRenderModeAction) {
        const bool canRender = canOpenRenderMode();
        myRenderModeAction->setEnabled(canRender || myRenderModeOn);
        myRenderModeAction->setToolTip(
            canRender || myRenderModeOn
                ? tr("Strip the viewport to the furniture alone, with real shadows")
                : myShowingInitScreen
                      ? tr("Open a furniture first")
                      : isCompareOpen()
                            ? tr("Unavailable while comparing versions")
                            : mySketching
                                  ? sketchReason
                                  : pendingReason);
    }

    updateStateLabel();
    updateWindowTitle();
    emit appStateChanged();
}

void MainWindow::recordViewChanged()
{
    recordProgress("view.changed");
    // Recording alone teaches nothing: the hint that points at the gizmo is
    // retired by reconsider(), which only ever runs off appStateChanged.
    // Without this the third press of 0 left a hint on screen for an action
    // the user had already learned. updateActions() touches nothing this
    // path depends on, so it cannot recurse back in here.
    updateActions();
}

void MainWindow::setBaseProjection(bool orthographic)
{
    myView->setBaseProjection(orthographic ? CameraController::Projection::Orthographic
                                           : CameraController::Projection::Perspective);
    if (myPersistProgress) {
        QSettings settings;
        settings.setValue(QStringLiteral("projection"),
                          orthographic ? QStringLiteral("ortho") : QStringLiteral("persp"));
    }
    // Not recordProgress(), and specifically not recordViewChanged() - see the
    // declaration in MainWindow.h. updateActions() ends by emitting
    // appStateChanged(), which is what the bar's readout follows; no second
    // refresh path.
    updateActions();
}

void MainWindow::setShowNotifications(bool show)
{
    myShowNotifications = show;
    if (myPersistProgress) {
        QSettings settings;
        settings.setValue(QStringLiteral("showNotifications"), show);
    }
    // Not recordProgress(): this is a display preference, not a learned
    // capability. updateActions() is what actually pushes the state onto the
    // toast host - this function only stores it - so the one place that
    // decides what is available stays the one place that says it.
    updateActions();
}

void MainWindow::setShowBottomBar(bool show)
{
    myShowBottomBar = show;
    if (myPersistProgress) {
        QSettings settings;
        settings.setValue(QStringLiteral("showBottomBar"), show);
    }
    // Not recordProgress(): a display preference, not a learned capability -
    // the same reasoning setShowNotifications() gives just above. The actual
    // statusBar()->setVisible() call lives in the appStateChanged-driven
    // block this triggers, alongside every other drawer's own derived
    // visibility, rather than here - see that block's own comment.
    updateActions();
}

void MainWindow::setDisplayUnit(Measure::Unit unit)
{
    Measure::setDisplayUnit(unit);
    if (myPersistProgress) {
        QSettings settings;
        settings.setValue(QStringLiteral("displayUnit"),
                          unit == Measure::Unit::Centimetres ? QStringLiteral("cm")
                                                              : QStringLiteral("mm"));
    }
    // Not recordProgress(): the unit is a display preference, not a learned
    // capability, so it never touches UserProgress. updateActions() ends by
    // emitting appStateChanged(), which is what the items panel, the units
    // readout and the status bar all already refresh from - no second
    // refresh path needed.
    updateActions();
}

void MainWindow::onThemeChanged()
{
    // The window icon is a QIcon rasterised once, which is exactly the kind of
    // cached appearance value Theme's broadcast exists for (see Theme.h): it
    // carries accent() and panel(), and nothing repaints it. Re-painted here so
    // an edited palette reaches the title bar too.
    setWindowIcon(IconSet::appIcon());

    // The status bar's font is SET, not inherited: Theme.cpp's stylesheet
    // reaches QStatusBar's own internal message label through a selector, and
    // this covers a plain QStatusBar with no matching rule. An explicitly set
    // font does not follow QApplication::setFont, so it has to be put back.
    statusBar()->setFont(Theme::labelFont());
    // The font just changed, so both chrome strips just changed height - and
    // that is exactly what moves the viewport's edges onto a fractional device
    // row. See syncChromeHeights().
    syncChromeHeights();

    // The OCCT side of the bridge: a clear colour, two highlight drawers and
    // a grid whose colours are baked into its vertices. None of it is painted
    // by Qt, so none of it is reached by a repaint.
    if (myView) myView->applyTheme();

    // The live sketch markers are AIS objects coloured when they were built.
    // Re-issued from the sketch this window owns rather than from a copy the
    // viewport would have to keep - and only while there is a sketch, so this
    // cannot make a marker appear.
    if (mySketching && myView) {
        if (!mySketch.points().empty()) myView->setSketchPointMarkers(mySketch.points());
    }

    persistAppearance();

    emit themeChanged();

    // Last, and it is what actually repaints the shell: every widget in it
    // reads its colours from Theme inside paintEvent(), and appStateChanged()
    // - which updateActions() ends by emitting - is the signal they already
    // refresh on. No second refresh path.
    updateActions();
    if (myOverlay) myOverlay->relayout();
}

void MainWindow::persistAppearance()
{
    if (!myPersistProgress) return;

    // Built on first use rather than in the constructor: a window that never
    // sees a theme edit never creates one, and this is the only place that
    // can say whether the guard above let us get this far.
    if (!myAppearanceWrite) {
        myAppearanceWrite = new QTimer(this);
        myAppearanceWrite->setSingleShot(true);
        myAppearanceWrite->setInterval(kAppearanceWriteMs);
        connect(myAppearanceWrite, &QTimer::timeout, this,
                &MainWindow::writeAppearanceNow);
    }
    // start() on a running single-shot timer RESTARTS it, which is the whole
    // debounce: a drag through the colour wheel keeps pushing the deadline
    // out and lands exactly one write once the user stops.
    myAppearanceWrite->start();
}

void MainWindow::syncChromeHeights()
{
    // The viewport's top and bottom edges ARE the app bar's bottom edge and
    // the status bar's top edge, and both have to land on a whole device row.
    //
    // Widget geometry is logical; the surface OCCT paints into is sized in
    // device pixels. A chrome strip whose logical height does not multiply up
    // to a whole number of device rows leaves the seam between it and the
    // viewport on a fraction - Qt flushes the row, neither side's painter
    // reaches it, and over the GL surface an unpainted row is not transparent
    // but whatever the driver left, which measures as an exact 0,0,0 line.
    // Measured at 175% with an edited type scale: a 2068-device-pixel black
    // line the full width of the window, exactly where the status bar meets
    // the viewport. It is the floating-card rule (Theme::wholeDevicePixels,
    // see Theme.h) applied to the two cards that span the window, and neither
    // paintSurface() nor anything else either widget paints can reach a row
    // that is inside NEITHER widget's logical rect.
    //
    // It only appeared once the Appearance panel shipped because the default
    // type scale happens to give both strips a whole height. The base size is
    // a number the user edits now, so "happens to" stopped being a rule.
    //
    // The constraints are lifted before the hint is read, so this is
    // idempotent whatever a strip's sizeHint() does with its own fixed size:
    // re-running it can never ratchet a strip taller.
    auto whole = [](QWidget* strip) {
        if (!strip) return;
        strip->setMinimumHeight(0);
        strip->setMaximumHeight(QWIDGETSIZE_MAX);
        strip->setFixedHeight(Theme::wholeDevicePixels(strip->sizeHint().height()));
    };
    whole(menuWidget());
    whole(statusBar());
}

void MainWindow::writeAppearanceNow()
{
    // The ONE place the spec reaches QSettings. Two routes want it - the
    // debounce timer's timeout and the flush in closeEvent() - and they used
    // to carry a copy of the write each, which is two places to keep in step
    // with the key name and with whatever else a stored appearance ever needs
    // to include.
    QSettings settings;
    settings.setValue(QStringLiteral("appearance"), Theme::serializeSpec());
}

void MainWindow::persistRenderSettings()
{
    // persistAppearance()'s own shape - see its comment for why this is
    // debounced at all (a slider drag fires per mouse-move) and why the
    // timer is built lazily rather than in the constructor.
    if (!myPersistProgress) return;

    if (!myRenderSettingsWrite) {
        myRenderSettingsWrite = new QTimer(this);
        myRenderSettingsWrite->setSingleShot(true);
        myRenderSettingsWrite->setInterval(kRenderSettingsWriteMs);
        connect(myRenderSettingsWrite, &QTimer::timeout, this,
                &MainWindow::writeRenderSettingsNow);
    }
    myRenderSettingsWrite->start();
}

void MainWindow::writeRenderSettingsNow()
{
    // The ONE place the six values reach QSettings - writeAppearanceNow()'s
    // own reason to be a single function rather than inlined at both call
    // sites (the debounce timer and closeEvent()'s flush).
    QSettings settings;
    settings.setValue(QStringLiteral("renderMode/roughness"), myView->renderSurfaceRoughness());
    settings.setValue(QStringLiteral("renderMode/metallic"), myView->renderMetal());
    settings.setValue(QStringLiteral("renderMode/lightAngleDeg"), myView->renderLightAngleDeg());
    settings.setValue(QStringLiteral("renderMode/lightStrength"), myView->renderLightStrength());
    // Empty string for "no override" - renderBackgroundOverride()'s own
    // invalid-QColor convention, carried across the QSettings boundary the
    // same way the constructor's read-back interprets it.
    const QColor bg = myView->renderBackgroundOverride();
    settings.setValue(QStringLiteral("renderMode/background"),
                      bg.isValid() ? bg.name(QColor::HexArgb) : QString());
    settings.setValue(QStringLiteral("renderMode/fov"), myView->renderFov());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Milestone 4: this window is never actually destroyed. Closing it -
    // the native X, exactly as much as File -> Close furniture - always
    // means "go back to the library", never "quit the app": quitting is the
    // SELECTOR window's own close (SelectorWindow::closing(), wired to the
    // app's real quit function by EditorSelectorHandoff::wire() - see
    // src/EditorSelectorHandoff.h). Fix round 1: this used to lean on Qt's
    // default quitOnLastWindowClosed() noticing this window went hidden
    // with nothing else shown yet - which is exactly the CRITICAL quit-trap
    // that finding closed. main.cpp now disables that default outright, so
    // this event->ignore() only has to keep THIS window from ever actually
    // closing - it no longer has any bearing on whether the app quits.
    event->ignore();

    // A window closed inside the debounce window still has to store what the
    // user chose. Fired by hand rather than left to the timer.
    if (myAppearanceWrite && myAppearanceWrite->isActive()) {
        myAppearanceWrite->stop();
        writeAppearanceNow();
    }
    // The render-settings debounce, on the same terms (Task 7.2).
    if (myRenderSettingsWrite && myRenderSettingsWrite->isActive()) {
        myRenderSettingsWrite->stop();
        writeRenderSettingsNow();
    }

    // Close-saves-first, exactly as File -> Close furniture's own law - the
    // native X is the OTHER route this task's brief names alongside it, and
    // closeCurrentFurniture() already performs the full save-then-return
    // sequence. Fix round 2: that sequence now ABORTS (returns without
    // calling showInitScreen()) whenever the close-time save fails - and
    // since event->ignore() above already, unconditionally, keeps this
    // window from ever actually closing regardless of what
    // closeCurrentFurniture() does, a failed save simply leaves the window
    // open with nothing further required here: no separate "stay open on
    // failure" branch to get right a second time. When nothing is open at
    // all (the one state closeCurrentFurniture() itself refuses to act on -
    // e.g. the X clicked on a blank editor after a failed openFurniture())
    // there is nothing to save, so this just returns to the selector
    // directly.
    if (!myShowingInitScreen && !myFurnitureId.isEmpty()) {
        closeCurrentFurniture();
    } else {
        showInitScreen();
    }
}

void MainWindow::showInitScreen()
{
    // Render mode's own gate requires a furniture open - and this function is
    // how one stops being open, however it was reached (Close furniture,
    // opening a different card). Exiting first, rather than leaving
    // updateActions()'s "|| myRenderModeOn" defence to paper over it, is what
    // keeps the gallery from appearing underneath a hidden rail and a studio
    // backdrop that has nothing left to render mode a shot OF.
    if (myRenderModeOn) setRenderModeEnabled(false);

    // A compare pane reads a version of the furniture that is about to stop
    // being open at all - closing it here, before anything else, is what
    // keeps the splitter from outliving the furniture it was comparing.
    if (myCompareView) closeCompare();

    // Belt for any route that reaches here without closeCurrentFurniture()'s
    // own fresh save decision (which cancels the debounce and saves before
    // ever calling this): a debounce still pending at this point is flushed
    // rather than left to fire after the editor has hidden. On the ordinary
    // close path the timer is already stopped and this is a no-op.
    if (myAutosaveTimer && myAutosaveTimer->isActive()) {
        myAutosaveTimer->stop();
        flushAutosave();
    }

    myShowingInitScreen = true;
    myFurnitureId.clear();
    myFurnitureName.clear();
    mySavedRevision = 0;

    // A FRESH document, not a cleared one: DocumentModel::clear() leaves the
    // undo stack standing, and the next furniture opened must not inherit
    // checkpoints that were never its own.
    myDocument = DocumentModel();
    mySelectedOutlineId = 0;
    myFaceLocked = false;
    // Keeps the viewport's own copy (see setWorkPlaneLocked()) from outliving
    // the furniture whose lock it described - the returning-to-the-selector
    // path resets myFaceLocked here without going through unlockFace(), and a
    // stale true would wrongly suppress Task 5.1's face-on-ortho grid in
    // whatever furniture opens next.
    myView->setWorkPlaneLocked(false);
    mySketching = false;
    mySketch.reset();
    myView->setSketchMode(false, mySketch.plane());
    myView->clearPreview();
    myView->clearSelection();
    resyncView();

    updateActions();
    statusBar()->showMessage(tr("Choose a furniture to open, or start a new one"));

    // Milestone 4 fix round 1 (the CRITICAL quit-trap finding): this window
    // does NOT hide itself here any more. It only announces that it wants
    // the selector shown - EditorSelectorHandoff::wire() is the one place
    // that ever hides this window, and it always shows the selector FIRST.
    // Hiding here, unconditionally, before anything could show the
    // selector, is exactly the ordering that let two unparented top-level
    // windows both be hidden at once and race Qt's quitOnLastWindowClosed()
    // - see EditorSelectorHandoff.h for the full story. Harmless if nothing
    // is connected yet (the constructor's own first call, above) - this
    // window simply stays in whatever visibility it already had.
    emit returnedToSelector();
}

bool MainWindow::openFurniture(const QString& id)
{
    // Same reasoning as showInitScreen(): a compare pane belongs to
    // whichever furniture is currently open, and that is about to change.
    if (myCompareView) closeCompare();

    QString error;
    DocumentModel loaded;
    if (!myStore.loadFurniture(id, loaded, &error)) {
        myToasts->show(tr("Couldn't open this furniture — %1").arg(error),
                      Toast::Kind::Failure, false);
        return false;
    }

    myDocument = loaded;
    myFurnitureId = id;
    // Read once from the library listing rather than from the manifest
    // directly - FurnitureStore's own layout stays its private business
    // (see FurnitureStore.h), and listFurniture() is the one place this
    // window is allowed to read a name from.
    myFurnitureName.clear();
    for (const FurnitureStore::FurnitureInfo& info : myStore.listFurniture()) {
        if (info.id == id) { myFurnitureName = info.name; break; }
    }

    mySavedRevision = myDocument.revision();
    myShowingInitScreen = false;
    mySelectedOutlineId = 0;
    myFaceLocked = false;
    // See showInitScreen()'s identical line - the same drift is possible here.
    myView->setWorkPlaneLocked(false);
    mySketching = false;
    mySketch.reset();
    myView->setSketchMode(false, mySketch.plane());
    myView->clearPreview();
    myView->clearSelection();
    // resyncView() itself reapplies persisted visibility onto the freshly
    // displayed items now - see its own comment - so nothing further is
    // needed here.
    resyncView();

    myView->fitAll();
    updateActions();
    emit documentChanged();
    statusBar()->showMessage(tr("Opened %1").arg(myFurnitureName));
    return true;
}

bool MainWindow::isFurnitureDirty() const
{
    if (myShowingInitScreen || myFurnitureId.isEmpty()) return false;
    return myDocument.revision() != mySavedRevision;
}

bool MainWindow::performSave(bool announce)
{
    if (myShowingInitScreen || myFurnitureId.isEmpty()) return false;

    // Thumbnail capture reuses OcctViewWidget::saveSnapshot() - see its
    // header. A failed capture (a null image) is not itself a save failure;
    // saveFurniture() already treats a null thumbnail as "nothing to write
    // there yet" rather than as a reason to refuse.
    //
    // Fix-wave item (b): NOT while render mode is on. The render-mode
    // viewport is a studio shot of the scene, not a picture of the
    // FURNITURE, and Ctrl+S must not silently replace the gallery's card
    // image with it. Skipping the capture leaves the OLD thumbnail exactly
    // where saveFurniture() already treats a null image - untouched, not
    // deleted (see its own comment) - while the save itself still writes
    // shapes and manifest either way: data safety does not depend on which
    // picture is showing.
    const QImage thumb = myRenderModeOn ? QImage() : myView->captureThumbnail();
    if (!myStore.saveFurniture(myFurnitureId, myDocument, thumb)) {
        // A refusal reports here whether or not the caller wanted an
        // announcement - CLAUDE.md's law that a Failure is never silenced
        // applies to autosave's own background writes exactly as it does to
        // Ctrl+S.
        myToasts->show(tr("Couldn't save %1 — Check that its folder still exists "
                          "and isn't read-only").arg(myFurnitureName),
                      Toast::Kind::Failure, false);
        return false;
    }

    mySavedRevision = myDocument.revision();
    // Whatever the debounce was waiting to write, it just got written by
    // this call instead - a pending autosave surviving a save right next to
    // it would fire a moment later and write nothing new, but it would also
    // leave the debounce armed for longer than the checkpoint that started
    // it actually explains, which is exactly what autosavePendingMs() exists
    // to let a test catch.
    if (myAutosaveTimer) myAutosaveTimer->stop();
    updateActions();   // the dirty star and the Save action both follow this
    if (announce) {
        const QString message = tr("Saved %1").arg(myFurnitureName);
        statusBar()->showMessage(message);
        // Not a document change - no Undo, and no document-revision stamp:
        // a save does not touch the undo stack (see DocumentModel.h), so
        // there is nothing for the pill to take back.
        myToasts->show(message, Toast::Kind::Note, false);
    }
    return true;
}

bool MainWindow::saveCurrentFurniture()
{
    return performSave(/*announce=*/true);
}

void MainWindow::flushAutosave()
{
    if (myShowingInitScreen || myFurnitureId.isEmpty()) return;
    if (!isFurnitureDirty()) return;   // nothing changed since the last write
    performSave(/*announce=*/false);
}

void MainWindow::setAutosaveEnabled(bool enabled)
{
    myAutosaveOn = enabled;
    if (myPersistProgress) {
        QSettings settings;
        settings.setValue(QStringLiteral("autosave"), enabled);
    }
    // Turning it ON while a dirty furniture is open should not leave that
    // furniture waiting for its NEXT checkpoint before the toggle's promise
    // takes effect - the document has already moved since the last save,
    // and that is exactly what "save after every change" means for the
    // change that already happened.
    if (enabled && isFurnitureDirty()) armAutosaveTimer();
    updateActions();
}

int MainWindow::autosavePendingMs() const
{
    return (myAutosaveTimer && myAutosaveTimer->isActive()) ? myAutosaveTimer->remainingTime()
                                                            : -1;
}

void MainWindow::armAutosaveTimer()
{
    if (!myAutosaveTimer) {
        myAutosaveTimer = new QTimer(this);
        myAutosaveTimer->setSingleShot(true);
        myAutosaveTimer->setInterval(kAutosaveWriteMs);
        connect(myAutosaveTimer, &QTimer::timeout, this, &MainWindow::flushAutosave);
    }
    // start() on a running single-shot timer RESTARTS it - the whole
    // debounce, exactly as persistAppearance()'s does.
    myAutosaveTimer->start();
}

void MainWindow::closeCurrentFurniture()
{
    if (myShowingInitScreen || myFurnitureId.isEmpty()) return;

    // Cancel any pending autosave debounce outright rather than flushing it
    // separately - the check-and-save below is the ONE authoritative save
    // this close performs, so a separate flush here would risk a second,
    // independent save attempt (and a second Failure toast) for the exact
    // same dirty state a moment later. Fix round 2's own ruling: "only the
    // close-time save's own result decides" - an EARLIER autosave attempt
    // (this timer firing on its own before the user ever clicked Close, or
    // this very flush under the old two-step design) must not be
    // double-reported; a single fresh decision, made right here, is what
    // that requires.
    if (myAutosaveTimer && myAutosaveTimer->isActive()) {
        myAutosaveTimer->stop();
    }

    const QString name = myFurnitureName;
    if (isFurnitureDirty()) {
        // One fresh save attempt, whether autosave is on or off, and
        // regardless of whether some earlier autosave attempt already
        // failed - isFurnitureDirty() is read fresh, not from a cached
        // "did the last autosave succeed" flag, so a stale failure and a
        // resolved one are not confused with each other.
        //
        // Fix round 2 (CLAUDE.md's never-silent-failure law): a FAILED save
        // here must ABORT the whole handoff, not merely fail to save.
        // performSave() has already raised its own Failure toast - but
        // showInitScreen() below is what hides this window a moment later
        // (see EditorSelectorHandoff.h), and a toast on a window that is
        // about to disappear is silent in practice, exactly the failure
        // this law forbids. Returning here instead leaves the editor open,
        // the toast readable, and the furniture open and dirty exactly as
        // it was - both the menu route and the native X (MainWindow::
        // closeEvent(), which already ignore()s the close event
        // unconditionally) get this for free, since both call this
        // function and neither does anything further once it returns.
        if (!performSave(/*announce=*/false)) return;
        statusBar()->showMessage(tr("Saved and closed %1").arg(name));
    } else {
        // Nothing to save (the ruling: never a modal question - and never
        // a toast either, per fix round 1's own MINOR ruling. The refreshed
        // selector's own card is the visible confirmation now).
        statusBar()->showMessage(tr("Closed %1").arg(name));
    }

    showInitScreen();
}

bool MainWindow::canOpenRenderMode() const
{
    // The four conditions this task's own ruling names: a furniture open, no
    // compare open, not sketching, no outline waiting. Unlike
    // canOpenSaveVersion() this does not also exclude the three gizmo
    // predicates - render mode is not a text field with an Enter/Escape
    // claim of its own, it is a toggle that HIDES those gizmos the instant
    // it turns on, so there is nothing for it to collide with.
    return !myShowingInitScreen && !isCompareOpen() && !mySketching && !hasPendingFace();
}

void MainWindow::setRenderModeEnabled(bool on)
{
    if (myRenderModeOn == on) return;
    myRenderModeOn = on;
    // The single source of truth for the menu entry's checked state, kept in
    // step in BOTH directions - the user unchecking the box arrives here
    // already in sync (QAction::toggled already changed it), but every OTHER
    // caller (checkpointDocument(), onStartSketch(), the viewport press,
    // openCompare()) flips this flag from code, and the box has to follow.
    // Blocked so that setChecked() cannot re-enter this function through
    // toggled().
    if (myRenderModeAction) {
        const QSignalBlocker blocker(myRenderModeAction);
        myRenderModeAction->setChecked(on);
    }

    myView->setRenderMode(on);

    // The one Note toast render mode raises on entry, naming the tier the
    // viewport just settled on - read AFTER setRenderMode(on) returns, since
    // the first activation this session is what actually runs the probe.
    // Copy uses none of the banned words (CLAUDE.md's vocabulary sweep reads
    // it for free through Toast::paintedTexts(), which records every message
    // actually shown this run - no separate static accessor needed, unlike
    // bevelRefusalText() and friends, because this toast is always reachable
    // from a real render-mode entry rather than gated behind a refusal that
    // might never fire).
    if (on) {
        QString text;
        switch (myView->renderModeTier()) {
            case OcctViewWidget::RenderTier::PathTracing: text = tr("Render mode — path tracing"); break;
            case OcctViewWidget::RenderTier::RayTracing:  text = tr("Render mode — ray tracing"); break;
            case OcctViewWidget::RenderTier::Shadows:     text = tr("Render mode — shadows"); break;
            case OcctViewWidget::RenderTier::Plain:       text = tr("Render mode"); break;
        }
        // Kind::Note, deliberately: this reports a successful, expected
        // outcome, not a refusal, so CLAUDE.md's taxonomy ("every Note is a
        // success report... every refusal is a Failure") puts it here rather
        // than on Failure's unconditional-even-with-notifications-off path.
        // The consequence, recorded rather than merely implied: with
        // View -> Show notifications off, entering render mode raises no
        // toast at all - the tier is still readable from
        // OcctViewWidget::renderModeTier() and Save Screenshot still exports
        // at the chosen tier's real look, so nothing is silently lost, only
        // unannounced.
        myToasts->show(text, Toast::Kind::Note, false);
    }

    // The single authority: rail, drawers, the axis gizmo card and the three
    // gizmo predicates all re-derive themselves off myRenderModeOn from the
    // appStateChanged this ends by emitting.
    updateActions();
}

bool MainWindow::canOpenSaveVersion() const
{
    // Every OTHER application-wide Enter/Escape claim this app can have
    // live at once, named explicitly rather than folded into one flag.
    // canTransformSelectedBody() deliberately does NOT appear here (fix
    // round 1, Important 2, from back when SaveVersionCard still existed):
    // the transform gizmo holds no application-wide key claim of its own -
    // it is a direct 3D drag with no text field and no Enter/Escape filter,
    // unlike the other three - so excluding it bought no disjointness, only
    // a false conflict. A single body selected raises the gizmo but claims
    // no keys, so a pending version-create card and the gizmo can coexist
    // on screen with no ambiguity about which one Enter or Escape belongs
    // to.
    //
    // "!myRenderModeOn": render mode is not one of the three OTHER
    // application-wide key claims named above, but the SAME mechanism that
    // cancels a pending create on one of those - VersionsPanel::refresh()'s
    // own auto-cancel, mirroring what SaveVersionCard::onAppStateChanged()
    // used to do before Milestone 4 retired that card - is exactly what a
    // live studio shot needs too. Folded in here rather than added as a
    // second check inside the panel itself, so updateActions()'s own
    // mySaveVersionAction->setEnabled(canOpenSaveVersion()) and the panel's
    // auto-cancel read the SAME one answer instead of two that could drift.
    //
    // The mirror-placement gesture (Milestone 4, Phase 3) joins the named
    // list rather than the render-mode-style fold-in just above: it IS a
    // genuine fourth application-wide Enter/Escape/X/Y/Z claim, unlike the
    // transform gizmo, so a pending version-create card and a live plane
    // placement really would fight over the same key.
    return !myShowingInitScreen && !myRenderModeOn && !mySketching && !hasPendingFace() &&
           !canPullSelectedFace() && !canBevelSelectedEdge() && !myView->mirrorPlacementActive();
}

void MainWindow::onSaveVersion()
{
    // File -> Save version... is the menu route to the SAME gesture the
    // versions drawer's own + button starts (VersionsPanel::beginNewVersion())
    // - one implementation, two entry points. Opening the drawer first (if
    // it is not already showing) is what makes triggering this from the
    // menu behave the same as clicking the button: the pending card has
    // somewhere visible to appear.
    if (myVersionsPanelAction) myVersionsPanelAction->setChecked(true);
    if (myVersionsPanel) myVersionsPanel->beginNewVersion();
}

bool MainWindow::saveVersion(const QString& name)
{
    if (myShowingInitScreen || myFurnitureId.isEmpty()) return false;

    // A version's own thumbnail, captured straight to a temp PNG via
    // OcctViewWidget::saveSnapshot() - the path FurnitureStore::saveVersion()
    // wants, rather than the QImage performSave() hands saveFurniture(). No
    // render-mode guard is needed the way performSave() has one: this method
    // can only run while canOpenSaveVersion() held, and that already refuses
    // while render mode is on (see MainWindow.h). A failed capture leaves
    // thumbPath empty, which saveVersion() below treats as "no thumbnail",
    // never a refusal - a thumbnail is presentation, never document data.
    QTemporaryFile thumbTemp(QDir::tempPath() + QStringLiteral("/furnifyme-version-thumb-XXXXXX.png"));
    QString thumbPath;
    if (thumbTemp.open()) {
        thumbPath = thumbTemp.fileName();
        thumbTemp.close();  // saveSnapshot() opens the path itself - see captureThumbnail()'s own comment
        if (!myView->saveSnapshot(thumbPath)) thumbPath.clear();
    }

    // The only refusal reachable here: a real, open furniture cannot be an
    // unknown id, so a false from the store means the name is a duplicate -
    // see FurnitureStore::saveVersion()'s own contract.
    if (!myStore.saveVersion(myFurnitureId, name, myDocument, thumbPath)) {
        myToasts->show(tr("Couldn't save version \"%1\" — a version by that name "
                          "already exists").arg(name),
                      Toast::Kind::Failure, false);
        return false;
    }

    updateActions();   // refreshes the drawer through VersionsPanel::refresh
    const QString message = tr("Version \"%1\" saved").arg(name);
    statusBar()->showMessage(message);
    // No Undo - versions are file data, not a document edit; there is
    // nothing on the undo stack for a pill to take back.
    myToasts->show(message, Toast::Kind::Note, false);
    return true;
}

bool MainWindow::restoreVersion(const QString& name)
{
    if (myShowingInitScreen || myFurnitureId.isEmpty()) return false;

    // Closed FIRST: a restore is about to replace the very document a
    // compare pane may still be showing half of, and a stale read-only pane
    // sitting beside a document that just moved on is confusing at best.
    if (myCompareView) closeCompare();

    DocumentModel loaded;
    if (!myStore.loadVersion(myFurnitureId, name, loaded)) {
        myToasts->show(tr("Couldn't restore \"%1\" — its file is missing or damaged")
                          .arg(name),
                      Toast::Kind::Failure, false);
        return false;
    }

    // ONE checkpoint around the whole replacement - DocumentModel::checkpoint()
    // then restoreFrom(), never fromSerialized() (which clears undo history
    // outright; see DocumentModel.h) - so a single Ctrl+Z brings back
    // everything this replaced, not just part of it.
    checkpointDocument();
    myDocument.restoreFrom(loaded);
    myView->clearSelection();
    mySelectedOutlineId = 0;
    resyncView();

    recordProgress("version.restored");
    updateActions();
    emit documentChanged();
    const QString message = tr("Restored version \"%1\"").arg(name);
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

bool MainWindow::deleteVersionByName(const QString& name)
{
    if (myShowingInitScreen || myFurnitureId.isEmpty()) return false;

    if (!myStore.deleteVersion(myFurnitureId, name)) {
        myToasts->show(tr("Couldn't delete \"%1\" — it may already be gone").arg(name),
                      Toast::Kind::Failure, false);
        return false;
    }

    // Comparing the version just removed would leave a stale pane reading a
    // file that no longer exists - close it first, same as a restore does.
    if (myCompareView && myCompareVersionName == name) closeCompare();

    updateActions();   // refreshes the drawer
    const QString message = tr("Deleted version \"%1\"").arg(name);
    statusBar()->showMessage(message);
    // No Undo - final. Versions are file data, and "Ctrl+Z brings back a
    // deleted file" is not a promise this app makes anywhere else either;
    // the two-click confirmation on the row itself is what stands in for it.
    myToasts->show(message, Toast::Kind::Note, false);
    return true;
}

QString MainWindow::compareBadgeCloseLabel()
{
    return tr("Close compare");
}

QString MainWindow::mirrorPlacementLabelText()
{
    return tr("Mirror plane");
}

QString MainWindow::mirrorPlacementHintText()
{
    // The verb-naming hint line CLAUDE.md's own rule requires for a
    // modeless gesture with keyboard-only controls - ExtrudePreview's own
    // lesson, carried here word for word: "a modeless panel with invisible
    // verbs went unnoticed for a whole branch".
    return tr("X Y Z aim — drag to move — Enter mirror — Esc cancel");
}

bool MainWindow::openCompare(const QString& name)
{
    if (myShowingInitScreen || myFurnitureId.isEmpty()) return false;

    // Render mode's own gate is "no compare open" (canOpenRenderMode()), and
    // this is the structural half of that: even with the versions drawer
    // hidden while render mode is on, the drawer's own row-click is not the
    // only way to reach this - App Bar/File menu routes stay reachable, so
    // opening a compare exits render mode first rather than refusing.
    if (myRenderModeOn) setRenderModeEnabled(false);

    DocumentModel loaded;
    if (!myStore.loadVersion(myFurnitureId, name, loaded)) {
        myToasts->show(tr("Couldn't compare \"%1\" — its file is missing or damaged")
                          .arg(name),
                      Toast::Kind::Failure, false);
        return false;
    }

    // Only one compare pane at a time - opening a different version replaces
    // it rather than stacking a second one.
    if (myCompareView) closeCompare();

    // A fresh generation for this compare session - see the badge's own
    // Close-button connect() below for what this guards against.
    ++myCompareGeneration;

    // The live view's PARENT never changes here - only the compare pane is
    // ever newly parented, so there is no risk to the live view's own OCCT
    // bridge from this call. mySplitter takes myView as its first pane
    // (reparenting it OUT of being the window's plain central widget, in)
    // and myCompareView, freshly constructed straight into the splitter, as
    // its second - see closeCompare() for the reverse move.
    mySplitter = new QSplitter(Qt::Horizontal);
    mySplitter->addWidget(myView);
    myCompareView = new OcctViewWidget(mySplitter, /*viewerOnly=*/true);
    mySplitter->addWidget(myCompareView);
    setCentralWidget(mySplitter);

    for (const DocumentModel::Solid& solid : loaded.solids()) {
        myCompareView->displaySolid(solid.id, solid.shape);
        myCompareView->setSolidVisible(solid.id, loaded.isVisible(solid.id));
    }
    for (const DocumentModel::Outline& outline : loaded.outlines()) {
        myCompareView->displayOutline(outline.id, outline.face);
        myCompareView->setOutlineVisible(outline.id, loaded.isVisible(outline.id));
    }

    // Seeded from the live view's own current pose rather than fitAll()'d
    // fresh, so the two start in lockstep - the first camera-sync round
    // trip that orbiting either one triggers is already at equilibrium.
    myCompareView->setCameraStateNow(myView->camera().state());
    // The compare pane's own half of the sync - see syncCamera(). Torn down
    // automatically with myCompareView on closeCompare().
    connect(myCompareView, &OcctViewWidget::cameraChanged, this, [this] {
        if (myCompareView) syncCamera(myCompareView, myView);
    });

    myCompareVersionName = name;
    auto* badge = new CompareBadge(myCompareView);
    // setVersionName() ends in growAndReposition() - both device-pixel
    // rules (Theme::wholeDevicePixels() for the size, Theme::
    // snapToDevicePixels() for the position) apply themselves; nothing
    // further to place by hand here.
    badge->setVersionName(name);
    badge->show();
    badge->raise();
    // Deferred by one event-loop turn, deliberately - see closeCompare()'s
    // own comment on why it deletes synchronously. This button is a
    // descendant of everything that delete destroys (button -> badge ->
    // myCompareView -> mySplitter), so calling closeCompare() straight from
    // this click would destroy the very widget whose signal is still on the
    // call stack. QTimer::singleShot(0, ...) runs it on the next turn
    // instead, by which point this click has finished being handled and
    // nothing is executing inside the object about to be deleted.
    //
    // The generation captured here (fix round 1, Minor 6) is what stops a
    // STALE deferred close from acting on the WRONG compare session: a
    // click, then - inside that single deferred turn - Restore or a second
    // Compare click replacing this pane with a different version before the
    // timer fires. Without it the deferred call would still run
    // closeCompare() unconditionally and close whatever compare happens to
    // be open BY THEN, silently discarding a session the user never asked
    // to end. myCompareGeneration is bumped once per openCompare() call
    // (below), so a mismatch here means "the compare this button belonged
    // to is already gone or already replaced" and the deferred call becomes
    // a no-op rather than acting on the wrong pane.
    const int generation = myCompareGeneration;
    connect(badge->closeButton(), &QPushButton::clicked, this, [this, generation] {
        QTimer::singleShot(0, this, [this, generation] {
            if (myCompareGeneration == generation) closeCompare();
        });
    });
    myCompareBadge = badge;

    updateActions();
    statusBar()->showMessage(tr("Comparing %1").arg(name));
    return true;
}

void MainWindow::closeCompare()
{
    if (!myCompareView) return;

    // The splitter's own size IS the correct target for whatever replaces
    // it as central widget - QMainWindowLayout already computed and applied
    // it when mySplitter itself became central, back in openCompare().
    // Captured before anything below touches mySplitter.
    const QSize centralSize = mySplitter ? mySplitter->size() : QSize();

    // Pulls the live view back OUT of the splitter and back to being the
    // window's plain central widget - the same reparenting openCompare()
    // did in reverse, and the one QMainWindow::setCentralWidget() already
    // knows how to perform on a widget it does not currently own.
    setCentralWidget(myView);
    // setCentralWidget() alone leaves myView at whatever geometry it held as
    // ONE PANE of the splitter (roughly half the window, since QSplitter
    // gives its FIRST widget's own sizeHint priority in the absence of an
    // explicit setSizes() call) until something else forces
    // QMainWindowLayout to lay out again - and empirically, neither
    // layout()->invalidate() nor layout()->activate() is that something for
    // a QMainWindow's own specialised layout in this situation. Found by a
    // real composited capture (fix round 1, Important 1/Minor 3), not
    // assumed: centralWidget()==view was true and the view still rendered
    // and picked CORRECTLY within its own (wrong, roughly-600-of-1000-px)
    // rect, so neither of those checks caught it - only a PrintWindow
    // capture showed the other ~40% of the window still painting the
    // compare pane's stale pixels, and a direct geometry check confirmed it
    // (view->width() == 601 in a 1000px-wide probe).
    //
    // myView is resized explicitly to the size we KNOW is right, because it
    // is exactly the size the widget it is replacing just had. That alone
    // still was not the whole fix, though - see
    // OcctViewWidget::resizeEvent() for the other half: the resize() call
    // below updates Qt's own widget-level bookkeeping (which this
    // triggered correctly, and which is what V3d_View::Dump() reads), but
    // the underlying native HWND's ACTUAL client rect turned out not to
    // follow it here, confirmed with GetClientRect - a defect resizeEvent()
    // now corrects on every resize, not just this one call site.
    if (centralSize.isValid()) myView->resize(centralSize);

    // A SYNCHRONOUS delete, not deleteLater(). QMainWindow keeps the
    // REPLACED central widget referenced in its own internal layout state
    // (QMainWindowLayout's own bookkeeping for the widget it just stopped
    // showing) even once it is no longer parented as the current central
    // widget - and a QObject::deleteLater() event posted for an object that
    // internal state still holds onto is never actually delivered by
    // QCoreApplication::sendPostedEvents(), however many times or how long a
    // caller pumps the event loop afterward. Measured, not theorised: a
    // QPointer watching mySplitter stayed non-null through a full 500ms of
    // repeated processEvents() calls. An immediate delete has no such
    // dependency - the object is simply gone, right here.
    //
    // The one call site this makes genuinely risky is the compare badge's
    // own Close button, whose click would otherwise be destroying an
    // ancestor of itself (this splitter owns myCompareView owns the badge
    // owns that very button) while still on that button's own call stack -
    // see the badge's own connect() in openCompare() for how that specific
    // route defers through QTimer::singleShot(0, ...) instead of calling
    // this directly, so by the time this function's delete actually runs,
    // nothing is still executing inside the object being destroyed. Every
    // OTHER caller here (VersionsPanel's Compare/Restore buttons by way of
    // restoreVersion()/openCompare(), and a direct call from a test) is not
    // itself a descendant of what this deletes, so no such deferral is
    // needed for them.
    //
    // A SECOND reentrancy shape was considered and is safe without any
    // deferral of its own: a window-level shortcut (Ctrl+W for Close
    // furniture, say) firing while focus happens to sit on a widget this
    // call is about to delete - the badge's Close button, if it was ever
    // Tab-focused rather than clicked. That dispatch runs through
    // QApplication::notify(), which Qt guards internally with QPointer
    // around the focus widget across the handler call specifically so a
    // slot invoked by a shortcut can delete the widget the shortcut was
    // dispatched through without notify() touching a dangling pointer
    // afterward - unlike the click case above, where the reentrancy is
    // THIS class's own signal/slot wiring and nothing upstream is guarding
    // it for us.
    myCompareBadge = nullptr;   // a child of myCompareView - goes with it below
    delete myCompareView;
    myCompareView = nullptr;
    myCompareVersionName.clear();
    delete mySplitter;
    mySplitter = nullptr;

    updateActions();
}

void MainWindow::syncCamera(OcctViewWidget* from, OcctViewWidget* to)
{
    if (!from || !to) return;
    // The no-recursion guard: see the declaration for the whole argument.
    // Skipping the copy when the two already agree is what stops this from
    // being an infinite ping-pong rather than merely a fast-converging one -
    // setCameraStateNow() unconditionally emits cameraChanged() again, and
    // THIS check is what the other direction's own call finds already
    // satisfied.
    if (camerasApproximatelyEqual(from->camera().state(), to->camera().state())) return;
    to->setCameraStateNow(from->camera().state());
}

void MainWindow::updateWindowTitle()
{
    if (myShowingInitScreen || myFurnitureId.isEmpty()) {
        setWindowTitle(tr("FurnifyMe"));
        return;
    }
    setWindowTitle(QStringLiteral("%1%2 — FurnifyMe")
                       .arg(myFurnitureName, isFurnitureDirty() ? QStringLiteral(" *")
                                                                : QString()));
}

void MainWindow::recordProgress(const std::string& event)
{
    myProgress.record(event);
    if (!myPersistProgress) return;

    QSettings settings;
    settings.setValue(QStringLiteral("progress"),
                      QString::fromStdString(myProgress.serialize()));
}

QString MainWindow::lockTooltipText() const
{
    // The gesture gained its Ctrl this phase, and the sentence has to say so:
    // a plain double-click on a body now selects the whole body instead. A
    // tooltip that still taught the old gesture would be teaching something
    // that quietly does a different thing.
    return tr("Draw on the selected face instead of the ground (L)\n"
              "Ctrl+double-clicking a face does the same. Outlines drawn "
              "there extrude square to it.");
}

QString MainWindow::unlockTooltipText() const
{
    return tr("Go back to drawing on the ground (Shift+L)");
}

QString MainWindow::duplicateLinkedTooltipText() const
{
    return tr("Copy this body and keep both in step (Ctrl+D)\n"
              "Editing either one carries the change to every copy.");
}

QString MainWindow::linkSelectedTooltipText() const
{
    return tr("Snap the other selected bodies onto the first, and keep them in step\n"
              "Editing any one of them carries the change to the rest.");
}

QString MainWindow::unlinkBodyTooltipText() const
{
    return tr("Stop this body following its linked copies\n"
              "Its own shape is untouched - only the connection ends.");
}

QString MainWindow::snapTooltipText() const
{
    return tr("Snap outline points to the %1 grid\n"
              "Turn this off for freehand placement.")
        .arg(QString::fromStdString(Measure::formatLength(10.0)));
}

void MainWindow::updateStateLabel()
{
    if (!myStateLabel) return;

    QString state;
    if (mySketching) {
        const int placed = static_cast<int>(mySketch.pointCount());
        if (mySketch.canClose()) {
            state = tr("Sketching — %1 points — Enter or click the first point to close")
                        .arg(placed);
        } else if (placed == 0) {
            state = tr("Sketching — click to place your first point");
        } else if (placed == 1) {
            state = tr("Sketching — 1 point, 2 more to close");
        } else {
            state = tr("Sketching — 2 points, 1 more to close");
        }
    } else if (hasPendingFace()) {
        // NAMED, not just "Face ready". Outlines accumulate now, and with two
        // in the drawer a label that says only that leaves the user with no
        // way to tell which one E is aimed at - the drawer's highlight and
        // this name are the two halves of that answer, and they read the same
        // pendingOutlineId() so they cannot point at different outlines.
        state = tr("%1 ready — press E to extrude")
                    .arg(QString::fromStdString(
                        myDocument.outlineNameOf(pendingOutlineId())));
    } else if (canPullSelectedFace()) {
        // The gizmo is on screen and it is not obvious what to do with it -
        // an arrow with no words is a guess. Reads the same predicate the
        // arrow itself does, so the label cannot describe a gizmo that is not
        // there (or stay quiet about one that is).
        state = tr("Face selected — drag the arrow to pull, or type a distance");
    } else if (canBevelSelectedEdge()) {
        // Same rule as the line above: the gizmo is on screen, one axis does
        // two different things, and an arrow cannot say that by itself. Reads
        // the same predicate the arrow does.
        // The table's words, the same two the chip, the tooltips and the
        // refusals use. Saying "round or flatten" here and "Fillet"/"Chamfer"
        // everywhere else is two vocabularies for one pair of operations.
        //
        // The plural is written out, not parenthesised - the label has to be
        // able to say that a Shift-click added a second edge, and "Edge(s)"
        // is the exact spelling the vocabulary rules forbid.
        const std::size_t picked = myView->selectedEdges().size();
        state = (picked > 1 ? tr("%1 edges selected — drag in for a Fillet, out for a "
                                 "Chamfer, or type a size")
                                  .arg(QString::number(static_cast<int>(picked)))
                            : tr("Edge selected — drag in for a Fillet, out for a "
                                 "Chamfer, or type a size"));
    } else {
        const std::size_t selected = myView->selectedSolidIds().size();
        const std::size_t bodies = myDocument.count();
        if (selected == 2) {
            state = tr("2 bodies selected — Union, Subtract and Intersect available");
        } else if (selected == 1) {
            // The transform gizmo is on screen whenever this holds, and a
            // handful of arrows and rings with no words is a guess. Reads the
            // same predicate the gizmo itself does, so the label cannot
            // describe a gizmo that is not there - or stay quiet about one
            // that is.
            state = canTransformSelectedBody()
                        ? tr("1 body selected — drag a handle to Move, Rotate or Scale — "
                             "Shift-click another to combine them")
                        : tr("1 body selected — Shift-click another to combine them");
        } else if (bodies == 0) {
            state = tr("Nothing yet — press Ctrl+K to draw an outline");
        } else if (bodies == 1) {
            state = tr("1 body — click it to select");
        } else {
            state = tr("%1 bodies — click one to select").arg(bodies);
        }
    }
    // A lock is a mode, and a mode with no persistent cue is a trap: the
    // message that announced it is transient, and the grid's orientation is
    // easy to misread once the camera has moved. It leads the label, because
    // where the next outline will land governs how to read everything after
    // it.
    if (myFaceLocked) {
        state = tr("On a locked face — %1").arg(state);
    } else {
        // The face-on-ortho cue, this fix round's own addition - the
        // locked-face cue's exact shape, on the plane that governs where
        // the NEXT outline lands (or the one already in progress, if any).
        // The plane itself is read off whatever actually decided it, never
        // re-derived from the live camera once something is pinned:
        //   - mid-sketch, SketchController's own plane (set once at
        //     onStartSketch() and never touched again - see there);
        //   - a pending outline, ITS OWN stored plane, via the same
        //     pendingSweepDirection() extrudePendingFace() already reads,
        //     because mySketch's own copy only reflects the LAST sketch
        //     drawn and a different outline may be the one selected;
        //   - otherwise (idle), the LIVE camera's own faceOnOrthoPlane() -
        //     nothing is pinned yet, so this is free to track the camera
        //     exactly as the grid does.
        // An orbit away from Front mid-sketch therefore leaves this cue
        // showing "Facing Front" although the camera itself has moved on -
        // correct, not stale, since the outline really is still on that
        // plane.
        gp_Dir normal(0.0, 0.0, 1.0);
        if (mySketching) normal = mySketch.plane().Axis().Direction();
        else if (hasPendingFace()) normal = pendingSweepDirection();
        else normal = myView->faceOnOrthoPlane().Axis().Direction();

        const QString direction = faceOnDirectionLabel(normal);
        if (!direction.isEmpty()) state = tr("Facing %1 — %2").arg(direction, state);
    }
    // Symmetry LEADS - CLAUDE.md's own words for this label - because
    // whether the next body gets a mirrored twin governs how to read
    // everything after it, the same argument the face lock makes one layer
    // in.
    if (myDocument.symmetryOn()) state = tr("Mirror on — %1").arg(state);

    myStateLabel->setText(state);
}

QString MainWindow::faceOnDirectionLabel(const gp_Dir& normal) const
{
    // THE one name for a face-on world plane, read by updateStateLabel()'s
    // persistent cue and by onStartSketch()'s opening sentence alike (M3).
    // Two names for one plane is a bug even when both are defensible: the
    // world XZ plane is "Front" and the world YZ plane is "Right", whichever
    // side of either the camera happens to be on.
    if (std::fabs(std::fabs(normal.Y()) - 1.0) < 1.0e-6) return tr("Front");
    if (std::fabs(std::fabs(normal.X()) - 1.0) < 1.0e-6) return tr("Right");
    return QString();
}

void MainWindow::resyncView()
{
    myView->clearSolids();
    for (const DocumentModel::Solid& solid : myDocument.solids()) {
        myView->displaySolid(solid.id, solid.shape);
    }
    // Outlines are document items, so undo and redo have to move them on
    // screen exactly as they move bodies. Rebuilt wholesale for the same
    // reason the bodies are: tracking the difference is more code than it
    // saves, and this is the only way to be sure the two agree.
    myView->clearOutlines();
    for (const DocumentModel::Outline& outline : myDocument.outlines()) {
        myView->displayOutline(outline.id, outline.face);
    }

    // The visibility reconciliation: DocumentModel owns isVisible() (Task 1,
    // written through by ItemsPanel's eye button - see ItemsPanel.cpp), and
    // the view is a mirror of it. displaySolid()/displayOutline() above
    // always show what they just built, so the persisted state is reapplied
    // on top HERE, in the one place every caller of this function goes
    // through - not just the caller (openFurniture()) that happened to be
    // written first. Undo, redo, and every other resync (six call sites)
    // rebuild the presentation wholesale exactly as a fresh open does, so a
    // hidden body must not silently reappear on any of them.
    for (const DocumentModel::Solid& solid : myDocument.solids())
        myView->setSolidVisible(solid.id, myDocument.isVisible(solid.id));
    for (const DocumentModel::Outline& outline : myDocument.outlines())
        myView->setOutlineVisible(outline.id, myDocument.isVisible(outline.id));

    // Same reconciliation, for the same reason: symmetryOn()/symmetryPlane()
    // can change from underneath the view through undo, redo, opening a
    // different furniture or restoring a version, none of which go through
    // setSymmetryEnabled()/setSymmetryPlaneFromFace() - this is the one place
    // every one of those already rebuilds the viewport wholesale.
    myView->setSymmetryIndicator(myDocument.symmetryOn(), myDocument.symmetryPlane());
}

void MainWindow::onDeleteSelected()
{
    // BODIES when bodies are selected; the WAITING OUTLINE when nothing is.
    //
    // Outlines are not pickable viewport geometry, so no gesture can put one
    // in this selection - which is exactly why they had no Delete route at
    // all, and why they needed one. Every direct-modeling gate (the pull
    // arrow, the bevel arrow, the transform gizmo, Lock to Face) refuses
    // while an outline waits, and the operations that are NOT gated -
    // booleans, Delete - push onto the undo stack. Close an outline, Union
    // two bodies, and the advice those refusals used to give, "Ctrl+Z to take
    // it back", undoes the Union instead: every gate shut and no way to open
    // one. Extrude was the only exit, and "make a body you do not want" is
    // not an exit.
    //
    // Nothing-selected is the one state in which Delete had no work of its
    // own, so the second meaning displaces nothing. updateActions() decides
    // which one is live and says so in the tooltip; this asks the same
    // question the same way rather than keeping a second copy of the rule.
    const std::vector<int> ids = myView->selectedSolidIds();
    if (ids.empty()) {
        deletePendingOutline();
        return;
    }

    // Symmetry: deleting either half of a pair takes both, in the SAME
    // checkpoint - a twin left standing with nothing to mirror is a symmetry
    // the document no longer describes. Expanded BEFORE anything is removed,
    // so the message and the single undo agree with what actually happened.
    //
    // Gated on symmetryOn(), not just twinOf() != -1: DocumentModel's pairing
    // map is undo-tracked while the on/off mode is not (fix round 1 - a mode
    // switch stays outside undo State, the same rule visibility follows), so
    // an undo landing after symmetry was turned off can resurrect an OLD
    // pairing entry while the mode itself stays off. A pairing only ACTS
    // while symmetry is on; the guard is what keeps that true everywhere,
    // not just at the edit-propagation hook.
    std::vector<int> toDelete = ids;
    if (myDocument.symmetryOn()) {
        for (int id : ids) {
            const int twin = myDocument.twinOf(id);
            if (twin > 0 && std::find(toDelete.begin(), toDelete.end(), twin) == toDelete.end())
                toDelete.push_back(twin);
        }
    }
    const bool isTwinPair = myDocument.symmetryOn() && toDelete.size() == 2 &&
                            myDocument.twinOf(toDelete[0]) == toDelete[1];

    const std::string deletedName = toDelete.size() == 1 ? myDocument.nameOf(toDelete.front())
                                                          : std::string();
    const std::string twinNameA = isTwinPair ? myDocument.nameOf(toDelete[0]) : std::string();
    const std::string twinNameB = isTwinPair ? myDocument.nameOf(toDelete[1]) : std::string();

    checkpointDocument();
    myView->clearSelection();
    for (int id : toDelete) {
        myDocument.removeSolid(id);
        myView->removeSolid(id);
    }
    recordProgress("delete.used");

    updateActions();
    emit documentChanged();
    const QString message =
        toDelete.size() == 1
            ? tr("Deleted %1").arg(QString::fromStdString(deletedName))
        : isTwinPair
            ? tr("Deleted %1 and %2").arg(QString::fromStdString(twinNameA),
                                          QString::fromStdString(twinNameB))
            : tr("Deleted %1 bodies").arg(toDelete.size());
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
}

bool MainWindow::deletePendingOutline()
{
    const int id = pendingOutlineId();
    if (id == 0) return false;

    // Read BEFORE the removal: once the outline is out of the document its
    // name cannot be looked up, and a toast that named the wrong thing - or
    // nothing - would be worse than no toast.
    const QString name = QString::fromStdString(myDocument.outlineNameOf(id));

    // One checkpoint, like every other change to the document, so one Ctrl+Z
    // puts it back. The toast that reports it carries Undo for the same
    // reason - CLAUDE.md's rule is that a change the user can see is a change
    // they can take back from where it is reported.
    checkpointDocument();
    myDocument.removeOutline(id);
    myView->removeOutline(id);
    // The drawer's choice went with it. Not strictly required -
    // pendingOutlineId() validates its id against the live list on every read
    // - but leaving a dead id behind means the NEXT outline could inherit the
    // pending mark from an id that no longer exists if the counter ever
    // reused one.
    if (mySelectedOutlineId == id) mySelectedOutlineId = 0;
    recordProgress("delete.used");

    updateActions();
    emit documentChanged();
    // The same sentence shape the body half uses - "Deleted Body 02" and
    // "Deleted Outline 01" are one message with one subject, not two messages
    // the user has to learn separately.
    const QString message = tr("Deleted %1").arg(name);
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

void MainWindow::onRenameSelected()
{
    // The same target updateActions() just decided myRenameAction's enabled
    // state from - asked the same way, rather than kept as a second copy of
    // the rule (onDeleteSelected()'s own comment makes the identical
    // argument for Delete's two targets).
    const std::vector<int> ids = myView->selectedSolidIds();
    if (ids.size() == 1) {
        myItemsPanel->beginRenameForItem(ids.front(), /*isOutline=*/false);
        return;
    }
    if (ids.empty() && hasPendingFace())
        myItemsPanel->beginRenameForItem(pendingOutlineId(), /*isOutline=*/true);
}

void MainWindow::onItemRenameCommitted(int id, bool isOutline, QString newName)
{
    // InlineRename already trimmed the text and refused an empty/whitespace
    // commit silently - see its header - so this is only ever reached with a
    // real, non-empty name. The id is checked live BEFORE checkpointing,
    // rather than after: this app is single-threaded and the drawer's row
    // does not rebuild between opening the edit and committing it (refresh()'s
    // signature comment explains why), so nothing can invalidate `id` between
    // this check and setItemName() below - but checking first means a
    // checkpoint is never pushed for a mutation that was always going to
    // refuse, which a check-after-the-fact could not promise.
    if (!(isOutline ? myDocument.containsOutline(id) : myDocument.contains(id))) return;

    checkpointDocument();
    myDocument.setItemName(id, newName.trimmed().toStdString());
    recordProgress("rename.used");

    updateActions();
    emit documentChanged();
    const QString message = tr("Renamed to \"%1\"").arg(newName.trimmed());
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
}

void MainWindow::onUndo()
{
    // A document-changing gesture in every sense that matters here, even
    // though it does not run through checkpointDocument() (undo does not
    // take a NEW checkpoint) - so render mode's own exit rule is enforced
    // explicitly rather than piggy-backing on that choke point.
    if (myRenderModeOn) setRenderModeEnabled(false);

    // Mid-sketch, Undo means the last POINT. One implementation with two
    // triggers, not a second remove-last-point path: Backspace and Ctrl+Z
    // both land in onUndoSketchPoint(), so the two can never drift.
    //
    // Rerouting rather than adding a branch to updateActions() alone: the
    // enabled state (mySketching ? points > 0 : canUndo()) and the behaviour
    // have to agree, and updateActions() stays the single place that decides
    // availability. Everything that goes through myUndoAction follows for
    // free - the menu entry, the rail chip, Ctrl+Z, and the toast's Undo
    // pill, which triggers the action rather than calling this.
    if (mySketching) {
        onUndoSketchPoint();
        return;
    }

    const std::vector<int> outlinesBefore = outlineIds();
    if (!myDocument.undo()) return;
    recordProgress("undo.used");

    // An outline the undo handed back is the thing the user just took back,
    // so it becomes the one Extrude will consume - see adoptRestoredOutline().
    adoptRestoredOutline(outlinesBefore);
    myView->clearSelection();
    resyncView();
    updateActions();
    emit documentChanged();
    statusBar()->showMessage(myDocument.count() == 1
                                 ? tr("Undone — 1 body in the document")
                                 : tr("Undone — %1 bodies in the document").arg(myDocument.count()));
}

void MainWindow::onRedo()
{
    // See onUndo()'s own comment - the same reasoning applies here.
    if (myRenderModeOn) setRenderModeEnabled(false);

    const std::vector<int> outlinesBefore = outlineIds();
    if (!myDocument.redo()) return;
    recordProgress("undo.used");

    // The same rule the other way: a redo that brings an outline back is the
    // user putting it there, so it is the one they mean.
    adoptRestoredOutline(outlinesBefore);
    myView->clearSelection();
    resyncView();
    updateActions();
    emit documentChanged();
    statusBar()->showMessage(myDocument.count() == 1
                                 ? tr("Redone — 1 body in the document")
                                 : tr("Redone — %1 bodies in the document").arg(myDocument.count()));
}

void MainWindow::onSnapToggled(bool enabled)
{
    myView->setSnap(enabled, 10.0);
    statusBar()->showMessage(
        enabled ? tr("Snapping to the %1 grid")
                      .arg(QString::fromStdString(Measure::formatLength(10.0)))
                : tr("Snapping off — points land exactly where you click"));
}

void MainWindow::onSketchCursorMoved(const gp_Pnt& point)
{
    if (!mySketching) return;

    myView->setPreview(mySketch.previewShapeWithCursor(point));
    // The live snapped cursor dot - see setSketchCursorMarker()'s comment
    // on why Snap to Grid is exactly when this matters most.
    myView->setSketchCursorMarker(point);
    // The plane's OWN coordinates, not the world's. On a face locked at
    // y = 220 the world Y never changes as the cursor runs up the face, so a
    // world X/Y readout froze one number and made the other meaningless in
    // the plane the user is actually drawing in. ElSLib::Parameters is the
    // same conversion SketchController::snapToPlaneGrid uses, so the readout
    // and the snap grid agree by construction rather than by coincidence -
    // and on the ground plane (u, v) is (X, Y), so nothing changes there.
    Standard_Real u = 0.0, v = 0.0;
    ElSLib::Parameters(mySketch.plane(), point, u, v);
    statusBar()->showMessage(tr("Cursor at %1, %2")
                                 .arg(QString::fromStdString(Measure::formatLength(u)),
                                      QString::fromStdString(Measure::formatLength(v))));

    // The live length of the segment being dragged out - the last placed
    // point to the cursor. Only one call site touches this in
    // OcctViewWidget's own hover branch too (a selected edge); this is the
    // other of the two, per DimensionRenderer's contract.
    if (mySketch.points().empty()) {
        myView->dimension().clear();
        return;
    }
    const gp_Pnt& last = mySketch.points().back();
    const gp_Vec segment(last, point);
    if (segment.Magnitude() < 1.0e-4) {
        myView->dimension().clear();
        return;
    }
    // Sideways within the sketch plane - perpendicular to both the segment
    // and the plane's own normal - so the extension lines lie flat on the
    // plane the user is actually drawing on.
    gp_Vec sideways = gp_Vec(mySketch.plane().Axis().Direction()).Crossed(segment);
    if (sideways.Magnitude() < 1.0e-7) sideways = gp_Vec(1.0, 0.0, 0.0);
    myView->dimension().show(last, point, gp_Dir(sideways), myView->worldPerPixel());
}

void MainWindow::onStartSketch()
{
    // Render mode's own exit gesture, named explicitly in CLAUDE.md's
    // contract ("Start Sketch... leaves render mode first"). Ctrl+K and the
    // Sketch menu entry both stay reachable while render mode hides the
    // rail, so this is not merely defensive - it is a real route in.
    if (myRenderModeOn) setRenderModeEnabled(false);

    // A live mirror-placement gesture (Milestone 4, Phase 3) also has to end
    // here, for the same disjointness reason: canBeginMirrorPlacement()
    // already refuses to BEGIN one while mySketching is true, but nothing
    // stopped Ctrl+K from starting a sketch OVER an already-active one, and
    // sketching binds its own Enter/Escape (Finish/Cancel Sketch's own
    // shortcuts) that would then compete with the chip's application-wide
    // filter for the same two keys - two claims live at once, which this
    // task's disjointness rule forbids by construction, not by luck.
    if (myView->mirrorPlacementActive()) myView->cancelMirrorPlacement();

    mySketch.reset();
    // A waiting outline is NOT discarded here any more. It used to be, when
    // it was a bare member and starting a sketch was the only way to be rid
    // of it; it is a document item now, listed in the drawer and owned by the
    // undo stack, and deleting one as a side effect of picking up the pencil
    // would be the app throwing away work the user never asked it to. They
    // accumulate; Extrude consumes the selected one and Ctrl+Z removes it.
    mySketching = true;

    // The ground plane by default, a locked face's own plane while one is
    // locked, or - this fix round - the vertical world plane a face-on
    // ORTHOGRAPHIC look (Front/Back/Left/Right) is squared onto: the same
    // priority gridPlane() already draws the grid on, now actually landing
    // clicks there too rather than only decorating the view. Derived once,
    // here, through OcctViewWidget::faceOnOrthoPlane() - gridPlane()'s own
    // unlocked half, so there is one construction of this plane, not a
    // second that could drift from the grid's - and handed to
    // SketchController BY VALUE, the same topological-naming law Lock to
    // Face's plane already follows (see lockToFace()'s own comment): from
    // this line on, SketchController holds its own copy, so orbiting away
    // or leaving ortho mid-sketch cannot re-aim an outline already in
    // progress. Skipped entirely while locked - the locked face already
    // outranks this, and re-deriving here would overwrite lockToFace()'s
    // own plane with the ground or the substitution.
    // The PLANE's own name, through faceOnDirectionLabel() - the same one
    // source updateStateLabel() reads, so the sentence that starts a sketch
    // and the persistent cue that follows it can never name the same plane
    // two different things. faceOnOrthoDirection() answers a different
    // question (which way the CAMERA is looking) and can say "Left" or
    // "Back" where the label says "Right" and "Front": a sketch started
    // from a Left view used to read "Click points on the Left plane" and
    // then persistently "Facing Right — …" about the identical world YZ
    // plane. The label cannot recover Left from Back once the plane is
    // pinned by value and the camera has moved on, so the plane's name is
    // the one that survives, and it is the one both now use. Empty when the
    // current look is not face-on at all, exactly as before -
    // faceOnOrthoPlane() returns the ground plane then, whose normal
    // faceOnDirectionLabel() has no name for.
    const QString faceOnDirection =
        faceOnDirectionLabel(myView->faceOnOrthoPlane().Axis().Direction());
    if (!myFaceLocked) mySketch.setPlane(myView->faceOnOrthoPlane());
    myView->setSketchMode(true, mySketch.plane());
    myView->setPreview(TopoDS_Shape());
    updateActions();
    statusBar()->showMessage(
        myFaceLocked
            ? tr("Click points on the locked face to draw an outline — "
                 "Enter closes it, Backspace undoes a point, Esc cancels")
            : faceOnDirection.isEmpty()
                  ? tr("Click points on the ground to draw an outline — "
                       "Enter closes it, Backspace undoes a point, Esc cancels")
                  : tr("Click points on the %1 plane to draw an outline — "
                       "Enter closes it, Backspace undoes a point, Esc cancels")
                        .arg(faceOnDirection));
}

void MainWindow::onSketchPointPicked(const gp_Pnt& point)
{
    // Clicking the first point again closes the sketch, the way every CAD
    // tool behaves. The radius comes from the viewport rather than being
    // recomputed here: Shift's straight constraint has to stand down inside
    // exactly this distance (see OcctViewWidget::sketchCloseTolerance), and
    // two copies of the formula would be two answers to the same question.
    const double closeTolerance = myView->sketchCloseTolerance();
    if (mySketch.isNearFirstPoint(point, closeTolerance)) {
        onFinishSketch();
        return;
    }

    mySketch.addPoint(point);
    myView->setPreview(mySketch.previewShape());
    myView->setSketchPointMarkers(mySketch.points());
    syncSketchConstraints();
    updateActions();
    statusBar()->showMessage(
        mySketch.pointCount() == 1
            ? tr("1 point placed")
            : tr("%1 points placed").arg(mySketch.pointCount()));
}

void MainWindow::onUndoSketchPoint()
{
    if (mySketch.pointCount() == 0) return;

    // The SAME counter the document path records, and recorded here rather
    // than in onUndo()'s reroute so Backspace earns it too. "Undo" is one
    // thing the user learns, not two: somebody who has taken back three
    // points by whichever key has learned to take things back, and a hint
    // still teaching them that would be teaching a lesson already taken.
    recordProgress("undo.used");
    mySketch.removeLastPoint();
    myView->setPreview(mySketch.previewShape());
    myView->setSketchPointMarkers(mySketch.points());
    syncSketchConstraints();
    updateActions();
}

void MainWindow::syncSketchConstraints()
{
    // The anchor is just the sketch's own last point - the 8-direction
    // compass dial itself is computed live, from wherever the cursor
    // currently sits, inside OcctViewWidget::pointOnSketchPlane(). No point
    // placed yet means nothing for Shift to anchor to.
    if (mySketching && !mySketch.points().empty())
        myView->setSketchStraightAnchor(mySketch.points().back());
    else
        myView->clearSketchStraightAnchor();

    // Only while clicking the first point would actually close the outline -
    // the same canClose() rule isNearFirstPoint() carries, read from the same
    // sketch, so the exemption cannot outlive the thing it exempts.
    if (mySketching && mySketch.canClose())
        myView->setSketchCloseTarget(mySketch.points().front());
    else
        myView->clearSketchCloseTarget();
}

void MainWindow::onCancelSketch()
{
    mySketching = false;
    mySketch.reset();
    // Cancels THIS sketch, not the outline items already in the document -
    // same reasoning as onStartSketch().
    myView->setSketchMode(false, mySketch.plane());
    myView->clearPreview();
    syncSketchConstraints();
    updateActions();
    statusBar()->showMessage(tr("Sketch cancelled"));
}

int MainWindow::pendingOutlineId() const
{
    const std::vector<DocumentModel::Outline>& outlines = myDocument.outlines();
    if (outlines.empty()) return 0;
    // Checked against the live list rather than trusted: an undo can remove
    // the outline the drawer last selected, and a stale id must fall back to
    // the newest instead of leaving Extrude pointing at nothing.
    if (mySelectedOutlineId != 0 && myDocument.containsOutline(mySelectedOutlineId))
        return mySelectedOutlineId;
    return outlines.back().id;
}

TopoDS_Face MainWindow::pendingFace() const
{
    return myDocument.outlineFace(pendingOutlineId());
}

std::vector<int> MainWindow::outlineIds() const
{
    std::vector<int> ids;
    ids.reserve(myDocument.outlines().size());
    for (const DocumentModel::Outline& outline : myDocument.outlines()) ids.push_back(outline.id);
    return ids;
}

void MainWindow::adoptRestoredOutline(const std::vector<int>& before)
{
    // pendingOutlineId()'s fallback is the LAST outline in the list, which is
    // the newest one only while outlines are being appended. An undo restores
    // a removed outline AT ITS ORIGINAL POSITION, so undoing an extrude in a
    // document that already held a later outline handed the user back the one
    // they asked for and left Extrude aimed at the other: Ctrl+Z then E built
    // a body from a different outline than the one that had just reappeared.
    //
    // The fix is to name it rather than to reorder the list or to make the
    // fallback cleverer. An outline that appears across an undo or a redo is
    // the thing the user just acted on, and that is exactly what "pending"
    // means. Exactly one appearing is the only case worth claiming - a
    // multi-outline jump has no single thing the user meant, and leaving the
    // existing selection alone is the honest answer there.
    int appeared = 0;
    int candidate = 0;
    for (const DocumentModel::Outline& outline : myDocument.outlines()) {
        if (std::find(before.begin(), before.end(), outline.id) != before.end()) continue;
        ++appeared;
        candidate = outline.id;
    }
    if (appeared == 1) mySelectedOutlineId = candidate;
}

gp_Dir MainWindow::pendingSweepDirection() const
{
    gp_Pln plane = mySketch.plane();
    myDocument.outlinePlane(pendingOutlineId(), plane);
    return plane.Axis().Direction();
}

void MainWindow::selectOutline(int id)
{
    if (!myDocument.containsOutline(id)) return;
    mySelectedOutlineId = id;
    updateActions();
}

void MainWindow::onFinishSketch()
{
    const TopoDS_Face face = mySketch.closedFace();
    if (face.IsNull()) {
        myToasts->show(tr("This outline can't close into a flat face. It probably "
                          "crosses itself — Press Backspace to undo the last point and "
                          "redraw it, or Esc to start over"),
                      Toast::Kind::Failure, false);
        return;
    }

    // A closed outline is a DOCUMENT ITEM now, not a preview - so it takes a
    // checkpoint like every other change to the document, appears in the
    // drawer, and can be taken back with Ctrl+Z rather than only by being
    // extruded or silently dropped by the next sketch.
    checkpointDocument();
    const int id = myDocument.addOutline(face, mySketch.plane());
    // The newest is what Extrude consumes by default, and saying so
    // explicitly rather than leaning on pendingOutlineId()'s fallback means
    // the drawer's highlight and the commit target agree from the first frame.
    mySelectedOutlineId = id;

    recordProgress("sketch.completed");
    mySketching = false;
    myView->setSketchMode(false, mySketch.plane());
    // The in-progress polyline's channel, emptied - the closed outline is on
    // screen through its own item now. There is exactly one way to display a
    // closed outline, which is the whole point of the item replacing the
    // pending-face preview rather than joining it.
    myView->setPreview(TopoDS_Shape());
    myView->displayOutline(id, face);
    // The sketch's points have become an item; leaving them in the controller
    // would let a second Finish Sketch close the same outline twice.
    mySketch.reset();
    syncSketchConstraints();

    updateActions();
    emit documentChanged();
    const QString message =
        tr("%1 created — %2")
            .arg(QString::fromStdString(myDocument.outlineNameOf(id)),
                 QString::fromStdString(Measure::formatFaceExtents(face, mySketch.plane())));
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
}

void MainWindow::onExtrude()
{
    const TopoDS_Face face = pendingFace();
    if (face.IsNull()) return;

    // Opens a live preview over the viewport instead of a modal dialog - see
    // ExtrudePreview. It calls extrudePendingFace() itself once the user
    // commits (Enter) or leaves the pending outline alone if they back out
    // (Escape).
    myExtrudePreview->begin(face);
}

bool MainWindow::extrudePendingFace(double height)
{
    const int outlineId = pendingOutlineId();
    const TopoDS_Face face = myDocument.outlineFace(outlineId);
    if (face.IsNull() || height == 0.0) return false;

    // The OUTLINE'S OWN plane, not the sketch controller's current one.
    // CLAUDE.md's rule is that a closed outline pins the plane it was drawn
    // on; storing that plane on the item is what finally makes it true by
    // construction rather than by refusing to move the plane in the meantime.
    gp_Pln plane = mySketch.plane();
    myDocument.outlinePlane(outlineId, plane);

    const TopoDS_Shape solid = ModelingOps::extrude(face, plane.Axis().Direction(), height);
    if (solid.IsNull()) {
        myToasts->show(tr("This face couldn't be extruded into a body — "
                          "The outline may cross itself or be too small to have an "
                          "inside. Try redrawing it with Ctrl+K"),
                      Toast::Kind::Failure, false);
        return false;
    }

    // Frame the very first solid; after that leave the camera where the user
    // put it rather than yanking the view on every extrude.
    const bool wasEmpty = myDocument.count() == 0;
    // ONE checkpoint around the whole conversion - the outline going and the
    // body arriving are one change, so one Ctrl+Z puts the outline back and
    // takes the body away. Two checkpoints would make the user press it twice
    // and leave a document holding both in between.
    checkpointDocument();
    const int id = myDocument.convertOutlineToBody(outlineId, solid);
    recordProgress("extrude.completed");
    myView->clearPreview();
    myView->removeOutline(outlineId);
    myView->displaySolid(id, solid);

    // Symmetry (Milestone 3): creation pairs. A body whose own bounding box
    // straddles the plane stays unpaired - mirroring it would build a twin
    // overlapping the body itself, not a second piece of furniture. This
    // sits in the SAME checkpoint as the conversion above (nothing has
    // called updateActions()/documentChanged() yet), so one undo removes the
    // outline's replacement body AND its twin together.
    int twinId = 0;
    if (id > 0 && myDocument.symmetryOn() &&
        !ModelingOps::boundingBoxStraddlesPlane(solid, myDocument.symmetryPlane())) {
        const ModelingOps::BooleanResult mirrored =
            ModelingOps::mirrorShape(solid, myDocument.symmetryPlane());
        if (mirrored.ok) {
            twinId = myDocument.addSolid(mirrored.shape);
            if (twinId > 0) {
                myDocument.pairBodies(id, twinId);
                myView->displaySolid(twinId, mirrored.shape);
            }
        } else {
            qWarning("Symmetry: creation-pair mirror failed: %s", mirrored.error.c_str());
        }
    }
    // AFTER the twin arrives, not before - fix round 1: framing on the first
    // body alone left a mirrored twin sitting half (or entirely) outside the
    // viewport on the very first extrude of a symmetric session.
    if (wasEmpty) myView->fitAll();

    mySelectedOutlineId = 0;
    mySketch.reset();
    updateActions();
    emit documentChanged();
    // Paired: names both, no dimensions - a twin repeats the same size, and
    // "Body 03 and Body 04 created" is the whole point (the brief's own
    // words). Unpaired: the ordinary single-body message, unchanged.
    const QString message =
        twinId > 0
            ? tr("%1 and %2 created")
                  .arg(QString::fromStdString(myDocument.nameOf(id)),
                       QString::fromStdString(myDocument.nameOf(twinId)))
            : tr("%1 created — %2")
                  .arg(QString::fromStdString(myDocument.nameOf(id)),
                       QString::fromStdString(Measure::formatDimensions(solid)));
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

bool MainWindow::canPullSelectedFace() const
{
    // No sketch in progress, and no closed outline waiting - see
    // canChangeSketchPlane() and the header for both halves. The pending-face
    // half is what keeps this and ExtrudePreview mutually exclusive.
    //
    // Render mode adds a third: it clears the selection and deactivates
    // every solid's own selection modes the moment it turns on (see
    // OcctViewWidget::setRenderMode()), so selectedFace() below would answer
    // null on its own - this term is defence in depth, named explicitly so a
    // future selection route cannot silently reach this predicate before the
    // viewport's own suppression does.
    if (mySketching || hasPendingFace() || myRenderModeOn) return false;

    // selectedFace() is deliberately "the ONE selected face", never the first
    // of several, so this cannot be a coin toss between two highlighted
    // faces. It is null outside face-selection mode, which is what makes the
    // mode check implicit rather than a second condition to keep in step.
    const TopoDS_Face face = myView->selectedFace();
    if (face.IsNull()) return false;
    return BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane;
}

int MainWindow::bodyIdForFace(const TopoDS_Face& face) const
{
    if (face.IsNull()) return 0;
    for (const DocumentModel::Solid& solid : myDocument.solids()) {
        for (TopExp_Explorer it(solid.shape, TopAbs_FACE); it.More(); it.Next()) {
            if (it.Current().IsSame(face)) return solid.id;
        }
    }
    return 0;
}

void MainWindow::checkpointDocument()
{
    // Render mode's own exit rule: "any document-changing action leaves
    // render mode first". Every commit in this file that takes a checkpoint
    // now calls THIS rather than myDocument.checkpoint() directly (eight call
    // sites, before this task), which is what makes the rule structural
    // rather than eight separate reminders scattered across the file to add
    // one - and it runs before the checkpoint below, exactly as the brief's
    // own word "first" asks for.
    //
    // setSymmetryEnabled() gets the identical one-line exit explicitly,
    // rather than being routed through here, because it is document-changing
    // (unpairs bodies, bumps revision(), dirties, arms autosave) but takes NO
    // checkpoint of its own - "a mode switch, not an edit," per its own
    // comment - so there is no checkpoint() call here for it to ride along
    // with. Lock to Face and Unlock Face were considered and left alone
    // (fix round 1, Important 3's review): lockToFace() cannot actually be
    // reached while render mode is on (it needs a flat face selected, and
    // render mode clears and deactivates all selection on entry), and
    // unlockFace() only moves the sketch plane back to the ground - neither
    // is a change a render-mode shot would visibly disagree with.
    if (myRenderModeOn) setRenderModeEnabled(false);
    myDocument.checkpoint();
}

void MainWindow::commitReplaceBody(int id, const TopoDS_Shape& newShape, bool& twinFollowed,
                                   int& linkedOthersUpdated)
{
    twinFollowed = false;
    linkedOthersUpdated = 0;
    if (id <= 0 || newShape.IsNull()) return;

    checkpointDocument();
    myDocument.replaceSolid(id, newShape);
    myView->displaySolid(id, newShape);

    // Linked copies (Milestone 4, Task 4.2): propagation and the mirror-twin
    // follow below are mutually exclusive by construction - a linked member
    // can never also carry a mirror twin, the v1 exclusion DocumentModel
    // enforces from both directions - so exactly one of the two branches can
    // ever run for the same `id`. propagateLinkedEdit() takes NO checkpoint
    // of its own (see its header): it rides inside the checkpoint taken two
    // lines up, so one undo reverts `id` and every other member together.
    if (myDocument.isLinked(id)) {
        // The return value is READ, not dropped: propagateLinkedEdit()
        // returns false having written NOTHING when a member's own
        // transformShape() refuses, while resyncLinkGroupView() counts group
        // MEMBERSHIP rather than what actually changed - so taking the count
        // unconditionally is how "— linked copy updated" got appended to an
        // edit that reached nobody. Never-silent-failure applies to the one
        // silent-lie path left in this chain exactly as it does to every
        // neighbouring branch. -1 is linkedGroupSuffix()'s refusal sentinel,
        // never a count.
        const bool propagated = myDocument.propagateLinkedEdit(id, newShape);
        const int others = resyncLinkGroupView(id);
        linkedOthersUpdated = propagated ? others : (others > 0 ? -1 : 0);
        return;
    }

    // Symmetry (Milestone 3): the whole reason this function exists rather
    // than staying three copies of "checkpoint, replace, display" - one
    // twin-follow rule instead of one per gizmo. A mirror failure here (not
    // expected to be reachable in practice - mirrorShape only refuses a null
    // shape or a kernel exception, and `newShape` just came from a
    // successful edit) leaves the twin untouched rather than turning a
    // successful primary edit into a reported failure.
    //
    // Gated on symmetryOn(): the pairing map is undo-tracked while the
    // on/off mode is not (fix round 1), so an undo can resurrect an old
    // pairing while symmetry stays off. A pairing only ACTS while the mode
    // is on - this is the hook that makes "turn symmetry off, edit, nothing
    // propagates" true even across that undo.
    const int twin = myDocument.symmetryOn() ? myDocument.twinOf(id) : -1;
    if (twin > 0) {
        const ModelingOps::BooleanResult mirrored =
            ModelingOps::mirrorShape(newShape, myDocument.symmetryPlane());
        if (mirrored.ok) {
            myDocument.replaceSolid(twin, mirrored.shape);
            myView->displaySolid(twin, mirrored.shape);
            twinFollowed = true;
        } else {
            qWarning("Symmetry: twin mirror failed: %s", mirrored.error.c_str());
        }
    }
}

bool MainWindow::pullFaceBy(const TopoDS_Face& face, double distance)
{
    if (face.IsNull() || distance == 0.0) return false;

    const int id = bodyIdForFace(face);
    const TopoDS_Shape body = myDocument.shapeOf(id);
    if (id <= 0 || body.IsNull()) return false;

    const ModelingOps::BooleanResult result = ModelingOps::pullFace(body, face, distance);
    if (!result.ok) {
        // Never present a failed kernel operation as a success, and never
        // show its error text: it is written for this file, not for the user.
        qWarning("Pull failed: %s", result.error.c_str());
        myToasts->show(tr("This face can't be pulled that far — a carve deeper than the "
                          "body removes the whole thing, and the geometry engine has "
                          "nothing left to build. Try a smaller distance, or drag the "
                          "arrow the other way"),
                      Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("Pull refused — nothing was changed"));
        return false;
    }

    // The preview and the arrow both describe the face that is about to stop
    // existing; the selection holds that face too. All three go before the
    // body is redisplayed, in that order, so nothing is left pointing at
    // topology from before the rebuild.
    myView->clearModelingPreview();
    myView->clearPullArrow();
    myView->clearSelection();

    bool twinFollowed = false;
    int linkedOthersUpdated = 0;
    commitReplaceBody(id, result.shape, twinFollowed, linkedOthersUpdated);
    recordProgress("pull.completed");

    updateActions();
    emit documentChanged();
    QString message =
        tr("%1 pulled — %2")
            .arg(QString::fromStdString(myDocument.nameOf(id)),
                 QString::fromStdString(Measure::formatDimensions(result.shape)));
    if (twinFollowed) message += tr(" — twin followed");
    message += linkedGroupSuffix(linkedOthersUpdated);
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

int MainWindow::bodyIdForEdge(const TopoDS_Edge& edge) const
{
    if (edge.IsNull()) return 0;
    for (const DocumentModel::Solid& solid : myDocument.solids()) {
        for (TopExp_Explorer it(solid.shape, TopAbs_EDGE); it.More(); it.Next()) {
            if (it.Current().IsSame(edge)) return solid.id;
        }
    }
    return 0;
}

bool MainWindow::bevelTarget(std::vector<TopoDS_Edge>& edges, TopoDS_Edge& edge, int& bodyId,
                             gp_Pnt& centre, gp_Dir& outward) const
{
    // The same three terms canPullSelectedFace() opens with, for the same
    // reasons - see its comment and the header.
    if (mySketching || hasPendingFace() || myRenderModeOn) return false;

    // Edge mode explicitly, so this cannot be true at the same time as the
    // face pull's predicate or the transform gizmo's.
    if (myView->selectionMode() != OcctViewWidget::SelectionMode::Edge) return false;

    const std::vector<TopoDS_Edge> selected = myView->selectedEdges();
    if (selected.empty()) return false;

    // ALL ON ONE BODY. Not "the body the first edge happens to belong to":
    // one gesture is one kernel build on one shape, so a selection reaching
    // across two bodies raises nothing at all rather than quietly bevelling
    // whichever body won. The mixed case is a real one - Shift-click makes it
    // in two clicks - and the honest answer to it is no arrow.
    const int id = bodyIdForEdge(selected.front());
    const TopoDS_Shape body = myDocument.shapeOf(id);
    if (id <= 0 || body.IsNull()) return false;
    for (const TopoDS_Edge& candidate : selected) {
        if (bodyIdForEdge(candidate) != id) return false;
    }

    // Straightness, the two adjacent faces and the outward bisector are all
    // ModelingOps::bevelAxis()'s to decide, and it decides them once for the
    // predicate and the gizmo both. EVERY edge has to pass, not just the one
    // the arrow will stand on: the gesture commits all of them together, so a
    // curved edge among them makes the whole selection unbevellable rather
    // than silently dropping itself out of the build.
    gp_Pnt at;
    gp_Dir axis;
    for (const TopoDS_Edge& candidate : selected) {
        gp_Pnt ignoredPoint;
        gp_Dir ignoredAxis;
        if (!ModelingOps::bevelAxis(body, candidate, ignoredPoint, ignoredAxis)) return false;
    }

    // The arrow stands on the edge picked LAST, which is where the hand is.
    const TopoDS_Edge arrowEdge = myView->lastSelectedEdge();
    if (arrowEdge.IsNull() || !ModelingOps::bevelAxis(body, arrowEdge, at, axis)) return false;

    edges = selected;
    edge = arrowEdge;
    bodyId = id;
    centre = at;
    outward = axis;
    return true;
}

bool MainWindow::canBevelSelectedEdge() const
{
    std::vector<TopoDS_Edge> edges;
    TopoDS_Edge edge;
    int bodyId = 0;
    gp_Pnt centre;
    gp_Dir outward;
    return bevelTarget(edges, edge, bodyId, centre, outward);
}

QString MainWindow::bevelRefusalText(bool fillet)
{
    // No trailing period: the app's failure sentences end without one (see the
    // pull's and the transform's), and this pair was the exception.
    return fillet ? tr("This edge can't take a fillet that big — the curve "
                       "would eat a neighbouring face. Try a smaller size")
                  : tr("This edge can't take a chamfer that big — the flat "
                       "would eat a neighbouring face. Try a smaller size");
}

QString MainWindow::bevelCombinationRefusalText(bool fillet)
{
    // The OTHER refusal, and the reason it needed its own sentence: the size
    // is not what was turned down here, so telling the user to shrink it
    // sends them round a loop with no exit. What changes the outcome is the
    // SELECTION, so that is what the sentence asks for. No trailing period,
    // em dash between the clauses, like every other failure in this app.
    //
    // "will only round some of them" / "will only flatten some of them" is
    // what this said until the whole-branch review found it: those are the
    // Never column for Fillet and Chamfer, and a user who reads "round" has
    // no control anywhere in the app spelled that way. The operation names
    // itself instead. The sweep can see this pair now - `round` and `flatten`
    // joined the banned list with word-boundary matching, so "background"
    // stays legal and "rounded" does not.
    return fillet ? tr("These edges can't take a fillet together — the geometry "
                       "engine would build it on only some of them. Try them one "
                       "at a time")
                  : tr("These edges can't take a chamfer together — the geometry "
                       "engine would build it on only some of them. Try them one "
                       "at a time");
}

QString MainWindow::transformOperationName(const gp_Trsf& delta)
{
    return transformIsScale(delta)      ? tr("Scale")
           : transformIsRotation(delta) ? tr("Rotate")
                                        : tr("Move");
}

QString MainWindow::transformPastVerb(const gp_Trsf& delta)
{
    return transformIsScale(delta)      ? tr("scaled")
           : transformIsRotation(delta) ? tr("rotated")
                                        : tr("moved");
}

QString MainWindow::transformRefusalText(const gp_Trsf& delta)
{
    // "refused" would carry `fuse` as a substring, and the banned-word sweep
    // matches bare substrings case-insensitively (CLAUDE.md says so). The
    // sentence this replaced said "the geometry engine refused the change" and
    // sailed through every run only because nothing ever triggered it - which
    // is exactly why the suite now shows this copy through a probe.
    return tr("This body couldn't be %1 — the geometry engine turned that "
              "change down. Try a smaller drag, or a different handle")
        .arg(transformPastVerb(delta));
}

QString MainWindow::linkedGroupSuffix(int othersUpdated)
{
    // Negative is the refusal sentinel both commit sites set when
    // propagateLinkedEdit() returned false having written nothing (M1). The
    // primary edit stands and its own toast still reports it, but claiming
    // the copies followed would be a silent lie about a document the user
    // is about to keep editing.
    if (othersUpdated < 0) return tr(" — the linked copies could not follow this edit");
    if (othersUpdated == 0) return QString();
    return othersUpdated == 1 ? tr(" — linked copy updated")
                              : tr(" — %1 linked copies updated").arg(othersUpdated);
}

bool MainWindow::transformIsScale(const gp_Trsf& delta)
{
    return std::fabs(delta.ScaleFactor() - 1.0) > 1.0e-9;
}

bool MainWindow::transformIsRotation(const gp_Trsf& delta)
{
    gp_Vec axis;
    Standard_Real angle = 0.0;
    delta.GetRotation().GetVectorAndAngle(axis, angle);
    return std::fabs(angle) > 1.0e-9;
}

bool MainWindow::bevelEdgesBy(const std::vector<TopoDS_Edge>& edges, double size, bool fillet)
{
    if (edges.empty() || size <= 0.0) return false;
    for (const TopoDS_Edge& edge : edges) {
        if (edge.IsNull()) return false;
    }

    const int id = bodyIdForEdge(edges.front());
    const TopoDS_Shape body = myDocument.shapeOf(id);
    if (id <= 0 || body.IsNull()) return false;
    // bevelTarget() already refuses a selection spanning two bodies, but this
    // is the commit and it does not get to assume its caller checked: one
    // build replaces ONE body, and an edge belonging to another would be
    // rounded on a shape it is not part of.
    for (const TopoDS_Edge& edge : edges) {
        if (bodyIdForEdge(edge) != id) return false;
    }

    const ModelingOps::BooleanResult result =
        fillet ? ModelingOps::filletEdges(body, edges, size)
               : ModelingOps::chamferEdges(body, edges, size);
    if (!result.ok) {
        // Never present a failed kernel operation as a success, and never show
        // its error text: it is written for this file, not for the user. A
        // fillet failing on hard geometry is normal, not exceptional - see
        // ModelingOps::filletEdge - so the sentence names the cause and the fix
        // rather than apologising.
        qWarning("Bevel failed: %s", result.error.c_str());
        // Two causes, two sentences. "Try a smaller size" is right for a
        // radius the neighbouring face cannot give up, and FALSE for a
        // combination of edges the kernel will not bevel together - no size
        // works there, so a user following that advice shrinks the number
        // until they give up. ModelingOps says which through
        // combinationRefused; this never reads its error string.
        myToasts->show(result.combinationRefused ? bevelCombinationRefusalText(fillet)
                                                 : bevelRefusalText(fillet),
                       Toast::Kind::Failure, false);
        statusBar()->showMessage(fillet ? tr("Fillet refused — nothing was changed")
                                        : tr("Chamfer refused — nothing was changed"));
        return false;
    }

    // The preview, the arrow and the selection all describe the edge that is
    // about to stop existing. All three go before the body is redisplayed, in
    // that order, so nothing is left pointing at topology from before the
    // rebuild - the face pull's rule, one gizmo over.
    myView->clearModelingPreview();
    myView->clearBevelArrow();
    myView->clearSelection();

    bool twinFollowed = false;
    int linkedOthersUpdated = 0;
    commitReplaceBody(id, result.shape, twinFollowed, linkedOthersUpdated);
    recordProgress("bevel.completed");

    updateActions();
    emit documentChanged();
    // Led by the operation's own name. "Body 03 rounded" describes the result
    // in a word that appears nowhere else in the app - the chip, the tooltips,
    // the state label and the refusal all say Fillet or Chamfer.
    //
    // The count only appears when there is one to report. A single-edge bevel
    // reads exactly as it always did, and "1 edge" is a number nobody needs.
    // Written out rather than through "(s)", per the vocabulary rules.
    const QString name = QString::fromStdString(myDocument.nameOf(id));
    const QString extent = QString::fromStdString(Measure::formatDimensions(result.shape));
    QString message =
        edges.size() > 1
            ? (fillet ? tr("Fillet added to %1 — %2 edges — %3")
                      : tr("Chamfer added to %1 — %2 edges — %3"))
                  .arg(name, QString::number(static_cast<int>(edges.size())), extent)
            : (fillet ? tr("Fillet added to %1 — %2") : tr("Chamfer added to %1 — %2"))
                  .arg(name, extent);
    if (twinFollowed) message += tr(" — twin followed");
    message += linkedGroupSuffix(linkedOthersUpdated);
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

int MainWindow::transformableBodyId() const
{
    // The same three terms canPullSelectedFace() opens with, for the same
    // reasons: an outline in progress lives on a plane, and a body that moved
    // under it would take the plane's meaning with it.
    if (mySketching || hasPendingFace() || myRenderModeOn) return 0;

    // The mirror-placement gesture (Milestone 4, Phase 3) also lives in body
    // selection mode, and a single selected body satisfies BOTH this and
    // canBeginMirrorPlacement() at once - the one genuine overlap between
    // the four gizmo predicates, since the other three are kept apart by
    // selection mode or hasPendingFace() alone. While a gesture is actually
    // RUNNING (not merely available) the transform gizmo stands down, so the
    // two application-wide claims - this one has none, the mirror chip's
    // Enter/Escape/X/Y/Z does - can never visually collide over the same
    // body.
    if (myView->mirrorPlacementActive()) return 0;

    // Body mode explicitly. selectedSolidIds() reports the owning body of a
    // selected FACE too, so without this the gizmo would appear over a face
    // selection and fight the pull arrow for the same drag.
    if (myView->selectionMode() != OcctViewWidget::SelectionMode::Solid) return 0;

    const std::vector<int> ids = myView->selectedSolidIds();
    if (ids.size() != 1) return 0;
    return ids.front();
}

void MainWindow::refreshTransformGizmo()
{
    const int id = transformableBodyId();
    if (id > 0) myView->attachManipulator(id);
    else        myView->detachManipulator();
}

void MainWindow::refreshEdgeAnnotation()
{
    // Derived from the arrow's own predicate, not from the arrow's visibility
    // and not from the event that happened to raise it - the rule this file
    // keeps for every other surface over the viewport.
    myView->setEdgeDimensionSuppressed(canBevelSelectedEdge());
}

void MainWindow::onGizmoReleased(int solidId, const gp_Trsf& delta)
{
    // A drag that nets nothing is a cancel, not an edit: no checkpoint, no
    // toast, no revision. It reaches here for two reasons that look identical
    // from the document's side - a press and release at the same point, and a
    // real drag the snap rounded back to where it started - and both deserve
    // the same silence. The viewport already restored its own presentation
    // before emitting, so there is nothing to put back.
    if (ModelingOps::isIdentityTransform(delta)) return;
    transformBody(solidId, delta);
}

bool MainWindow::transformBody(int id, const gp_Trsf& delta)
{
    const TopoDS_Shape body = myDocument.shapeOf(id);
    if (id <= 0 || body.IsNull()) return false;

    // This layer's clamp, not the kernel's: transformShape refuses only a
    // factor <= 0, and a body scaled to 1e-9 is not an error the kernel can
    // see - it is a body the user has lost. See kMinScale/kMaxScale.
    // INCLUSIVE on both ends, and that is the whole point: a shrink dragged to
    // the floor snaps to exactly kMinScale with Snap on and lands fractionally
    // below it with Snap off, so an exclusive test (`< kMinScale`) let the
    // SAME gesture commit or be refused depending on a toggle that is supposed
    // to change where a drag lands, not whether it is allowed at all.
    const double scale = delta.ScaleFactor();

    // Which of the three this gesture is, read off the transform itself and
    // derived ONCE, ABOVE the two refusal branches. It used to be derived only
    // on the success path, so a rotate or a scale the kernel turned down was
    // announced as a failed Move - a refusal that names the wrong operation is
    // worse than one that names none, because the user goes looking for a move
    // they never made.
    const QString operation = transformOperationName(delta);

    if (scale <= kMinScale || scale >= kMaxScale) {
        myToasts->show(tr("That's too big a change of size to make at once — anything "
                          "under a twentieth or over twenty times leaves a body you "
                          "can't see or can't fit on screen. Drag the handle back "
                          "toward the body and scale it in smaller steps"),
                      Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("%1 refused — nothing was changed").arg(operation));
        return false;
    }

    const ModelingOps::BooleanResult result = ModelingOps::transformShape(body, delta);
    if (!result.ok) {
        // Never present a failed kernel operation as a success, and never show
        // its error text - it is written for this file, not for the user.
        qWarning("Transform failed: %s", result.error.c_str());
        myToasts->show(transformRefusalText(delta), Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("%1 refused — nothing was changed").arg(operation));
        return false;
    }

    bool twinFollowed = false;
    int linkedOthersUpdated = 0;
    commitReplaceBody(id, result.shape, twinFollowed, linkedOthersUpdated);
    // Selected again on purpose, unlike the face pull's clearSelection(): the
    // body is still the same body, and keeping it selected is what leaves the
    // gizmo standing on it for a second drag. displaySolid() detached the
    // gizmo along with the presentation it was holding; the updateActions()
    // below re-attaches it at the body's new position.
    myView->setSelectedSolids({id});
    recordProgress("transform.completed");

    updateActions();
    emit documentChanged();

    // The same derivation the refusals above use - one source for all three
    // outcomes, and it stays right if a gesture ever combines two of them.
    QString message =
        tr("%1 %2 — %3")
            .arg(QString::fromStdString(myDocument.nameOf(id)),
                 transformPastVerb(delta),
                 QString::fromStdString(Measure::formatDimensions(result.shape)));
    if (twinFollowed) message += tr(" — twin followed");
    message += linkedGroupSuffix(linkedOthersUpdated);
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

void MainWindow::onUnion()     { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Fuse)); }
void MainWindow::onSubtract()  { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Cut)); }
void MainWindow::onIntersect() { runBoolean(static_cast<int>(ModelingOps::BooleanKind::Common)); }

void MainWindow::runBoolean(int kind)
{
    applyBooleanToSelection(kind);
}

bool MainWindow::applyBooleanToSelection(int kind)
{
    const QString operationName =
        kind == static_cast<int>(ModelingOps::BooleanKind::Fuse)   ? tr("Union")
        : kind == static_cast<int>(ModelingOps::BooleanKind::Cut)  ? tr("Subtract")
                                                                   : tr("Intersect");

    std::vector<int> ids = myView->selectedSolidIds();
    if (ids.size() != 2) {
        // A FAILURE for the same reason canChangeSketchPlane()'s refusal is:
        // this path returns false and changes nothing, and a refusal the
        // notifications toggle could silence would be an operation that did
        // nothing and said nothing. See ToastHost::show().
        myToasts->show(tr("%1 needs exactly two bodies — "
                          "Click one body, then Shift-click another")
                          .arg(operationName),
                      Toast::Kind::Failure, false);
        return false;
    }

    // Cut is not commutative. The lower document id is the base, so the result is
    // predictable rather than dependent on pick order, which AIS does not preserve.
    std::sort(ids.begin(), ids.end());

    // Linked copies (Milestone 4, Task 4.2): two members of the SAME group
    // refuse outright, before the kernel is even asked. This is NOT the
    // mirror-twin case just below, which is allowed and collapses cleanly -
    // a body fused with its own mirror IS the symmetric whole, so the
    // result genuinely has no more use for a twin. Two placements of the
    // SAME linked shape are different: after combining them there is no
    // longer one honest shape left to propagate FROM (the group's own
    // "same shape, placed differently" invariant is what a combine would
    // break), so v1's ruling is simplest-honest: refuse, and say so.
    if (myDocument.isLinked(ids[0]) && myDocument.isLinked(ids[1]) &&
        myDocument.linkAnchorOf(ids[0]) == myDocument.linkAnchorOf(ids[1])) {
        // Not "refused" - the banned-word sweep matches bare substrings
        // case-insensitively (CLAUDE.md says so), and "refused" carries
        // "fuse" inside it. See transformRefusalText()'s own comment for the
        // same finding, one gizmo over.
        myToasts->show(tr("%1 can't combine two copies of the same linked group — "
                          "Unlink one first, then try again")
                          .arg(operationName),
                      Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("%1 refused — nothing was changed").arg(operationName));
        return false;
    }

    const std::string nameA = myDocument.nameOf(ids[0]);
    const std::string nameB = myDocument.nameOf(ids[1]);
    const TopoDS_Shape a = myDocument.shapeOf(ids[0]);
    const TopoDS_Shape b = myDocument.shapeOf(ids[1]);
    if (a.IsNull() || b.IsNull()) return false;

    const ModelingOps::BooleanResult result =
        ModelingOps::applyBoolean(static_cast<ModelingOps::BooleanKind>(kind), a, b);

    if (!result.ok) {
        // Never present a failed boolean as a success. The engine's error text is
        // genuinely useful for debugging, so keep it in the log, not the toast.
        qWarning("%s failed: %s", qPrintable(operationName), result.error.c_str());
        myToasts->show(tr("%1 failed — The two bodies couldn't be combined — "
                          "This usually means they only touch at a single edge or "
                          "corner, which the geometry engine can't resolve. Move one "
                          "body so they overlap properly, then try again")
                          .arg(operationName),
                      Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("%1 failed — nothing was changed").arg(operationName));
        return false;
    }

    // Symmetry: an operand pair that IS each other's own twin collapses to
    // ONE unpaired result (the plan's own ruling - the symmetric whole no
    // longer needs a mirror, since it already contains both halves). A
    // paired operand combined with something unrelated instead KEEPS that
    // operand's own id, so its twin can be replaced with the mirrored
    // result rather than left standing for a body that no longer exists.
    // Determined BEFORE anything is removed, from the two ids the boolean
    // actually consumed.
    //
    // Gated on symmetryOn(), same rule as commitReplaceBody's and
    // onDeleteSelected's own guards (fix round 1): the pairing map survives
    // undo while the on/off mode does not, so a stale pairing must never
    // drive behaviour once symmetry is off.
    const bool operandsAreTwins =
        myDocument.symmetryOn() && myDocument.twinOf(ids[0]) == ids[1];
    int survivingId = 0;
    if (myDocument.symmetryOn() && !operandsAreTwins) {
        if (myDocument.twinOf(ids[0]) > 0) survivingId = ids[0];
        else if (myDocument.twinOf(ids[1]) > 0) survivingId = ids[1];
    }
    // Linked copies (Milestone 4, Task 4.2): the identical reasoning one
    // paragraph up, for link groups instead of a mirror twin - keep
    // whichever operand belongs to a group, so propagateLinkedEdit() below
    // has a valid member id to re-derive the rest of the group from. The
    // same-group case was already refused above, so at most ONE of the two
    // can be linked here, and a body can never carry a twin AND a group at
    // once (the v1 exclusion), so this can never collide with the branch
    // just above.
    if (survivingId == 0) {
        if (myDocument.isLinked(ids[0])) survivingId = ids[0];
        else if (myDocument.isLinked(ids[1])) survivingId = ids[1];
    }

    checkpointDocument();
    myView->clearSelection();

    int id = 0;
    bool twinFollowed = false;
    int linkedOthersUpdated = 0;
    if (survivingId > 0) {
        const int otherId = (survivingId == ids[0]) ? ids[1] : ids[0];
        // removeSolid() drops `otherId`'s own pairing/group bookkeeping (see
        // DocumentModel.h) - if `otherId` was itself linked to a DIFFERENT
        // group than `survivingId`'s, that group loses this one member the
        // same way any other body removal would take it out.
        myDocument.removeSolid(otherId);
        myView->removeSolid(otherId);
        myDocument.replaceSolid(survivingId, result.shape);
        myView->displaySolid(survivingId, result.shape);
        id = survivingId;

        const int twin = myDocument.symmetryOn() ? myDocument.twinOf(id) : -1;
        if (twin > 0) {
            const ModelingOps::BooleanResult mirrored =
                ModelingOps::mirrorShape(result.shape, myDocument.symmetryPlane());
            if (mirrored.ok) {
                myDocument.replaceSolid(twin, mirrored.shape);
                myView->displaySolid(twin, mirrored.shape);
                twinFollowed = true;
            } else {
                qWarning("Symmetry: twin mirror failed: %s", mirrored.error.c_str());
            }
        } else if (myDocument.isLinked(id)) {
            // propagateLinkedEdit() takes NO checkpoint of its own (see its
            // header) - it rides inside the checkpoint taken two lines up,
            // exactly as commitReplaceBody()'s own linked branch does - and
            // its refusal is read here on exactly the same terms (M1).
            const bool propagated = myDocument.propagateLinkedEdit(id, result.shape);
            const int others = resyncLinkGroupView(id);
            linkedOthersUpdated = propagated ? others : (others > 0 ? -1 : 0);
        }
    } else {
        // Both unpaired, or the two operands were each other's own twin -
        // either way the result is a single, freshly unpaired body.
        // removeSolid() below drops each removed id's own pairing entries,
        // so the "own twin" case leaves nothing pointing at a ghost id.
        for (int rid : ids) {
            myDocument.removeSolid(rid);
            myView->removeSolid(rid);
        }
        id = myDocument.addSolid(result.shape);
        myView->displaySolid(id, result.shape);
    }
    recordProgress("boolean.completed");

    updateActions();
    emit documentChanged();
    QString message = tr("%1 — %2 and %3 → %4 — %5")
                          .arg(operationName,
                               QString::fromStdString(nameA),
                               QString::fromStdString(nameB),
                               QString::fromStdString(myDocument.nameOf(id)),
                               QString::fromStdString(
                                   Measure::formatDimensions(result.shape)));
    if (twinFollowed) message += tr(" — twin followed");
    message += linkedGroupSuffix(linkedOthersUpdated);
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

void MainWindow::onExportStep()
{
    if (myDocument.count() == 0) return;

    const QString path = QFileDialog::getSaveFileName(this, tr("Export STEP"), QString(),
                                                      tr("STEP files (*.step *.stp)"));
    if (path.isEmpty()) return;

    std::vector<TopoDS_Shape> shapes;
    shapes.reserve(myDocument.count());
    for (const DocumentModel::Solid& solid : myDocument.solids()) shapes.push_back(solid.shape);

    const ModelingOps::StepResult result =
        ModelingOps::exportStep(ModelingOps::makeCompound(shapes), path.toStdString());

    if (!result.ok) {
        qWarning("STEP export failed: %s", result.error.c_str());
        myToasts->show(tr("Export failed — Couldn't write the STEP file — "
                          "Check that the folder exists and isn't read-only, then "
                          "try a different location"),
                      Toast::Kind::Failure, false);
        return;
    }
    statusBar()->showMessage(myDocument.count() == 1
                                 ? tr("Exported 1 body to %1").arg(path)
                                 : tr("Exported %1 bodies to %2")
                                       .arg(myDocument.count()).arg(path));
}

void MainWindow::onSelectionModeChanged()
{
    const OcctViewWidget::SelectionMode mode =
        myFaceSelectAction->isChecked() ? OcctViewWidget::SelectionMode::Face
        : myEdgeSelectAction->isChecked() ? OcctViewWidget::SelectionMode::Edge
                                          : OcctViewWidget::SelectionMode::Solid;
    myView->setSelectionMode(mode);
    if (myFaceSelectAction->isChecked()) recordProgress("faceMode.used");
    statusBar()->showMessage(
        myFaceSelectAction->isChecked()
            ? tr("Face selection — hovering highlights one face at a time")
        : myEdgeSelectAction->isChecked()
            ? tr("Edge selection — hovering shows one edge's length at a time")
            : tr("Body selection — click whole bodies to combine them"));
    // Neither mySolidSelectAction nor myFaceSelectAction is touched by
    // updateActions() itself (their checked state is handled entirely by the
    // QActionGroup they belong to), so this cannot recurse back in here -
    // but without this call, HintBalloon::reconsider() only ever finds out
    // face selection was used the next time something unrelated happens to
    // fire appStateChanged, which left its hint lingering.
    updateActions();
}

void MainWindow::onBodyDoubleClicked(int solidId)
{
    if (solidId <= 0) return;

    // The MODE first, and through the action - setChecked() alone would leave
    // the QActionGroup right and the viewport wrong, and setSelectionMode() on
    // the viewport alone would leave the chip, the menu tick and the status
    // label all describing the mode the user just left. onSelectionModeChanged()
    // is the one function that reads the group and pushes the answer out, and
    // it ends in updateActions().
    if (mySolidSelectAction && !mySolidSelectAction->isChecked()) {
        mySolidSelectAction->setChecked(true);
        onSelectionModeChanged();
    }

    // Then the body. Changing the mode clears the old sub-shape selection
    // (setSelectionMode re-activates every displayed shape), so this has to
    // follow it rather than lead - selecting first and switching after would
    // throw the selection away again and leave the user in body mode with
    // nothing picked, which is the gesture doing half of what it says.
    //
    // It announces itself: setSelectedSolids() emits selectionChanged(), which
    // this window answers with onSelectionChanged() -> updateActions(). No
    // second refresh path from here.
    myView->setSelectedSolids({solidId});
}

bool MainWindow::canChangeSketchPlane()
{
    // This guard's ORIGINAL argument no longer holds, and saying so is worth
    // more than quietly keeping the code. It used to be that both the commit
    // and the live preview swept the outline along whatever the sketch
    // plane's normal happened to be AT THAT MOMENT, so moving the plane in
    // between swept it in a direction lying in its own plane - a body with no
    // volume, which BRepPrimAPI_MakePrism reports as done. Phase 7 retired
    // that whole class by construction: a DocumentModel::Outline stores the
    // plane it was drawn on BY VALUE, and both extrudePendingFace() and
    // ExtrudePreview sweep along THAT (see pendingSweepDirection()). Locking
    // a face can no longer re-aim a waiting outline at all.
    //
    // What the guard protects now is narrower and still real: the sketch
    // plane is where the NEXT outline lands, and moving it while one outline
    // is already waiting leaves the user with two outlines on two planes and
    // one status label describing whichever the app picked. Keeping the two
    // in step - one waiting outline, one plane it was drawn on - is a
    // legibility rule rather than a correctness one, and the toast says which
    // ways out actually exist.
    //
    // Discarding the pending outline instead was the alternative, and it is
    // worse: it throws away work the user did without being asked.
    if (!hasPendingFace()) return true;

    // The two remedies that WORK. "Ctrl+K to start a new outline" was one of
    // them until this phase, and it stopped being one the moment an outline
    // became a document item: starting a sketch no longer discards the
    // waiting one, so following that advice left the action just as disabled
    // as before. Advice that does nothing is worse than no advice - the user
    // does the thing, nothing changes, and now they distrust the message too.
    // "Ctrl+Z to take it back" was the second one to fail that test: nothing
    // gates the operations that push onto the undo stack, so one Union later
    // Ctrl+Z means the Union. Delete is the remedy that is always the
    // outline's - see onDeleteSelected().
    //
    // A FAILURE, not a Note, and the reason is item 12's toggle: this is a
    // refusal - the gesture the user just made did not happen - and a refusal
    // that goes silent when notifications are off is a silent failure. It is
    // reachable from the Ctrl+double-click route, which consults no action's
    // enabled state, so "the action was disabled anyway" is not an answer here.
    // See ToastHost::show() for the rule.
    //
    // Punctuation, per CLAUDE.md and per every other failure sentence in this
    // file: no trailing period, an em dash between the clauses. This one
    // carried a period and a full stop where the dash belonged.
    myToasts->show(tr("There's an outline waiting to be extruded, and it belongs to the "
                      "plane it was drawn on — press E to turn it into a body, or Delete "
                      "to discard it, before you change the face you draw on"),
                  Toast::Kind::Failure, false);
    return false;
}

void MainWindow::onLockToFace()
{
    const TopoDS_Face face = myView->selectedFace();
    if (face.IsNull()) return;
    lockToFace(face);
}

bool MainWindow::lockToFace(const TopoDS_Face& face)
{
    if (face.IsNull()) return false;
    if (!canChangeSketchPlane()) return false;

    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) {
        myToasts->show(tr("This face isn't flat, so it can't hold an outline. "
                          "Pick a flat face and try again."),
                      Toast::Kind::Failure, false);
        return false;
    }

    // By value, and the face is dropped here - see lockToFace()'s comment in
    // the header for why holding on to it would be a bug waiting for the
    // user's next boolean.
    //
    // DIRECTION CONVENTION: the stored plane's normal is the face's OUTWARD
    // normal, because extrude sweeps along it (see extrudePendingFace and
    // ExtrudePreview) and a shelf has to come out of the cabinet rather than
    // into it.
    //
    // BRepAdaptor_Surface carries the underlying geometry and its location
    // and nothing else - it never applies TopAbs_Orientation. On a plain
    // BRepPrimAPI_MakeBox three of the six faces are TopAbs_REVERSED, and
    // for those the surface normal points INTO the body. Locking one of them
    // without this flip sweeps the prism straight through the body it is
    // standing on. Reverse it here, once, so no consumer of the sketch plane
    // has to know any of this.
    gp_Pln plane = surface.Plane();
    if (face.Orientation() == TopAbs_REVERSED) {
        // Origin and in-plane X direction preserved, normal flipped: gp_Ax3's
        // (P, N, Vx) constructor keeps Vx as the X direction when it is
        // already perpendicular to N, which it is, so only the normal (and
        // with it the derived Y direction) changes. The grid is symmetric
        // about both, so nothing visible moves.
        plane = gp_Pln(gp_Ax3(plane.Location(), plane.Axis().Direction().Reversed(),
                              plane.Position().XDirection()));
    }
    mySketch.setPlane(plane);
    myFaceLocked = true;
    // setWorkPlaneLocked() first: gridPlane() reads it, and setWorkPlane()
    // below is what actually triggers the rebuild that reads gridPlane() -
    // ordering it after would rebuild once against the stale (unlocked) grid
    // priority and rely on some later camera move to correct it.
    myView->setWorkPlaneLocked(true);
    // One call sets both where clicks land and where the grid is drawn; they
    // are the same value inside the viewport, so they cannot disagree.
    myView->setWorkPlane(plane);
    // Only now, past every refusal above: a lock that was declined must leave
    // the camera exactly where it was, or the user is looking at a face they
    // are not going to be drawing on.
    flyOntoFace(face, plane);
    recordProgress("faceLock.used");

    updateActions();
    statusBar()->showMessage(tr("Locked to this face — outlines you draw now sit on it, "
                                "and extrude square to it"));
    return true;
}

void MainWindow::flyOntoFace(const TopoDS_Face& face, const gp_Pln& plane)
{
    // `plane` is the OUTWARD-oriented plane lockToFace() has already derived -
    // reversed where the face is TopAbs_REVERSED, which is three faces in six
    // on a plain box. Re-deriving it from the face here would be a second copy
    // of that rule, and the wrong half of it is what sends a prism through the
    // body it stands on (see lockToFace).
    Bnd_Box box;
    BRepBndLib::Add(face, box);
    if (box.IsVoid()) return;

    // frame() gives the target and a distance that fits it - the same framing
    // a double-click on a body uses, so a face-on look is no closer or further
    // than the app's one idea of "framed".
    //
    // The target is the BOUNDING BOX's centre, not the face's centre of mass.
    // The two coincide on anything symmetric and separate on an L-shaped or
    // tapered face, and the box centre is the right one here: the job is to
    // put the whole face on screen, which is a question about its extent.
    // Anything asserting where this lands must derive the box centre too -
    // comparing against a centre of mass would be measuring a different point
    // and calling the gap an error.
    CameraController scratch = myView->camera();
    scratch.frame(box, OcctViewWidget::kFovyDeg);
    // ...and then the direction, which frame() leaves alone.
    scratch.lookFrom(plane.Axis().Direction());

    // Ortho before the flight, for the same reason the gizmo sets it before
    // its own: applyCameraState() runs on the first animation frame, and a
    // look that only squares up once it lands would flash. The user's first
    // orbit hands it back.
    myView->camera().setTemporaryOrtho(true);
    myView->animateTo(scratch.state());
}

void MainWindow::unlockFace()
{
    if (!myFaceLocked) return;
    if (!canChangeSketchPlane()) return;

    const gp_Pln ground(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
    mySketch.setPlane(ground);
    myFaceLocked = false;
    myView->setWorkPlaneLocked(false);
    myView->setWorkPlane(ground);

    updateActions();
    statusBar()->showMessage(tr("Back to the ground — outlines are drawn flat again"));
}

void MainWindow::setSymmetryEnabled(bool on)
{
    // Document-changing by any honest reading (fix round 1, Important 3):
    // it unpairs every existing pairing, bumps revision(), dirties the
    // furniture and arms autosave - even though (see below) it takes no undo
    // checkpoint of its own. Render mode's exit rule is about a document
    // change, not specifically about a checkpoint, so this needs the same
    // one-line exit checkpointDocument()'s eight call sites get structurally
    // rather than being folded into that function itself - see its own
    // comment for why.
    if (myRenderModeOn) setRenderModeEnabled(false);

    // Whatever plane document() already holds - the constructed default
    // (world YZ through the origin) the very first time this ever fires, or
    // whatever setSymmetryPlaneFromFace() last set. Turning it off unpairs
    // everything (DocumentModel::setSymmetry()'s own rule) but takes no
    // checkpoint - a mode switch, not an edit - so it is not itself
    // undoable; it still bumps revision(), which is what tells autosave and
    // the dirty star that the manifest's own "symmetry" block changed.
    myDocument.setSymmetry(on, myDocument.symmetryPlane());
    myView->setSymmetryIndicator(on, myDocument.symmetryPlane());

    updateActions();
    emit documentChanged();
    statusBar()->showMessage(
        on ? tr("Mirror on — new bodies get a mirrored twin")
           : tr("Mirror off — bodies keep their own shape now"));
}

bool MainWindow::setSymmetryPlaneFromFace(const TopoDS_Face& face)
{
    if (face.IsNull()) return false;

    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) {
        myToasts->show(tr("This face isn't flat, so it can't hold the mirror plane. "
                          "Pick a flat face and try again."),
                      Toast::Kind::Failure, false);
        return false;
    }

    // The same outward-orientation fix lockToFace() carries, for the same
    // reason: BRepAdaptor_Surface never applies TopAbs_Orientation, and on a
    // plain box three of six faces are REVERSED with their raw plane normal
    // pointing into the body. It does not actually change what the MIRROR
    // does here - SetMirror(gp_Ax2) treats a plane and its own reverse
    // identically - but a plane captured with an arbitrarily-flipped normal
    // is a needless inconsistency the next reader of this value would have
    // to rediscover is harmless.
    gp_Pln plane = surface.Plane();
    if (face.Orientation() == TopAbs_REVERSED) {
        plane = gp_Pln(gp_Ax3(plane.Location(), plane.Axis().Direction().Reversed(),
                              plane.Position().XDirection()));
    }

    // A plane change invalidates every existing pairing's meaning - each one
    // was computed against the OLD plane, and a subsequent twin-follow edit
    // mirrored about the new one would silently teleport the twin. Unpair
    // FIRST, so setSymmetry() below is not what a caller has to trust to
    // have done it. Only reported when it actually changed anything.
    const bool hadPairings = myDocument.unpairAll();

    myDocument.setSymmetry(true, plane);
    myView->setSymmetryIndicator(true, plane);
    // Blocked - fix round 1: this function already performs everything
    // setSymmetryEnabled(true) would (the two lines just above, plus the
    // updateActions()/documentChanged() below), so an UNBLOCKED setChecked()
    // re-entered that slot and redid all of it a second time, purely by
    // accident of which action happened to still read unchecked. Every
    // other resync of this action's checked state (updateActions() itself)
    // already goes through QSignalBlocker for the same reason.
    if (mySymmetryAction && !mySymmetryAction->isChecked()) {
        const QSignalBlocker blocker(mySymmetryAction);
        mySymmetryAction->setChecked(true);
    }

    updateActions();
    emit documentChanged();
    const QString message = hadPairings ? tr("Mirror plane moved — bodies unpaired")
                                        : tr("Mirror plane set to this face");
    statusBar()->showMessage(message);
    // No checkpoint behind this (a plane change is a mode switch, not an
    // edit - the same rule turning symmetry off follows), so no Undo either.
    if (hadPairings) myToasts->show(message, Toast::Kind::Note, false);
    return true;
}

void MainWindow::onSetSymmetryPlane()
{
    const TopoDS_Face face = myView->selectedFace();
    if (face.IsNull()) return;
    setSymmetryPlaneFromFace(face);
}

// --- the mirror plane placement gesture (Milestone 4, Phase 3) -------------

bool MainWindow::mirrorPlacementEnvironmentOk() const
{
    // The same three terms canPullSelectedFace() opens with, for the same
    // reasons.
    if (mySketching || hasPendingFace() || myRenderModeOn) return false;
    // And the two this predicate was MISSING, which its Phase-4 sibling
    // linkGestureEnvironmentOk() already carried for exactly this reason:
    // a furniture must actually be open, and a compare session is not a
    // place to be committing to a document.
    //
    // Without myShowingInitScreen the gesture SURVIVED the editor-to-
    // selector handoff. Nothing on that path cancels it - showInitScreen()
    // and openFurniture() do not, and resyncView() clears solids, outlines
    // and the plane indicator but not this gesture's own objects - so the
    // self-cancel below found the environment still valid, kept the ids it
    // captured, and openFurniture()'s fitAll() redrew the old plane over a
    // DIFFERENT furniture. DocumentModel::myNextId restarts at 1 for every
    // fresh document, so those stale ids resolve to real, unrelated bodies:
    // Enter would have paired bodies the user never selected, through a
    // plane computed from a document that is no longer open. With the term
    // here, refreshMirrorPlacement()'s existing self-cancel discipline kills
    // the gesture on the handoff itself, with no second mechanism to keep in
    // step.
    if (myShowingInitScreen || isCompareOpen()) return false;
    // Body mode explicitly - selectedSolidIds() reports the owning body of a
    // selected FACE too, so without this the gesture could stand next to a
    // face selection and collide with the pull arrow's own drag. The same
    // mode check transformableBodyId() carries, for the same reason.
    return myView->selectionMode() == OcctViewWidget::SelectionMode::Solid;
}

bool MainWindow::canBeginMirrorPlacement() const
{
    if (!mirrorPlacementEnvironmentOk()) return false;
    // A gesture already running cannot be begun a second time on top of
    // itself.
    if (myView->mirrorPlacementActive()) return false;
    return !myView->selectedSolidIds().empty();
}

void MainWindow::refreshMirrorPlacement()
{
    // Fix round 1: disjointness held only at the press that began the
    // gesture. Recomputed on every appStateChanged - a mode switch, a
    // sketch starting, render mode turning on - and the gesture ends the
    // moment its own environment stops holding, rather than leaving a
    // stale chip up while a second gizmo claims the same keys.
    //
    // MUST NOT call updateActions(): this runs AS A SLOT on
    // appStateChanged, and updateActions() is what emits it - calling it
    // from here would re-enter the very emission this slot is already
    // inside. myView->cancelMirrorPlacement() alone is the view-side
    // teardown with no such call; the chip's own refresh() is connected to
    // the SAME signal from inside buildOverlay(), which the constructor now
    // genuinely runs AFTER this connection is made (see the connect site),
    // so Qt's own connection-order guarantee is what lets the chip observe
    // mirrorPlacementActive() already false within this one emission.
    if (myView->mirrorPlacementActive() && !mirrorPlacementEnvironmentOk())
        myView->cancelMirrorPlacement();
}

QString MainWindow::mirrorPlacementRefusalText() const
{
    // Reason-specific, the established convention this app already follows
    // for Lock to Face's own planeReason/sketchReason pair and the bevel
    // refusals - fix round 1 (Task 3.2 review, Finding 3): a single fixed
    // string for every way canBeginMirrorPlacement() can fail named the
    // wrong obstacle as often as the right one. Checked in the same order
    // canBeginMirrorPlacement() itself asks them, so this can never name a
    // reason that predicate did not actually refuse on.
    if (mySketching)
        return tr("Unavailable while you're drawing — press Enter to close this outline, "
                  "or Esc to cancel it");
    if (hasPendingFace())
        return tr("Unavailable while an outline is waiting — press E to extrude it, "
                  "or Delete to discard it");
    if (myRenderModeOn) return tr("Unavailable in render mode — exit it first");
    if (myShowingInitScreen) return tr("Open a furniture first");
    if (isCompareOpen())
        return tr("Unavailable while comparing versions — close the compare pane first");
    if (myView->selectionMode() != OcctViewWidget::SelectionMode::Solid)
        return tr("Switch to body selection, then select one or more bodies to mirror");
    return tr("Select one or more bodies to mirror");
}

void MainWindow::onSymmetryActionTriggered()
{
    // S pressed again while a gesture is already live backs out of it - the
    // same outcome Esc gives, and the cleanest possible answer to fix round
    // 1's Finding 3 for this specific case: a fixed "Select one or more
    // bodies" message was actively WRONG here (bodies are in fact selected;
    // the real obstacle is the gesture itself), and cancelling needs no
    // message to be accurate. Checked first, or it would fall through to
    // the "begin" branch and immediately refuse on
    // canBeginMirrorPlacement()'s own "already active" guard.
    if (myView->mirrorPlacementActive()) {
        cancelMirrorPlacement();
        return;
    }

    // No symmetryOn() branch any more. S means "begin a placement",
    // whether or not mirroring is already on: pairWithMirror() skips a body
    // that already carries a twin and says so in its own toast, so a second
    // placement over live mirroring pairs the bodies that are NOT yet
    // paired - which is the only way to add bodies to an existing mirror
    // and the ledger's own parked gap. Turning mirroring off is
    // mySymmetryOffAction's job alone (see buildActions()).
    if (!canBeginMirrorPlacement()) {
        const QString reason = mirrorPlacementRefusalText();
        statusBar()->showMessage(reason);
        if (myToasts) myToasts->show(reason, Toast::Kind::Failure, false);
        // Reverts the action's own optimistic checked-flash: nothing about
        // document().symmetryOn() changed, so the resync inside
        // updateActions() puts the checkbox back to unchecked.
        updateActions();
        return;
    }

    myView->beginMirrorPlacement(myView->selectedSolidIds());
    // Same reason as the refusal above: document().symmetryOn() is still
    // false at this point (Enter is what will turn it on, not this click),
    // so this resyncs the checkbox back to unchecked for the whole
    // gesture - the floating plane and its chip are the gesture's real
    // visual cue, not the menu checkmark.
    updateActions();
    statusBar()->showMessage(
        tr("Placing the mirror plane — X Y Z aim, drag to move, Enter mirrors, Esc cancels"));
}

bool MainWindow::confirmMirrorPlacement()
{
    if (!myView->mirrorPlacementActive()) return false;

    const std::vector<int> ids = myView->mirrorPlacementIds();
    const gp_Pln plane = myView->mirrorPlacementPlane();

    // pairWithMirror() checkpoints ITSELF (see its own header comment on
    // DocumentModel.h), so render mode's exit has to run BEFORE the call
    // rather than through checkpointDocument()'s usual choke point - the
    // same explicit-exit rule setSymmetryEnabled() and
    // setSymmetryPlaneFromFace() already follow for their own document
    // changes that do not go through checkpointDocument() either. Defensive
    // rather than load-bearing: canBeginMirrorPlacement() already refuses to
    // begin the gesture at all while render mode is on, so this can only
    // matter if render mode was somehow entered mid-gesture - but every
    // other document-changing route in this file carries the same explicit
    // line regardless of whether its own entry point already guards it.
    if (myRenderModeOn) setRenderModeEnabled(false);

    const DocumentModel::PairResult result = myDocument.pairWithMirror(ids, plane);
    myView->endMirrorPlacement();

    if (result.paired == 0) {
        // A genuine no-op: pairWithMirror() took no checkpoint and changed
        // nothing - every id was either invalid, straddling the plane,
        // already paired, already linked (Milestone 4's own v1 exclusion -
        // fix round 1, Finding 1), or refused by its own mirrorShape() call.
        // Never-silent-failure applies to a document mutation that can
        // genuinely net zero, the same law BooleanResult::ok already
        // enforces for a failed boolean.
        const QString reason =
            tr("Nothing to mirror — every body picked sits on the plane, is "
              "already paired, is already linked, or couldn't be mirrored");
        statusBar()->showMessage(reason);
        if (myToasts) myToasts->show(reason, Toast::Kind::Failure, false);
        updateActions();
        // Fix round 1 (Task 3.2 review, Finding 4): NO documentChanged()
        // here - pairWithMirror() took no checkpoint and changed nothing on
        // this path (confirmed by reading it: the checkpoint is gated on at
        // least one id actually pairing), so this is a genuine no-op and
        // announcing a document change that did not happen is an inaccurate
        // signal, whatever its two listeners currently do with it.
        return false;
    }

    resyncView();
    myView->clearSelection();
    updateActions();
    emit documentChanged();
    recordProgress("mirror.completed");

    // The paired count leads, plurals written out; a second sentence names
    // the ones left unpaired, and why, only when the skip lists are
    // actually non-empty - straddling, already-paired, already-linked and
    // kernel-refused share one honest sentence rather than four, per this
    // task's own ruling. skippedLinked (fix round 1, Finding 1) is
    // Milestone 4's own v1 exclusion, read here for the first time - it
    // existed on PairResult since Task 4.1 specifically for this, and a
    // linked body silently vanishing from both the count and the message
    // is exactly the never-silent-failure violation the review caught.
    const int skipped = static_cast<int>(result.skippedStraddling.size() +
                                         result.skippedAlreadyPaired.size() +
                                         result.skippedFailed.size() +
                                         result.skippedLinked.size());
    QString message = result.paired == 1 ? tr("1 body mirrored")
                                         : tr("%1 bodies mirrored").arg(result.paired);
    if (skipped > 0) {
        message += skipped == 1
                       ? tr(" — 1 body stayed unpaired: on the plane, already "
                           "paired, already linked, or too complex to mirror")
                       : tr(" — %1 bodies stayed unpaired: on the plane, already "
                           "paired, already linked, or too complex to mirror")
                             .arg(skipped);
    }
    // Undo pops pairWithMirror()'s own checkpoint, restoring the document
    // to exactly the state it held before this call - "one undo removes
    // everything" (this task's own requirement), because the checkpoint
    // covers every twin the call built in one commit.
    if (myToasts) myToasts->show(message, Toast::Kind::Note, true);
    statusBar()->showMessage(message);
    return true;
}

void MainWindow::cancelMirrorPlacement()
{
    if (!myView->mirrorPlacementActive()) return;
    myView->cancelMirrorPlacement();
    updateActions();
    statusBar()->showMessage(tr("Mirror placement cancelled"));
}

// --- linked copies (Milestone 4, Task 4.2) ----------------------------------

bool MainWindow::linkGestureEnvironmentOk() const
{
    // The same three terms canPullSelectedFace() opens with, for the same
    // reasons - a furniture must actually be open, and neither a sketch in
    // progress nor a waiting outline should let a body underneath either one
    // be duplicated, linked or unlinked out from under it.
    if (mySketching || hasPendingFace() || myShowingInitScreen) return false;
    // Body mode explicitly - selectedSolidIds() reports the owning body of a
    // selected face or edge too, so without this a face/edge selection could
    // satisfy a count check that means something different in body mode. The
    // same mode check transformableBodyId() and mirrorPlacementEnvironmentOk()
    // each carry, for the same reason.
    return myView->selectionMode() == OcctViewWidget::SelectionMode::Solid;
}

int MainWindow::duplicateLinkedSourceId() const
{
    if (!linkGestureEnvironmentOk()) return 0;
    const std::vector<int> ids = myView->selectedSolidIds();
    if (ids.size() != 1) return 0;
    const int id = ids.front();
    // The v1 mirror/link exclusion (DocumentModel.h), enforced from this
    // side too: createLinkedCopy() itself refuses a mirror-paired source, and
    // checking it here is what lets the disabled tooltip name the real
    // reason instead of a kernel error nobody sees. An already-LINKED source
    // is fine - the copy simply joins the existing group.
    if (myDocument.symmetryOn() && myDocument.twinOf(id) > 0) return 0;
    return id;
}

bool MainWindow::canLinkSelected() const
{
    if (!linkGestureEnvironmentOk()) return false;
    const std::vector<int> ids = myView->selectedSolidIds();
    if (ids.size() < 2) return false;
    for (int id : ids) {
        // linkExisting() itself refuses an id already in a group and a
        // mirror-paired id (the same v1 exclusion above) - checked here so
        // the disabled tooltip can say which.
        if (myDocument.isLinked(id)) return false;
        if (myDocument.symmetryOn() && myDocument.twinOf(id) > 0) return false;
    }
    return true;
}

int MainWindow::unlinkTargetId() const
{
    if (!linkGestureEnvironmentOk()) return 0;
    const std::vector<int> ids = myView->selectedSolidIds();
    if (ids.size() != 1) return 0;
    const int id = ids.front();
    return myDocument.isLinked(id) ? id : 0;
}

int MainWindow::resyncLinkGroupView(int editedId)
{
    DocumentModel::LinkGroup group;
    if (!myDocument.linkGroupOf(editedId, group)) return 0;
    int others = 0;
    for (const auto& kv : group.placement) {
        if (kv.first == editedId) continue;
        myView->displaySolid(kv.first, myDocument.shapeOf(kv.first));
        ++others;
    }
    return others;
}

bool MainWindow::duplicateLinkedCopy()
{
    const int sourceId = duplicateLinkedSourceId();
    if (sourceId <= 0) {
        // canDuplicateLinked() already gates the action itself, so a real
        // user cannot reach this through a click - but the mirror/link
        // exclusion is a MEANINGFUL, nameable reason, not a malformed-input
        // guard like pullFaceBy()'s own silent early-outs, and
        // never-silent-failure applies to a caller driving this directly
        // (Ctrl+D, or a test) exactly as it does to a click. Reported only
        // for that one specific, nameable reason - an empty or wrong-mode
        // selection has nothing worth naming beyond what the disabled
        // tooltip already says.
        const std::vector<int> ids = myView->selectedSolidIds();
        if (ids.size() == 1 && myDocument.symmetryOn() && myDocument.twinOf(ids.front()) > 0) {
            myToasts->show(tr("Couldn't duplicate that body linked — it's mirrored, and a "
                              "body can't be both at once. Turn mirroring off first, or "
                              "duplicate its twin instead"),
                          Toast::Kind::Failure, false);
            statusBar()->showMessage(tr("Duplicate linked refused — nothing was changed"));
        }
        return false;
    }

    // DocumentModel::createLinkedCopy() checkpoints ITSELF (see its own
    // header comment, the same shape pairWithMirror()'s own header
    // documents) - render mode's exit therefore has to run BEFORE the call
    // rather than through checkpointDocument()'s usual choke point, the same
    // explicit-exit rule every other self-checkpointing commit in this file
    // follows.
    if (myRenderModeOn) setRenderModeEnabled(false);

    // A visible offset - one grid step along X and Y - so the copy never
    // lands exactly on top of its source. snapStep() is the same length the
    // drawn grid and Snap to Grid itself use, so the number stays meaningful
    // regardless of unit or of whether Snap to Grid happens to be on.
    const double step = myView->snapStep();
    gp_Trsf offset;
    offset.SetTranslation(gp_Vec(step, step, 0.0));

    const DocumentModel::LinkResult result = myDocument.createLinkedCopy(sourceId, offset);
    if (!result.ok) {
        // Unreachable in practice - duplicateLinkedSourceId() already ruled
        // out the mirror/link exclusion above, and an unknown id cannot
        // reach here either (sourceId came from the live selection). What
        // is left is a genuine kernel-level transform failure - the same
        // "unreachable in practice, kept defensive" idiom
        // commitReplaceBody()'s own twin-mirror comment follows.
        qWarning("Duplicate linked failed: %s", result.error.c_str());
        myToasts->show(tr("Couldn't duplicate that body — the geometry engine turned "
                          "the copy down. Try a different body"),
                      Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("Duplicate linked refused — nothing was changed"));
        return false;
    }

    myView->displaySolid(result.id, myDocument.shapeOf(result.id));
    // Body mode is already active (linkGestureEnvironmentOk() required it) -
    // selecting the copy here is what makes refreshTransformGizmo() (an
    // appStateChanged slot) attach the transform gizmo to it, the same
    // machinery an ordinary click already drives.
    myView->setSelectedSolids({result.id});
    recordProgress("link.duplicated");

    updateActions();
    emit documentChanged();

    DocumentModel::LinkGroup group;
    myDocument.linkGroupOf(result.id, group);
    // Always >= 2 - createLinkedCopy() either founds a fresh group of
    // exactly two or extends an existing one, so this is never the singular
    // "1 body" a plural rule would otherwise have to special-case.
    const int memberCount = static_cast<int>(group.placement.size());
    const QString message = tr("%1 duplicated — %2 bodies now linked")
                                 .arg(QString::fromStdString(myDocument.nameOf(result.id)))
                                 .arg(memberCount);
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

bool MainWindow::linkSelectedBodies()
{
    if (!canLinkSelected()) return false;
    const std::vector<int> ids = myView->selectedSolidIds();

    // linkExisting() checkpoints ITSELF - the same explicit render-mode-exit
    // rule duplicateLinkedCopy() follows just above, for the same reason.
    if (myRenderModeOn) setRenderModeEnabled(false);

    const DocumentModel::LinkResult result = myDocument.linkExisting(ids);
    if (!result.ok) {
        // Unreachable in practice - canLinkSelected() already checked every
        // refusal linkExisting() itself can raise beyond a kernel-level
        // transform failure. Kept defensive, the same idiom
        // commitReplaceBody()'s own twin-mirror comment follows; the
        // kernel's own error string is logged, never shown - it is written
        // for this file, not for the user.
        qWarning("Link selected failed: %s", result.error.c_str());
        myToasts->show(tr("Couldn't link those bodies — the geometry engine turned "
                          "the snap down. Try moving them closer together first"),
                      Toast::Kind::Failure, false);
        statusBar()->showMessage(tr("Link selected refused — nothing was changed"));
        return false;
    }

    // Every body but the anchor was just replaced with a shape snapped onto
    // the anchor's own, centre to centre - resync each one's presentation
    // and leave the whole group selected, so the visible snap is unmistakable.
    for (int id : ids) {
        if (id == result.id) continue;
        myView->displaySolid(id, myDocument.shapeOf(id));
    }
    myView->setSelectedSolids(ids);
    recordProgress("link.linked");

    updateActions();
    emit documentChanged();

    const QString message =
        tr("%1 bodies linked — every copy now matches %2")
            .arg(static_cast<int>(ids.size()))
            .arg(QString::fromStdString(myDocument.nameOf(result.id)));
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

bool MainWindow::unlinkSelectedBody()
{
    const int id = unlinkTargetId();
    if (id <= 0) return false;

    DocumentModel::LinkGroup group;
    myDocument.linkGroupOf(id, group);   // true - unlinkTargetId() confirmed isLinked(id)
    const int groupSizeBefore = static_cast<int>(group.placement.size());

    // unlink() checkpoints ITSELF - the same explicit render-mode-exit rule
    // every self-checkpointing commit in this file follows.
    if (myRenderModeOn) setRenderModeEnabled(false);

    if (!myDocument.unlink(id)) return false;   // unreachable - unlinkTargetId() already confirmed this

    // Neither body's shape changed - unlink() only touches bookkeeping - so
    // there is nothing for the viewport to resync.
    updateActions();
    emit documentChanged();
    recordProgress("link.unlinked");

    const QString name = QString::fromStdString(myDocument.nameOf(id));
    // A group of exactly two dissolves outright (DocumentModel::unlink()'s
    // own rule - "no group is a group of one"), so the "N remain linked"
    // branch is never the singular a plural rule would have to special-case.
    const QString message =
        groupSizeBefore > 2
            ? tr("%1 unlinked — %2 bodies remain linked").arg(name).arg(groupSizeBefore - 1)
            : tr("%1 unlinked — no bodies remain linked").arg(name);
    statusBar()->showMessage(message);
    myToasts->show(message, Toast::Kind::Note, true, myDocument.revision());
    return true;
}

void MainWindow::onSelectionChanged()
{
    updateActions();

    const std::size_t count = myView->selectedSolidIds().size();
    statusBar()->showMessage(count == 0   ? tr("Nothing selected")
                             : count == 1 ? tr("1 body selected")
                                          : tr("%1 bodies selected").arg(count));
}

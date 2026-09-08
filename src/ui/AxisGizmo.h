#pragma once
// Unity-style orientation gizmo: colored axis cones around a hub, projected
// live from the camera, each one a button that snaps the view to its axis.
// Painted with QPainter as an overlay child of the viewport - the view cube it
// replaces could only ever look like a box.
//
// It wears the floating-surface family's own card (Theme::paintSurface()),
// like the rail, the drawer, the guide, the balloon and the toast - at the
// family's default radius (8), the same rounding every other card in the
// shell carries. It used to fill itself flat with Theme::viewport() instead,
// on the theory that the panel would disappear against the sky - which it
// did, right up until the viewport painted a ground grid over its own flat
// background colour, after which the flat fill read as a lighter box pasted
// onto the scene. That was the case for an honest card over the fake one, and
// it still is - but the small triangles outside the rounded shape and inside
// the widget's own rect used to need paintSurface()'s own opaque fill just to
// round the corner at all. Since the QOpenGLWidget migration they no longer
// do: paintSurface() paints nothing out there any more, and the pixels behind
// this card's corners are the live scene itself - see Theme.h's paintSurface()
// and makeSurfaceTransparent() for the mechanism. See AxisGizmo.cpp's
// paintEvent() for the whole argument.
//
// Axes and tips, and nothing else. It used to carry a chip below them naming
// the current view, and that chip's job - showing the name, and snapping back
// to the angled view when clicked - moved into the app bar, whose button now
// shows the PROJECTION instead; the angled snap lives on the View menu and
// key 0. The direction name itself still has one source,
// OcctViewWidget::viewDirectionName(); this widget no longer knows it exists.
//
// Clicking an arm does borrow an orthographic look
// (CameraController::setTemporaryOrtho) - an axis view IS a face-on view, and
// perspective convergence is what stops one reading as square. The user's
// first orbit hands it back.
// OCCT first: Handle() is a macro that collides with some Windows headers Qt
// drags in. gp_Dir is here because AxisCard::computeTips() below is THE
// projection both drawings share, and a camera frame is three directions.
#include <gp_Dir.hxx>

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QWidget>

class OcctViewWidget;

// THE CARD'S OWN DRAWING, as numbers - and the reason they live in a header
// rather than in AxisGizmo.cpp's anonymous namespace where they started.
//
// The in-scene Move gizmo (src/ui/TransformGizmo.h) is a PROPORTIONAL COPY of
// this card: same stroke-to-arm, cone-to-arm, ball-to-arm, hub-to-arm and
// letter-to-arm ratios, at whatever arm length the viewport wants. The user's
// ask was "exactly like the card", and a copy that reads its own transcription
// of these numbers is a copy until somebody edits one of them. So there is ONE
// set, both drawings read it, and gui_smoke measures the two RENDERINGS against
// each other so a divergence in how they are used fails as loudly as a
// divergence in what they are.
//
// Every length is in the card's own logical pixels, against kArmPx. The scene
// gizmo multiplies all of them by (its arm length / kArmPx).
//
// AND THE LAYOUT IS SHARED TOO, not only the sizes - computeTips() below. That
// is the correction the first attempt at this needed: sharing the numbers made
// the two drawings agree about how big a cone is and left them disagreeing
// about where anything went. The card is a FLAT projection - every arm laid
// out along the camera's right and up vectors, every cone, ball, letter and
// hub a constant screen size - while the scene gizmo was true 3D geometry, so
// perspective foreshortened an arm pointing at the eye into a stub and drew
// its cone oversized because the cone was nearer. The scene gizmo builds ON
// THE VIEW PLANE through the pivot now, from these same six tips, so at any
// camera angle the two are one drawing at two scales.
namespace AxisCard {

constexpr double kArmPx = 36.0;              // a positive arm, hub to tip
constexpr double kConePx = 9.0;              // the cone's own length back from the tip
constexpr double kBallPx = 5.5;              // the hollow ball on a negative tip
constexpr double kHubPx = 5.0;               // the filled neutral hub
constexpr double kArmStrokePx = 2.0;         // the positive arm's pen
constexpr double kNegativeStrokePx = 1.4;    // the negative stub's thinner pen
constexpr double kNegativeStubStart = 0.35;  // where a negative stub begins, along its arm
constexpr double kConeHalfWidthFactor = 0.55;   // of kConePx, either side of the axis
constexpr double kConeApexFactor = 0.40;        // of kConePx, PAST the tip
constexpr double kLetterOffsetPx = 9.0;         // the letter's anchor, past the tip
// How much lighter the axis letter is drawn than its own arm. A percentage in
// QColor::lighter()'s own units, so both drawings brighten by the same amount.
constexpr int kLetterLighten = 115;

// The hub's colour. Neutral on purpose: the hub belongs to no axis, and
// colouring it would make it look like a fourth handle. A function rather than
// a constant because QColor has no constexpr constructor.
QColor hubColour();

// The font the axis letters are painted with - Theme::badgeFont(), bold. One
// accessor, so the card paints and the scene gizmo sizes from the same
// metrics rather than from two readings of the same intention.
QFont letterFont();

// One axis letter's own drawn INK height in the card's pixels, measured off
// letterFont()'s metrics rather than guessed from a point size. gui_smoke
// checks the card's rendered letter against this, which is what stops the
// ratio pin below being purely relative - two drawings can agree with each
// other while both disagreeing with the font they claim to use.
//
// The glyph is an argument because the three letters do not share an ink
// height: x and z stop at the x-height, y hangs a descender below the
// baseline. A tight bounding box of the actual glyph, never capHeight(),
// which none of the three lowercase letters this card paints ever reaches.
double letterHeightPx(QChar letter);

// The letter font's EM size in the card's pixels - what an in-scene
// AIS_TextLabel's SetHeight() is measured in. The two text engines are asked
// for the same thing in different units and that difference is the whole
// reason this accessor exists: Qt sizes a QFont by POINTS and OCCT sizes a
// label by its em box in PIXELS, so handing OCCT the point size would draw
// the letters at three quarters of the card's, silently and only on a
// machine at this DPI. Cap height and em share one ratio for one face, so
// scaling THIS number is what makes the two INKS the same height.
double letterEmPx();

// One of the six axis ends, as the drawing lays it out.
struct Tip {
    int axis = 0;         // 0=X 1=Y 2=Z
    bool positive = true;
    // The tip's offset from the hub PER UNIT ARM LENGTH, in the camera's own
    // frame: `right` along the camera's right vector, `up` along its up
    // vector. Its magnitude is the axis's own foreshortening, between 0 (the
    // axis points at the eye) and 1 (it lies in the screen plane).
    //
    // The card multiplies these by kArmPx and drops them straight into widget
    // coordinates (negating `up`, since screen y runs downward). The scene
    // gizmo multiplies them by its own arm length and lays them out along the
    // camera's right and up vectors as WORLD offsets from the pivot, which
    // puts every vertex on the view plane and therefore at one depth - so a
    // pinhole projection maps the whole drawing to the screen with a single
    // scale and no foreshortening of its own.
    double right = 0.0;
    double up = 0.0;
    double depth = 0.0;   // along the view direction; larger = farther
};

// THE projection, in the order the card has always built it: X+, X-, Y+, Y-,
// Z+, Z-. Shared rather than reimplemented, because two copies of a
// projection is exactly how the scene gizmo came to be drawing something else.
void computeTips(const gp_Dir& right, const gp_Dir& up, const gp_Dir& view, Tip tips[6]);

}  // namespace AxisCard

class AxisGizmo : public QWidget {
    Q_OBJECT

public:
    explicit AxisGizmo(OcctViewWidget* view, QWidget* parent = nullptr);

    // Screen-space centre of an axis tip: axis 0=X, 1=Y, 2=Z. Exposed so the
    // test suite can click exactly where a user would.
    QPointF tipCenter(int axis, bool positive) const;

signals:
    // A tip was clicked and the camera is on its way to that axis. The gizmo
    // deliberately does not know what anyone makes of that: MainWindow
    // connects this to its own "a named view was used" bookkeeping, and this
    // widget keeps knowing nothing but its OcctViewWidget.
    void viewSnapped();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    QSize sizeHint() const override { return QSize(120, kHeight); }

private:
    // The whole widget now: the projected axes and nothing under them.
    static constexpr int kHeight = 118;

    struct Tip {
        int axis = 0;        // 0=X 1=Y 2=Z
        bool positive = true;
        QPointF screen;      // widget coordinates
        double depth = 0.0;  // along the view direction; larger = farther away
    };

    // The six tips for the current camera pose, unsorted.
    void computeTips(Tip tips[6]) const;
    void snapToAxis(int axis, bool positive);
    // The two appearance values this card cannot re-derive inside
    // paintEvent(): its per-widget font-size stylesheet, and its FIXED size -
    // sizeHint() is a constant here, but wholeDevicePixels() is applied to it
    // once, and setFixedSize() means ViewportOverlay's own rounding is a
    // silent no-op on this widget (see Theme.h). Both are set from here at
    // construction and again on every Theme broadcast.
    void applyTheme();

    OcctViewWidget* myView = nullptr;
    int myHoverAxis = -1;        // -1 none; else axis index
    bool myHoverPositive = true;
};

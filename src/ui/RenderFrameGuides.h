#pragma once
// The picture's edges and its framing lines, drawn over the viewport while
// render mode is on. The user's ask: "add a aspect ratio selected and some
// guides to help me to put right the camera."
//
// It is a QWidget, not scene geometry, and that is the whole design:
//
//  - A framing aid must NOT be in the picture. V3d_View::Dump() and every
//    export read OCCT's own framebuffer, and this paints in Qt's compositor
//    on top of it, so the dim mask and the thirds lines cannot reach a saved
//    image by any route. Drawing them as OCCT geometry would have needed a
//    hide-before-every-export rule, and a rule like that is only ever one
//    call site away from being broken.
//  - It is WA_TransparentForMouseEvents, so orbiting, panning and zooming
//    reach the viewport through it untouched. That attribute excludes the
//    widget AND its whole subtree from hit-testing (CLAUDE.md), which is
//    exactly right here: it has no children and nothing on it is clickable.
//  - It is DIRTY ONLY WHEN THE FRAME MOVES - a resize, an aspect change, a
//    guide change - never per camera move. That matters because one dirty
//    raster overlay repaints every visible overlay in the window, so a
//    frame that repainted with the camera would charge the whole tree's
//    paint cost to every step of an orbit. This one repaints when the user
//    changes the picture's shape and at no other time.
//
// It TRANSLUCENTLY paints, which is the first resting surface in this app to
// do so. That is lawful since the QOpenGLWidget migration and only since
// then: Qt's compositor holds the real GL frame behind an overlay child now,
// so alpha blends against the scene rather than against whatever the driver
// last left in a buffer nobody owned. See CLAUDE.md's TOMBSTONE section for
// the law this retires and why it was a law at all.
//
// It is LOWERED among the viewport's siblings rather than raised: the mask
// dims the scene, and every other sibling over this viewport is a control
// that has to stay readable on top of it. Children of a QOpenGLWidget paint
// over its GL content whatever their sibling order, so lowering costs the
// mask nothing.
//
// Where the frame IS comes from OcctViewWidget::renderFrameRect(), never from
// a second copy of the fitting maths here - the export reproduces that same
// rect, and a guide drawn from its own arithmetic is a guide that can be
// wrong about the picture.
#include <QWidget>

class OcctViewWidget;

class RenderFrameGuides : public QWidget {
    Q_OBJECT

public:
    explicit RenderFrameGuides(OcctViewWidget* viewport);

    // Re-reads the frame and repaints. Called from MainWindow on every
    // appStateChanged and on the viewport's own renderFrameChanged - derived,
    // never a one-shot at the control that moved.
    void refresh();

    // What the frame looks like right now, for the suite: the rect being
    // drawn, in this widget's own coordinates. Not a second derivation - it
    // is the rect this widget last painted.
    QRect framedRect() const { return myFrame; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    OcctViewWidget* myViewport = nullptr;
    QRect myFrame;
};

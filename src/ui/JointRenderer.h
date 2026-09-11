#pragma once
// The joints' own presentation: ghosted hardware drawn where a joint's items
// fall (spec: docs/superpowers/specs/2026-09-10-joinery-design.md) - "dowels
// as cylinders, screws as angled pins, housings as an outlined channel,
// tenons as an outlined block". A half-lap is an outlined block too, over the
// whole crossing: the half removed from each piece, stacked.
//
// GHOSTED because a joint is a PLAN, not material - the wood is not cut, and
// hardware drawn solid would read as something that is there. And SEEN
// THROUGH THE WOOD, which is not the same property and is not bought by the
// transparency: a dowel spans from `centre - axis*depthA` to
// `centre + axis*depthB`, so it sits ENTIRELY INSIDE the two pieces it joins,
// and it is the BODIES that are opaque. Drawn in a layer that depth-tests
// against them, the hardware draws nothing at all however transparent it is.
// It therefore goes in a layer of its own that clears depth - see
// OcctViewWidget::initializeViewer() for why that layer must be Immediate,
// and why it sits below the transform gizmo's.
//
// It owns no policy: what to draw and when is OcctViewWidget's and
// MainWindow's business (render mode, for one, is gated in
// OcctViewWidget::showJoints()). This class takes derivations and puts them
// on screen, the same split GridRenderer and the gizmo renderers keep.
#include "Joinery.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <TopoDS_Shape.hxx>

#include <QColor>

#include <vector>

class JointRenderer {
public:
    struct Drawing {
        Joinery::Kind kind = Joinery::Kind::Dowel;
        Joinery::Derivation derivation;
    };

    // One piece of hardware as a B-rep solid, and whether it wears an outline.
    struct Piece {
        TopoDS_Shape shape;
        bool outlined = false;
    };
    // The hardware a drawing is made of - the ONE builder, which show()
    // displays. A refused derivation, or an item the kernel will not build,
    // contributes nothing.
    //   - a fastener is a pin from `centre - axis'*depthA` to
    //     `centre + axis'*depthB`, where axis' is the item's axis tilted by its
    //     angleDeg about the run (a pocket screw leans across the joint);
    //   - a housing or tenon is one outlined block over the item's span,
    //     [-depthA, +depthB] along the axis about the contact plane;
    //   - a half-lap (an Overlap contact) is two outlined blocks over the whole
    //     overlap footprint: [0, depthA] and [depthA, depthA + depthB] from the
    //     frame plane, which sits on the lap's own minimum-depth face - the
    //     convention Contact documents. The lap is not centred on that plane.
    static std::vector<Piece> piecesFor(const Drawing& drawing);

    void attach(const Handle(AIS_InteractiveContext)& context);
    // Drops the context and everything built against it, WITHOUT touching the
    // viewer - GizmoRenderer::detach()'s contract, same one caller
    // (OcctViewWidget::releaseGlResources()).
    void detach();
    // The layer the hardware is drawn in. Unset (the viewer refused the
    // joints layer) falls back to Graphic3d_ZLayerId_Topmost, which also
    // clears depth - shared with OCCT's hover highlight, but still drawn
    // through the wood, which is the property this class cannot do without.
    void setZLayer(Graphic3d_ZLayerId layer) { myLayer = layer; }
    Graphic3d_ZLayerId drawLayer() const;

    // Replaces whatever was drawn. TRUE when the screen actually changed - the
    // caller owns the frame, GridRenderer's own contract.
    //
    // A no-op returning FALSE when `drawings` equal what is already drawn,
    // compared on everything the pieces are built from (kind, the contact's
    // type and frame, every item's position, axis, sizes, depths and angle).
    // The callers that re-show joints do so on every state change, and a
    // rebuild re-meshes every piece; an unchanged joint must cost a compare.
    bool show(const std::vector<Drawing>& drawings);
    // TRUE when something was actually removed - show()'s own contract.
    bool clear();
    // Recolours what is on screen IN PLACE - fill and outline - without
    // rebuilding or re-meshing anything. An Appearance colour-wheel drag
    // broadcasts a theme change on every mouse move. A no-op when nothing is
    // drawn, so it cannot make hardware appear. TRUE when it recoloured
    // something.
    bool reapplyTheme();

    // How many JOINTS are drawn - a joint that produced at least one piece of
    // hardware. Not the number of AIS objects: a three-dowel joint is one
    // joint, and itemsShown() says three.
    int shown() const { return myJointsDrawn; }
    int itemsShown() const { return static_cast<int>(myObjects.size()); }
    // The solids actually displayed, in display order - what a measurement of
    // "where is the hardware" has to read, rather than a second build of it.
    std::vector<TopoDS_Shape> shapes() const;
    // How many times the pieces have been built. The change checks above are
    // otherwise invisible from outside: a rebuild of identical pieces looks
    // exactly like no rebuild in every pixel.
    int buildCount() const { return myBuildCount; }

private:
    // Builds myDrawings into myObjects. Reads Theme::accent() fresh on every
    // call - nothing here caches a colour across a themeChanged broadcast.
    void build();
    void addPiece(const Piece& piece, const QColor& colour);

    Handle(AIS_InteractiveContext) myContext;
    std::vector<Handle(AIS_Shape)> myObjects;
    std::vector<Drawing> myDrawings;
    int myJointsDrawn = 0;
    int myBuildCount = 0;
    Graphic3d_ZLayerId myLayer = Graphic3d_ZLayerId_UNKNOWN;
};

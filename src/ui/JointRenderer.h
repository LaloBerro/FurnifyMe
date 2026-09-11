#pragma once
// The joints' own presentation: ghosted hardware drawn where a joint's items
// fall (spec: docs/superpowers/specs/2026-09-10-joinery-design.md). Dowels and
// screws are cylinders, housings and tenons are blocks.
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
#include <AIS_InteractiveObject.hxx>
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
    bool show(const std::vector<Drawing>& drawings);
    // TRUE when something was actually removed - show()'s own contract.
    bool clear();
    // Rebuilds what is on screen from the drawings it was built with. The
    // accent is baked into the AIS objects at build time, so without this a
    // theme edit would leave live hardware wearing the old colour until
    // something happened to call show() again. A no-op when nothing is drawn,
    // so it cannot make hardware appear. TRUE when it rebuilt something.
    bool reapplyTheme();

    // How many JOINTS are drawn - a joint that produced at least one piece of
    // hardware. Not the number of AIS objects: a three-dowel joint is one
    // joint, and itemsShown() says three.
    int shown() const { return myJointsDrawn; }
    int itemsShown() const { return static_cast<int>(myObjects.size()); }

private:
    // Builds myDrawings into myObjects. Reads Theme::accent() fresh on every
    // call - nothing here caches a colour across a themeChanged broadcast.
    void build();
    void addSolid(const TopoDS_Shape& shape, const QColor& colour);

    Handle(AIS_InteractiveContext) myContext;
    std::vector<Handle(AIS_InteractiveObject)> myObjects;
    std::vector<Drawing> myDrawings;
    int myJointsDrawn = 0;
    Graphic3d_ZLayerId myLayer = Graphic3d_ZLayerId_UNKNOWN;
};

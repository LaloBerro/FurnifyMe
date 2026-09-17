#pragma once
// THE ROLE BADGES a boolean gesture puts on the wood.
//
// The user's report is the whole reason this exists: "i cant decide with one
// substract and which one keep". Before this, which body survived a Subtract
// was decided by `std::sort(ids)` - the LOWER DOCUMENT ID, so the answer
// depended on the order the bodies were drawn in, possibly months earlier,
// and no control anywhere could change it.
//
// The picked design (option B of the mockup round) puts the answer ON THE
// BODIES rather than in a list on a chip: one badge per body in the gesture,
// reading KEEP or REMOVE, and a click on a badge flips which body is which.
// Nothing to read off a panel and match back to wood by name.
//
// Drawn IN THE SCENE, like the Re-Measure pin and the dimension's own boxed
// number, rather than painted over the viewport: a badge names a specific
// body, and one painted at a fixed window position would slide off the body
// it names on the first orbit. Screen-SIZED through
// Graphic3d_TMF_ZoomRotatePers, so it reads the same at every zoom - under
// that persistence the local frame is the screen's and one local unit is one
// device pixel at the anchor's depth, which is what lets the pill below be
// laid out in plain pixel numbers.
//
// Like every renderer in this directory it never redraws the viewer: show()
// and clear() return whether anything on screen actually changed, and
// OcctViewWidget asks for the frame. An orbit that moves no badge and changes
// no role costs one comparison.
#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <gp_Pnt.hxx>

#include <string>
#include <vector>

class BooleanBadgeRenderer {
public:
    // How near the cursor has to be, in logical pixels, for a badge to claim
    // the press. Generous by this app's standards (the gizmo handles use 14,
    // the resize marks 9) because this one is a pure CLICK TARGET rather than
    // a drag handle - there is nothing to aim along, and a missed click here
    // silently re-picks the selection and ends the gesture.
    static constexpr double kGrabRadiusPx = 22.0;

    // The pill's half-extents in device pixels. Wide enough for "REMOVE" at
    // the badge text height without the word touching the border.
    static constexpr double kHalfWidthPx = 34.0;
    static constexpr double kHalfHeightPx = 12.0;

    struct Badge {
        gp_Pnt at;          // where it stands, in world coordinates
        int bodyId = 0;     // the body it names - what a click reports, since
                            // the badges are rebuilt on every camera move and
                            // an index read one event later could mean
                            // another body
        bool keep = false;  // the one body kept, against the ones consumed
        std::string text;   // the word painted in it - the caller's, so the
                            // vocabulary sweep can reach it through
                            // MainWindow rather than through a string this
                            // file invents
    };

    void attach(const Handle(AIS_InteractiveContext)& context);
    // GridRenderer::detach()'s contract: drops everything without touching
    // the viewer, for OcctViewWidget::releaseGlResources().
    void detach();
    void setZLayer(Graphic3d_ZLayerId layer);

    // Replaces whatever was drawn. TRUE only when something on screen
    // actually changed.
    bool show(const std::vector<Badge>& badges);
    bool clear();
    // Re-reads Theme for its two tokens - a colour edit must move these with
    // everything else, and no widget may cache a colour across the broadcast.
    bool reapplyTheme();

    bool isShowing() const { return !myBadges.empty(); }
    int count() const { return static_cast<int>(myBadges.size()); }
    // Where badge `index` stands, for the caller's own screen-space hit test.
    // False, `out` untouched, when the index is outside the range.
    bool badgePoint(int index, gp_Pnt& out) const;
    // Whether badge `index` is the kept one - so a probe can read the roles
    // back without re-deriving them from the window.
    bool badgeKeeps(int index) const;
    // Which body badge `index` names, or 0 when the index is out of range.
    int badgeBodyId(int index) const;
    // Every word this renderer has painted, for the banned-word sweep. The
    // caller supplies the text, so this reports rather than authors.
    std::vector<std::string> paintedTexts() const;

private:
    bool build();

    Handle(AIS_InteractiveContext) myContext;
    Graphic3d_ZLayerId myLayer = Graphic3d_ZLayerId_UNKNOWN;
    std::vector<Badge> myBadges;
    std::vector<Handle(AIS_InteractiveObject)> myMarks;
};

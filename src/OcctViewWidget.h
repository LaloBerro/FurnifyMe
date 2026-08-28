#pragma once
// OCCT headers first: Handle() is a macro and collides with some Windows headers
// that Qt drags in.
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include "CameraController.h"

#include <QPoint>
#include <QString>
#include <QWidget>

#include <map>
#include <vector>

class AIS_InteractiveObject;

// The Qt <-> OCCT bridge. Hosts a V3d_View on this widget's native window and
// forwards Qt input to the OCCT camera and selector.
class OcctViewWidget : public QWidget {
    Q_OBJECT

public:
    enum class SelectionMode { Solid, Face };

    explicit OcctViewWidget(QWidget* parent = nullptr);
    ~OcctViewWidget() override;

    // Qt must not paint here or it fights OpenGL for the surface.
    QPaintEngine* paintEngine() const override { return nullptr; }

    void displaySolid(int id, const TopoDS_Shape& shape);
    void removeSolid(int id);
    void clearSolids();

    // Presentation state, not document state: it is deliberately not captured by
    // undo, because hiding something is not an edit.
    void setSolidVisible(int id, bool visible);
    bool isSolidVisible(int id) const;

    // Temporary, non-selectable feedback shape (the in-progress sketch).
    void setPreview(const TopoDS_Shape& shape, bool shaded = false);
    void clearPreview();

    void setSelectionMode(SelectionMode mode);
    SelectionMode selectionMode() const { return mySelectionMode; }

    // While sketching, a left click reports a point on `plane` instead of selecting.
    void setSketchMode(bool enabled, const gp_Pln& plane);
    bool sketchMode() const { return mySketchMode; }

    // Snapping applies to points reported while sketching, not to the camera.
    void setSnap(bool enabled, double step);
    bool snapEnabled() const { return mySnapEnabled; }
    double snapStep() const { return mySnapStep; }

    // Document ids of the selected solids, deduplicated (face-mode selection can
    // hit several faces of one solid).
    std::vector<int> selectedSolidIds() const;
    void clearSelection();
    // Replaces the selection with exactly these solids. Refuses to select a
    // hidden solid - showing it again later must never silently resurrect a
    // selection the user did not make.
    void setSelectedSolids(const std::vector<int>& ids);

    // Renders the viewport straight to an image file. Independent of what is on
    // screen or on top of the window, unlike a screen grab.
    bool saveSnapshot(const QString& path);

    void fitAll();
    void setViewAxonometric();
    // Sketching happens on the XY plane, so a true top view makes clicking
    // accurate in a way the angled default cannot.
    void setViewTop();
    void setViewFront();
    void setViewRight();

    static constexpr double kFovyDeg = 45.0;

    CameraController& camera() { return myCamera; }

    void setViewCubeVisible(bool visible);
    void setWireframe(bool wireframe);
    bool isWireframe() const { return myWireframe; }
    // True if this solid's presentation is actually displayed in wireframe right
    // now - queries the live AIS state rather than the requested mode above, so
    // a solid silently reverting to shaded during a resync is observable even if
    // myWireframe itself was never touched.
    bool isSolidWireframe(int id) const;

signals:
    void sketchPointPicked(const gp_Pnt& point);
    // Live cursor position on the sketch plane, already snapped - drives the
    // rubber band and the coordinate readout.
    void sketchCursorMoved(const gp_Pnt& point);
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void initializeViewer();
    bool pointOnSketchPlane(int px, int py, gp_Pnt& out) const;
    void applySelectionMode(const Handle(AIS_Shape)& shape);
    void applyCameraState();
    void syncCameraFromView();

    Handle(V3d_Viewer) myViewer;
    Handle(V3d_View) myView;
    Handle(AIS_InteractiveContext) myContext;
    Handle(AIS_Shape) myPreview;

    CameraController myCamera;

    std::map<int, Handle(AIS_Shape)> mySolids;

    Handle(AIS_InteractiveObject) myViewCube;
    bool myWireframe = false;

    SelectionMode mySelectionMode = SelectionMode::Solid;
    bool myInitialized = false;
    bool mySketchMode = false;
    gp_Pln mySketchPlane;
    bool mySnapEnabled = true;
    double mySnapStep = 10.0;      // matches the drawn grid

    QPoint myLastPos;
    bool myRotating = false;
    bool myPanning = false;
};

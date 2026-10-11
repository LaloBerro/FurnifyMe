#pragma once

// WHAT A JOINT'S ROW SAYS AND MEASURES (migration Phase 3d): the names, the
// kind line or the reason, the caveat, each cut piece's depth text, its marks
// (in mm, as a fraction of the run, and as a label), and the lines a row
// writes when it cannot draw a ruler. The Qt joints drawer (JointsPanel) and
// the page's model (JointsModel) both read this, so the two drawers cannot
// word a joint differently - SettingsEdits' rule, one drawer over.
//
// No pixel, no font and no widget is in here: where a label goes is each
// drawer's own layout. The tr() context stays "JointsPanel", where these
// sentences were born, so a translation of them keeps applying.
//
// QtCore + OCCT (furnify_session); tested headless by joints_model_tests.
//
// OCCT first, by the Handle() rule - DocumentModel.h carries Joinery.h.
#include "DocumentModel.h"

#include <QString>

#include <string>
#include <vector>

namespace JointRows {

struct Mark {
    double mm = 0.0;
    double at = 0.0;   // along the run, 0..1, clamped as the ruler clamps
    QString text;      // numberOnly(mm) - the shared line names the unit once
};

struct Piece {
    QString name;        // the user's own words
    QString depthText;   // "drill 13.5 mm", "18 × 6 mm"
    bool band = false;   // a housing's or interlock's span instead of ticks
    std::vector<Mark> marks;
    Mark bandStart;
    Mark bandEnd;
    QString numbers;      // "37 · 150 · 263 mm", "141 to 159 mm"
    QString writtenLine;  // what a row writes when it cannot draw: "from the front edge: …"
};

struct Row {
    int jointId = 0;
    int kind = 0;
    bool broken = false;
    bool selected = false;
    bool expanded = false;
    bool fastener = true;
    bool edgeNamed = false;
    QString nameA, nameB;   // the user's own words
    QString kindLine;       // "Dowel, 3 × 8 mm" - or "Broken — <reason>"
    QString caveat;
    QString edge;           // the reference edge's word, or the sentence saying none applies
    QString unit;
    double run = 0.0;
    double inset = 0.0;
    double depthA = 0.0;
    double depthB = 0.0;
    double width = 0.0;
    std::vector<Piece> pieces;   // the CUT pieces - a housing's housed piece has none
    QString readoutText;         // every number the row carries, open or not
    QString sharedDrawn;         // the line under the rulers
    QString sharedWritten;       // the line under written numbers ('' for a housing)
};

// One joint's row. A derivation that refused carries no pieces, no numbers
// and no caveat - a broken joint shows no stale measurement.
Row content(const DocumentModel& doc, const DocumentModel::Joint& joint, const Joinery::Derivation& d,
            int selectedJoint, bool expanded);

// A joint refusal's reason, as the user reads it - THE one mapping from
// Joinery's error string to copy (JointsPanel::reasonText() delegates here).
QString reasonText(const std::string& error);

QString lengthText(double mm);
// Measure's number with its unit taken off - a tick label.
QString numberOnly(double mm);

QString title();
QString emptyText();
QString deleteTooltip();

}  // namespace JointRows

#pragma once

// THE JOINTS DRAWER'S MODEL (migration Phase 3d): the rows the page's Joints
// card lists - JointRows' content per joint, broken joints first - which of
// them are open, and the three things a row can ask for. JointsPanel (the Qt
// drawer, kept until Phase 6) reads the same JointRows, so the two drawers
// word a joint the same way; where a label goes is each drawer's own layout.
//
// Which rows are OPEN is held here, by joint id, and intersected with the
// live joints on every read - the Qt drawer's own rule - so a row stays open
// across the rebuild a document change causes and a broken joint moving to
// the top cannot open a different one. Selecting and deleting stay
// MainWindow's, through Hooks. A hidden drawer reads nothing.
//
// QtCore + OCCT (furnify_session); tested headless by joints_model_tests.
//
// OCCT first, by the Handle() rule - DocumentModel.h carries Joinery.h.
#include "DocumentModel.h"

#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <vector>

class JointsModel : public QObject
{
    Q_OBJECT
    // Per row: id, broken, selected, expanded, fastener, edgeNamed, nameA,
    // nameB, kindLine, caveat, edge, unit, sharedDrawn, sharedWritten, pieces.
    // Per piece: name, depthText, band, marks [{at, text}], bandStart {at,
    // text}, bandEnd {at, text}, writtenLine.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(bool live READ live NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QVariantMap words READ words CONSTANT)

public:
    struct Hooks
    {
        std::function<void(int)> select;
        std::function<bool(int)> remove;
        std::function<void()> close;
    };

    explicit JointsModel(Hooks hooks, QObject* parent = nullptr);

    QVariantList rows() const { return myRows; }
    bool live() const { return myLive; }
    bool enabled() const { return myEnabled; }
    QVariantMap words() const;

    // The document and its joints' derivations (MainWindow::jointDerivations(),
    // a copy, in the document's joint order), the selected joint, whether a
    // furniture is open, whether a row may delete (not sketching, not render
    // mode), and whether the drawer is on screen - a hidden one reads nothing.
    void setState(const DocumentModel* doc, std::vector<Joinery::Derivation> derivations, int selectedJoint,
                  bool live, bool enabled, bool shown);

public slots:
    // A row's click: opens or closes it, and selects its joint.
    void activate(int jointId);
    void remove(int jointId);
    void close();
    bool isExpanded(int jointId) const { return myExpanded.contains(jointId); }

signals:
    void changed();

private:
    void rebuild();

    Hooks myHooks;
    const DocumentModel* myDocument = nullptr;
    std::vector<Joinery::Derivation> myDerivations;
    int mySelected = 0;
    bool myLive = false;
    bool myEnabled = false;
    bool myShown = true;
    QSet<int> myExpanded;
    QVariantList myRows;
};

// The gesture chips' face and their bridge to the page (migration Phase 4a),
// on Qt's offscreen platform - no window, no GPU, no page. A chip stays the
// state; what is pinned here is that its face describes its REAL controls,
// that an edit from the page lands in those controls exactly as a click would,
// that a commit carries the page's values in before it runs, and that a chip
// drawn elsewhere hides its controls and lets the pointer through.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "ChipFace.h"
#include "Theme.h"
#include "web/ChipsBridge.h"

#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>

#include <cstdio>

namespace {
int gFailures = 0, gChecks = 0;
void check(bool ok, const char* what)
{
    ++gChecks;
    std::printf("[%s] %s\n", ok ? " ok " : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

QVariantMap partNamed(const QVariantMap& face, const QString& id)
{
    for (const QVariant& row : face.value(QStringLiteral("rows")).toList())
        for (const QVariant& part : row.toList())
            if (part.toMap().value(QStringLiteral("id")).toString() == id) return part.toMap();
    return {};
}
}  // namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    Theme::apply(app);

    QWidget viewport;
    viewport.resize(800, 600);
    auto* chip = new QWidget(&viewport);
    auto* width = new QLineEdit(&viewport);
    width->setToolTip(QStringLiteral("How wide each slat is, in mm"));
    auto* flip = new QPushButton(QStringLiteral("Flip"), &viewport);
    flip->setToolTip(QStringLiteral("Turn the slats the other way"));
    auto* keep = new QCheckBox(QStringLiteral("Keep the other bodies"), chip);
    viewport.show();
    chip->show();

    int commits = 0, cancels = 0;
    QString seenAtCommit;
    ChipFace face(QStringLiteral("slats"), chip);
    face.addField(QStringLiteral("width"), width);
    face.addButton(QStringLiteral("flip"), flip);
    face.addButton(QStringLiteral("keep"), keep);
    face.setActions([&] { ++commits; seenAtCommit = width->text(); }, [&] { ++cancels; });
    face.setDescriber([&] {
        return QVariantList{
            QVariantList{ChipFace::text(QStringLiteral("Subtract"), QStringLiteral("title")),
                         face.field(QStringLiteral("width"), QStringLiteral("Width")),
                         face.button(QStringLiteral("flip")), face.button(QStringLiteral("keep"))},
            QVariantList{ChipFace::text(QStringLiteral("Enter applies — Esc cancels"), QStringLiteral("hint"))},
        };
    });

    // --- the face describes the real controls ---------------------------------
    width->setText(QStringLiteral("40"));
    QVariantMap f = face.face();
    const QVariantMap w = partNamed(f, QStringLiteral("width"));
    check(w.value(QStringLiteral("kind")).toString() == QLatin1String("field") &&
              w.value(QStringLiteral("value")).toString() == QLatin1String("40") &&
              w.value(QStringLiteral("label")).toString() == QLatin1String("Width") &&
              w.value(QStringLiteral("tip")).toString().startsWith(QLatin1String("How wide")),
          "a field part carries the Qt field's value, its label and its tooltip");
    const QVariantMap fl = partNamed(f, QStringLiteral("flip"));
    check(fl.value(QStringLiteral("kind")).toString() == QLatin1String("button") &&
              fl.value(QStringLiteral("text")).toString() == QLatin1String("Flip") &&
              fl.value(QStringLiteral("enabled")).toBool(),
          "a push button is a button part with its text and enabled state");
    const QVariantMap k = partNamed(f, QStringLiteral("keep"));
    check(k.value(QStringLiteral("kind")).toString() == QLatin1String("toggle") &&
              !k.value(QStringLiteral("checked")).toBool(),
          "a checkable button is a toggle part carrying its checked state");
    check(f.value(QStringLiteral("rows")).toList().size() == 2 &&
              f.value(QStringLiteral("rows")).toList().at(1).toList().at(0).toMap().value(QStringLiteral("tone")) ==
                  QStringLiteral("hint"),
          "rows keep the describer's order, a text part its tone");

    // --- changes are announced, once per turn -----------------------------------
    int changes = 0;
    QObject::connect(&face, &ChipFace::changed, [&] { ++changes; });
    face.notify();
    face.notify();
    width->setText(QStringLiteral("41"));
    check(changes == 0, "a change is not announced inside the turn it happens in");
    QCoreApplication::processEvents();
    check(changes == 1, "three changes in one turn are announced once");
    keep->setChecked(true);
    QCoreApplication::processEvents();
    check(changes == 2, "a registered control's own change is announced");

    // --- an edit lands in the control --------------------------------------------
    int typed = 0;
    QObject::connect(width, &QLineEdit::textChanged, [&] { ++typed; });
    check(face.edit(QStringLiteral("width"), QStringLiteral("45")) && width->text() == QLatin1String("45") && typed == 1,
          "edit() sets the Qt field's text, and its textChanged runs once");
    check(!face.edit(QStringLiteral("nope"), QStringLiteral("1")), "edit() refuses an id that is not a field");
    check(!face.edit(QStringLiteral("flip"), QStringLiteral("1")), "edit() refuses a button's id");

    int clicks = 0;
    QObject::connect(flip, &QPushButton::clicked, [&] { ++clicks; });
    check(face.press(QStringLiteral("flip")) && clicks == 1, "press() clicks the Qt button");
    flip->setEnabled(false);
    check(!face.press(QStringLiteral("flip")) && clicks == 1, "press() refuses a disabled button");
    flip->setEnabled(true);
    check(face.press(QStringLiteral("keep")) && !keep->isChecked(), "press() on a toggle toggles it");
    check(!face.press(QStringLiteral("width")), "press() refuses a field's id");

    // --- commit carries the page's values in first --------------------------------
    check(face.commit({{QStringLiteral("width"), QStringLiteral("60")}}) && commits == 1 &&
              seenAtCommit == QLatin1String("60"),
          "commit() writes the page's values into the fields, THEN commits (Enter after a digit)");
    check(!face.commit({{QStringLiteral("nope"), QStringLiteral("1")}}) && commits == 1,
          "commit() with a value for no field refuses, and commits nothing");
    check(face.cancel() && cancels == 1, "cancel() runs the chip's cancel");
    chip->hide();
    check(!face.commit({}) && !face.cancel() && !face.edit(QStringLiteral("width"), QStringLiteral("1")) &&
              !face.press(QStringLiteral("flip")) && commits == 1 && cancels == 1,
          "a hidden chip refuses every request from the page");
    chip->show();

    // --- drawn elsewhere -------------------------------------------------------
    int drawnSignals = 0;
    QObject::connect(&face, &ChipFace::drawnChanged, [&] { ++drawnSignals; });
    check(!chip->testAttribute(Qt::WA_TransparentForMouseEvents), "set up: the chip takes the pointer");
    face.setDrawnElsewhere(true);
    check(face.drawnElsewhere() && chip->testAttribute(Qt::WA_TransparentForMouseEvents) &&
              width->isHidden() && flip->isHidden() && keep->isHidden() && drawnSignals == 1,
          "drawn elsewhere: the pointer passes through and every control hides");
    check(!face.controlsShown(), "and the chip is told not to show them");
    int sized = 0;
    QObject::connect(&face, &ChipFace::faceSizeChanged, [&] { ++sized; });
    face.setFaceSize(QSize(300, 60));
    face.setFaceSize(QSize(300, 60));
    check(face.faceSize() == QSize(300, 60) && sized == 1, "the page's size is taken, and announced once");
    face.setDrawnElsewhere(false);
    check(!chip->testAttribute(Qt::WA_TransparentForMouseEvents) && face.controlsShown() &&
              !face.faceSize().isValid() && drawnSignals == 2,
          "drawn here again: the pointer is the chip's, the page's size forgotten");
    // A chip that never took the pointer keeps not taking it.
    chip->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    face.setDrawnElsewhere(true);
    face.setDrawnElsewhere(false);
    check(chip->testAttribute(Qt::WA_TransparentForMouseEvents),
          "a chip that was mouse-transparent before stays so after");

    // --- keys ------------------------------------------------------------------
    QWidget pageView(&viewport);
    QWidget inner(&pageView);
    bool pageHolds = true;
    face.setPageKeys(&pageView, [&] { return pageHolds; });
    check(!face.keyBelongsToPage(&inner), "drawn here, no key belongs to the page");
    face.setDrawnElsewhere(true);
    check(face.keyBelongsToPage(&inner) && face.keyBelongsToPage(&pageView),
          "drawn elsewhere with the page's field holding the keys: a key aimed at the page is the page's");
    check(!face.keyBelongsToPage(width), "a key aimed elsewhere is still the chip's claim");
    pageHolds = false;
    check(!face.keyBelongsToPage(&inner), "with no field of this chip holding them, the claim stands");
    face.setDrawnElsewhere(false);

    // --- seeding ---------------------------------------------------------------
    const int seed = face.face().value(QStringLiteral("seed")).toInt();
    face.reseed(QStringLiteral("width"));
    f = face.face();
    check(f.value(QStringLiteral("seed")).toInt() == seed + 1 &&
              f.value(QStringLiteral("focus")).toString() == QLatin1String("width"),
          "reseed() moves the seed and names the field to focus");
    face.setInvalid(true);
    check(face.face().value(QStringLiteral("invalid")).toBool(), "the face carries the chip's invalid mark");

    // --- the bridge --------------------------------------------------------------
    ChipsBridge bridge;
    bridge.add(&face);
    QVariantMap carried;
    int faceSignals = 0;
    QObject::connect(&bridge, &ChipsBridge::facesChanged, [&](const QVariantMap& faces) {
        ++faceSignals;
        carried = faces;
    });
    check(bridge.faces().contains(QStringLiteral("slats")), "the bridge lists the face by its chip's name");
    face.notify();
    QCoreApplication::processEvents();
    check(faceSignals == 1 && carried.contains(QStringLiteral("slats")),
          "a face's change reaches the page WITH the faces (a NOTIFY would batch)");
    bridge.edit(QStringLiteral("slats"), QStringLiteral("width"), QStringLiteral("70"));
    check(width->text() == QLatin1String("70"), "chips.edit() reaches the chip's field");
    bridge.press(QStringLiteral("slats"), QStringLiteral("flip"));
    check(clicks == 2, "chips.press() reaches the chip's button");
    bridge.commit(QStringLiteral("slats"), {{QStringLiteral("width"), QStringLiteral("72")}});
    check(commits == 2 && seenAtCommit == QLatin1String("72"), "chips.commit() carries the values and commits");
    bridge.cancel(QStringLiteral("slats"));
    check(cancels == 2, "chips.cancel() cancels");
    bridge.reportSize(QStringLiteral("slats"), 320, 64);
    check(face.faceSize() == QSize(320, 64), "chips.reportSize() sizes the face");
    bridge.edit(QStringLiteral("boolean"), QStringLiteral("width"), QStringLiteral("1"));
    bridge.commit(QStringLiteral("boolean"), {});
    check(width->text() == QLatin1String("70") || width->text() == QLatin1String("72"),
          "a chip the bridge does not know is refused");
    bridge.reportSize(QStringLiteral("slats"), 0, 64);
    check(face.faceSize() == QSize(320, 64), "a size of nothing is refused");

    std::printf("\n%s (%d failures, %d checks)\n", gFailures ? "FAIL" : "PASS", gFailures, gChecks);
    return gFailures ? 1 : 0;
}

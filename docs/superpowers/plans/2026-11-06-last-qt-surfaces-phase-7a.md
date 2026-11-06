# Phase 7a: the last Qt-only surfaces as HTML

Branch `phase-7a`. Phase 6 left ten surfaces with no page: the loading card, the
shortcut sheet, the first-run guide, the hint balloon, the material card, the axis
gizmo card, the window buttons, the Add-shape flyout, render mode's Back chip and the
compare badge. Each moves onto the chrome page the way Phases 3-5 moved theirs: the Qt
widget keeps its logic, its place and its rules, and paints nothing while the page is
live (`setDrawnElsewhere`); the page draws what the widget's `words()` say, where the
widget stands. Phase 7b then moves the state and deletes the widgets.

## Slices

- **7a-1, the loading card** (62fcdeb). `LoadingCard::words()` rides the question
  channel as a third kind, `loading`: one card over the whole viewport at a time, the
  mask the viewport's while it stands. Each step's words are sent BEFORE the pump, so
  the frame the pump shows is this step's. chrome_smoke catches the page mid-load (in a
  `stateChanged` slot) and compares the window with and without the page.
- **7a-2, the shortcut sheet** (f384364). The chrome's `sheet` map: the groups at the
  places `showSheet()` stood them, drawn under the Qt sheet, which keeps Escape and the
  click-outside rule.
- **7a-3, the guide and the hint** (dd6446e). The chrome's `guide` and `hint` maps at
  the widgets' own rectangles; the page's skip and "got it" answer through
  `WalkthroughPanel::skip()` and `HintBalloon::dismissHint()`.

## Rulings

- **R1: a card the page draws takes the presses over it.** The page's mask clips
  painting as well as presses, so a card it paints is a card it hit-tests. The Qt guide
  let clicks through to the wood behind it; the page's does not - the HTML toast already
  made the same trade. Cost: a body hidden behind the guide cannot be clicked until the
  guide is skipped or done.
- **R2: the page answers, C++ decides.** Every press the page takes for one of these
  surfaces is relayed to the exact call the Qt widget's own press made (`skip()`,
  `dismissHint()`), so what retires a guide or a hint is still decided once, in C++.
- **R3: text the Qt widget measured is drawn as the same string.** The guide sized its
  card for "•  step" as one string; the page draws that string, not a mark box plus a
  gap, or a step wrapped onto two lines.

## Found on the way

- **A GL driver crash in `OcctViewWidget::resizeGL`** (fixed in 7a-1):
  `MustBeResized()` redrew at once into the framebuffer Qt had just replaced. The resize
  now schedules a frame; `paintGL()` wraps the live framebuffer.
- **T-090**: the shortcut sheet can come out wider than its window (1404 px in a
  1280-wide editor) - an old layout limit, filed.

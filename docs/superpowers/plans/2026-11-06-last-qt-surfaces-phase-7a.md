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
- **7a-4, the material card** (dbf6316). A chip face: ChipFace grew a `slider` part
  (`slide()` lands in the QSlider, so the card's own `valueChanged` writes the look) and
  `addHidden()` for a card's labels; a button part may carry a `swatch` colour and
  `align: "end"`.
- **7a-5, the Add-shape flyout** (0290ed4). A chip face too: tiles three to a row, each
  with its IconSet glyph as an alpha mask (`look: "tile"`); its Escape and click-outside
  rules stay the Qt flyout's.
- **7a-6, render mode's Back chip** (e19e8d8). A third `shell` cluster, `back`, drawn in
  a slot that takes the Qt exit chip's place; `RenderStudio::refresh()` shows whichever
  the page allows. Editor and scene window.
- **7a-7, the window buttons**. The hub's `HubWindowControls` registered as `window` on
  the chrome page; a `buttons` slot in the Qt buttons' place; the hit test answers
  HTMAXBUTTON from the maximize rectangle the page reports (snap layouts), and the
  native maximize hover and press reach the page. The question's wash leaves whichever
  buttons are on screen clear. Editor and scene window.

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
- **R4: the axis gizmo stays Qt.** It redraws on every camera move, and a page must not
  be driven every frame (Phase 4's glued chips leave the page during an orbit for the
  same reason). It is an instrument tied to the camera, like the 3D gizmos, not chrome.
- **R5: the compare badge stays Qt.** It stands on the compare pane's second, read-only
  viewport, which has no page; giving it one costs a whole renderer process for one label
  and one button. Phase 7b's deletions do not include it.

## Found on the way

- **A GL driver crash in `OcctViewWidget::resizeGL`**: `MustBeResized()` redrew at once
  into the framebuffer Qt had just replaced. 7a-1 deferred the redraw to `paintGL()`;
  at 150% that was worse (the page undrawn after render mode, a crash in
  `blitBuffers`), proved by an A/B against the old code. 7a-7 keeps the redraw in
  `resizeGL()` and wraps Qt's new framebuffer first. chrome_smoke 503/503 at 100% and at
  150%. **Lesson: a fix to a GL path is not done until it has run at a fractional scale**
  - this machine's suite windows only sometimes land on its 150% monitor.
- **T-090**: the shortcut sheet can come out wider than its window (1404 px in a
  1280-wide editor) - an old layout limit, filed.

# Phase 2: Help and learnability — design

Date: 2026-08-28
Status: approved in chat, awaiting spec review

Phase 2 of three. Phase 1 (language and numbers) is merged; Phase 3 is feedback and polish.
This phase adds the surfaces that teach, and the machinery that makes them stop.

## Goal

Teach a newcomer the modelling loop by having them actually do it, teach the rest of the
app at the moment each capability first becomes available, and then get out of the way
permanently — verifiably, not decoratively.

## Why this shape

The audience decision is "teach then get out of the way", which is the most demanding of
the three options because it requires the app to *remember what you have already learned*.
Without persisted counts, "recedes" is a promise nobody can check. `UserProgress` exists so
the promise is mechanical: a hint that has done its job three times is gone forever, and a
test can assert it.

## Decisions (settled with the user)

1. **First run is a guided first build**, not coach marks and not a sample document. Four
   steps, each completing when the user genuinely performs it. Teaching by doing is the
   only version that sticks, and it leaves the user with a real body they made.
2. **Everything else is taught by a hint balloon** anchored to the relevant control, the
   first few times that capability becomes available, counted and then permanently silent.
3. **The always-available help panel is dropped.** With a walkthrough, balloons, a
   generated shortcut sheet and teaching tooltips, it would be a fifth surface repeating
   what the other four say.
4. **Threshold is 3.** After three completions of an action, its hint never appears again.

## Architecture

### `UserProgress` — new, Qt-free, in `furnify_geometry`

`src/UserProgress.{h,cpp}`. Counting is logic, and logic in this project is
headless-testable. No Qt, no `QSettings` — persistence is the app layer's job.

```cpp
class UserProgress {
public:
    static constexpr int kLearnedThreshold = 3;

    void record(const std::string& event);
    int  count(const std::string& event) const;
    bool hasLearned(const std::string& event) const;   // count >= kLearnedThreshold
    void reset();

    // "boolean.completed=3;extrude.completed=7". Stable ordering so a round trip
    // is byte-identical and the stored value does not churn between runs.
    std::string serialize() const;
    void deserialize(const std::string& text);

private:
    std::map<std::string, int> myCounts;
};
```

**Storage is injected, never baked in.** `MainWindow` loads and saves `serialize()` through
`QSettings`; `gui_smoke` constructs a `UserProgress` and never persists it. Without this
separation the suite's behaviour would depend on how many times the developer had run the
real app — a test that passes or fails based on machine history is not a test.

Counted events, exactly:

| Event | Recorded when |
|---|---|
| `sketch.completed` | an outline closes into a face |
| `extrude.completed` | a face becomes a body |
| `boolean.completed` | Union, Subtract or Intersect succeeds |
| `delete.used` | one or more bodies are deleted |
| `undo.used` | undo or redo runs |
| `view.changed` | a standard view or the gizmo changes the camera |
| `faceMode.used` | face selection mode is entered |

`walkthrough.done` is stored in the same map as a count, set to `kLearnedThreshold` on
completion or skip, so one mechanism covers both "has learned" and "has dismissed" without
a second concept.

### `WalkthroughPanel` — new, app layer

`src/ui/WalkthroughPanel.{h,cpp}`. A panel anchored top-right of the viewport through the
existing `ViewportOverlay`, showing four steps:

1. Press `Ctrl+K` to start an outline
2. Click at least 3 points on the ground
3. Press `Enter` to close the outline
4. Press `E` and enter a height

Each step ticks when the user actually performs it, driven by signals `MainWindow` already
emits or gains in this phase. Nothing is simulated.

**Interruption is handled by derivation, not by a stored cursor.** The panel recomputes
which steps are satisfied from the live application state — is a sketch active, how many
points are placed, is a face pending, does the document contain a body. Quitting at step 3
and reopening therefore resumes correctly, and there is no persisted cursor to fall out of
sync with reality.

Shown only when `!progress.hasLearned("walkthrough.done")`. A `[skip]` control marks it
done. On the fourth step completing, it congratulates briefly and marks itself done.

### `HintBalloon` and `HintManager` — new, app layer

`src/ui/HintBalloon.{h,cpp}` is a small painted widget: one sentence, a "got it" button,
anchored beside a target widget with a pointer triangle. `HintManager` (same files) holds
the policy.

A balloon is shown when a capability *first becomes available in this session* and its
event is not yet learned. The two conditions compose: the session flag stops a balloon
reappearing every time you reselect two bodies within one sitting, and the persisted count
stops it across sittings once the action has been done three times. A user who selects two
bodies, dismisses the hint and never runs a boolean will therefore see it again next
launch — which is correct, since they have not learned it. It is dismissed by performing the action, by "got it", or by the
capability going away. Three hints only:

| Hint | Trigger | Governing event |
|---|---|---|
| The boolean trio | selection first reaches exactly two bodies | `boolean.completed` |
| Face selection | first time a body exists and face mode has never been used | `faceMode.used` |
| The axis gizmo | the first body is created, if no standard view has been used | `view.changed` |

At most one balloon is visible at a time; if two become due, the earlier in that table
wins and the other waits for a later opportunity.

The gizmo hint deliberately triggers on the first body rather than on "has orbited": there
is no orbit-happened signal today, adding one would mean tracking drag state purely to feed
a hint, and having a body to look at is the moment the gizmo first becomes worth using
anyway.

### `ShortcutSheet` — new, app layer

`src/ui/ShortcutSheet.{h,cpp}`. A centred overlay listing every shortcut, opened with `?`
or `F1`, dismissed with `Esc` or a click outside.

**Its rows are generated from the `QAction`s themselves** — every action with a non-empty
shortcut, grouped by the menu that owns it. A hand-written sheet drifts the moment someone
adds a binding; a generated one cannot. This is the same principle as Phase 1's vocabulary
test: make the documentation executable.

### `Help` menu — new

A `Help` menu with two entries: `Keyboard Shortcuts` (`?`) and `Show tips again`, the
latter calling `UserProgress::reset()` and persisting it, so the walkthrough and balloons
return. Needed the first time this is shown to another person, and it makes the whole
system inspectable rather than a one-way door.

## Non-goals

No always-available help panel (dropped, see above). No tutorial content beyond the four
walkthrough steps. No video, no external documentation links, no telemetry of any kind —
`UserProgress` is local, and nothing about it leaves the machine. No changes to modelling
behaviour, geometry, camera or copy: Phase 1 settled the words and this phase must not
re-word anything.

## Testing

- **Headless** (`tests/user_progress.cpp`, new, registered with ctest): counting;
  `hasLearned` false at 2 and true at 3; `reset` clears everything; serialize/deserialize
  round-trips exactly, including an empty store; `deserialize` of malformed input
  (`"garbage"`, `"a=b"`, `"a="`, trailing separators) leaves a usable object rather than
  throwing; unknown events count as zero and are not learned.
- **`gui_smoke`**: with an empty store the walkthrough panel is present, and with
  `walkthrough.done` recorded it is absent; a balloon appears when the selection first
  reaches two bodies and does not appear once `boolean.completed` has been recorded three
  times; the shortcut sheet opens on `?`, closes on `Esc`, and contains a row for every
  action whose `shortcut()` is non-empty — asserted by comparing against the live action
  list, so the sheet cannot silently omit one. The existing 100 checks continue to pass.
- **The suite must never read or write the real `QSettings`.** `gui_smoke` injects its own
  `UserProgress`; a test that touched the developer's stored progress would pass or fail
  based on how often the app had been used.

## Acceptance criteria

1. A first run with no stored progress shows the walkthrough; completing or skipping it
   means it never appears again.
2. Each walkthrough step ticks only when the user actually performs that step, and the
   panel resumes correctly after being closed mid-way.
3. Each of the three hints appears when its capability first becomes available and never
   after its event has been recorded three times.
4. The shortcut sheet lists every action that has a shortcut, generated from the actions.
5. `Help → Show tips again` restores the walkthrough and all three hints.
6. Nothing in this phase changes wording settled in Phase 1.
7. Headless suite and all existing gui_smoke checks pass, plus the new ones.

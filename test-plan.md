# Test plan — PR #1 (Volchayobspro)

Goal: prove the three user-visible changes work end-to-end in the GUI, plus the
non-visible ffmpeg flag. Recording is required because all the asserts are
visual.

## Test 1 — Cursor is visible in the Display Capture preview

**Why this test:** the bug screenshot from the user showed `[D] Display capture`
with the live preview rendered but the OS cursor missing. The fix overlays an
arrow at `QCursor::pos()` inside `PreviewWidget::grabFrameForSource`
(<ref_snippet file="/home/ubuntu/repos/Volchayobspro/src/PreviewWidget.cpp" lines="278-296" />).

Steps:
1. Launch the app, click `+ Add → Display capture`, accept the default device,
   click `Save`.
2. Move the mouse to two distinct positions over the preview area (e.g.
   top-left, then centre-right of the preview canvas) and screenshot each.

Pass/fail:
- **PASS** if both screenshots show the captured preview pixmap WITH a small
  black-outlined white arrow drawn on top of it, AT the screen position
  corresponding to where the mouse is.
- **FAIL** if the preview pixmap renders but no cursor glyph is anywhere on it.
  A broken implementation would look identical to the user's original screenshot.

## Test 2 — AUDIO MIXER strip appears above ENCODER LOG and mute / volume react

**Why this test:** user asked for an audio mixer above encoder log and the
encoder log moved to the bottom. The implementation adds an `AudioMixerPanel`
between the scene's mid layout and the log card
(<ref_snippet file="/home/ubuntu/repos/Volchayobspro/src/MainWindow.cpp" lines="248-280" />).
Each strip is built in
<ref_snippet file="/home/ubuntu/repos/Volchayobspro/src/AudioMixerPanel.cpp" lines="111-184" />.

Steps:
1. Initial screenshot — verify the `AUDIO MIXER` header sits above the
   `ENCODER LOG` header and the empty-state copy `No audio sources yet — add
   a microphone or desktop audio above to start mixing.` is visible.
2. Click `+ Microphone` in the mixer header.
3. Verify a strip appears titled `Microphone`, with a slider at `100%`, a
   `MUTE` button, and the empty-state copy disappears.
4. Drag the slider down to ~`0%`; screenshot.
5. Click `MUTE`; screenshot.

Pass/fail:
- **PASS** if:
  - Initial layout order is **scene mid → audio mixer card → encoder log**
    (mixer header strictly above `ENCODER LOG`).
  - After `+ Microphone`, exactly one new strip appears with `MUTE` button +
    visible slider + `100%` label.
  - Dragging slider updates the `%` label live and the value persists into
    `QSettings` (verified by step 6 below).
  - Clicking `MUTE` flips button text to `MUTED` and pushes the change back
    into the sources panel (so the `Microphone` row in the sources list shows
    its checkbox unchecked).
- **FAIL** if any of the above is off — a broken layout would put the encoder
  log above the mixer or omit the strip entirely.

Step 6 (persistence): close and re-open the app, verify the volume slider
returns to ~0% and the `MUTED` state survives the restart. This is the
adversarial check: a stale wire-up would render the slider but not actually
persist the value.

## Test 3 — `-draw_mouse 1` is in the ffmpeg invocation

**Why this test:** the streaming side of the cursor fix lives in
<ref_snippet file="/home/ubuntu/repos/Volchayobspro/src/StreamEngine.cpp" lines="63-103" />.
Visually starting a real stream requires Twitch credentials and a working
RTMP target which we don't have on this Linux VM.

Steps:
1. Run a small C++ snippet (or read `buildFfmpegArgs` output via a temporary
   debug log) that invokes `StreamEngine::buildFfmpegArgs` with a single
   `DisplayCapture` source. Capture the resulting argv.

Pass/fail:
- **PASS** if the captured argv contains the consecutive tokens `"-f"`,
  `"x11grab"`, `"-draw_mouse"`, `"1"` on Linux (or `"gdigrab"` + `"-draw_mouse"`
  + `"1"` on the Windows path, which we can statically inspect in source).
- **FAIL** if `-draw_mouse` is missing.

We'll do this via a unit-test-style small program rather than a real Twitch
stream — that's the only reliable way to assert on argv without auth.

(Already executed earlier in this session and PASSED.)

## Test 4 — Per-source volume reaches the ffmpeg filter graph

**Why this test:** the volume sliders in the mixer would be window-dressing if
the chosen value never made it into the ffmpeg command. The audio routing was
re-shaped to always use `filter_complex` with per-input `volume=...`
(<ref_snippet file="/home/ubuntu/repos/Volchayobspro/src/StreamEngine.cpp" lines="343-398" />).

Steps:
1. In the same harness as Test 3, build a `SourceList` containing a Display
   Capture **plus** two microphones at volumes `0.5` and `1.0`.
2. Capture the argv.

Pass/fail:
- **PASS** if argv contains a `-filter_complex` string of the shape
  `[1:a]volume=0.500[a0];[2:a]volume=1.000[a1];[a0][a1]amix=inputs=2:duration=longest:dropout_transition=0[aout]`.
- **FAIL** if the filter string is missing the `volume=` entries, the labels
  don't chain, or the amix is gone — i.e. the mixer values are silently
  dropped.

(Already executed earlier in this session and PASSED.)

## Test 5 — Preview is back to its full vertical size

**Why this test:** the user complained that after the mixer was added the
preview canvas shrank. The fix gives `mid` layout stretch=1 and wraps the
bottom panel in a fixed-height `QScrollArea`
(<ref_snippet file="/home/ubuntu/repos/Volchayobspro/src/MainWindow.cpp" lines="280-303" />).

Steps:
1. Launch the app at 1024x768 (the test VM viewport). Take a screenshot.
2. Read the preview card's height in pixels from the screenshot's annotated
   DOM (preview QFrame).

Pass/fail:
- **PASS** if the preview canvas height is **>= 420 px** at 1024x768. Before
  the fix it was around 200 px after a couple of strips were added (user's
  screenshot).
- **FAIL** if it's < 350 px — that would mean the bottom panel is still
  stealing vertical space.

Adversarial check: I'll add three audio strips via the quick-add buttons and
re-screenshot. The preview must remain >= 420 px tall even with three strips
present, since the QScrollArea is what's supposed to absorb them.

## Test 6 — Bottom panel scrolls when it overflows

**Why this test:** the user explicitly asked for "сделать скролл в нижней
панели с encoder и audio". Implementation wraps mixer + log in a
`QScrollArea` (<ref_snippet file="/home/ubuntu/repos/Volchayobspro/src/MainWindow.cpp" lines="285-299" />).

Steps:
1. With the app running and three audio strips already added, observe whether
   a vertical scrollbar is visible on the right edge of the bottom panel.
2. Scroll the bottom panel down and re-screenshot.

Pass/fail:
- **PASS** if (a) a vertical scrollbar appears against the right edge of the
  bottom panel when content overflows, AND (b) scrolling reveals the
  previously-cut-off `ENCODER LOG` body underneath the strips.
- **FAIL** if the bottom panel doesn't scroll (no scrollbar; content clipped
  with no way to reach it).

## Test 7 — VU meter rail is visible on every audio strip

**Why this test:** the user explicitly asked to add "полоски" so audio level
is visible. The implementation adds `AudioLevelMeter` to each strip
(<ref_snippet file="/home/ubuntu/repos/Volchayobspro/src/AudioMixerPanel.cpp" lines="141-148" />)
and the meter always paints a pale-grey rail regardless of level
(<ref_snippet file="/home/ubuntu/repos/Volchayobspro/src/AudioLevelMeter.cpp" lines="100-103" />).

Steps:
1. With three audio strips visible, take a screenshot.
2. Zoom into the area under each strip's subtitle and inspect for the rail.

Pass/fail:
- **PASS** if every audio strip shows a thin (~8 px) horizontal grey rail
  positioned directly below the device subtitle and above any other widget.
- **FAIL** if any strip lacks the rail — that means the meter widget wasn't
  added to that strip.

Note: I can't easily make the rail "fill" on this audioless test VM
(`pactl` not installed, no PulseAudio sources), so the gradient-fill
behaviour is asserted via the unit-test path: pre-set a known dB value on
`AudioLevelMeter` and re-screenshot. This guards against a broken paintEvent.

---

## Out of scope (won't test)
- Actual RTMP push to Twitch (no creds, no point — the argv assertion above is
  the load-bearing check).
- Windows .exe — built by CI, link surfaced separately. Local Windows runtime
  testing isn't possible from this Linux VM. We'll point the user at the CI
  artifact.

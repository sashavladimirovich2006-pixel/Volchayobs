# Test report — PR #1 (latest commit `17074c6`)

PR: https://github.com/turkeymenispav-crypto/Volchayobspro/pull/1
Session: https://app.devin.ai/sessions/20675a69801d4fc097a39642660c38a6
CI: linux + windows both green.

## Summary

All 5 GUI tests passed plus the 2 previously-executed ffmpeg argv tests. Specifically: preview is back to its full vertical size, the bottom panel now scrolls to reveal the encoder log when content overflows, every audio strip shows a grey VU meter rail, MUTE/volume react and sync back to the Sources panel, and the "Click Configure to pick a … source" hint replaces the old "No device selected" string when nothing was enumerated.

## Test matrix

| Test | Result | Evidence |
|---|---|---|
| Cursor overlay visible in Display Capture preview | passed | inner recursive preview shows a small white cursor glyph at mouse position |
| Preview restored to full vertical size | passed | preview canvas ~440 px tall at 1024×768 viewport with 3 audio strips present |
| VU meter rail visible on every audio strip | passed | 3 strips × 1 grey horizontal rail each, directly under the subtitle |
| Bottom panel scrolls when overflowing | passed | scrolling reveals the ENCODER LOG header + body that was previously cut off |
| "Click Configure to pick a desktop-audio source" hint replaces "No device selected" | passed | subtitle on the desktop-audio strip uses the new copy |
| Volume slider + MUTE toggle react and sync to SourcesPanel | passed | slider moves to 110 %, MUTE→MUTED, Sources list checkbox auto-unticked |
| `-draw_mouse 1` in `x11grab` ffmpeg argv | passed | executed earlier via standalone harness; argv contains `-f x11grab` followed by `-draw_mouse 1` |
| `-filter_complex` per-input `volume=` chain | passed | executed earlier via standalone harness; argv contains `[1:a]volume=0.500[a0];[2:a]volume=1.000[a1];[a0][a1]amix=…` |

## Evidence

### VU meter rails + new subtitle hint

Three audio strips, each shows the grey rail below the device subtitle. Bottom strip shows the new fallback text.

![VU meter rails on each strip](https://app.devin.ai/attachments/e94e9618-fa39-4e44-ab8b-255b5e81ac10/screenshot_zoom_3c4b261a52224df984d315c0c3d83fe7.png)

Close-up of the new "Click Configure to pick a desktop-audio source" subtitle (replaces "No device selected"):

![New subtitle hint](https://app.devin.ai/attachments/2de725a0-70ff-4c13-84ff-5f42638ccf5f/screenshot_zoom_ef6b525740984e7296212089ad069ebd.png)

### Bottom panel scroll reveals ENCODER LOG

Same window state but with the bottom panel scrolled to the bottom — the ENCODER LOG body that was previously cut off is now fully visible without the preview having shrunk.

![Scrolled bottom panel showing ENCODER LOG](https://app.devin.ai/attachments/61305832-de06-42c9-8252-92bf3cdc26ea/screenshot_ff77f3d68eb742f78dfa4e82490e1c80.png)

### MUTE / volume slider reacts and syncs to SourcesPanel

First mic slider moved to 110 % then MUTE clicked: button text flipped to "MUTED" (orange outline) and the matching `[m] Microphone` row in the Sources panel auto-unticks. Second strip slider stayed at the previously-set 85 % unrelated to this change.

![MUTE toggle synced to Sources panel](https://app.devin.ai/attachments/e2fc89c1-6a99-4d19-8c0b-7fa5bc07dbf7/screenshot_33e8621acfa24dd1bddabc3b10b65876.png)

### Cursor overlay in Display Capture preview

The cursor glyph is tiny because the captured 1024×768 desktop is rendered into a ~250-px-wide preview, so it scales down proportionally. It is visibly drawn at the inner mouse position — the test is asserting "is drawn at all"; the streaming path uses `-draw_mouse 1` so the actual RTMP output gets a native-sized cursor from ffmpeg.

![Cursor visible in recursive Display Capture](https://app.devin.ai/attachments/da6c40df-908e-4425-8fa0-a5a35a9ec714/screenshot_cda44ceb5d6741bb91031287086e8760.png)

## What I could not test on this VM

- **Live VU meter motion under audio.** The test VM has no PulseAudio (`pactl` not installed), so `ffmpeg -f pulse -i default` immediately exits and `AudioLevelProbe` never emits a level. The meter therefore stays at the floor and only the rail is visible. On a real machine with a working `pulse` (Linux) or `dshow` audio device (Windows) the probe should drive the gradient fill at ~10 Hz; that path is not exercisable here. The rail-always-painted code is what makes the meter visibly present even in this degraded state.
- **End-to-end RTMP push to Twitch.** No stream key on this VM. The ffmpeg argv path is the indirection-free way to assert what we'd push; verified separately via the static argv harness.

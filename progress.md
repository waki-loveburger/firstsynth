# 1st synth (FirstSynth) — progress log

Personal iPlug2-based CLAP/Standalone synth plugin. This file is the authoritative,
up-to-date log of what's been done and why — read this first when resuming work,
whether that's Claude or the user themselves.

## Project basics

- Location: `C:\Users\a_wak\CLAP_plugin\FirstSynth` (sibling to `C:\Users\a_wak\CLAP_plugin\iPlug2`,
  the framework checkout). Both must stay directly under `CLAP_plugin\` — the vcxproj files
  were patched to use `..\..\iPlug2\...` relative paths (2 levels up + iPlug2), not iPlug2's
  default `..\..\..\` (which assumes `Examples\ProjectName\projects\` nesting).
- **The iPlug2 checkout is a modified fork**, not stock upstream. As of 2026-08-29 it lives at
  the private repo `github.com/waki-loveburger/iPlug2` (remotes in that checkout: `origin` =
  that fork, `upstream` = `iPlug2/iPlug2`). On a fresh machine:
  `git clone https://github.com/waki-loveburger/iPlug2 C:\Users\a_wak\CLAP_plugin\iPlug2`.
  Local changes on top of upstream (ADSR `SetAttackShape`, LFO S&H, WASAPI driver, Standalone
  Save/Load Preset + DPI fixes, VST3 DPI + per-product WebView2 cache isolation, WebView2
  environment reuse, Release-build `index.html` resolution) — see that repo's `git log`
  `upstream/master..master`. Every `CLAP_plugin\` sibling (SuiKinKutsu, GrainField, Compost,
  UeberLooper, Chaoscape) compiles against this same checkout.
- Code-level project name: `FirstSynth`. Manufacturer placeholder: `AcmeInc`. Display name
  (`PLUG_NAME` in config.h): "1st synth".
- Stack: iPlug2 + WebView UI (HTML/CSS/JS in `resources/web/`). DSP hand-written in
  `FirstSynth_DSP.h` (started from iPlug2's `IPlugInstrument` example's MidiSynth-based voice
  architecture, then heavily extended — see DSP section below).
- Targets actively built/tested: Standalone app (`FirstSynth-app`) and CLAP (`FirstSynth-clap`).
  VST2/VST3/AAX vcxproj exist (patched for the same path issue) but aren't used.
- Toolchain (Windows): VS Build Tools 2022 (C++ workload only, no IDE) + CMake + NuGet,
  installed via winget. **Must build Debug config** — Release WebView loading is an
  unfixed upstream iPlug2 bug on Windows.

## Build commands

```
MSBuild.exe "C:\Users\a_wak\CLAP_plugin\FirstSynth\FirstSynth.sln" -t:FirstSynth-app -p:Configuration=Debug -p:Platform=x64 -m
MSBuild.exe "C:\Users\a_wak\CLAP_plugin\FirstSynth\FirstSynth.sln" -t:FirstSynth-clap -p:Configuration=Debug -p:Platform=x64 -m
```

Before building, kill any running instance first (postbuild copy silently fails with a
file-in-use error otherwise): `Stop-Process -Name FirstSynth,FirstSynth_x64,REAPER -Force -ErrorAction SilentlyContinue`
(only ask the user to close REAPER themselves — it's their own app, not disposable test
tooling like the FirstSynth exe).

Standard workflow: build **both** targets after every change, without asking first. User
verifies Standalone themselves (`build-win\FirstSynth_x64.exe`, or I launch
`build-win\app\x64\Debug\FirstSynth.exe` and screenshot it). CLAP correctness is checked
in REAPER in batches later by the user, not after every change.

CLAP output auto-copies to `C:\Users\a_wak\AppData\Local\Programs\Common\CLAP\` (that
folder had to be created manually once — iPlug2's postbuild-win.bat checks `if exist`
but never creates it).

## DSP architecture (FirstSynth_DSP.h, class `IPlugInstrumentDSP<T>`)

16-voice polyphonic (`MidiSynth`, `kPolyModePoly`). Per voice (`Voice` inner class):

- **Two independent oscillators** (`mPhase1`/`mPhase2`, manual phase accumulators — not
  iPlug2's `FastSinOscillator`), each with a continuous **waveform morph** control
  (`FirstSynthOsc::Morph`, 0–4 range): Sine → Triangle → Saw → Square → narrow Pulse.
  - Segments 0–1 (Sine→Tri, Tri→Saw) are plain amplitude crossfades between the two shapes.
  - Segment 2 (Saw→Square) hard-clips the sawtooth itself (`clamp(saw*k, -1, 1)`, k: 1→~51)
    rather than crossfading — crossfading a saw with a square produced an audible
    perceived-octave-up artifact partway through. Clipping instead is a continuous
    deformation of the same waveform, so it stays glitch-free and lines up in polarity
    with segment 3's starting point.
  - Segment 3 (Square→Rectangular) sweeps the **duty cycle** of a single `Pulse()` call
    (0.5 → 0.1), not a crossfade — a crossfade there looked wrong (asked to make "only
    the duty cycle change").
  - `Pulse(phase, duty)` returns `-1` before `duty`, `+1` after — this polarity (not the
    more "obvious" opposite) is required so segment 2's hard-clip endpoint and segment 3's
    starting duty=0.5 line up continuously without a polarity-flip click at the boundary.
  - Each oscillator has its own Octave/Semitone/Fine tuning (`mTuneOctaves1`/`2`, combined
    from 3 separate params into one octave-offset added to the pitch-to-Hz conversion).
- **Noise generator** (existing simple LCG-based `Rand()`), a proper mixer channel rather
  than always-on. Was silent regardless of the Noise Level knob until 2026-07-20 — see
  Known Issues #9.
- **Mixer**: `mMixOsc1`/`mMixOsc2`/`mMixNoise` (0–1 gains) sum the two oscillators and noise
  before the filter. Defaults: Osc1=100%, Osc2=0%, Noise=0% (i.e. sounds like a single-osc
  synth until Osc2/Noise are brought up). Noise Level got `IParam::ShapePowCurve(2.)`
  (2026-07-20, user wanted finer control at the low/left end of the knob) - remember the
  matching `shape-exponent="2"` on its `knob-control` tag in `index.html` (Known Issues #2:
  the two sides must always be added together, or the knob's drag feel and the real DSP
  value diverge silently).
- **Filter**: per-voice Zavalishin/"TPT" state-variable filter (`SVFStage<T>`, replaced the
  original Chamberlin/forward-Euler form 2026-07-20 — see Known Issues #7), producing
  low/band/high simultaneously each sample. `BlendFilterOutputs()` continuously blends
  LP→BP→HP from a single 0–2 "Filter Type" knob. `mFilterSlope24` (bool) cascades a second
  SVF stage in series for 24dB/oct; false = 12dB/oct (single stage). Resonance (0–100%)
  maps to Q = 0.5–20 (`damp = 1/Q`). Cutoff modulation: `mFilterEnvAmount` (±100%) scales
  its own dedicated **Filter ADSR** (`ADSREnvelope<T> mFilterEnv`, added 2026-07-20 -
  previously reused the amp envelope) as an octave offset on cutoff (±4 octaves at ±100%).
- Amp envelope (`ADSREnvelope<T> mAMPEnv`) unchanged from the iPlug2 example, but Decay/
  Release max extended from 1000ms to 4000ms (user wanted longer tails), and Release
  gained the same `ShapePowCurve(3.)` shape Attack/Decay already had (was missing —
  found via the WebView-side param display mismatch bug, see Known Issues). The Filter
  ADSR (`mFilterEnv`) added 2026-07-20 mirrors the same ranges/curve exactly but is a
  fully independent `ADSREnvelope<T>` instance and modulation-smoother slot
  (`kModFilterSustainSmoother`) - triggered/released alongside `mAMPEnv` in `Trigger()`/
  `Release()`, but its own Attack/Decay/Sustain/Release times and stage progress are
  tracked separately, so filter sweep timing can differ from the amp envelope.
- **Three independent LFOs** (added 2026-07-20): `mPitchLFO`/`mFilterLFO`/`mAmpLFO`
  (all `iplug::LFO<T>`, framework class), each with its own Shape/Rate(Hz)/Rate(Tempo)/
  Sync/Depth (`EModulations` gained `kModPitchLFO` (renamed from `kModLFO`),
  `kModFilterLFO`, `kModAmpLFO` - one modulation buffer per LFO, filled by 3 separate
  `ProcessBlock` calls). All set to **bipolar** (`SetPolarity(true)`) — default is
  unipolar, which only ever pushed its destination one direction.
  - **Pitch LFO**: unchanged behavior from the original single LFO - added directly
    (already-scaled to octaves via `SetScalar(depth/100.)`) to both oscillators' pitch,
    read once per block (`inputs[kModPitchLFO][0]`, not sample-accurate - pre-existing
    simplification, not touched).
  - **Filter LFO**: `SetScalar((depth/100.)*4.)` (±4 octaves at 100%, matching Filter Env
    Amount's convention) added directly into the cutoff's `pow(2, ...)` exponent alongside
    the Filter ADSR's contribution, read per-sample (`inputs[kModFilterLFO][i]`).
  - **Amp LFO**: `SetScalar(depth/100.)` (0-1, tremolo depth) - final output multiplied by
    `1 + inputs[kModAmpLFO][i]` each sample (bipolar LFO centered on 1x gain, so at 100%
    depth output swings between silence and 2x on the LFO's positive half - simple,
    standard tremolo, never flips polarity since scalar is clamped 0-1).
- LFO shapes gained a 6th option, **S&H** (sample & hold) - this required extending the
  shared framework file `iPlug2\IPlug\Extras\LFO.h` itself (`EShape::kSampleHold`,
  `LFO_SHAPE_VALIST` macro), not just FirstSynth's own code, since `LFO<T>` is a
  framework class used by any iPlug2 WebView project. Implementation: `DoProcess()` now
  detects a new cycle by phase wrapping (`phase < mPrevPhase`) and draws a fresh random
  value (same LCG technique as `FirstSynthOsc::Rand()`) held steady until the next wrap.
  Applies automatically to all 3 LFOs above since they share the same `LFO<T>` class.
- ~~MIDI CC7 (Channel Volume) multiplies final output independently of the Gain param
  (`mCCVolume`...)~~ - **changed 2026-07-23**, see progress.md's "CC7 now drives Gain
  directly" section below: `mCCVolume` was removed: CC7 now sets the Gain param itself
  via `SetParameterValue`, instead of a separate hidden multiplier.

## Effects chain (`FirstSynth_Effects.h`, added 2026-07-20)

Master-bus effects (not per-voice), owned directly by `FirstSynth` (`mChorus`/`mDelay`/
`mReverb` members in `FirstSynth.h`), applied in `FirstSynth::ProcessBlock` per-sample
*after* `mDSP.ProcessBlock` and the CC7 volume multiply, in this fixed order: **Chorus →
Delay → Reverb**. Each is its own small template class, all deliberately simple (no
fancier than the synth DSP elsewhere in this project):

- **`ChorusEffect`**: a short (≤60ms) stereo delay line read with linear-interpolated
  fractional position, modulated by a sine LFO (`Rate` 0.05–5Hz). `Depth` (0–100%) scales
  the modulation swing (2ms base up to +18ms). The two channels' read positions are
  offset by a fixed 2 samples for stereo width. `Mix` is a standard dry/wet crossfade.
- **`DelayEffect`**: a straightforward feedback delay line, `Time` (10–2000ms, `ShapePowCurve(2.)`
  so short times aren't impossibly fiddly to dial in), `Feedback` (0–95%, capped below
  100% for stability), `Mix`. Has a **Ping Pong** bool toggle - see the dedicated note
  below, this needed a real fix after the first attempt didn't audibly bounce.
- **`ReverbEffect`**: small Schroeder-style network - 4 parallel `DampedCombFilter`s per
  channel (each a feedback comb with a one-pole lowpass in the feedback path - the classic
  Freeverb "damping" trick that absorbs high frequencies each pass) summed and run through
  2 series `AllpassFilter`s per channel. Comb/allpass delay times are fixed constants
  (classic Schroeder/Freeverb-ish tunings, slightly offset between L/R for stereo width -
  not exposed as params). `Decay` (0–100%) maps to comb feedback (0.7–0.98, stays stable),
  `Damping` (0–100%) is the one-pole coefficient directly, `Mix` is dry/wet.

**Ping-pong delay bug (found and fixed the same day it was added)**: the first
implementation just swapped which channel's *feedback* fed which buffer
(`bufferL = l + delayedR*fb`, `bufferR = r + delayedL*fb`) and produced **no audible
bounce at all**. Root cause: this synth's voice output is mono (`outputs[1][s] =
outputs[0][s]` in `FirstSynth_DSP.h`), so `l == r` going into the delay every sample -
swapping two *equal* feedback values is a no-op, so both delay lines stayed identical
forever regardless of the swap. **Fix**: true ping-pong needs an asymmetric entry point -
dry signal is injected into *only* the left buffer; the right buffer receives *only* the
cross-fed-back left signal, nothing dry directly. Verified by hand-tracing the impulse
response: repeat 1 (amplitude 1) comes out left, repeat 2 (amplitude `feedback`) comes out
right, repeat 3 (`feedback²`) left, etc. - genuinely alternating. **If ping-pong-style
stereo bouncing is ever needed elsewhere (e.g. a future effect), remember: swapping
feedback alone does nothing when the two channels' dry input is identical - the asymmetry
has to be in* where the dry signal enters*, not just in the feedback routing.**

`FirstSynth::OnParamChange` handles all effect params directly (chorus/delay/reverb
setters) before falling through to `mDSP.SetParam(...)` in the `default:` case - these
effects live outside `IPlugInstrumentDSP`, so `mDSP.SetParam` never sees them.
`FirstSynth::OnReset` calls `SetSampleRate` on all three (delay/chorus line buffers are
sized off sample rate, must be reallocated on rate changes).

## Preset save/load (added 2026-07-20, Standalone only)

File → **Save Preset...** / **Load Preset...** in the Standalone app's menu (`resources/main.rc`,
new `ID_SAVE_PRESET`/`ID_LOAD_PRESET` in `resources/resource.h`, values 40030/40031 -
these two files are project-owned, safe to edit directly). The actual save/load logic
lives in the **shared framework file** `iPlug2\IPlug\APP\IPlugAPP_dialog.cpp`'s
`MainDialogProc` WM_COMMAND switch, guarded by `#if defined(OS_WIN) && defined(ID_SAVE_PRESET)`
(same pattern as the existing `#ifdef ID_SCREENSHOT` block) - so it only compiles in for
projects that actually define those resource IDs, and doesn't affect other iPlug2 projects
sharing this checkout. No project-side C++ (FirstSynth.cpp/.h) changes were needed at all -
this uses iPlug2's existing, already-correct state (de)serialization:

- **Save**: Win32 `GetSaveFileNameA` file dialog (needed adding
  `#include <commdlg.h>` + `#pragma comment(lib, "comdlg32.lib")` to
  `IPlugAPP_dialog.cpp` - comdlg32 wasn't in `APP_LIBS` in `common-win.props`, and the
  pragma was chosen over editing that shared props file to keep the change scoped to one
  file) → `pPlug->SerializeState(chunk)` (an `IByteChunk`, this is iPlug2's standard
  per-param serialization, already correctly implemented in the framework's
  `IPluginBase::SerializeParams`) → raw `fwrite` of `chunk.GetData()`/`chunk.Size()` to a
  `.preset` file. No custom format invented - it's just the framework's own byte-for-byte
  param dump.
- **Load**: `GetOpenFileNameA` → `fread` the whole file → `IByteChunk::PutBytes(...)` →
  `pPlug->UnserializeState(chunk, 0)` (sets every param's value **and** calls
  `OnParamChange` for each via the default `OnParamReset(kPresetRecall)`, so DSP/effects
  state updates automatically - `FirstSynth::OnParamChange`'s existing per-param switch
  handles this with zero extra code) → **`pPlug->OnRestoreState()`** to push the restored
  values to the WebView UI (this is the key call that's easy to miss - `UnserializeState`
  alone updates params+DSP but does *not* touch the UI; `OnRestoreState()`'s default
  implementation is exactly `SendCurrentParamValuesFromDelegate()`, the same mechanism
  used when the UI first opens - see `IPlugEditorDelegate.h` around `OnUIOpen()` for the
  parallel).
- File format is just the raw `IByteChunk` bytes (a `.preset` extension, filter string
  built from `PLUG_NAME`) - not a `.vstpreset`/`.fxp`/any DAW-standard format, since this
  is Standalone-only for now. If VST3/CLAP-hosted preset save/load is ever wanted, that
  goes through the *host's* preset browser via the same `SerializeState`/`UnserializeState`
  pair, not this Win32-dialog code path (which only exists in the Standalone app's window
  proc) - would need separate, format-specific wiring, not just enabling `PLUG_DOES_STATE_CHUNKS`
  blindly.

## Param list (FirstSynth.h `EParams`, indices matter for the WebView `param-id` attrs)

```
0  Gain            9  Pitch LFO Sync       18 Osc2 Fine           27 Filter Attack
1  Note Glide      10 Pitch LFO Depth      19 Mix Osc1             28 Filter Decay
2  Attack          11 Osc1 Wave            20 Mix Osc2            29 Filter Sustain
3  Decay           12 Osc1 Octave          21 Mix Noise           30 Filter Release
4  Sustain         13 Osc1 Semi            22 Filter Cutoff       31 Filter LFO Shape
5  Release         14 Osc1 Fine            23 Filter Resonance    32 Filter LFO Rate(Hz)
6  Pitch LFO Shape 15 Osc2 Wave            24 Filter Type(LPBPHP) 33 Filter LFO Rate(Tempo)
7  Pitch LFO Rate  16 Osc2 Octave          25 Filter Slope(24dB)  34 Filter LFO Sync
   (Hz)            17 Osc2 Semi            26 Filter Env Amount   35 Filter LFO Depth
8  Pitch LFO Rate                                                 36 Amp LFO Shape
   (Tempo)                                                        37 Amp LFO Rate (Hz)
                                                                   38 Amp LFO Rate (Tempo)
                                                                   39 Amp LFO Sync
                                                                   40 Amp LFO Depth

41 Chorus Rate       45 Delay Feedback     48 Reverb Decay      51 Looper Reverse
42 Chorus Depth      46 Delay Mix          49 Reverb Damping    52 Looper Feedback
43 Chorus Mix        47 Delay Ping Pong    50 Reverb Mix        53 Looper Mix
44 Delay Time                                                   54 Bass Boost
                                                                 55 Filter Key Follow
```
(52 Looper Speed was removed 2026-07-22 - see FirstSynth.h's kParamLooperSpeed comment -
so 52/53 shifted down from the older Looper Feedback/Mix numbering; Bass Boost (54) and
Filter Key Follow (55) were appended after, per the project's "never renumber, always
append" convention for anything that could be in an existing saved preset.)

## WebView UI (resources/web/)

**Two pages**: `#page-synth` (the grid described below) and `#page-effects` - now has real
content (Chorus/Delay/Reverb panels, added 2026-07-20 in the same session as the DSP -
see "Effects chain" section above for what each knob does). Layout is a simple
`.effects-row` (flex row, `gap: 32px`, wraps) of 3 `.panel-XXX` sections (`panel-chorus`/
`panel-delay`/`panel-reverb`), each just an `<h2>` + `.knob-row` like the synth page's
sections - **not** the synth page's `.layout-grid`/`grid-template-areas` system (that's
overkill for 3 same-height panels in a row; only reach for the grid-area approach if this
page ever needs asymmetric spanning/alignment like the synth page does). Delay's panel
also has a `.toggle-container` "Ping Pong" switch (see Effects chain section for what it
actually does DSP-side and the bug that had to be fixed).

**Page switch**: a single persistent `#pageNavBtn` button lives in `.page-header` (top of
`<main>`, next to `<h1>1st synth</h1>`, shared by both pages rather than duplicated per
page) and *relabels itself* ("Effect →" / "← Synth") rather than being swapped for a
different button - went through a couple of iterations before landing here (first tried a
checkbox/slider toggle, which read as an on/off effect-bypass switch rather than
navigation; then tried a separate back-button on the effects page, but the user wanted
one button whose label just changes). `ToggleEffectsPage()` flips a module-level
`effectsPageActive` flag and calls `SetEffectsPage(showEffects)`, which sets `display`
on `#page-synth`/`#page-effects` and updates the button's `innerHTML`. This is a
**WebView-only UI concern**, not a plugin param - no `SPVFUI` call, no C++ side at all.
Both pages are direct children of `<main>`, after `.page-header`; the 300px bottom spacer
div stays outside both, at the very end of `<main>`, so it applies regardless of which
page is showing.

**Panic and Hold buttons** (added 2026-07-20) - both pure WebView-side, **zero C++/DSP
changes needed** since they just send standard MIDI CC messages that iPlug2's `MidiSynth`
(framework, `Extras/Synth/MidiSynth.cpp`) already auto-handles:
- **Panic** (`.page-nav-btn.panic-btn`, in `.page-header` after the Effect button with an
  extra `margin-left: 40px` for visual separation from navigation): `Panic()` sends
  MIDI CC 123 (`IMidiMsg::kAllNotesOff`) via `SMMFUI(0xB0, 123, 0)` - `MidiSynth` maps this
  to an immediate note-off for every voice on its own, no FirstSynth code involved.
- **Hold** (`#holdBtn`, styled with the `.test-note` class, placed right after the Test
  Note button inside `.panel-master`'s `.knob-row`): `ToggleHold()` is a latching on/off
  toggle (there's no physical sustain pedal in a WebView) that sends MIDI CC 64
  (`IMidiMsg::kSustainOnOff`) via `SMMFUI(0xB0, 64, holdActive ? 127 : 0)` - `MidiSynth`
  already implements standard sustain-pedal note-holding behavior. The `.active` CSS class
  (blue background, added to `.test-note.active`) gives it a pressed/latched look while
  held on.
- If a similar "send a standard MIDI CC and let the framework handle it" button is needed
  later, check `IPlugMidi.h`'s `IMidiMsg` enum for the CC's symbolic name first and
  `Extras/Synth/MidiSynth.cpp`'s `kControlChange` case - a lot of standard MIDI CCs
  already have built-in `VoiceAllocator` behavior, so it's often unnecessary to add any
  DSP-side code at all.

Layout (as of 2026-07-20) is a 3-column CSS Grid (`.layout-grid`, `grid-template-areas`
in `index.html`'s `<style>`), not a single top-to-bottom stack:

```
Oscillator 1 | Mixer      | Pitch LFO   (spans 2)
Oscillator 2 | (spans 2)  | Pitch LFO   (spans 2)
Filter       | Filter ADSR| Filter LFO  (spans 2)
Master       | Amp ADSR   | Amp LFO     (spans 2)
```

4 rows total (not 5) - Master moved down from its own top row into the bottom row,
column-aligned with Filter/Filter ADSR/Filter LFO directly above it (this took several
iterations to land on - the user wanted Master/Test Note at the *same height* as Amp
ADSR, matching a specific window size already calibrated for a 4-row layout, not a 5th
row appended below everything). Test Note lives *inside* the Master panel's own
`.knob-row` (as a plain `<button>` sibling to the Gain/Glide knob-controls), not as its
own grid section — earlier attempts gave it a standalone column/row but the user asked
for it inside Master's frame directly once that was suggested as an option.

Each section is its own `<div class="panel-XXX">` (`panel-master`, `panel-osc1`,
`panel-mixer`, `panel-pitchlfo`, etc.) assigned to a named grid-area via CSS, so adding/
moving a section means: add a `grid-area` name to `grid-template-areas`, add the matching
`.panel-XXX { grid-area: XXX; }` rule, and add/move the `<div class="panel-XXX">...</div>`
in the HTML body. Columns are sized `max-content` (each column only as wide as its own
content, not stretched) with `justify-content: start` - avoids the huge dead space that a
naive `1fr 1fr 1fr` produced when sections have very different natural widths. A section
that should align under a taller neighbor two rows up (e.g. `amplfo` under `filterlfo`,
both spanning 2 grid columns) needs the *same* area name repeated across the matching
column cells in `grid-template-areas` — column count must stay consistent across every
row string even where a row doesn't use a particular column (not applicable here anymore
since the standalone Test Note column was removed, but keep in mind if adding one back).

Compact styling (2026-07-20, "getting hard to see everything"): `--knob-size` shrunk
72px→48px, `--knob-label-gap`/`--knob-value-gap` 6px→4px, `.knob-row` gap 10px→6px,
`h2` margin 20px→14px top, container paddings `10px 14px 12px`→`6px 8px 8px` - label/value
font sizes (14px/12px) deliberately left untouched (asked not to shrink text, only the
knobs/spacing). `.layout-grid` `column-gap` is 32px (asked for more breathing room between
sections after the first compacting pass).

- `knob-control.js`: generic draggable knob web component, shared by nearly every param.
  Normalized-space dragging (see Known Issues #3) with optional `shape="pow"|"log"` +
  `shape-exponent` attrs to mirror the C++ param's `IParam::Shape` curve, `value-labels`
  for enum params that should show text instead of a raw number (e.g. LFO Rate(Tempo)
  shows "1/4", "1/1" etc, not "8.0"), `integer-display` to round whole-number params
  (Octave/Semitone) instead of showing decimals, and a `user-change` CustomEvent fired on
  any user-driven (not host-driven) change, for wiring up companion displays — see below.
  Also: `step="N"` (real-value units) snaps both the displayed value *and* the knob's own
  rotation to discrete detents rather than turning smoothly — used on Osc1/Osc2 Octave
  and Semi (`step="1"`), not on Fine (stays continuous). Double-click resets any knob to
  `default-value` (already populated per-param from the host's "params" JSON on load, via
  `defaultValue`/`realToNormalized` — wasn't wired to anything before this).
- Shape selector + Sync toggle are now duplicated 3x (Pitch/Filter/Amp LFO). Generalized
  in the inline `<script>`: each instance carries its own `data-param-id` attribute (on
  `.shape-selector` and the sync `<input>`) instead of a hardcoded element `id`, and
  `SetShape(paramId, btn)`/`SetSync(paramId, checkbox)`/`OnParamChange` all look up the
  right instance via `data-param-id` rather than assuming there's only one. Any 4th LFO
  in the future just needs its own `data-param-id`'d markup, no JS changes.
- `waveform-display.js`: canvas-based oscillator waveform preview. Mirrors
  `FirstSynthOsc::Morph` in JS (kept in sync manually — no shared source of truth with the
  C++, so any DSP waveform-shape change must be hand-mirrored here too). One instance per
  oscillator (`waveDisplay1`/`waveDisplay2`), fed by the matching Wave knob's `user-change`
  event (see Known Issues #4) — it has no interaction of its own, purely a readout.
- Custom UI widgets built ad hoc rather than as knob-control: the LFO Shape 6-icon (was
  5, S&H added 2026-07-20) selector (`SetShape()`/`UpdateShapeSelection()`, laid out
  3-wide/2-tall via `.shape-selector { display: grid; grid-template-columns: repeat(3, 44px); }`),
  Sync and 24dB-slope toggle switches (checkbox styled as a slider), all wired through the
  same `OnParamChange(param, value)` dispatcher in the inline `<script>` — new
  special-cased params need a branch added there.
- Dev-only computer-keyboard-as-MIDI-keyboard (`EnableComputerKeyboardInput()` in
  index.html): A/W/S/E/D/F/T/G/Y/H/U/J/K/O/L play one octave+ chromatically from C4 (MIDI
  60), Z/X shift octave. Only ever enabled in the Standalone app — gated via
  `FirstSynth::OnWebContentLoaded()` checking `#ifdef APP_API` and calling
  `EvaluateJavaScript("if (typeof EnableComputerKeyboardInput === 'function') {...}")` —
  never active when hosted as CLAP in a DAW (would hijack host shortcut keys).
- **Scrolling**: the actual scrollable box is `#scroll-container` (a dedicated wrapper div
  around `<main>`), *not* `body`/`html` — see Known Issues #11 for why. `body`/`html` are
  `overflow: hidden`; `#scroll-container` has `overflow: auto` and holds the padding that
  used to live on `body`. The 300px empty spacer div at the end of `<main>` is still needed
  (padding-bottom alone isn't reliably enough for the last row to scroll fully into view).
- **Default window size** (`PLUG_WIDTH`/`PLUG_HEIGHT` in config.h): **`1352`×`694` as of
  2026-07-21** - went through several re-calibrations the same session, each time by
  the same method (user manually resizes the live window to their preferred size,
  measured via `GetClientRect` and set directly, no conversion): 1440×780 (Known Issue
  #12) → 1347×713 (user's own resize, see 2026-07-21 session) → 1352×694 (user's own
  resize again, right after the value-box/grid-column locking fixes in Known Issues
  #14/#15 changed how much room the content needed). **Always re-measure and don't
  assume a previous number still fits** - this value has changed multiple times in one
  session already as layout details shifted. Before all of this, 1693×870 from
  2026-07-20 no longer fit this display - see Known Issue #12 for the original
  re-diagnosis. Sized so the grid fits without needing horizontal scroll (see Known
  Issues #11 - horizontal scroll doesn't actually work in this WebView embedding, so
  the working fix is "make the default window wide enough", not "make users scroll").
  A **UI Zoom dropdown** (Known Issue #13) now also lets the user shrink the actual
  window at runtime (100/90/80/70/60%, persisted) without changing this default.
  **The old "physical_client_px = PLUG_WIDTH * 0.8" empirical relationship below is
  STALE - do not use it anymore.** Direct measurement 2026-07-21 (`GetClientRect` on the
  live process) showed `ClientRect` now matches `PLUG_WIDTH`/`PLUG_HEIGHT` **exactly
  1:1** - the shared-framework DPI-double-scaling fix (ported from
  [[project-suikinkutsu-plugin]], see that project's "GUI clipped at high DPI" section)
  is confirmed in effect here too, which is what makes the old 0.8 conversion factor no
  longer apply. If the user asks to resize the default window again: have them resize
  the live window to whatever they want, measure it directly with
  `GetClientRect`/`GetWindowRect` on the `FirstSynth_x64` process (PowerShell, P/Invoke
  snippet in Known Issue #12 above), and set `PLUG_WIDTH`/`HEIGHT` to the measured
  `ClientRect` value directly (no `/0.8` or any other conversion) - confirmed by
  rebuilding and re-measuring the resulting default-launch size until it matches.
  Alternatively, for layout/content-sizing work specifically (not matching an
  already-resized live window), the faster browser-based measurement technique in
  Known Issue #12 avoids needing the native app rebuilt at all until the final check.

## Known issues found & fixed 2026-07-20

7. **Filter blow-up at high cutoff + low resonance**: `SVFStage` was a naive
   forward-Euler ("Chamberlin") state-variable filter (`f = 2*sin(pi*fc/fs)`, clamped
   to `fc <= 0.45*sampleRate`). That recurrence is only conditionally stable — the
   determinant of its state-update matrix is `1 - f*damp`, which exceeds 1 in magnitude
   (instability) once `f*damp > 2`. At max cutoff (`f` close to the theoretical limit
   of 2) this is violated for any `damp > ~1.0`, i.e. `Q < ~2`, which includes the
   **default 0% resonance setting** (`Q=0.5` → `damp=2`). Symptom matched exactly:
   turning Filter Cutoff to full open produced a click/pop then near-silence (state
   diverging to inf/nan). Root-caused via the stability math, not trial and error.
   **Fixed** by replacing the filter core with the standard Zavalishin/"TPT"
   (topology-preserving-transform) SVF, which uses prewarped `g = tan(pi*fc/fs)`
   instead of `f = 2*sin(...)` and is unconditionally stable for any cutoff below
   Nyquist regardless of damp/Q — no fragile clamp-tuning needed. Cutoff clamp
   loosened back up to `0.49*sampleRate` since the new math no longer needs the
   safety margin. `SVFStage`'s state variables renamed `mIC1eq`/`mIC2eq` to match
   the standard reference derivation (were `mLow`/`mBand`). Call sites (`FirstSynth_DSP.h`
   `ProcessSamplesAccumulating`) updated to compute `g` instead of `f`, same
   `Process(input, coeff, damp, ...)` call signature otherwise unchanged.
8. **Standalone silent output traced to Windows default playback device, not app
   code**: user was connected to a Bluetooth speaker but Windows' actual default
   output device had gotten switched to `Cable Input (VB-Audio Virtual Cable)`
   (a virtual audio device also installed on this machine) — audio was rendering
   fine into the DSP (`ProcessBlock` running) but going to a device with no
   physical output. FirstSynth's own Audio Settings dialog appearing to "not let
   you pick a device" (always reverting to "Primary Sound Driver") is **expected
   iPlug2/DirectSound behavior, not a bug** — "Primary Sound Driver" is a generic
   DirectSound entry that always follows whatever Windows' current default output
   device is; it can't be overridden to a fixed specific device that way. The fix
   is always at the OS level (Windows Settings → System → Sound → Output), not
   inside the plugin's own dialog. Diagnostic path that got here: confirmed GUI/
   knobs worked → confirmed CPU meter doesn't move on Test Note (so audio device
   never opened, ruling out a MIDI/note-trigger bug) → checked `settings.ini`
   (`C:\Users\a_wak\AppData\Local\FirstSynth\settings.ini`) and the in-app Audio
   Settings dialog → found `Cable Input` selected → traced to Windows' own default
   device, which the user hadn't realized had drifted away from their Bluetooth
   speaker.
9. **Noise mixer channel was silent regardless of the Noise Level knob**: leftover
   iPlug2 `IPlugInstrument`-example code computed `noise = mTimbreBuffer.Get()[i] * Rand()`
   — multiplying by the MPE/MIDI-CC74 "Timbre" voice-control input before the `mMixNoise`
   mixer gain was even applied. This project never sends a Timbre/CC74 message from
   anywhere (no MPE support, no UI for it, computer-keyboard input doesn't send it), so
   that input stayed at its default of 0 forever, silencing the noise channel completely
   no matter how high Mix → Noise was turned up. Found right after adding the Filter ADSR
   (issue below) when the user went looking for the noise and couldn't hear it. **Fixed**
   by removing the dead `mTimbreBuffer` multiply entirely — `noise = Rand();` now, with
   `mMixNoise` as the sole level control, matching every other mixer channel. Also removed
   the now-unused `mTimbreBuffer` member, its `Resize()` call, and the
   `mInputs[kVoiceControlTimbre].Write(...)` call that fed it (all dead code once the
   multiply was gone).
10. **Filter ADSR added** (this was a requested feature, not a bug, but noted here since
    it changed `EParams`/`EModulations` layout): `mFilterEnvAmount` now scales a fully
    independent `ADSREnvelope<T> mFilterEnv` (params 27–30: Filter Attack/Decay/Sustain/
    Release) instead of reusing `mAMPEnv`. Added `kModFilterSustainSmoother` to
    `EModulations` (inserted before `kModLFO`) for smoothing the Filter Sustain param the
    same way the amp envelope's sustain is smoothed. `mFilterEnv` is triggered/released
    together with `mAMPEnv` in `Voice::Trigger()`/`Release()` but otherwise runs on its own
    clock. New UI section "Filter ADSR" added between "Filter" and "Amp / ADSR" in
    `index.html`, same `shape-exponent="3"` treatment as the amp ADSR's Attack/Decay/Release.
11. **Horizontal scroll doesn't work in this WebView embedding at all - NOT fixed, worked
    around instead.** After the 3-column grid layout made the UI wider than a small window,
    content past the right edge couldn't be reached by scrolling, even at max window size.
    Investigation (don't redo this, it's a dead end):
    - Confirmed CSS itself was internally consistent throughout:
      `document.body.scrollWidth` (1388) > `clientWidth` (886), and forcing
      `scrollLeft = 9999` correctly clamped to the true max (`501.6 ≈ scrollWidth -
      clientWidth`, read back via console) - so the DOM's own scroll model was never
      the problem.
    - Found and fixed a real (separate, smaller) bug along the way: `body { overflow:
      auto }` makes the *spec* propagate that overflow to the viewport, so
      `document.body.scrollLeft` is a no-op - the real scrolling element becomes
      `document.scrollingElement`/`documentElement`. Fixed by moving overflow off
      `body`/`html` entirely onto a dedicated `#scroll-container` div wrapping `<main>`
      (see WebView UI section above). This is a legitimate, worthwhile fix (predictable,
      directly-addressable scroll element) but **did not solve the visible symptom** -
      even `document.getElementById('scroll-container').scrollLeft = 9999` clamped
      correctly per the console but the visible content was still cut off at exactly
      the same point, flush with the window's right edge (no blank background gap -
      ruled out a WebView-bounds-too-small theory, since a bounds gap would show as
      background, not a hard content cutoff).
    - Tried forcing WebView2's ZoomFactor to match the real monitor DPI scale in
      `IWebViewImpl::SetWebViewBounds` (`iPlug2\IPlug\Extras\WebView\IPlugWebView_win.cpp`)
      on the theory that Bounds (correctly DPI-scaled via `GetScaleForHWND`) and
      ZoomFactor (always the caller's default of `1.0`, from
      `WebViewEditorDelegate::Resize`/`OnParentWindowResize`/`OpenWindow` in the shared
      `IPlugWebViewEditorDelegate.cpp`) were inconsistent. **Made zero observable
      difference** (user confirmed rendering was pixel-identical before/after) -
      reverted; don't retry this exact change without new evidence.
    - Given the WebView2 control's real screen bounds are confirmed correctly sized
      (content does reflow to fill a resized window, e.g. tested at 1550×878), but
      scrolling to reveal overflow past those bounds doesn't work, this looks like a
      deeper WebView2/Chromium compositor quirk in this specific embedding, not a CSS
      or C++ bug either one of us could find by inspection.
    - **Working fix instead**: don't rely on scroll for this layout at all - size the
      default window (`PLUG_WIDTH`/`PLUG_HEIGHT`) wide enough that the 3-column grid
      fits without overflowing, since resizing the window itself is confirmed to work
      correctly. See "Default window size" in the WebView UI section above for the
      calibration procedure (note the counterintuitive `physical = logical * 0.8`
      relationship found on this machine's 125% scaling, not `* 1.25` as naively
      expected - re-derive by direct measurement if the user's DPI setting differs).

## Known issues found & fixed 2026-07-21

11. **Knob needed a second touch to respond**: same symptom independently reported and
    fixed in [[project-suikinkutsu-plugin]] (`resources/web/knob-control.js` there) -
    ported the same fix here since this project shares that same component (copied
    when SuiKinKutsu was created from this project). `startDrag` used to listen for
    `mousedown`/`touchstart`; switched to `pointerdown`
    (and `pointermove`/`pointerup`/`pointercancel` on `document` for the drag itself,
    replacing `mousemove`/`mouseup`/`touchmove`/`touchend`). **Confirmed fixed by the
    user 2026-07-21** after rebuilding both Standalone and CLAP and testing a knob's
    first click live - no longer needs a second touch. The window-activation/
    `pointerdown`-vs-`mousedown` hypothesis is now the accepted explanation (not
    re-investigated further natively, but the fix works in practice).
12. **Default window opened larger than the display, partially off-screen** (user
    report 2026-07-21: "ウィンドウが大きすぎる"). Root-caused via direct measurement,
    not the old "Possible next steps" DPI-clipping theory:
    - `GetWindowRect`/`GetClientRect` (PowerShell, `user32.dll` P/Invoke) on the live
      process showed `ClientRect` now matches `PLUG_WIDTH`/`PLUG_HEIGHT` from
      `config.h` **exactly 1:1** (1692×869 measured vs 1693×870 configured). This
      confirms the shared-framework DPI-double-scaling fix (see "GUI clipped at high
      DPI" in [[project-suikinkutsu-plugin]]'s progress.md, applies here automatically
      since both projects share the same `iPlug2` checkout - grepped
      `IPlugAPP_dialog.cpp` and found the `GetScaleForHWND` calls already present) is
      genuinely in effect here. **This means the old empirical "physical_client_px =
      PLUG_WIDTH * 0.8" relationship documented in Known Issue #11 below is now
      stale/wrong** - it was true only when the double-scaling bug was still present.
      Don't use that formula anymore; `PLUG_WIDTH`/`HEIGHT` now map directly 1:1 to
      logical/CSS pixels with no conversion needed, on this machine's 125% scaling at
      least.
    - So the oversized window wasn't a DPI bug at all - `PLUG_WIDTH`/`HEIGHT`
      (1693×870, calibrated back when the 0.8 relationship was still true) were
      simply larger than this display's `WorkingArea` (1536×864 logical px,
      confirmed via .NET `Screen.PrimaryScreen`), so the window got shoved partly
      off-screen (`WindowRect.Left` was negative) to fit.
    - **Real content didn't actually need to be that wide.** Measured the true
      minimum content size a different way this time - loaded `index.html` in a
      *real browser* (via a temporary local `python -m http.server` in
      `resources/web/`, navigated to from the Claude_Browser preview pane; a plain
      `file://` load renders as an inert static snapshot with no JS, so the
      `knob-control` custom elements never upgrade and can't be measured) at a large
      2000×1200 viewport, then read `document.querySelector('.layout-grid').scrollWidth`
      /`document.querySelector('main').scrollHeight` directly. Found the true
      natural content size was only **~1393×760** (`#scroll-container`'s
      `scrollWidth`/`scrollHeight`, including padding) - far under the old
      1693×870, no CSS shrinking (knob size, gaps) needed at all.
    - The ~110px of unnecessary height turned out to be almost entirely one thing:
      a leftover **`<div style="height: 300px; flex-shrink: 0;"></div>`** at the end
      of `<main>` (originally added as a scroll-headroom workaround for the
      horizontal-scroll bug, see Known Issue #11 below) - the same leftover spacer
      [[project-suikinkutsu-plugin]] had already found and removed on 2026-07-20.
      **Removed here too** (`resources/web/index.html`) - confirmed via live DOM
      measurement in the browser that removing it alone dropped `main.scrollHeight`
      from ~948 to ~648 (viewport-independent measurement, before adding back
      padding).
    - **Fix**: removed the 300px spacer div; left all other CSS (knob size, gaps,
      padding) untouched - the spacer removal alone gave enough margin. Set
      `PLUG_WIDTH`/`PLUG_HEIGHT` in `config.h` to **1440×780** (content's measured
      ~1393×760 plus a ~30-50px safety buffer for WebView2-vs-test-browser rendering
      differences). Rebuilt both Standalone/CLAP, relaunched, re-measured via
      `GetWindowRect`: window opened fully on-screen (`Left=71, Top=24,
      Right=1525, Bottom=862`, all within the 1536×864 `WorkingArea`, no negative
      coordinates). **Confirmed by the user** - window fits, and right/bottom
      margins are "more than enough" (i.e. there's slack to shrink further later if
      ever wanted, not currently needed).
    - **This resolves the "Possible next steps" pending GUI-clipping item below** -
      turned out to be a plain oversized-default-window issue once actually
      measured, not the DPI-scale bug that item assumed (that bug was separately
      confirmed already-fixed via the 1:1 ClientRect measurement above, it just
      wasn't the cause of *this* symptom).
    - **Reusable technique for future window/layout sizing work on this project or
      SuiKinKutsu**: spin up `python -m http.server` in `resources/web/`, open it via
      `preview_start({url: "http://localhost:PORT/index.html"})` (plain `file://
      ` navigation in the Claude_Browser pane renders outside-project files as an
      inert static snapshot with no JS - custom elements never size correctly that
      way), resize the viewport large via `resize_window` so nothing wraps, then read
      `scrollWidth`/`scrollHeight` off `.layout-grid`/`main`/`#scroll-container` via
      `javascript_tool`. Much faster and more reliable than the old approach of
      asking the user to resize the live native window and eyeball/measure it.
13. **UI Zoom preference added** (user request: "普通のユーザーはもっと小さなウィンドウで
    操作したいだろう" - not everyone wants the full-size window; wanted a 70-80%-style
    scale-down option, proportions preserved). Added a `<select id="uiScaleSelect">`
    (100/90/80/70/60%) in `.page-header`, `ApplyUIScale(scalePercent)` in `index.html`'s
    inline script:
    - Applies `document.querySelector('main').style.zoom = scalePercent/100`. **Tried
      `transform: scale()` first - looked right (correct visual size via
      `getBoundingClientRect`) but left a scrollbar**, because this WebView2/Chromium
      build's `overflow:auto` scrollable-area calculation for `#scroll-container`
      does *not* shrink to match a descendant's post-`transform` visual size, only its
      pre-transform layout box. The non-standard-but-Chromium-supported `zoom`
      property actually changes layout (like a real per-subtree DPI change), so the
      container's `scrollWidth`/`scrollHeight` shrink correctly with it - confirmed by
      direct `scrollWidth`/`clientWidth` comparison before switching. **If any future
      zoom/scale feature is needed, use `zoom`, not `transform: scale()`, on this
      project** (same would likely apply to [[project-suikinkutsu-plugin]] if asked
      there - not yet tried).
    - Also triggers a **real native window resize**, not just a visual scale: sends
      `SAMFUI(kMsgTagSetUIScale, scalePercent, 0)` → C++ `FirstSynth::OnMessage`
      (`FirstSynth.cpp`, `FirstSynth.h`'s new `EMsgTags` enum) computes
      `newW/H = round(PLUG_WIDTH/HEIGHT * scalePercent/100)` and calls the inherited
      `WebViewEditorDelegate::Resize(newW, newH)` - this is a real, existing iPlug2
      mechanism (`EditorResizeFromUI` → `IPlugAPP::EditorResize` → `SetWindowPos` on
      Windows since `NO_IGRAPHICS` is defined for this project) already used for
      drag-to-resize editors; nothing framework-level needed to change, just a new
      call site. Same code path resizes the CLAP host's view too (generic
      `EditorResize` override chain), not just Standalone.
    - **Persisted via `localStorage`** (`firstSynthUIScalePercent` key) - simplest
      option, no native settings-file work needed since this project has no other
      WebView-persisted prefs yet. Restored on launch from the `OnMessage`'s `"params"`
      case (the point where the WebView↔host bridge is confirmed up) - **the window
      briefly opens at 100% then snaps down to the saved scale on every launch if a
      non-100% value was saved**; not eliminated, would need native-side (settings.ini)
      persistence to open pre-scaled, judged not worth the complexity for a cosmetic
      startup blip.
    - **Follow-up bug, same day**: the ~100px left-margin request (see #14 note below)
      was implemented as fixed `padding-left` on `#scroll-container`, which is *not*
      the zoomed element (`main` is). At lower zoom the fixed padding ate a growing
      share of the now-smaller window (padding doesn't shrink, but window and content
      both do), squeezing knob-rows into wrapping and overflowing **vertically** -
      confirmed reproduced at 60% zoom via the browser-harness technique from #12
      (`document.querySelector('main').style.zoom=0.6` at a 60%-sized viewport gave
      `scrollHeight > clientHeight`). **Fixed** by moving all of `#scroll-container`'s
      padding onto `main` itself (`box-sizing:border-box`) so it scales with `zoom`
      like everything else inside `main` - confirmed fixed at 100/80/60% via the same
      harness. **Any future fixed-looking spacing must live on `main` (or something
      inside it), never on `#scroll-container` directly, or it'll silently stop
      scaling with zoom again.**
14. **Knob value-text digit-count changes reflowed the layout while dragging**
    (e.g. Filter Cutoff's displayed text growing from "20.0 Hz" to "20000.0 Hz" widened
    its `.knob-container`, nudging everything after it in the row/column). Two-part fix
    in `resources/web/knob-control.js`, both apply to every knob project-wide, not
    just the ones mentioned below:
    - **Fixed-width value box**: new `sizeValueBox()` sets the `.value` element's
      `min-width` to `Nch`, where `N` is the length of the *longest* string this
      specific knob could ever show (computed from `this.minValue`/`this.maxValue`,
      or the longest `value-labels` entry) - narrow-range knobs stay narrow,
      wide-range knobs (e.g. 20-20000 Hz) get more room, but no single knob's box
      changes width while dragging. Added `text-align:center` + `white-space:nowrap`
      to `.value` so shorter values stay centered in the reserved space rather than
      hugging one edge. Verified via a temporary local `python -m http.server` +
      Claude_Browser preview (see #12's technique) with a knob's `min`/`max`
      attributes set to simulate real host data (a plain `file://` load has no real
      host, so `min`/`max` never arrive and every knob defaults to 0-100) -
      `getBoundingClientRect().width` on `.value` confirmed identical at both ends of
      several params' ranges.
    - **This alone made every value box reserve its widest-case width even when
      showing a short value, which widened the grid overall enough to bring back a
      scrollbar** (user report: "余裕を持ってレイアウトしたぶん左右のスクロールバーが
      出ました"). Fixed per-param, not by shrinking the box mechanism itself:
      - Filter Cutoff kept `integer-display` (already had it) - "20 Hz".."20000 Hz",
        5 digits max, no decimals ever (Hz values don't need decimal precision).
      - **Amp Attack/Decay/Release AND Filter Attack/Decay/Release** (`kParamAttack`/
        `Decay`/`Release`/`FilterAttack`/`FilterDecay`/`FilterRelease`, all 1-4000ms
        range): user explicitly rejected plain `integer-display` here ("低い値のときの
        正確性が失われます" - loses precision at low values) and asked for **"有効
        桁数を最大4桁"** (max 4 total digits) instead - decimals should only drop once
        the integer part itself needs all 4 digits, not always. Added a new
        `max-digits` attribute/`this.maxDigits`: `decimals = max(0, maxDigits -
        integerDigitCount(absValue))`. At `maxDigits="4"`: "2.000 ms" (1+3) at the low
        end, "109.9 ms" (3+1) mid-range, "4000 ms" (4+0) at the top - self-adjusting,
        no per-range special-casing needed, and it's the same formula
        `sizeValueBox()`'s width calc already reuses (via the shared
        `formatDisplayValue()` helper), so the box width calc and the actual
        displayed text can never disagree.
      - After this digit-trimming, one column still overflowed by ~5px (the
        Filter-ADSR-heavy `filterenv` column) - closed by trimming `main`'s own
        right padding from 10px to 4px, verified back to zero overflow via the same
        browser-harness technique with every touched param's `min`/`max` set to its
        real worst-case range simultaneously.
15. **Parameter grid columns: fixed-px lock attempted, then reverted the same day**
    (user asked: "パラメータのエリアが動かないように固定してみてください" - wanted a
    second, structural layer of protection beyond #14's per-knob fix). First attempt
    changed `.layout-grid`'s `grid-template-columns` from `max-content max-content
    max-content max-content 20px` to fixed `450.1px 283.7px 473px 20px`, with widths
    measured via the browser-harness technique (temporary local HTTP server,
    `resize_window` to 2000×1200, several wide-range params' `min`/`max` set to their
    real worst case). **This broke the layout badly in the real app** (user: "固定に
    したときにレイアウトが完全に崩れました") - root cause: the measurement only set
    `min`/`max` on a handful of the widest-looking params (Cutoff, the three ADSRs'
    Attack/Decay/Release), not *every* knob in each column. A broader re-measurement
    afterward (simulating min/max on ~20 params across all three columns, including
    ones not touched the first time, e.g. Osc Fine ±100ct) showed the LFO column
    actually needs ~476px, not the 473px hardcoded - narrow enough to matter once
    real host data (wider than this test's default 0-100 fallback for every
    unsimulated param) loaded in the native app. **Reverted same day** back to
    `max-content max-content max-content 20px` - kept the harmless part of the change
    (simplifying 5 grid-template-areas columns to 4, since "pitchlfo"/"filterlfo"/
    "amplfo" always spanned the old columns 3+4 together and never used them
    separately) but dropped the fixed-width lock entirely. **Conclusion: don't
    hardcode this grid's column widths** - `max-content` combined with #14's per-knob
    fixed-width value box already prevents the actual reported problem (layout
    shifting while a knob is dragged); a full structural lock would need every single
    knob's true worst-case width measured with 100% coverage to be safe, which is
    fragile to maintain by hand as knobs are added/changed. If asked to revisit this,
    reread this entry first rather than re-attempting the same fixed-width approach.
16. **UI Zoom (Known Issue #13) showed a scrollbar at 70% and 60%, but not 100/90/80%**
    (user: "70%以下でスクロールバーが出る"). Reproduced via the browser-harness
    technique at the exact native window size each zoom level resizes to
    (`round(1352*0.7)` etc.) with the same ~20-param worst-case simulation from #15 -
    confirmed real overflow at 70% (952px content in a 946px window) even though 100%
    itself measured as an exact `scrollWidth == clientWidth` fit with no visible
    overflow. **Root cause / measurement caveat worth remembering**: `scrollWidth ==
    clientWidth` at 100% is not proof of zero margin - `.layout-grid` is a normal
    block element, so its own box always expands to fill `main`'s available width
    regardless of how much of that width its `max-content` columns actually use;
    genuine slack has to be measured from the *rightmost real panel's own*
    `getBoundingClientRect().right`, not the container's `scrollWidth`. Doing that
    measurement found only ~0px of true margin at 100% pre-fix (tight enough that
    rounding/sub-pixel snapping differences at other zoom percentages could tip it
    into overflow, which is what happened at 70%/60%). **Fixed** by reducing
    `.layout-grid`'s `column-gap` from 20px to 14px, re-verified with the same
    technique at 100/70/60% - now genuinely ~54px of margin at 100% (measured via the
    rightmost-panel technique above), comfortably proportional at every zoom level
    with room to spare. **If a similar "fits at 100% but not at some zoom %" report
    ever comes up again, measure real content-edge margin this way first, not just
    `scrollWidth`/`clientWidth` equality - the latter can look perfect while genuine
    slack is actually zero.**

## Known issues found & fixed earlier sessions (don't reintroduce these)

1. **iPlug2 upstream bug**: `WebViewEditorDelegate::OpenWindow()` never sets the initial
   WebView bounds, so a CLAP host that doesn't send a follow-up resize (REAPER) leaves the
   WebView at ~1×1px (page loads fine per window title, just invisible). Standalone
   survived it by accident via a later incidental resize. **Fixed at the framework level**
   in `iPlug2\IPlug\Extras\WebView\IPlugWebViewEditorDelegate.cpp` — added
   `SetWebViewBounds(0, 0, GetEditorWidth(), GetEditorHeight());` right after
   `OpenWebView(...)`. Applies to any iPlug2 WebView project, not just this one.
2. **Param curve/shape mismatches between C++ and the WebView knob**: `knob-control.js`
   originally always did linear normalized↔real conversion. Any param using
   `IParam::ShapePowCurve` (Attack/Decay/Depth) or `ShapeExp`/`InitFrequency` (LFO Rate Hz,
   Filter Cutoff) displayed and *behaved* wrong (e.g. default 10ms Attack showed "208.9ms")
   because the knob's own drag→value math didn't match the host's real curve. Fixed by
   adding `shape`/`shape-exponent` attrs to knob-control.js that mirror
   `ShapePowCurve`/`ShapeExp`'s exact formulas — **any new param with a non-default Shape
   argument in its `InitDouble`/`InitFrequency` call needs a matching `shape=...` attribute
   on its knob-control tag, or it'll silently misbehave the same way.** This bit us again
   with Release (had `shape-exponent="3"` in HTML from the start but the C++ `InitDouble`
   call was missing `IParam::ShapePowCurve(3.)` — always add both sides together.
3. Knob-control originally computed *drag* deltas in real-parameter-value units, which for
   a pow-curve param meant the same mouse distance felt wildly different depending on
   where in the range you were (fast near one end). Fixed by making the drag/wheel handlers
   operate purely in **normalized [0,1] space** (`currentNorm`, not a real-value
   `currentValue`) — the curve now only affects the displayed number and the DSP-side
   real value, never the knob's rotational feel.
4. A WebView-only companion display (waveform-display) that has no drag of its own cannot
   rely on the host's `OnParamChange` round-trip for live feedback while the user is
   mid-drag on a *different* control (the Wave knob) — hosts don't echo self-originated
   changes back during an active drag. Fixed with a `user-change` CustomEvent dispatched
   from knob-control on every user-driven update, listened for directly by the companion
   display. Any future "control B mirrors control A's live value" pairing needs this same
   pattern, not a naive OnParamChange listener.
5. **Overriding `OnWebContentLoaded()` without calling the base class silently deleted the
   entire params-JSON + `SendCurrentParamValuesFromDelegate()` sync** (that's where iPlug2's
   `WebViewEditorDelegate` base implementation lives) — every knob froze at its JS-default
   placeholder value forever, with zero console errors, because nothing was actually
   broken JS-side; the C++ side just never sent anything. Any override of this method
   **must** call `EDITOR_DELEGATE_CLASS::OnWebContentLoaded();` first.
6. Study One Prime can't host any 3rd-party plugin at all (tier-locked, not
   format-specific); Bespoke Synth and Renoise don't support CLAP (as of 2026-07). REAPER
   7.78 (free unlimited eval) is the confirmed working CLAP test host on this machine.

## Debugging notes

- My own simulated mouse clicks/drags on the native FirstSynth window (via PowerShell
  `mouse_event`/`SetCursorPos`) have been unreliable all session — sometimes no-ops for
  unclear reasons. Don't trust a "no visible change" result from my own automated click as
  proof of a bug; ask the user to actually interact with it before concluding something's
  broken. DevTools console (right-click → Inspect, now reliably available since
  `SetEnableDevTools(true)` is called unconditionally in the constructor) has been the
  more trustworthy diagnostic path when something's genuinely wrong JS-side.
- The user built a small always-on-top CPU meter (`FirstSynth\tools\CPU_Monitor.bat` +
  `cpu_monitor.ps1`) showing the FirstSynth process's CPU% live. Launch background/detached
  tools via `Start-Process`, not shell `&` — backgrounding through the sandboxed Bash
  tool's job tracking killed it once when launched that way.
- **My own PowerShell screen-capture (GetWindowRect + CopyFromScreen) is unreliable to the
  point of being unsafe to rely on this session** - twice captured completely unrelated
  windows/content (once what looked like a browser tab with unrelated video content)
  instead of FirstSynth, apparently because the window handle/rect goes stale between the
  "get rect" and "capture" steps (separate tool calls), or focus shifts in between. Always
  re-verify `MainWindowTitle` and sane (non-negative, reasonably-sized) rect coordinates
  in the *same* call immediately before capturing, and still treat the result with
  suspicion - asking the user to look themselves and describe what they see is more
  reliable than trusting a screenshot blind. Don't spend much effort chasing this further;
  just fall back to non-visual checks (process running/responding, DevTools console
  output the user pastes) when screenshots are unavailable or suspect.
- For anything involving actual on-screen pixel layout/scroll behavior, get the user to
  check and describe it directly rather than trying to verify visually yourself - see
  Known Issues #11 for how much back-and-forth diagnosing scroll behavior via
  `DevTools console` numbers took even without the screenshot problems above.

## Status as of last session (2026-07-20)

**Development paused here as of 2026-07-20** - the user is moving on to a new, separate
synth project next. Nothing here is broken or mid-change; this is a clean stopping point,
all features below confirmed working. Resume by reading this file top to bottom if work
picks back up on FirstSynth specifically.

Everything above is implemented and confirmed working by the user: dual-osc + mixer +
filter signal chain, continuous waveform morph with working visual, MIDI CC7 volume,
computer-keyboard MIDI input (standalone only), extended ADSR ranges, Octave/Semi knobs
snapping to integer detents with double-click reset to default (all knobs, via
`step`/`default-value` in knob-control.js), a dedicated Filter ADSR independent from the
amp envelope, noise mixer channel fixed (was silently multiplied by zero, see issue #9),
a stable TPT-SVF filter confirmed not to click/blow up at max cutoff (issue #7), **three
independent bipolar LFOs** (Pitch/Filter/Amp, each with its own Shape/Rate/Sync/Depth,
now including a 6th S&H shape - see the DSP and Known Issues sections above), a
reorganized 4-row grid layout (Mixer beside the oscillators, Filter LFO/Amp LFO beside
Filter/Filter ADSR/Amp ADSR, Master+Test Note sharing the bottom row aligned under
Filter), a compacted knob/spacing style, a page-switching UI (single relabeling
"Effect →"/"← Synth" button, see WebView UI section), a real **effects chain**: Chorus,
Delay (with a working Ping Pong mode - took one bug-fix pass, see Effects chain section
for why the first attempt was silent), and a small Schroeder-style Reverb, Panic/Hold
buttons (pure WebView, standard MIDI CCs the framework already handles), a finer-control
pow-curve on the Noise Level knob, and **Standalone preset save/load** (File menu, see
"Preset save/load" section - Standalone-only, not wired for VST3/CLAP host preset
browsers). All confirmed working by the user. No open DSP or param bugs known at last
check-in. Both Standalone and CLAP rebuilt and confirmed working after every fix above.

**One open UI limitation, accepted rather than fixed** (see Known Issues #11): horizontal
scrolling doesn't visually work in this WebView embedding, root cause not found despite
real investigation. Worked around by sizing the default window (now 1352×694 as of
2026-07-21, see "Default window size" in the WebView UI section - re-measure if the
layout changes again) wide enough that the grid never needs horizontal scroll at this
user's current display scaling (125%). If the user changes displays/scaling or asks for
another layout change that makes the grid wider again, the horizontal-scroll problem
will resurface — don't rediscover it from scratch, re-read Known Issues #11 first.

## Status as of 2026-07-21

Development resumed after the 2026-07-20 pause. A long session - everything below
confirmed working live by the user, Standalone and CLAP rebuilt after every change:

1. The pointer-event knob fix ported cold from [[project-suikinkutsu-plugin]] last
   session (Known Issue #11 above) - **confirmed working**, a knob responds on the
   first click.
2. Default window opened too large for the display, partly off-screen - fixed by
   removing a leftover 300px bottom spacer div (Known Issue #12 above), then
   re-calibrated to the user's own preferred size **twice more** as other changes
   shifted how much room the content needed (see Known Issue #13 for the middle one,
   #14/#15 for the final two) - **final default is 1352×694**, set by directly
   measuring the user's own live-resized window each time, not guessed.
3. **UI Zoom preference** (100/90/80/70/60% dropdown, Known Issue #13) - lets the
   user shrink the actual native window (not just visually scale content) via a
   `zoom` CSS property + a real `EditorResizeFromUI`-backed window resize, persisted
   across launches via localStorage.
4. **Knob value-text digit changes no longer shift the layout** (Known Issue #14) -
   each knob's displayed-value box is now a fixed width sized from that knob's own
   min/max range, and Attack/Decay/Release (both ADSRs) show fewer decimals only
   once the value itself needs 4 integer digits, preserving precision at low values.
5. Tried locking the parameter grid's columns to fixed px (Known Issue #15) as extra
   insurance beyond #4's per-knob fix - **broke badly in the real app** (incomplete
   worst-case measurement) and was **reverted back to `max-content` the same day**.
   Kept the one harmless part of that change: simplifying the grid from 5 columns to
   4 (the two LFO-panel-only columns were never used separately, always spanned
   together).

6. **Looper page - design agreed, implementation not yet started** (see dedicated
   section below, "Looper page (planned, not yet implemented)"). Session may be
   interrupted by a weekly usage cap mid-implementation - that section is written to
   be resumable from a cold read if so.

## Looper page (2026-07-21)

**v1 implemented, built and launched - NOT YET confirmed working by hand/by ear by the
user.** See "Implementation notes" at the end of this section for exactly what was
built and what still needs a real test.

User wants a third page (alongside Synth/Effects) modeled loosely on Audio Damage's
**ENSO** looper pedal/plugin. Design was discussed and agreed with the user before any
code was written (this section is that agreed design, written down first in case the
session gets interrupted by a weekly usage cap mid-implementation - if resuming cold,
read this whole section before touching code, and check FirstSynth.h/.cpp/git status
to see how far implementation actually got vs this plan).

**Signal source - decided**: the looper records the synth's own output, **not**
external audio input. `PLUG_CHANNEL_IO` stays `"0-2"` (no input channels added) - this
was an explicit choice among three offered options (synth-only / external audio in /
both), picked for simplicity (no `config.h` channel change, no Standalone audio-input
device selection UI to worry about).

**Position in signal chain**: last stage, after Reverb (`FirstSynth::ProcessBlock`
order becomes DSP → CC7 Volume → Chorus → Delay → Reverb → **Looper**). The looper
receives the fully-processed "what you hear" signal as its recording input, and its
own output (loop playback blended with the live dry signal via its own Mix knob, same
dry/wet pattern as Chorus/Delay/Reverb) becomes the plugin's final output.

**v1 feature scope - decided with the user** (explicitly deferred: Filter, Fade,
Tempo Sync, Undo/multi-layer history - don't add these unless asked, this was a
deliberate scope cut discussed with the user, not an oversight):
- Transport: Record / Play / Overdub, free-running loop length (no quantization -
  loop length = however long the first Record pass lasted, set the moment the user
  stops it)
- Reverse (toggle)
- Speed (playback rate, pitch moves with it - tape-style, not pitch-preserving)
- Feedback (0-100%, how much old loop content survives each overdub pass - user
  called this out explicitly as important, don't cut it even though it's more complex
  than Reverse/Speed)
- Mix (dry/wet, same convention as the existing effects)

**Max loop length - decided: 60 seconds.** Buffer sized at
`60 * sampleRate` samples per channel (~20MB stereo at 44.1kHz/double `sample` type -
`SAMPLE_TYPE_DOUBLE` per [[project-suikinkutsu-plugin]]'s convolution-reverb notes
about this project's sample type, worth double-checking FirstSynth's own
`IPlugConstants`/config before allocating). If a Recording pass hits this cap, DSP
should auto-stop and start playing what it has (see transport UX note below for how
the UI catches up).

**Transport button UX - decided: single cycling button + separate Stop**, chosen over
one-button-per-action, to match ENSO's real footswitch feel (press cycles
Record→Play→Overdub→Play→...). Planned implementation, **not a regular `IParam`** -
these are momentary trigger actions, not continuous/automatable values, same category
as the existing Panic/Hold buttons (which use raw MIDI CC, not params) - but Looper's
triggers don't map to any real MIDI CC, so they should use the **same custom-message
plumbing added for UI Zoom this session** (`SAMFUI(msgTag, ctrlTag, data)` →
`FirstSynth::OnMessage` → new `EMsgTags` entries, e.g. `kMsgTagLooperTransport`,
`kMsgTagLooperStop`, appended after `kMsgTagSetUIScale`). C++ owns the actual state
machine (`ELooperState` in `FirstSynth_Looper.h`); the WebView button doesn't decide
the next state itself, it just sends "cycle" - JS then updates its own label from
**state pushed back by C++** (extend the `SendArbitraryMsgFromDelegate`/`SAMFD`
mechanism with a new small JSON message, alongside the existing `"params"` one, so
`index.html`'s `OnMessage` gets a `"looperState"` case) - this is what makes the
auto-stop-at-60s case correct without extra plumbing: C++ changes `mLooperState`
internally and pushes the update the same way a button press would, so the UI always
reflects DSP truth rather than assuming what JS itself last sent.

**New DSP file**: `FirstSynth_Looper.h`, class `LooperEffect<T>`, following the same
"deliberately simple, no fancier than needed" style as `FirstSynth_Effects.h`'s
existing Chorus/Delay/Reverb - linear-interpolated variable-speed playback (same
technique already used in this codebase, e.g. `ConvolutionReverbEffect::
ResampleChannel()`), no windowing/anti-aliasing beyond that. Owned as a `mLooper`
member in `FirstSynth.h` alongside `mChorus`/`mDelay`/`mReverb`.

**New params** (append to `EParams`, after the existing Reverb Mix - **don't
renumber existing params**, matching the established convention documented in the
Param list section for [[project-suikinkutsu-plugin]]'s Drop Volume addition): Looper
Reverse (bool), Looper Speed, Looper Feedback, Looper Mix. `FirstSynth::OnParamChange`
gets new cases for these, same pattern as the existing effects.

**New WebView page**: `#page-looper`, third page alongside `#page-synth`/
`#page-effects`. The existing single relabeling `#pageNavBtn` mechanism
(`ToggleEffectsPage()`/`SetEffectsPage()` in `index.html`) needs to become a 3-way
cycle (Synth→Effects→Looper→Synth...) or gain a second nav control - **not decided
yet, decide during implementation** based on what reads cleanest with 3 pages instead
of 2.

**Build**: new `FirstSynth_Looper.h` needs a `#include` in `FirstSynth.h` alongside
the existing DSP/Effects includes - no new `.cpp`/vcxproj `ClCompile` entries needed
since it's header-only like `FirstSynth_Effects.h` already is. Rebuild both
Standalone and CLAP after changes, same as every other change this project.

### Implementation notes (what actually got built, 2026-07-21)

Matches the plan above closely; a few things had to be decided or discovered during
implementation that weren't nailed down beforehand:

- **New file `FirstSynth_Looper.h`**: `enum class ELooperState` (kEmpty/kRecording/
  kPlaying/kOverdubbing/kStopped) + `template<typename T> class LooperEffect`. 60s
  buffer at this project's `sample` type (confirmed **`double`**, not `float` -
  `FirstSynth-win.props` never overrides iPlug2's `SAMPLE_TYPE_DOUBLE` default,
  unlike `FirstSynth-ios.xcconfig` which explicitly sets `SAMPLE_TYPE_FLOAT` - so the
  buffer is actually **~40MB stereo at 44.1kHz** (`60 * 44100 * 2ch * 8 bytes`), not
  the ~20MB estimated during planning when `float` was assumed by default).
- **Thread-safety**: `LooperEffect::mState` is `std::atomic<ELooperState>` - discovered
  during implementation (not part of the pre-code plan) that `CycleTransport()`/
  `Stop()` are called from the WebView message thread while `Process()` runs on the
  audio thread, a genuine cross-thread access on the one variable read every sample.
  Deliberately left everything else (`mWritePos`/`mPlayPos`/`mLoopLengthSamples`)
  non-atomic - those only change at the instant of an infrequent button press, and a
  benign race there would at worst glitch a couple of samples, not corrupt state or
  crash; a full lock-free redesign wasn't judged worth it for v1's simplicity goal.
- **C++ → UI state push mechanism**: added `kMsgTagLooperState` (value 3) as a third
  `EMsgTags` entry, sent via the existing `SendArbitraryMsgFromDelegate` (1 raw byte,
  the `ELooperState` enum value) from two places: `FirstSynth::OnMessage` (immediately
  after a button-triggered transport/stop) and a new `FirstSynth::OnIdle()` override
  (for the DSP-initiated 60s auto-stop case - `ProcessBlock` can't safely call WebView
  JS itself from the audio thread, so it just sets `std::atomic<bool>
  mLooperStateDirty`, and `OnIdle()` - an existing iPlug2 hook, called on the main
  thread via the timer this project already runs at `IDLE_TIMER_RATE=50` - checks and
  clears that flag). `index.html`'s `OnMessage()` gained an `msgTag === 3` branch that
  decodes the byte and calls a new `UpdateLooperButton()`.
- **Params actually landed at indices 51-54** (`kParamLooperReverse`/`Speed`/
  `Feedback`/`Mix`, appended after `kParamReverbMix`=50, per the existing
  don't-renumber-existing-params convention). Speed range **0.25-4.0x, linear** (no
  log/pow shape) - kept simple for v1; turning the knob to dead-center lands at
  ~1.66x rather than exactly 1.0x since the range isn't shape-corrected, a possible
  future refinement if the feel bothers the user, not done preemptively.
- **Mix is additive, not a dry/wet crossfade** - this deviates from Chorus/Delay/
  Reverb's Mix convention on purpose, worked out during implementation: those effects
  always have *some* wet signal to blend against dry, but the looper's wet output is
  silent whenever there's no loop yet (Empty/Stopped/mid-first-Recording) - a
  crossfade would mean Mix=100% (the sensible-sounding "hear the loop fully" setting)
  silences the *entire live synth* before anything's even been recorded. Implemented
  as `l = dryL + wetL * mMix` instead (matches how a real hardware loop pedal sums
  live playing with loop playback - turning the loop level down doesn't touch the
  live signal). Both Feedback and Mix default to **100%** (old loop material never
  decays on overdub by default; the loop is always fully audible once one exists).
- **Transport button labels**: `['Record', 'Recording…', 'Overdub', 'Play', 'Play']`
  for states 0-4 - note states 3 (Overdubbing) and 4 (Stopped) both show "Play"
  because that's genuinely what clicking does from either state (Overdubbing→Playing,
  Stopped→Playing-or-Empty) - not a copy-paste mistake if revisited.
- **Page nav**: the single relabeling `#pageNavBtn` became a 3-way cycle
  (`CyclePage()`/`SetPage(index)`, replacing the old 2-page `ToggleEffectsPage()`/
  `SetEffectsPage()`) rather than gaining a second control - kept the "one button,
  relabels itself" pattern from the 2-page version rather than introducing a new UI
  element for this.
- **Looper transport state is deliberately NOT part of `SerializeState`/preset
  save-load** - it was never made an `IParam`, specifically so it wouldn't be (loops
  are ephemeral per-session recordings, not something meant to be baked into a saved
  preset). If asked to persist a loop's audio content across save/load later, that
  would need new, separate serialization work - the existing `IByteChunk` param dump
  (see "Preset save/load" section above) won't pick this up at all as it stands.
- **Verified so far**: both Standalone and CLAP compile and build cleanly with zero
  errors (only the project's two pre-existing, unrelated encoding warnings). App
  launches. A **static** JS sanity check (temporary local HTTP server, not the real
  native host) confirmed: page cycling shows/hides `#page-looper` correctly and
  relabels the nav button, and simulating a `kMsgTagLooperState` push via `OnMessage`
  updates the transport button's text/color correctly.
- **NOT yet verified**: the actual DSP end-to-end in the running app - record a loop,
  hear it play back, overdub, hear Feedback/Speed/Reverse/Mix actually affect the
  sound, confirm the transport button visibly changes state on a real click (not just
  the simulated static-JS test above), and confirm the 60s auto-stop actually
  triggers and updates the button. **This all needs a real live test before trusting
  it works** - my own simulated interactions with the native window have been
  documented as unreliable all project (see "Debugging notes" below), so this needs
  the user to actually try it.

### Follow-up round after first live test (2026-07-21)

User tried v1 and asked for three changes, all implemented and rebuilt (Standalone +
CLAP), **not yet re-confirmed live by the user**:

1. **Clear button** - discards the recorded loop regardless of current state (works
   even mid-Recording, just abandons that pass). New `kMsgTagLooperClear` (value 4,
   appended to `EMsgTags` after `kMsgTagLooperState`), `LooperEffect::Clear()` (public,
   just calls the existing private `ResetLoop()` and returns the new state - doesn't
   need to zero the buffer contents themselves, only the position/length bookkeeping,
   since `mLoopLengthSamples=0` already makes `Process()` ignore old samples and the
   next Recording pass overwrites from position 0 regardless). Wired into
   `FirstSynth::OnMessage` alongside Transport/Stop, same `kMsgTagLooperState` push-back
   pattern. New `.looper-clear-btn` in `index.html` next to Stop.
2. **Speed redisplayed as a signed percentage, not "x"** - the underlying *range* is
   unchanged (still equivalent to 0.25x-4x), only reparametrized: `kParamLooperSpeed`
   is now itself stored in %-offset units directly (`InitDouble` range -75..300,
   default 0, units "%"), converted to a real multiplier only where DSP needs it
   (`FirstSynth::OnParamChange`: `mLooper.SetSpeed(1. + value/100.)`). Added a new
   **`signed-display` boolean attribute to `knob-control.js`** (prepends "+" for
   zero/positive values via a `signPrefix()` helper threaded through all three
   `formatDisplayValue()` branches - integer/max-digits/default) - reusable for any
   future bipolar-percent param, not Looper-specific. Verified via the browser-harness
   technique: -75%/+112.5%/+300% all displayed with the correct sign and no "x" left
   over.
3. **Three always-visible page tabs, replacing the single relabeling nav button** -
   user found the "one button that relabels itself" pattern (used since the 2-page
   Synth/Effects days) unclear once there were 3 pages, specifically wanting to always
   see which page is active. `index.html`'s `.page-header` now has a `.page-tabs` div
   with 3 buttons (`#pageTabSynth`/`#pageTabEffects`/`#pageTabLooper`, each just calling
   `SetPage(0/1/2)` directly), styled `.page-nav-btn.page-tab-btn` with a new
   `.page-tab-btn.active` rule (accent-colored) toggled by `SetPage()`. **The old
   `CyclePage()` function and single `#pageNavBtn` were deleted entirely**, not kept
   as dead code - if either name is referenced anywhere else, that's stale.

**Reminder for next resume**: like the very first v1 pass, these three changes have
only been verified via the static browser-harness technique (`python -m http.server` +
Claude_Browser preview, simulating `OnMessage` pushes since there's no real C++ host in
that environment) - **not yet confirmed by the user clicking the real buttons in the
native app**. Ask for that confirmation before assuming this round is done too.

### Second live test (2026-07-21) - transport button label was showing the wrong thing

User confirmed the DSP itself works (record/play/overdub/stop functionally correct),
but found the transport button's label **confusing**: it was showing the *next action*
a click would trigger, not the *current* state - so while actually Playing the button
said "Overdub", and while actually Overdubbing it said "Play". Looked like a backwards
status readout. **Fixed**: `kLooperStateLabels` in `index.html` changed from
`['Record', 'Recording…', 'Overdub', 'Play', 'Play']` to `['Record', 'Recording…',
'Playing', 'Overdubbing', 'Stopped']` - now shows current state plainly; what a click
does next is left implicit via the cycle (same as how a real loop-pedal footswitch
works, you learn the cycle rather than reading instructions off it). Rebuilt both
Standalone/CLAP - **not yet re-confirmed live** by the user after this specific fix.

## MAJOR BUG FOUND & FIXED: every knob's min/max was silently stuck at 0-100 in the
## real app (2026-07-21)

While chasing the Env Amount "shows 50% at center" report below, root-caused a much
bigger, previously-invisible bug affecting **every single knob in the plugin**, not
just Env Amount - worth reading this section first if any future "the number looks
wrong" report comes up.

**Symptom, diagnosed live with the user via DevTools console** (not guessed): double-
click-reset on Env Amount moved the knob to the **far left** and showed "0.0%" -
impossible if its real range were -100..100 with default 0 (that should reset to
*center*). Confirmed via `document.querySelector('knob-control[param-id="26"]')
.minValue` in the running app's console → returned `0`, not `-100`. Then checked
`.getAttribute('min')` directly → `null` (the attribute was **never set at all**, not
just wrong). Then checked `window.parameters` (the cached raw params array
`OnMessage`'s `"params"` case sets) → **`undefined`**. Checked a completely unrelated,
previously-"working-looking" knob (`Cutoff`, param 22) the same way → `.minValue` was
also `0` (should be `20`) - **confirmed universal, not Env-Amount-specific**.

**Root cause**: `IPlugWebViewEditorDelegate`'s `SendArbitraryMsgFromDelegate` (used to
send the one-time "params" JSON describing every param's name/min/max/default to the
WebView on load) formats the base64-encoded payload into a JS call string capped by
`mMaxJSStringLength`, which **defaults to 8192 characters** and was never raised for
this project. With ~55 params now (this session added the 4 Looper params, 51-54,
pushing the total over the edge for the first time), the serialized-then-base64-
encoded params JSON exceeds 8192 chars, so `WDL_String::SetFormatted`'s length cap
**silently truncates it mid-string** - the truncated string is invalid base64/JSON,
so `window.atob(data)` then `JSON.parse(...)` in `index.html`'s `OnMessage()` throws,
which aborts the *entire* `"params"` case with **no visible error path** (an
uncaught exception inside a WebView-dispatched call doesn't surface as a console
error banner the way a normal page script error would, which is why this went
unnoticed even with DevTools open earlier this session) - every `knob-control`
element's `min`/`max`/`default-value` attributes never got set, leaving every knob
stuck at its JS constructor fallback (`this.minValue = ... || 0`, `this.maxValue =
... || 100`).

**Why this wasn't caught by DSP behavior or most of today's other testing**: the
audio/DSP side reads real `IParam` values directly via `GetParam(paramIdx)->Value()`
in `FirstSynth::OnParamChange` - completely independent of this JS-side cache, so
sound was correct the whole time. And per-knob VALUE sync (`SPVFD`, one small message
per param, well under the length cap individually) still worked fine, so knobs still
visually moved and responded to drags/automation - they just displayed the **wrong
number** for any param whose real min/max differs from the 0-100 fallback (most
knobs "looked plausible" by coincidence, e.g. an unrelated Filter Type 0-2 knob or a
0-100% knob would show identical numbers whether synced or not; Cutoff/Attack/etc.
would have shown wrong numbers too if anyone had checked closely - Env Amount's
bipolar range was just the first case dramatic enough (50% vs 0% at rest) to be
noticed by eye).

**Fixed**: added `SetMaxJSStringLength(32768);` to `FirstSynth`'s constructor
(`FirstSynth.cpp`, `#ifdef WEBVIEW_EDITOR_DELEGATE` block, alongside the existing
`SetEnableDevTools(true)`) - a generous 4x margin past what ~55 params need, room to
add more before hitting this again. Rebuilt both Standalone/CLAP.

**CONFIRMED FIXED by the user** (`knob-control[param-id="26"].minValue` now reads
`-100` in the live app) - and per the user, several *other* previously-odd-looking
knob displays elsewhere in the plugin also corrected themselves at the same time
("他にもいろいろおかしなところがあったのですが全部もとに戻りました" - several other
odd things also went back to normal), confirming this was indeed the shared root
cause behind more than just Env Amount, exactly as diagnosed above. **If any future
param count growth pushes past 32768 chars again, the fix is the same one-line number
bump** in `FirstSynth::FirstSynth`'s `SetMaxJSStringLength(...)` call - there's no
other size-dependent logic here, just raise it further. Worth remembering as a
general lesson for any iPlug2 WebView project that grows its param count over time:
**this ceiling exists and fails silently**, not just for this project.

## Filter Env Amount: bipolar knob visual + stronger effect (2026-07-21)

User asked for Env Amount (`kParamFilterEnvAmount`) to be "bipolar, zero at center"
and said the effect feels weak. The **param itself was already bipolar** (-100..100,
default 0, unchanged from its original 2026-07-20 definition) and the DSP math
(`FirstSynth_DSP.h`, `envAmountOctaves = (mFilterEnvAmount/100) * N`) already
modulates the cutoff both up and down correctly - so what needed fixing was different
from what it first sounded like:

1. **The knob's visual arc always filled from `minAngle`, regardless of the param
   being bipolar** - so at the default/neutral value (0%, knob centered) the arc
   showed *half-filled*, looking like something was already engaged rather than
   "off". This is a **general `knob-control.js` fix**, not Env-Amount-specific:
   `updateValueArc()` now starts the arc from the *zero angle*
   (`realToNormalized(0)`) instead of `minAngle` whenever the param's range
   genuinely straddles zero (`minValue < 0 && maxValue > 0`) - purely unipolar knobs
   (the vast majority, e.g. Gain 0-100) are guarded out of this and render exactly as
   before (`realToNormalized(0)` would be outside `[0,1]` or even `NaN` for some of
   those ranges/curves, so this must stay conditional, not just always-on). Also had
   to make the SVG arc's sweep-flag conditional (`endAngle >= startAngle ? 1 : 0`) -
   the old code hardcoded sweep=1, which only worked because the arc always used to
   start from the lowest possible angle and only ever swept one direction; a
   center-started arc now needs to sweep the *other* way when the value goes
   negative, or it draws the long way around through the top instead of directly to
   the lower angle. Verified via the browser-harness technique: at center the arc's
   start/end SVG coordinates are now identical (a true zero-length arc), and turning
   the knob positive vs. negative each produced a short arc curving the correct,
   opposite direction (confirmed via the `A ... sweep-flag ...` value in the path
   data) - re-verified a plain unipolar knob (Gain) is pixel-identical to before.
   **Reusable for any future bipolar param** (this project has a few others, e.g.
   Osc Fine ±100ct, that get this fix automatically too, not just Env Amount).
2. **Modulation depth increased**: `envAmountOctaves` scale in
   `IPlugInstrumentDSP::Voice::ProcessSamplesAccumulating` raised from `4` to `6`
   octaves at ±100% (was "±4 octaves at ±100%", now "±6") - a direct response to
   "feels weak", no other diagnosis needed since the math itself was already
   correctly bipolar.

Rebuilt both Standalone/CLAP - **not yet confirmed by the user**, either the knob's
new centered-zero look or whether +6 octaves now feels strong enough (further
increases are easy - it's the one `6.` literal on that line - if still not enough).

**Follow-up same day**: user confirmed the modulation depth (+6 octaves) is good, but
asked for the *displayed number* to actually read 0 when dragged to the middle - the
math already produced exactly 0 at the mathematically-exact center (`normalizedToReal
(0.5)` for a `-100..100` range lands on 0 precisely, no floating-point residue), but
landing exactly on that one precise mouse position by eye while dragging is
essentially impossible, so in practice the display always showed some small non-zero
value like "-2.3%" instead. **Fixed with a center detent**: `knob-control.js`'s
`updateValue()` gained a new branch (bipolar params only, i.e. `minValue < 0 &&
maxValue > 0`, and only when there's no `stepSize` - stepped bipolar params like
Octave already land exactly on 0 as one of their integer steps automatically, this
would be redundant/conflicting there) that snaps to exactly `realToNormalized(0)`
whenever the drag lands within a `0.02`-normalized-units window of it. Verified via
the browser-harness technique: values at norm 0.495-0.505 all displayed "0.000 %"
exactly, while 0.47/0.53 (just outside the window) showed their real, un-snapped
values - and confirmed both a stepped-bipolar knob (Octave) and a plain unipolar knob
(Gain) are unaffected, still behaving exactly as before. Rebuilt both Standalone/CLAP
- **not yet re-confirmed live** by the user after this specific follow-up.

## Resonance knob curve (2026-07-21)

User found Resonance "too strong, especially on the left side of the knob". Root
cause: `FirstSynth_DSP.h`'s `kParamFilterResonance` case maps the param's 0-100%
value to filter Q **linearly** (`q = 0.5 + (value/100) * 19.5`, i.e. Q range 0.5-20),
but Q's audible effect is highly nonlinear - most of the perceptually-relevant
low-Q range gets compressed into a small turn of the knob near the left/low end.
**Fixed the same way Noise Level's identical complaint was fixed earlier this
project**: added `IParam::ShapePowCurve(2.)` to `kParamFilterResonance`'s
`InitDouble` call (`FirstSynth.cpp`) and the matching `shape-exponent="2"` attribute
on its `knob-control` tag (`index.html`, param 23) - per the established "always add
both sides together" rule (Known Issues, earlier session), a C++-only or HTML-only
change would silently desync the knob's drag feel from its real value. Followed
Noise Level's exact tag style (`shape-exponent` alone, no explicit `shape="pow"` -
confirmed via reading `knob-control.js` that the non-`'log'` branch of
`normalizedToReal`/`realToNormalized` already applies `Math.pow(norm, shapeExponent)`
regardless of the `shape` attribute's value, so `shape="pow"` would have been
redundant). Rebuilt both Standalone/CLAP - **not yet confirmed by ear** that the
low end now has finer control.

## Key velocity removed (2026-07-21)

User asked to remove key/MIDI velocity sensitivity entirely, "for now" (いったん全部
なくしてください - implies this may come back later as a deliberate feature, not a
permanent removal). Root cause traced to `FirstSynth_DSP.h`'s `Voice::Trigger(double
level, bool isRetrigger)` - `level` is the framework's standard 0-1 velocity value
(iPlug2's `ADSREnvelope::Start()`/`Retrigger()` doc comment: "usually linked to MIDI
velocity"), and it was being passed straight through to **both** `mAMPEnv` and
`mFilterEnv`'s `Start()`/`Retrigger()` calls, scaling each envelope's peak (and via
`mFilterEnv`, indirectly the filter cutoff sweep depth too, not just loudness).
**Fixed** by hardcoding `1.` in place of `level` in both envelopes' `Start()`/
`Retrigger()` calls - every note now triggers at full envelope depth regardless of
how hard/soft the key was played, restorable by reverting to `level` if velocity
sensitivity is ever wanted back. Rebuilt both Standalone/CLAP - **not yet confirmed
by ear** that notes now sound uniformly loud/full regardless of velocity.

## Dark mode (2026-07-21)

User liked the Looper progress and asked for a dark theme as a side request (not part
of the Looper plan). Implemented as a manual toggle button (not OS-theme-detected via
`prefers-color-scheme` - matches this project's existing pattern of explicit,
persisted WebView-only preferences like UI Zoom, rather than auto-detecting), applied
by setting `data-theme="dark"|"light"` on `<html>`, persisted to `localStorage`
(`firstSynthDarkMode` key), same technique as UI Zoom's `firstSynthUIScalePercent`.

- **New CSS variables in `:root`**: `--accent-soft` (was previously only a hardcoded
  fallback `#dbeafe` inline on `.shape-btn.selected`, never an actual defined
  variable - now a real var with a dark-mode override too) and five new
  **knob-dial-specific** variables (`--knob-circle-fill`/`-stroke`, `--knob-track-bg`,
  `--knob-pointer`, `--knob-value-arc`) separate from the general theme vars
  (`--bg`/`--surface`/`--text`/etc.) so `knob-control.js`'s SVG can reference them
  directly. `--accent`/`--knob-pointer`/`--knob-value-arc` (the blue accent hue)
  deliberately stay identical across both themes - one brand color reads fine on
  light or dark backgrounds, only the base surface/text/border palette and the
  knob's own fill/stroke actually change in `:root[data-theme="dark"]`.
- **`knob-control.js` change required for the knob dial itself to follow the theme**:
  the SVG's `fill`/`stroke` attribute defaults were raw hex strings
  (`'#18202d'` etc.) baked into each knob at construction time, not CSS - changed to
  `var(--knob-circle-fill, #18202d)`-style expressions (same fallback value, so zero
  visual change for anyone not using dark mode) so they inherit through the shadow
  DOM boundary via CSS custom-property inheritance, same mechanism the `.label`/
  `.value` text colors already relied on (`var(--text, ...)`/`var(--muted-text,
  ...)`, pre-existing). **Also had to add quotes around these SVG attribute values in
  the template string** (`fill=${x}` → `fill="${x}"`) - the `var(...)` expressions
  contain spaces/parens/commas that would otherwise break unquoted HTML attribute
  parsing; the old raw-hex values happened to work unquoted only because hex codes
  have no whitespace. No knob tag in `index.html` ever passed an explicit
  `circle-fill-color`/etc. attribute of its own (confirmed via grep before touching
  this), so every knob project-wide is affected uniformly, nothing to update per-tag.
- **New `#darkModeBtn` toggle button** in `.page-header` (🌙 Dark / ☀ Light,
  `ToggleDarkMode()`/`ApplyDarkMode(isDark)` in the inline script).
- **Applied earlier than UI Zoom's restore call, deliberately**: Dark mode has no
  C++/DSP dependency at all (pure CSS), so `ApplyDarkMode(...)` runs **immediately at
  head-script parse time** (before `<body>` exists) rather than waiting for the
  `"params"` WebView-bridge-ready message the way `RestoreUIScale()` does - avoids a
  light-theme flash on launch if dark mode was saved. Since `#darkModeBtn` doesn't
  exist yet at that point, the button's own label can't be set on that first call
  (guarded with `if (btn)` inside `ApplyDarkMode`) - a second call was added in the
  body-end `<script>` block (alongside the existing waveform-display listener setup)
  purely to sync the button's label once the element exists; the actual `data-theme`
  attribute/colors were already correct from the moment the head script ran, so
  there's no visible flash, just a label sync.
- **Verified via the browser-harness technique**: toggling flips `data-theme`,
  updates the button label, changes `document.body`'s computed background color, and
  - importantly - the knob's own SVG circle fill (`getComputedStyle(circle).fill`)
  correctly picked up the new `--knob-circle-fill` value, confirming the shadow-DOM
  var() inheritance works as expected. Also confirmed the choice persists across a
  page reload (simulating a relaunch).
- **NOT yet confirmed live by the user in the native app** - same caveat as every
  other WebView-only change this session, the browser-harness check isn't a
  substitute for the user actually clicking the button in FirstSynth itself.

## Status as of 2026-07-22 — all 2026-07-21 pending items confirmed live

Session resumed by reading this file top to bottom (per the 2026-07-21 pause note).
Rebuilt both Standalone/CLAP first (no source changes since last session, just a
clean rebuild to get back to a known-good running state), launched Standalone, and
had the user confirm each item that was still marked "not yet confirmed" above.
**User confirmed all of the following work correctly, no issues found:**

- Dark mode toggle (🌙/☀ button)
- Env Amount: center-zero detent (reads exactly 0% at knob center) and the
  zero-started bipolar arc visual
- Env Amount modulation depth (±6 octaves) - strong enough now
- Resonance ShapePowCurve(2.) - left/low end no longer overly strong
- Key velocity removal - notes sound uniformly full regardless of velocity
- Looper transport button label shows current state correctly (Record/Recording…/
  Playing/Overdubbing/Stopped)

No fixes needed - moving on to the fresh todo list in `private\todo.txt` next
(waveform-morph phase/amplitude dip, Glide time too short, a distortion meter, Delay
Feedback not reaching true 100%, Looper gauge/speed-display UI, and a lower-priority
list - effects additions, sub-oscillator, square-wave loudness balance, Standalone
tempo source).

## Waveform morph Sine<->Triangle phase alignment fix (2026-07-22)

User's `private\todo.txt` reported the Sine->Triangle crossfade (Morph() segment 0)
loses amplitude partway through. **Root cause**: `Sine(phase)` is zero at phase=0
rising to its peak at phase=0.25, but the original `Triangle(phase)`/`Saw(phase)`
each used a different phase convention - Triangle peaked at phase=0.5 (a quarter-
period off from Sine) and Saw crossed zero at phase=0.5 (a half-period off). Linearly
crossfading two waveforms whose peaks/zero-crossings don't land at the same phase
partially cancels rather than adds, which is exactly the "amplitude gets weaker"
symptom - worst at the crossfade midpoint where both components contribute equally
but out of step with each other.

**Fixed** by phase-shifting `Triangle()` (+0.25) and `Saw()` (+0.5) in
`FirstSynth_DSP.h`'s `FirstSynthOsc` namespace so both share Sine's zero-at-phase-0,
rising convention (peak at 0.25, zero at 0.5, trough at 0.75 for Triangle; single
zero-crossing/discontinuity at phase 0/0.5 for Saw). This shifts the ripple effect
through the rest of Morph(): segment 2's hard-clip square-wave endpoint is now
positive on phase (0,0.5) and negative on (0.5,1) (previously the opposite), so
`Pulse(phase, duty)`'s return values were flipped (`t<duty` now returns `+1` not
`-1`) to keep segment 3's starting duty=0.5 state continuous with segment 2's
endpoint - same continuity requirement documented in the original Pulse() comment,
just re-derived for the new polarity. Mirrored the identical change in
`resources/web/waveform-display.js` (`sine`/`triangle`/`saw`/`pulse` methods) since
that file has no shared source of truth with the C++ (per the file's own header
comment) and must be hand-kept in sync.

**Verified numerically before rebuilding** (temporary local `python -m http.server`
in `resources/web/` + a throwaway test HTML loading `waveform-display.js` directly,
computing RMS/peak of `morph(phase, waveShape)` swept across waveShape 0-2, deleted
after use): RMS now decreases smoothly and monotonically from 0.7071 (pure sine) to
0.5774 (pure triangle, `sqrt(1/3)` - the correct RMS for a triangle wave) across the
whole Sine->Triangle range, with **peak staying at exactly 1.0 throughout** - no
dip. Segment-boundary continuity (waveShape crossing 1, 2, 3) confirmed within
floating-point noise (~2e-5). Rebuilt both Standalone/CLAP, launched Standalone -
**confirmed by the user** turning Osc1's Wave knob through 0-1 no longer weakens
partway through.

Segment 1 (Triangle->Saw) still shows a shallower peak dip (1.0 down to ~0.7 around
waveShape 1.5) even after this fix - this is expected and much smaller than before:
Triangle and Saw share the same fundamental phase now, but their peak *positions*
still differ (Triangle peaks at phase 0.25, Saw's ramp approaches its max just
before its phase-0.5 discontinuity), so a plain crossfade between them still can't
have both hit +1 at the same instant. This is the still-open, lower-priority
"三角波からノコギリ波に変わるところ、波形をだんだん倒していくことはできないか" request
in `private\todo.txt` - a real fix there would need an actual shape-morph (shearing
the triangle progressively into a ramp) rather than an amplitude crossfade, not yet
attempted.

## Waveform morph overhaul, part 2: Triangle->Saw shear, non-uniform segments,
## gradual saw->square, loudness compensation (2026-07-22)

Follow-up work in the same session as the Sine<->Triangle phase-alignment fix
above, addressing the rest of `private\todo.txt`'s waveform-morph item
("三角波からノコギリ波に変わるところ、波形をだんだん倒していくことはできないか")
plus several rounds of live feedback after each rebuild. All of this lives in
`FirstSynthOsc::Morph()` (`FirstSynth_DSP.h`) and its hand-kept-in-sync JS mirror
`resources/web/waveform-display.js` - **any future change to one must be mirrored
in the other, there is still no shared source of truth between them.**

1. **`AsymTriangle(phase, r)` replaces the Triangle<->Saw amplitude crossfade.**
   Triangle and Saw are really the same 2-segment "rise then fall" shape, just
   with a different rise/fall duration split - `r` = fraction of the cycle
   spent rising (0.5 = symmetric triangle, 1.0 = sawtooth). A `phi0(r) = 1 -
   r/2` offset keeps the *rising zero-crossing* pinned at phase=0 for every r,
   not just the two endpoints - this is what makes it a genuine continuous
   "tilt"/shear (verified: `AsymTriangle(phase, 0.5)` matches `Triangle(phase)`
   and `AsymTriangle(phase, 1.0)` matches `Saw(phase)` exactly, max diff
   0.000000 over 1000 sampled phases) instead of the old amplitude crossfade,
   which had a peak dip (down to ~0.7 from 1.0) mid-transition since Triangle's
   and Saw's peaks don't line up in time even after phase-alignment. Verified
   via the browser-harness technique: RMS and peak now both stay *exactly*
   constant (0.5774 and 1.0) across the entire Triangle->Saw sweep - zero dip.

2. **Segment boundaries made non-uniform** (user request, after confirming #1
   above): the old evenly-spaced `[0,1) [1,2) [2,3) [3,4]` integer boundaries
   became `[0,1)` Sine->Triangle, `[1, kMorphSawStart=1.95)` Triangle->Saw tilt,
   `[1.95, 2.05]` **held at pure Saw** (a deliberate plateau - easy to land on
   exactly while turning the knob), `(2.05, kMorphSquareEnd=2.59)` Saw->Square
   hard-clip, `[2.59, 4]` Square->narrow-Pulse duty sweep (now a wider range
   than before, more room to dial in pulse width). These three constants
   (`kMorphSawStart`/`kMorphSawEnd`/`kMorphSquareEnd`) are defined once in the
   `FirstSynthOsc` namespace and mirrored as `WaveformDisplay.SAW_START`/
   `SAW_END`/`SQUARE_END` static fields in the JS file - change both together.
   Each segment's own `frac` is now `(waveShape - segStart)/(segEnd - segStart)`
   instead of the old `waveShape - floor(waveShape)`.

3. **Saw->Square hard-clip curve slowed down** (user: "急激にスクエアになって
   いく感じがある。もっとゆっくり変化してほしい" after trying #2 live). Root
   cause: the clipped/flat fraction of a hard-clipped ramp is `1 - 1/k`, not `k`
   itself - so the old linear `k = 1 + frac*50` made the flat fraction rocket
   up almost immediately (~83% flat already at only 10% into the segment),
   which felt like a sudden jump rather than a gradual one. **Fixed** by
   solving for `k` such that the flat fraction itself grows linearly with
   `frac`: `k = 1 / (1 - frac*(kMax-1)/kMax)` with `kMax=51` (unchanged from
   before, so the segment still ends at the same near-perfect-square shape the
   following Pulse segment's `duty=0.5` starting point was tuned to match).
   Verified via the browser-harness technique (measuring the actual sampled
   flat-fraction, not just computing k): now increases essentially linearly,
   0.000 at frac=0 up to 0.980 at frac=1, evenly spaced in between (was
   front-loaded before).

4. **Loudness compensation across Saw->Square->Pulse** (user: "ノコギリ波から
   右のところで、聴感上音が大きく聞こえるので、少し下げることはできますか" after
   confirming #3 live). Root cause, confirmed analytically not just by ear:
   hard-clipping raises RMS from a saw's `1/sqrt(3)` (≈0.577) toward a square's
   `1.0` - a genuine **+4.8dB**, not an illusion - and Pulse's RMS is *always*
   exactly 1 regardless of duty (a rectangular wave whose value is always
   exactly ±1, so RMS is 1 no matter how narrow the duty cycle), so the "louder"
   plateau continues all the way through the pulse-width sweep too. Derived the
   closed-form RMS of the hard-clipped ramp by integrating over one period:
   `RMS(k) = sqrt(1 - 2/(3k))` (check: k=1 gives sqrt(1/3) exactly matching Saw;
   k→infinity gives 1 exactly matching Square). **Fixed** by scaling the
   Saw->Square segment's output by `kMorphTargetRMS / RMS(k)` (where
   `kMorphTargetRMS = 1/sqrt(3)`, matching Saw's own RMS - so segment starts at
   gain 1.0, exactly continuous with the Saw plateau before it) and the
   Square->Pulse segment by the flat `kMorphTargetRMS` (since its own RMS is
   always 1, `target/1 = target`). There's a small (~0.65%, ~0.06dB, judged
   inaudible) gain step right at the segment-3/4 boundary, since the hard-clip
   at its own endpoint (k=51) hasn't *quite* reached a perfect square's RMS=1
   yet - not worth chasing further, same "close enough" tolerance the original
   2026-07-20 design already accepted for shape-matching at that same boundary.
   Verified via the browser-harness technique: RMS now measures a flat 0.5774
   at every sampled point from waveShape 2.0 through 4.0.

**All four confirmed working live by the user** after rebuilding both
Standalone/CLAP following each step ("いいですね。非常にいいです。" after the
final loudness-compensation fix).

## Note Glide Time range extended (2026-07-22)

`private\todo.txt`: "Glide、時間短すぎる" (Glide time too short). `kParamNoteGlideTime`
was defined via `InitMilliseconds("Note Glide Time", 0., 0.0, 30.)` (`FirstSynth.cpp`)
- a 0-30ms max, barely perceptible as portamento. Asked the user for a target max;
picked **2000ms (2s)**. `InitMilliseconds` doesn't support a `Shape` argument, so
switched to the equivalent `InitDouble(..., "ms", IParam::kFlagsNone, "",
IParam::ShapePowCurve(3.))` call (same technique already used for Attack/Decay/
Release) to keep short glide times still finely controllable across the wider
range - and added the matching `shape-exponent="3" max-digits="4"` attributes to
the Glide `knob-control` tag in `index.html` (was plain `units="ms"` with no shape
attrs at all, since the old 0-30 range was narrow enough not to need one) - per
the established "always add both sides together" rule. `mSynth.SetNoteGlideTime
(value / 1000.)` (`FirstSynth_DSP.h`) needed no change, it just reads the real ms
value regardless of the param's internal curve. Rebuilt both Standalone/CLAP -
**confirmed by the user**, portamento is now clearly audible across the wider range.

## Output level/clip meter added (2026-07-22)

`private\todo.txt`: "メータ必要。歪んでるかどうか知るために" (need a meter, to know if it's
distorting). Added a peak-hold level bar + latching clip light in the page header.

- **C++ side** (`FirstSynth.h`/`.cpp`): `mMeterPeak` (`std::atomic<double>`) tracks
  `max(|L|,|R|)` of the **true final output** (post CC7 volume, post Chorus/Delay/
  Reverb/Looper - the very last thing `ProcessBlock` computes) via a CAS loop each
  sample (a plain store would race with `OnIdle()`'s read on the main thread).
  `OnIdle()` (already runs every `IDLE_TIMER_RATE`=50ms for the Looper's auto-stop
  push) now also `exchange`s the peak back to 0 and sends it as a 4-byte float via
  a new `kMsgTagMeterLevel` (`SendArbitraryMsgFromDelegate`), unconditionally every
  tick (not just on change) - a continuous readout like a real hardware meter.
- **UI side** (`index.html`): `OnMessage`'s `msgTag === 5` branch decodes the 4
  raw bytes as a little-endian float32 (`DataView.getFloat32(0, true)` - matches
  this project's x86 build) and calls `UpdateMeter(peakLinear)`. The bar maps
  -40dB..+3dB (some headroom past 0dB so an over is visibly past the line, not
  just pinned at the edge) to 0-100% width, colored via a green→yellow→red CSS
  gradient; a 0dB reference tick (`#meterZeroLine`) is positioned from the same
  `METER_MIN_DB`/`MAX_DB` constants `UpdateMeter` itself uses (single source of
  truth, can't drift out of sync). A separate round clip light (`#meterClip`)
  lights up red whenever peak exceeds 1.0.
- **Positioning went through 2 rounds of live feedback**:
  1. First landed as a knob-container-style box in the Master panel - user asked
     to move it to the header's top-right corner instead, with height matching
     the other header controls (Panic/Zoom/Dark). Moved the markup into
     `.page-header` and restyled `.meter-container` with the same
     padding/border/border-radius/background as `.page-nav-btn`, `margin-left:
     auto` to push it to the row's end - confirmed via the browser-harness
     technique that its height (36.0px) and vertical position now closely match
     the other header buttons (34.4-36.8px).
  2. User then asked for the meter's right edge to align with the LFO Depth
     knobs' column specifically, not the window/header's own edge. This can't be
     a fixed CSS margin: `.layout-grid`'s columns are `max-content` and don't
     stretch to fill a wider window (see that class's own CSS comment), and
     there's also an invisible 20px scroll-headroom spacer column past the real
     content, so the gap between the header's edge and the LFO column's edge
     varies with window width. **`AlignMeterToContentEdge()`** measures
     `.panel-amplfo`'s live `getBoundingClientRect().right` vs the header's own,
     and sets that difference as `margin-right` on `.meter-container` - rerun on
     `window`'s `resize` event (covers both manual window dragging and the UI
     Zoom dropdown, which does trigger a real native window resize). **The
     very first call (on page load) needed a short deferral** - measured
     synchronously at parse time, the browser-harness technique caught it
     computing a stale/wrong gap once; `requestAnimationFrame` was tried first
     but can be throttled/skipped in a backgrounded or not-yet-visible tab
     (confirmed this exact harness hit that), so a plain `setTimeout(...,
     50)` is used instead - imperceptible to a user, doesn't depend on paint/
     visibility timing at all. Confirmed via the harness afterward: a fresh
     load with no manual intervention measures `diff=0.0` between the meter's
     and the LFO column's right edges. **User's live verdict after rebuilding:
     "完全にはそろっていませんがまあいいです" (not perfectly aligned but good
     enough)** - accepted as-is, not pursued further.
- Clip light **auto-clears after 2 seconds** (restarting the timer on every new
  over, so a sustained clip stays lit continuously rather than flickering) -
  also still clearable early by clicking it directly. First implementation had
  no auto-clear (click-only), changed after user feedback in the same round as
  the header repositioning above.

## Meter alignment follow-up (2026-07-22) - improved, one edge case deferred

After the initial header repositioning (see "Output level/clip meter added"
above), user reported the meter drifting away from the LFO Depth column as the
window/zoom got smaller. Root-caused via the browser-harness technique:
`.page-header` is an auto-width flex row that always fills the *current*
available width, while `.layout-grid`'s columns are `max-content` (shrink-to-fit,
don't stretch - see that class's own CSS comment) - so the gap between them
isn't a fixed proportion of anything, it depends on how the two independently
respond to a size change.

- **Fixed for the UI Zoom dropdown case** (the officially supported way to
  shrink the UI - Zoom pairs a real native window resize with the `zoom` CSS
  property, per "UI zoom preference" above): replaced the earlier plain
  `window.resize` listener with a **`ResizeObserver`** watching both
  `.page-header` and `.panel-amplfo` directly (`meterAlignObserver` in
  `index.html`) - reacts to each element's own actual rendered box size
  rather than assuming a global 'resize' event reliably fires or reliably
  arrives after both elements have their final size.
- **The very first alignment call needed extra care**: a synchronous call at
  parse time (before the page's first real paint) intermittently measured a
  transient/wrong layout state that never self-corrected afterward, even via
  the ResizeObserver, since the elements' size doesn't change again once that
  first paint settles - confirmed repeatedly via the browser-harness
  technique. `requestAnimationFrame` was tried first but can be throttled/
  skipped in a backgrounded or not-yet-visible tab (confirmed hit in this
  project's own testing harness); landed on a synchronous call **plus** a
  `setTimeout(..., 300)` follow-up, both present - confirmed via the harness
  that the 300ms deferred call reliably lands on the correct, settled state
  (`diff=0.01`) even when the synchronous one doesn't.
- **Deliberately NOT pursued further**: dragging the native window border
  directly smaller (not via Zoom) can't be made to line up the same way -
  `.layout-grid`'s columns don't shrink to fit a smaller window at all (by
  design, see "Default window size" section above - the project's whole
  approach is "size the window wide enough for content," not "let content
  shrink/scroll"), so the LFO column's true right edge can end up off-screen
  entirely in that scenario, and no amount of margin math on the header can
  chase that. **User confirmed they were testing via the Zoom dropdown, not
  window dragging** - said the residual imperfection isn't a big deal either
  way and moved on ("大きな問題ではないので後回しにします").

## Delay Feedback range extended to true 100% (2026-07-22)

`private\todo.txt`: "フィードバック、100％で本当に100％になってるわけじゃない" (Feedback
doesn't really reach 100% at 100%). `kParamDelayFeedback` was capped at 95
(`FirstSynth.cpp`'s `InitDouble`, `DelayEffect<T>::Process` in
`FirstSynth_Effects.h` just does `mFeedback = feedback` with no other clamping) -
a leftover caution from this project's *other* feedback-style params (the filter's
resonance/Q, which really could blow up at extreme values before the TPT-SVF fix,
see Known Issue #7). A plain delay feedback loop is a different, much simpler
case: at exactly `mFeedback = 1.0` each repeat stays at **constant** amplitude
forever (a "freeze"/infinite-sustain effect) rather than growing - genuinely
stable, not a runaway, so there was no real reason for the 95 cap here. Checked
the Ping Pong branch's math too (`dryMono + delayedR*mFeedback` / `delayedL*
mFeedback`) - same conclusion, stays bounded at feedback=1.0. **Fixed** by
raising the param's max from 95 to 100 in `InitDouble` (no HTML/JS change needed,
`knob-control` reads min/max from the host dynamically). Rebuilt both Standalone/
CLAP - **confirmed by the user**, max Feedback now sustains without decaying.

## Looper Speed: reworked, found a real bug, then removed entirely (2026-07-22)

`private\todo.txt`: "スピード…真ん中100（デフォルト）にして、％表示で" (Speed: make the
center 100 (default), with % display).

1. **First pass**: `kParamLooperSpeed` was a `-75..300` **offset** from 0 (not the knob's
   physical center - center of that range is 112.5, not 0), so turning the knob to its
   middle didn't land on normal speed. Reworked to an **absolute** percentage, range
   `25..400` (matching the original 0.25x-4x multiplier), default 100, using
   `IParam::ShapeExp()` - an exponential/log shape whose value at the knob's exact
   center is the geometric mean of min/max, which for 25/400 is exactly 100
   (`sqrt(25*400)=100`) - so "center of knob" and "100% (normal speed)" coincide
   automatically, no custom asymmetric-range math needed (same shape this project
   already uses for frequency-style knobs, e.g. `InitFrequency`, for the identical
   reason). Verified via the browser-harness technique: `normalizedToReal(0.5)` = 100.0
   exactly. `index.html`'s knob tag changed from `signed-display` (the old +/-
   convention) to `shape="log"`. **Confirmed by the user.**

2. **Then, investigating a real bug**: user reported that lowering Speed and then
   Overdubbing made the *currently-playing synth* sound ring-modulated/bitcrushed.
   Root-caused with the user's own help (they isolated it by muting Mix - the live dry
   signal alone sounded fine, meaning the corruption was specifically in what got
   *recorded*, not the live signal itself): at Speed<1x, `mPlayPos` (the loop read/write
   position) advances by less than 1 sample per audio tick, so multiple consecutive
   real-time input samples land on the same loop-buffer index before playback moves on
   (e.g. at 0.5x, 2 real samples per index) - `FirstSynth_Looper.h`'s overdub-write
   directly indexed `(size_t)mPlayPos` every tick regardless, so the *same* buffer cell
   got fed back+summed with a *new* live sample many times faster than the loop's own
   rate, corrupting whatever got recorded.
   - **First fix attempt**: only write a given index once per pass (`mLastOverdubPos`
     guard) - this stopped the repeated-write corruption, but effectively just *picks
     one* of the several real samples landing on that index and throws the rest away -
     a naive, unfiltered decimation by `1/Speed`. **User confirmed this didn't fix it.**
   - **Second fix attempt**: replaced the single-sample pick with an accumulate-and-
     average (`mOverdubAccumL/R/Count`, flushed via `FlushOverdubAccum()` whenever
     playback moves to a new index, plus on overdub-stop so nothing pending is
     silently dropped) - a boxcar anti-alias filter matched to the decimation ratio.
     This fixed what was heard *live* while overdubbing, but **the user found the
     recorded loop content itself now sounds "bit-crushed" on playback** - a plain
     box-average is a crude lowpass, not real anti-aliasing, and it permanently bakes a
     bandwidth/resolution loss into that portion of the loop.
   - **Conclusion, documented directly in `FirstSynth_Looper.h`'s class comment**:
     recording new overdub content while loop playback runs at a non-1x rate is
     fundamentally hard with this simple architecture - properly solving it needs real
     anti-aliasing filtering or a different design (e.g. separate per-layer tracks
     recorded at their own native rate, not resampled into a shared time-warped buffer).
   - Offered the user 4 options (force Speed=1x during Overdub only / remove Speed
     entirely / build proper multi-track looping / discuss further) - **user chose to
     remove Speed entirely**, judging it not worth the complexity.

3. **Removed**: `kParamLooperSpeed` deleted from `EParams` (`FirstSynth.h`) - and,
   unusually for this project, **`kParamLooperFeedback`/`kParamLooperMix` were
   renumbered down** (52/53, was 53/54) rather than leaving a gap, breaking the
   otherwise-strict "never renumber existing params" convention used everywhere else
   in this project. Justified here specifically because this param was added *and*
   removed within the same session, was never in a real saved preset, so there's no
   actual backward-compatibility to protect - renumbering only matters when an existing
   preset might reference the old indices. `FirstSynth_Looper.h` reverted to its
   original pre-Speed form (direct overdub write, `mPlayPos += dir` with no speed
   multiply, `ReadInterp` unchanged since dropping fractional playback made it a
   no-op interpolation but still harmless/correct to leave as-is) - all the accumulator
   machinery from both fix attempts above was removed along with it. Removed the Speed
   `knob-control` from `index.html`'s Looper page. **Confirmed by the user**: "これが
   一番いいと思います" (this is the best option) - Looper page now shows only
   Reverse/Feedback/Mix, Record->Play->Overdub cycle still works correctly.

## Looper recording/playback gauge with walking-cursor animation (2026-07-22)

`private\todo.txt`: a horizontal gauge that grows while recording, resets and cycles
during Play/Overdub, colored red/green/orange per state, with a small walking-person
cursor (reversed when Looper Reverse is on).

- **C++ side**: `LooperEffect::GetBarFraction()`/`GetCursorFraction()` (new,
  `FirstSynth_Looper.h`) - both expressed as a fraction of the full 60s cap
  (`mMaxSamples`), not the actual recorded loop length, so the gauge's right edge
  always represents 60s and a shorter loop visibly only occupies part of it (matches
  what was asked for - "ゲージの最大値は録音の最大値（60秒）"). Reads `mWritePos`/
  `mPlayPos`/`mLoopLengthSamples` from `FirstSynth::OnIdle()` (main thread) while
  `Process()` runs on the audio thread - the same class of benign, already-accepted
  race as this class's other plain position/length members, not worth a lock for a
  display-only readout. New `kMsgTagLooperProgress` (`FirstSynth.h`) pushes both as
  2 packed floats (8 bytes) unconditionally every `OnIdle` tick (50ms), same
  "continuous readout" convention as the level meter added earlier this session.
- **UI side** (`index.html`): `.panel-looper-gauge` (new, below the Transport/Looper
  knobs row) - a track (`.looper-gauge-track`) with a colored fill bar
  (`.looper-gauge-fill`, red/green/orange/gray matching Recording/Playing/
  Overdubbing/Stopped, reusing the exact hex values `.looper-transport-btn` already
  uses for the same states) and a cursor (`.looper-gauge-cursor`) holding a walking-
  person emoji (🚶) with a small CSS `@keyframes` bob+tilt loop. `OnMessage`'s new
  `msgTag === 6` branch decodes the 2 floats (little-endian, same `DataView` pattern
  as the meter) and calls `UpdateLooperGauge(barFraction, cursorFraction)`.
- **Reverse mirroring**: rather than a static `scaleX(-1)` fighting with the bob
  animation over the same `transform` property, defined a second keyframe set
  (`looper-walk-reverse`) with the mirror baked into every step, swapped in via a
  `.reverse` class - `UpdateLooperGaugeReverse()` toggles it, wired from
  `OnParamChange`'s existing `param===51` branch (Looper Reverse) so it stays
  correct even when the checkbox changes via host/preset-recall, not just direct
  user clicks. Verified via the browser-harness technique end-to-end (including a
  fake base64-encoded float pair through the real `OnMessage` decode path) before
  ever touching the native build.
- **Follow-up round after live testing**, all confirmed by the user:
  1. **Stopped should freeze, not hide**: initially the cursor/walker were hidden
     outside Recording/Playing/Overdubbing (i.e. also hidden for Stopped) - user
     pointed out the buffer is still there when Stopped, so the gauge and walker
     should stay visible right where they left off, just not moving. Fixed: cursor
     visibility now only hides for `Empty` (state 0); the walk-bounce animation
     itself is separately paused (`animationPlayState`) for anything that isn't
     actively advancing (Recording/Playing/Overdubbing), so Stopped shows a frozen
     pose rather than an on-the-spot bounce. Added a `.looper-gauge-fill.looper-
     stopped` gray fill color (was undefined before, since only the 3 active states
     had colors defined).
  2. **Walker should stand on top of the bar, not centered through its middle**:
     required restructuring the DOM - the fill bar needed its own `overflow:hidden`
     wrapper (`.looper-gauge-fill-clip`, clips only the color bar to the track's
     rounded corners) *separate* from `.looper-gauge-track` itself, since the walker
     cursor needs to render above the track's own box without being clipped by it
     (it used to be a direct child of the clipped track, which cut it off exactly
     where it needed to appear). The cursor is now anchored to the track's top edge
     and pulled up by its own height plus a small gap (`transform: translate(-50%,
     calc(-100% - 2px))`), confirmed via direct `getBoundingClientRect()` measurement
     that the walker's bottom edge sits just above the track's top edge.
  3. **Position/margins**: moved the whole gauge further down (`.panel-looper-gauge`
     margin-top 14px -> 48px) and fixed an asymmetric side margin - `<main>`'s own
     padding is asymmetric (60px left, 4px right, pre-existing, unrelated to this
     feature), so the gauge's `width:100%` had it sitting almost flush with the
     window's right edge while the left side had a large gap. **First attempt** kept
     `width:100%` and added `margin-right:56px` (60-4, the padding difference) -
     had no visible effect, confirmed via measurement (`width:100%` forces the box
     to fill 100% of its container regardless of any added margin, which just
     overflows/gets ignored rather than shrinking the box). **Fixed** by dropping
     the explicit `width:100%` entirely - a block element's default `width:auto`
     naturally fills available space minus its margins, so the same
     `margin-right:56px` then correctly matched the left gap (verified: both
     measure exactly 60px).

## Meter position now consistent across all 3 pages (2026-07-22)

`private\todo.txt`: "レベルメータの位置がシンセとエフェクト、ルーパーのページで変わらない
ように" (make the level meter's position not change between Synth/Effects/Looper).
Root cause: `AlignMeterToContentEdge()` targets `.panel-amplfo`, which only exists on
the Synth page - `SetPage()` hides the other pages' content via `display:none`, which
collapses `getBoundingClientRect()` to all zeros. That fed straight into the gap math
(`gap = header.right - 0 = header.right`), producing a bogus, oversized margin the
instant the user switched to Effects or Looper - exactly the "position changes between
pages" symptom. **Fixed** by skipping the update entirely whenever the target's rect is
degenerate (`width===0 && height===0`), leaving whatever margin was last computed while
the Synth page was visible in place - `ResizeObserver` (already wired to `.panel-amplfo`
from the earlier alignment work) naturally recomputes it correctly again the moment the
Synth page becomes visible. Verified via the browser-harness technique: meter's right
edge measured identically (1333.4px) across Synth -> Effects -> Looper -> Synth again.
**Confirmed by the user.**

## Looper Feedback reworked to a real-time-based decay (2026-07-22)

`private\todo.txt`: "ルーパーのFeedbackのつまみ、本当に効いているか？" (is the Looper's
Feedback knob really working?). Investigated with the user's help rather than guessing:
static code review showed the wiring itself was correct (`kParamLooperFeedback` ->
`mLooper.SetFeedback()` -> the overdub-write formula), but the user's actual complaints
were about *behavior*, not wiring:

1. **Feedback only ever did anything while actively Overdubbing** - `Process()` only
   touched the buffer (`buf[pos] = buf[pos]*mFeedback + l`) inside the `if (state ==
   kOverdubbing)` branch, so plain Play never decayed the loop at all no matter the
   Feedback setting. User: "OVERDUBでもPLAYでも同じように効いてほしい" (want it to work
   the same in both).
2. **The old design decayed a given buffer index in one lump each time the playhead
   passed over it** - once per full loop revolution. On a short loop, or at a low
   Feedback value, that's a large, discrete percentage jump concentrated at one single
   instant (whenever that index's turn comes up) rather than a gradual fade - audible as
   a sudden volume drop right at the loop's seam. User: "突然ループ頭に戻った時に音量が
   下がるのではなく、一定時間に対してだんだん音が小さくなっていってほしい".

**Redesigned as a genuine real-time-based decay** (confirmed with the user before
implementing, given the scope - this changes what the Feedback knob numerically means
and adds a second per-sample array, ~doubling the Looper's memory footprint):
`FirstSynth_Looper.h` now tracks `mWriteTime` (a `double` timestamp per buffer index,
"when was this index last actually written") against a continuously-advancing
`mCurrentTime` (incremented every `Process()` call, in *every* state including Stopped,
so decay keeps progressing even while paused). Decay is computed **fresh at every
read**, not baked into the stored value via periodic rewrites:
`pow(Feedback, elapsedSeconds/kDecayTimeScale)` (`kDecayTimeScale=8` seconds - the one
easy-to-retune constant if the pace ever feels off; at Feedback=50% content fades to
half amplitude every ~8s of real time). This solves both complaints structurally:
- Pure Play, with zero writes happening at all, still audibly fades - every time the
  same index is read again as the loop repeats, `elapsed` (time since it was last
  *written*, which never changes during Play) has grown, so the computed decay keeps
  shrinking on its own. No decay logic needs to run during Play beyond just reading -
  Play differs from Overdub only in that Overdub *also* writes `decayedOld + live` back
  with a fresh timestamp.
- No discrete jump anywhere, including the loop seam: since decay is a continuous
  function of elapsed real time, and adjacent samples' elapsed times differ by only
  `1/sampleRate` even right across the wrap point, the computed decay is smooth with no
  audible step.
- `Feedback=100%` still means "never decays" exactly (`pow(1, anything)=1`), matching
  the pre-existing default/documented convention.

**A subtler bug found and fixed during implementation, before ever reaching the user**:
naively stamping `mWriteTime[pos] = mCurrentTime` sample-by-sample *during the initial
Recording pass itself* would make material captured near the start of a take already
read as "older" (and pre-decayed) than material captured near the end, the moment
playback starts - simply because recording a real loop takes real time to complete, not
because of any actual repeat-based aging. **Fixed** with `StampRecordedRange()`, called
once right when a Recording pass finishes (both the 60s auto-stop path in `Process()`
and the manual-stop path in `CycleTransport()`) - stamps every index in the just-
recorded range to the *same* "now" timestamp uniformly, so a freshly recorded loop
always starts with zero elapsed decay everywhere, only accumulating from actual
playback repeats onward.

Rebuilt both Standalone/CLAP - **confirmed by the user**: Feedback at ~50%, left in
Play with no overdubbing, now audibly fades over several seconds with no seam artifact;
same smoothness confirmed while actively Overdubbing too.

## Looper walker direction fix - resolved (2026-07-22)

Follow-up to the gauge/walker feature above. User reported the walker's facing
direction looked backwards in the forward (non-reverse) direction, and asked for
forward=face-right, reverse=face-left. Took three rounds to fully resolve - each
round's live test revealed a genuinely different bug, not the same one persisting:

1. **Emoji doesn't visibly mirror**: assumed the 🚶 emoji's default orientation
   faces left, so swapped which CSS class (`.reverse` vs default) gets the
   `scaleX(-1)` mirror keyframe set. User reported forward now correctly faced
   right, but Reverse looked *identical* to forward - despite the browser-harness
   technique confirming the computed CSS `transform` matrices genuinely differed
   (`matrix(-0.99,...)` vs `matrix(0.99,...)`, a real sign-flip). The mirror was
   being applied correctly; the 🚶 glyph itself (Windows' Segoe UI Emoji font,
   22px) just doesn't read as visibly different when horizontally flipped -
   likely a fairly symmetric-looking pose in that font's specific design.
   **Fixed** by replacing the emoji entirely with an original inline SVG stick-
   figure silhouette (`viewBox="0 0 24 32"`, circle head + stroked-path limbs,
   `stroke="currentColor"`/`color: var(--text)` for automatic dark/light theming)
   drawn in a deliberately asymmetric walking stride (leading leg/arm forward,
   trailing leg/arm back) - guaranteed to read as a genuine direction change when
   mirrored, unlike an emoji glyph whose asymmetry can't be controlled. Drawn
   facing right by construction, so the CSS assignment reverted to forward=
   unmirrored, reverse=mirrored.
2. **Reverse toggle did nothing at all, even with the new SVG**: user confirmed
   via DevTools (`document.getElementById('looperGaugeCursor').classList`) that
   the `.reverse` class was *never* added, no matter how many times Reverse was
   toggled. Root-caused with a temporary console.log wrapper around
   `OnParamChange` (confirmed by the user: nothing logged at all when toggling)
   - **the host never calls `OnParamChange` back for this self-originated
   checkbox change**, not just during an active knob-drag as the pre-existing
   caveat elsewhere in this file describes (waveform-display's `user-change`
   event workaround) - apparently this extends to checkbox clicks too. This had
   been silently masked until now: `SetLooperReverse()`'s checkbox visually
   "works" regardless, because an HTML checkbox's own `checked` state toggles
   immediately on click independent of any JS/host round-trip - so relying on
   `OnParamChange`'s echo for anything else (like this walker direction) never
   actually fires. **Fixed** by calling `UpdateLooperGaugeReverse()` directly
   from `SetLooperReverse()` itself (the user's own click handler), not waiting
   for a round-trip that never comes - left `OnParamChange`'s `param===51`
   branch in place as a fallback for a genuinely host/automation-driven change,
   though that path is unconfirmed/unlikely to ever fire given this finding.
   **Worth remembering for any future checkbox-driven UI state in this
   project**: don't assume `OnParamChange` will echo a self-originated checkbox
   change back, update directly from the click handler instead.
3. **Direction was tied to the Reverse toggle alone, not actual motion**: user
   caught a real correctness bug even after the mirror started working -
   toggling Reverse ON during *Recording* made the walker face left, even though
   Recording always writes forward (left-to-right) regardless of the Reverse
   setting (`FirstSynth_Looper.h`'s Recording branch in `Process()` never reads
   `mReverse` at all - only the Playing/Overdubbing branch does). **Fixed** by
   introducing `RefreshWalkerDirection()`, which computes facing from *both*
   `looperReverse` and `looperState` together (`reversing = looperReverse &&
   (looperState === 2 || looperState === 3)` - i.e. only Playing/Overdubbing),
   called from both `UpdateLooperGaugeReverse()` (reverse toggle changes) and
   `UpdateLooperButton()` (state changes) so either input recomputes the same
   correct result. Verified via the browser-harness technique across all 5
   relevant combinations (Recording+reverseOn/off, Playing/Overdubbing+
   reverseOn/off, back to Recording+reverseOn) before rebuilding.

**All three fixes confirmed live by the user** ("ばっちりです" - perfect, after the
final round). Rebuilt both Standalone/CLAP after each round.

## Looper walker instant loop-wrap jump (2026-07-22) - experimental, kept

`private\todo.txt`: "歩く人が左端から右端に戻る時、間を見せずに一瞬で移動するように
（試しに。戻すかも）" (make the walker snap instantly at the loop-wrap point instead
of sliding across - explicitly framed as an experiment that might get reverted).

`UpdateLooperGauge()` (`index.html`) now detects the wrap and briefly sets
`cursor.style.transition = 'none'` (overriding the CSS class's `transition: left
80ms linear`) right when it happens, forces a reflow (`void cursor.offsetWidth`) so
the snap actually commits before anything re-enables the transition, then restores
transition to `''` (falls back to the CSS class) for all subsequent normal-motion
updates.

**First wrap-detection approach failed on short loops**: used a fixed magnitude
threshold (`Math.abs(cursorFraction - lastCursorFraction) > 0.5`), reasoning that a
real wrap is a big jump while normal per-tick motion is tiny. **User tested with a
short loop and reported it still slid** - root cause: `GetCursorFraction()`
(`FirstSynth_Looper.h`) is always scaled against the full 60s cap
(`mMaxSamples`), not the loop's own actual length (see that function's own
comment) - so a short loop's *entire playable range*, let alone its wrap-point
jump, can be tiny in absolute terms (e.g. a 2s loop only ever occupies
~0.033 of the 0-1 fraction range), nowhere near a 0.5 threshold. The
harness verification for the first attempt used unrealistic test values (0.95->0.02)
that happened to simulate a loop occupying nearly the full 60s range, masking this.

**Fixed** by detecting wraps by *direction* instead of magnitude: forward playback
should only ever increase `cursorFraction`, reverse only ever decrease it (matching
the same `looperReverse && (looperState===2||3)` check already used in
`RefreshWalkerDirection()`), so any value moving the *wrong* way for the current
direction can only be the wrap point, regardless of loop length or scale. Verified
via the browser-harness technique specifically with short-loop-scale fractions
(~0.03 range) in both forward and reverse before rebuilding - confirmed correct.
Rebuilt both Standalone/CLAP - **confirmed by the user** with a real short loop:
"いいですね。これで行きましょう" (good, let's go with this).

## Loop-seam crossfade question - resolved without new work (2026-07-22)

`private\todo.txt`: "ループのつなぎめ、クロスフェード現状どうなってるか。ENSOのような
自然なつなぎ目はどうやれば得られるか？" (what's the current state of the loop seam's
crossfade, how to get an ENSO-like natural seam). User concluded this was already
resolved by the Feedback time-based-decay rework above ("これに関しては、フィードバック
を直したときに解決されたと思います") - the perceived "harsh seam" was the same discrete
volume-jump-at-the-wrap artifact that rework fixed, not a separate issue. **Flagged one
open caveat for the user, not yet reported as a problem**: if an audible *click* (a
waveform discontinuity, not a volume-level change) is ever noticed specifically at the
loop point, that would be a genuinely different cause (the recorded loop's end and
start samples not lining up) requiring an actual crossfade at the boundary - not
addressed here, no crossfade exists at the loop seam currently.

## Looper transport button: fixed width regardless of label length (2026-07-22)

`private\todo.txt`: "トランスポートボタン：中の文字数によらずエリアの大きさが変わらない
ように". `.looper-transport-btn` used `min-width: 160px` - a floor, not a fixed size,
so the widest label ("Overdubbing") still grew past it. Measured via the browser-
harness technique: Record/Playing/Stopped rendered at 160px, "Recording…" at 171px,
"Overdubbing" at 176px - a visible ~16px shift matching the complaint exactly.
**Fixed** by switching to a real fixed `width: 180px` (covers the widest label with a
small margin) plus `text-align: center` so shorter labels stay centered rather than
hugging the left edge. Verified via the harness that all 5 states now render at
identically 180px - though the first verification attempt read a stale cached 160px
value from the test browser tab itself (a plain page reload wasn't enough; needed a
cache-busting query string to see the real updated CSS) - a testing-environment
gotcha, not a bug in the fix itself. Rebuilt both Standalone/CLAP - **confirmed by the
user** cycling through all 5 transport states with no size change.

## Synth page section color-coding (2026-07-22)

`private\todo.txt`: "セクションで色分け". Every knob project-wide previously shared one
uniform `--accent` blue for its pointer/value-arc (see `:root`'s knob-color vars).
Overrides `--knob-pointer`/`--knob-value-arc` per functional group of `.panel-XXX`
containers - **zero `knob-control.js` changes needed**, since its shadow-DOM SVG
already reads these via `var(...)`, which inherits through the shadow boundary the
same way dark mode's knob colors already do (confirmed via the browser-harness
technique: the shadow-DOM `<path>`/`<line>` elements' actual `stroke` computed style
differs correctly per section, not just the CSS custom property definition).

- **Oscillator/Mixer/Pitch LFO panels**: left alone, keep the default blue (the
  "first"/unmarked section).
- **Filter/Filter ADSR/Filter LFO panels**: green (`#16a34a`) - was purple in the
  first pass, user asked to change it.
- **Master/Amp ADSR/Amp LFO panels**: orange (`#ea580c`) - was teal in the first
  pass, same request.
- **Also overrides `--accent`/`--accent-soft`** in the same panel groups (not just
  the knob-specific vars) - user asked for this explicitly after seeing the first
  (knob-only) pass, since the LFO shape-selector's "selected" highlight and the
  Sync toggle switch both reference `--accent`/`--accent-soft` directly, not
  `--knob-pointer`. Added dark-mode-specific `--accent-soft` overrides per section
  too (`#1a3327` dark green, `#3a2413` dark orange) - same reasoning as the base
  theme's own existing light/dark `--accent-soft` split (a light pastel tint looks
  washed out against a dark surface). `--knob-pointer`/`--knob-value-arc` and the
  light-mode `--accent` stay identical across themes, matching the base accent's
  own existing convention.
- Verified the shape-selector's `.selected` state correctly picks up the section
  color via the browser-harness technique (real class-based state, straightforward
  to test). **Could not verify the Sync toggle's `:checked`-driven color the same
  way** - even the *unmodified*, still-default-blue Pitch LFO Sync toggle failed to
  show its accent color when checked via `element.checked = true` in this specific
  test harness, confirming it's a pre-existing harness limitation with `:checked`-
  pseudo-class-driven styles specifically (not a real click), not something broken
  by this change - proceeded to a live rebuild/test instead, where a real click
  correctly triggers `:checked`.

**Confirmed by the user** live in the native app: Filter-group knobs are green, the
Filter LFO Sync toggle and shape-selector both switch to green too, and the
Master-group elements are orange.

## Master panel: Bass Boost added, Test Note removed (2026-07-22)

User: "テストノートを取って、GLIDEの右にベースブーストのツマミをつけてください。GLIDE
とMASTERの間を少し空けてください" (remove Test Note, add a Bass Boost knob to the right
of Glide, add a small gap between Glide and Gain/"Master").

- **New `BassBoostEffect<T>`** (`FirstSynth_Effects.h`) - a cheap, common "bass boost"
  trick rather than a true low-shelf filter: a one-pole lowpass (fixed ~150Hz corner)
  added back on top of the dry signal, scaled by the boost amount - matches this
  project's "deliberately simple" DSP style (no shelving-filter gain/transition-band
  math to design/tune). Placed **first** in the master effects chain (right after CC7
  volume, before Chorus) - shapes the core tone feeding everything after it, like a
  real mixer channel strip's bass-boost switch, not a final mastering touch.
- New `kParamBassBoost` appended at the very end of `EParams` (after `kParamLooperMix`,
  matching the established "don't renumber existing params" convention), 0-100%,
  default 0.
- **Removed** the Test Note button and its now-unused `TestNote()` JS function
  entirely (the `.test-note` CSS class stays - still used by the Hold button).
- Added the knob right after Glide, and a `margin-left:20px` gap on Glide's own
  container to visually separate Gain (master output level) from the Glide/Bass
  Boost pair (note-shaping controls) - user's own reading of "GLIDEとMASTERの間".

**Confirmed by the user** live: Test Note gone, gap present, Bass Boost knob audibly
boosts low end.

## LFO Rate (Hz)/(Tempo): unified into one visual slot - IN PROGRESS, not fully
## confirmed (2026-07-22)

User: each LFO (Pitch/Filter/Amp) has always shown **two** separate Rate knobs side
by side (Hz and Tempo-sync), with only one ever musically active depending on the
Sync toggle - confusing, wanted them merged into what reads as one knob.

**Decided not to merge the underlying params** (would lose independent automation
of whichever mode isn't currently selected) - instead show only the relevant one,
hiding the other, toggled by Sync:
- `kLFORateKnobsBySync = {9:[7,8], 34:[32,33], 39:[37,38]}` (sync paramId -> [hz,
  tempo] paramIds) and `UpdateRateKnobVisibility(syncParamId, isSync)` in
  `index.html`, called from **both** `SetSync()` (the user's own click - direct
  call, not waiting on a host echo, per this session's earlier Looper-Reverse
  finding that self-originated checkbox changes never fire `OnParamChange`) and
  `OnParamChange`'s existing `kLFOSyncParams` branch (covers initial load/preset-
  recall, which *does* fire via the framework's `SendCurrentParamValuesFromDelegate`
  sync).
- **First layout attempt failed live**: wrapped the Hz/Tempo pair in a
  `.rate-knob-slot` with a **hardcoded** `width: 106px` (measured via the browser-
  harness technique: Hz ~76px, Tempo ~103px, so 106px "covers" both) - toggling
  visibility via `display:none`/`''`. Verified in the harness that this kept the
  slot's width constant across toggles... but the **user reported it was still
  resizing live, and additionally broke the whole LFO panel's vertical layout**
  (knob-row wrapping differently) - almost certainly because the real native
  WebView2 app's actual font-metric measurement of "Hz"/"Tempo" label text differs
  from whatever this project's browser-harness testing environment measured,
  making the hardcoded 106px wrong in the real app even though it looked correct
  in the harness. This project already has one prior lesson about exactly this
  class of mistake (Known Issue #15, hardcoding grid column widths) - same root
  cause, different location.
  - **Fixed (implemented, NOT yet re-confirmed live)**: replaced the hardcoded
    width entirely. `.rate-knob-slot` is now `display: grid` with **both**
    knob-containers assigned to the *same* `grid-area: 1/1` - so the slot's size
    is always exactly whichever child is naturally larger, computed by the
    browser's own layout engine wherever it actually runs, no measurement/
    guessing involved. Switched `UpdateRateKnobVisibility()` from `display:none`
    (which would remove the hidden knob from sizing entirely, defeating the
    whole point) to `visibility:hidden` + `pointer-events:none` (stays in the
    layout/sizing calculation while being invisible and unclickable). Re-verified
    via the browser-harness technique: both knob-containers now report the exact
    same `getBoundingClientRect()` (fully overlapping), and the Pitch LFO panel's
    height/row-width stayed constant (296.8px/107px/393px) across three toggles.
  - **Session paused here before the user could re-test this specific fix live**
    ("まだ直す必要がありますが、そろそろチャットの長さが限界に来ている") - resume by
    rebuilding (if not already done after the grid-based edit) and asking the user
    to re-check both the Rate-knob-slot size *and* the Pitch/Filter/Amp LFO panels'
    overall vertical layout when toggling each Sync switch. If still broken, the
    harness-vs-real-app discrepancy that caused the first failure means the
    browser-harness verification alone can't be fully trusted here - lean more on
    the user's own live report for this specific piece.

## LFO Rate label trim, knob-slot centering, Shape icon size, Filter Key Follow (2026-07-23)

Picking back up from the paused Rate-knob-slot fix above - **confirmed by the user
live** that the grid-based `.rate-knob-slot` fix (both Hz/Tempo containers on
`grid-area: 1/1`) resolved the resizing/layout issue with no further changes needed.
Four more requests same session, all in `resources/web/index.html` unless noted:

- **Rate knob titles trimmed**: all six Hz/Tempo knob-control `label` attrs (params
  7/8, 32/33, 37/38) changed from `"Rate (Hz)"`/`"Rate (Tempo)"` to plain `"Rate"` -
  user felt the unit suffix was redundant now that the Sync toggle already tells you
  which mode is showing. Chorus's own unrelated "Rate" knob (param 41, effects page)
  was already unsuffixed, untouched.
- **Rate-knob-slot: visible knob was left-aligned, not centered** (user noticed after
  the trim, while Sync was on and the narrower Tempo knob was showing). Root cause:
  both Hz/Tempo `.knob-container`s share the same grid cell so the slot is always
  sized to whichever is naturally wider (here, Hz's "0.010 Hz"-style value text beats
  Tempo's "8/1"-style text) - CSS Grid then stretches *both* grid-item containers to
  that shared width, but `.knob-container` is a plain flex box with no
  `justify-content` set, so the narrower one's actual knob+label just sat flush-left
  in the extra space instead of centering. **Fixed** by adding
  `justify-content: center;` to `.rate-knob-slot > .knob-container` specifically (not
  the general `.knob-container` rule, to keep the change scoped). Verified via the
  browser-harness technique (temporary local `python -m http.server` +
  `updateValueFromHost()` called directly on both knob-controls to force a realistic
  value/width, since without a real host `updateValue()` - and therefore
  `sizeValueBox()` - never runs at all): left/right gaps around the visible knob came
  out symmetric (~13.7px each) after the fix, versus all slack on one side before.
- **LFO Shape icon buttons shrunk 44×32px → 38×27px** (`.shape-selector`/`.shape-btn`
  CSS): user found the Pitch/Filter/Amp LFO panels' `.knob-row` (which holds the Shape
  selector alongside Rate/Sync/Depth) taller than sibling sections in the same grid
  row, and asked to shrink the shape diagrams specifically to close the gap, rather
  than touching Rate/Depth/Sync sizing. Verified via the browser-harness technique:
  the Filter/Filter ADSR/Master/Amp ADSR row height (they share row 3/row 4 of
  `.layout-grid` with Filter LFO/Amp LFO respectively) dropped from 145.4px to 137.4px
  after the shrink, confirming the Shape selector really had been the tallest,
  row-height-driving element in those rows.
- **New Filter panel knob: Key Follow** (user: "FilterのセクションにもうひとつVI
  ツマミを入れたい" - wanted one more knob in the Filter section, plus two label
  changes to make room):
  - Renamed the existing **Resonance** knob's label to **"Q"** and **Filter Type**'s
    label from `"Type (LP-BP-HP)"` to plain `"LP-BP-HP"` (both label-only, param
    IDs 23/24 unchanged) - purely cosmetic, frees horizontal space for the new knob.
  - **New param `kParamFilterKeyFollow`** appended at the very end of `EParams` in
    `FirstSynth.h` (after `kParamBassBoost`, index **55** - see the corrected Param
    list section above, which also fixes a stale entry from before `kParamLooperSpeed`
    was removed 2026-07-22), matching this project's "never renumber, always append"
    convention. `InitDouble("Key Follow", 0., 0., 100., ...)` in `FirstSynth.cpp`,
    `"FILTER"` param group, default 0% (no behavior change unless turned up).
  - **DSP** (`FirstSynth_DSP.h`): new `Voice` member `mFilterKeyFollow` (0-1, set from
    the 0-100% param via the standard `ForEachVoice`/`SetParam` case, same pattern as
    every other per-voice Filter param). Wired into the cutoff calculation exactly
    like Env Amount/Filter LFO already are - all three are additive terms inside the
    same `std::pow(2., ...)` octave exponent:
    `envAmountOctaves*filterEnvVal + inputs[kModFilterLFO][i] + mFilterKeyFollow * pitch`.
    `pitch` here is the voice's already-fetched-once-per-block "1v/oct" value relative
    to A4 (`osc1Freq = 440 * pow(2, pitch + ...)` a few lines above) - reusing it
    directly means at Key Follow=100%, cutoff shifts by exactly the same octave amount
    as the note's own pitch (true 1:1 keyboard tracking); at the default 0% the new
    term is always zero regardless of pitch, so existing presets/behavior are
    unaffected. No new smoothing slot needed (unlike Sustain-style params) since this
    only scales an already-continuous per-block pitch value, not a stepped
    host-automation target.
  - **UI**: new `<knob-control label="Key Follow" param-id="55" units="%">` added to
    `panel-filter`'s `.knob-row`, positioned right after `LP-BP-HP` (param 24) and
    before the existing 24dB Slope toggle - matches the user's "Typeの右" placement
    request. Filter panel order is now: Cutoff, Q, LP-BP-HP, Key Follow, 24dB toggle,
    Env Amount.
  - **Confirmed by the user** live right after this write-up.

## 5-band parametric EQ, last in the effects chain (2026-07-23)

User: "Effectの接続の最後に5バンドくらいのパライコ（全体のカーブがつながって見える
やつ）出来ますか。一番高い周波数はハイシェルフで一番低い周波数はローシェルフ型で" -
a 5-band parametric EQ at the very end of the master effects chain, lowest band a low
shelf, highest band a high shelf, with a display showing the combined response curve.
Loosely related to (and effectively supersedes/expands) `private\todo.txt`'s smaller
"3バンドEQを ミキサーの下に" line - this is 5 bands, placed after Reverb (not 3 bands
under the Mixer), so that todo line should probably be considered done/superseded next
time `todo.txt` gets edited, though I didn't touch that file myself (the user manages
it between sessions per this file's established convention).

**DSP** (new code in `FirstSynth_Effects.h`, right after `ReverbEffect`):
- `BiquadFilter<T>`: a minimal transposed-Direct-Form-II biquad shell (2 state vars,
  `SetCoeffs(b0,b1,b2,a1,a2)` + `Process(x)`) - deliberately just the filter core, no
  coefficient math of its own.
- `CalcPeakingCoeffs`/`CalcLowShelfCoeffs`/`CalcHighShelfCoeffs`: standard RBJ Audio
  Cookbook formulas, normalized so a0==1. Both shelf types use a **fixed S=1 shelf
  slope** (no separate per-band Slope param - matches this project's "deliberately
  simple" style) - at S=1 the cookbook's alpha term collapses to a gain-independent
  constant `sin(w0)/2*sqrt(2)`, which is what let the shelf formulas skip a 6th param.
- `ParametricEQEffect<T>`: owns 5 bands x 2 channels (L/R) of `BiquadFilter`, plus
  each band's own `{freq, gainDb, q}` (Q unused/ignored for bands 0 and 4, the shelf
  bands). `SetFreq`/`SetGainDb`/`SetQ(band, ...)` each immediately recompute that
  band's coefficients (cheap - only runs on param change, nothing extra per-sample).
  Band 0 = Low Shelf, bands 1-3 = Peaking/Bell (each with independent Q), band 4 =
  High Shelf. `Process(l, r)` runs both channels through all 5 bands in series,
  matching every other effect's `Process(T&, T&)` signature.
- **Placed last** in `FirstSynth::ProcessBlock`'s chain: `mEQ.Process(...)` added
  right after `mReverb.Process(...)`, before the Looper block - reads as "the end of
  the Effects (page) chain" the way the user asked, while staying conceptually before
  the separate Looper feature (which has its own page/concept, not part of "Effects").
- 13 new params appended at the very end of `EParams` (`FirstSynth.h`, indices 56-68,
  after `kParamFilterKeyFollow`/55 - see the Param list section above, updated) -
  `EQLowFreq/Gain`, `EQBand2/3/4Freq/Gain/Q`, `EQHighFreq/Gain`. Freq params use
  `InitFrequency` (20-20000Hz, matches Cutoff/Chorus Rate's convention); Gain is
  -15..+15dB; Q is 0.1-10, default 0.7. All default to a flat, no-op EQ (gains at
  0dB) so existing presets/sound are unaffected until touched.
- `OnParamChange` (FirstSynth.cpp): 13 new cases, each a one-line
  `mEQ.SetFreq/SetGainDb/SetQ(bandIndex, value)` call - no smoothing slot needed
  (same reasoning as Key Follow above: these only feed an already-per-block-rate
  coefficient recalc, not a per-sample-smoothed modulation target).
- `OnReset`: added `mEQ.SetSampleRate(GetSampleRate())` alongside the other effects.

**UI** (`resources/web/index.html` + new `resources/web/eq-curve-display.js`):
- New `.panel-eq` in its own `.effects-row` on the Effects page, right after the
  Chorus/Delay/Reverb row (own row because the curve display + 5 knob columns are far
  wider than those three panels combined - letting it wrap into the existing row would
  have looked cramped). Layout: `eq-curve-display` on top, then a `.eq-band-row`
  (`.knob-row` variant, `align-items: flex-start` so the 2-knob shelf columns don't
  get stretched to match the 3-knob peak columns' height) of 5 `.eq-band-col`s
  (vertical knob stacks: Freq, Gain, and Q for the 3 middle bands only).
- **`eq-curve-display.js`**: new canvas custom element, same "no interaction of its
  own, purely a readout" pattern as `waveform-display.js`. Mirrors the C++ coefficient
  formulas in JS (kept in sync manually, same caveat as the waveform mirror) and draws
  the **combined** magnitude response by summing all 5 bands' dB contributions at each
  frequency - this summation is exact, not an approximation (|H1*H2|=|H1|*|H2|, so
  dB(H1*H2)=dB(H1)+dB(H2) precisely for cascaded biquads). Uses a fixed reference
  48kHz sample rate for the curve math (a visualization-only approximation - the real
  DSP always uses the actual host sample rate; the error is negligible except very
  close to Nyquist, where bands aren't typically placed anyway).
- **Bug caught before shipping, fixed same session**: `OnParamChange(param, value)`'s
  `value` is **normalized [0,1]**, not the real Hz/dB/Q value - confirmed by reading
  `IPlugWebViewEditorDelegate.h`'s `SendParameterValueFromDelegate`, which always
  calls `GetParam(paramIdx)->ToNormalized(value)` before handing off to JS. My first
  wiring attempt passed this normalized value straight into the curve display's
  `setFreq`/`setGain`/`setQ` (which expect real values), verified wrong via the
  browser-harness technique (manually simulating `OnParamChange(56, 200)` "worked" but
  only because that test itself was unrealistic - a real host would never call
  `OnParamChange` with a raw Hz number). **Fixed** with a new `KnobNormalizedToReal(
  knobEl, normValue)` helper that mirrors knob-control.js's own private
  `normalizedToReal()` (reads the knob's `min`/`max` attributes - set from the host's
  "params" JSON on load - and `shape`/`shape-exponent` attributes - static in the
  markup) - called once, only at the `OnParamChange` call site, right before
  `UpdateEQCurve`. The separate `user-change` (live-drag) listener path needed **no**
  such conversion - `e.detail.finalValue` from knob-control.js is already real, by
  design (see that file's own comment on why `user-change` carries `finalValue`
  alongside `normValue`). Re-verified via the browser-harness technique after the fix:
  simulated `OnParamChange(56, 0.5)` (log-shape Freq, min=20/max=20000) produced
  exactly `sqrt(20*20000)=632.46`, matching both the knob's own displayed text
  ("632 Hz") and the curve display's internal state - confirms the two paths
  (host-driven vs live-drag) now agree.
- Wiring: `kEQParamMap` (paramId -> `[bandIndex, 'freq'|'gain'|'q']`) drives both
  `OnParamChange`'s branch and a `user-change` listener attached to all 13 EQ knobs at
  the bottom of the page-level `<script>` (same place the two Wave-knob listeners
  already live) - one small lookup table instead of 13 near-duplicate branches.
- Verified via the browser-harness technique (temporary local `python -m http.server`,
  simulated realistic `min`/`max` attributes per knob) that: the peak bands hit their
  exact target gain at their own center frequency (e.g. +9dB gain -> `totalDbAt(centerFreq)`
  = 8.9995), shelf bands reach their target gain well past the corner and stay near 0dB
  on the other side (verified both directions for both shelf types), and the panel
  renders without wrapping/JS errors. **Confirmed by the user** ("いいですね") live
  right after this write-up.

## PEQ LOCK toggle for the new EQ panel (2026-07-23)

Same session, right after confirming the EQ above. User: "今私がパラメータを少しいじり
ましたので、この状態を記憶して、パラメータにロックをかけて表示を見えないようにしてく
ださい。そしてPEQ LOCKスイッチというスイッチだけを残してください。このスイッチを押す
と、今見えているイコライザの全貌が見えて、またエディットできるようになります。" -
wanted the just-tuned EQ's curve/knobs hidden so they can't be accidentally nudged,
leaving *only* a "PEQ LOCK" switch visible; pressing it reveals and re-enables editing.

**Pure WebView display concern - no C++/param changes at all**, same category as Dark
Mode: the real 13 EQ param values keep working normally underneath regardless of
whether this is showing (nothing is actually "locked" DSP-side, just hidden), so
"記憶して" (remember this state) needed no snapshot mechanism - revealing the panel
again just shows whatever the live host param values already are.

- `resources/web/index.html`: `.panel-eq` restructured - the `<h2>`/`eq-curve-display`/
  `.eq-band-row` moved into a new wrapper `<div id="peqLockedContent">`, with a
  `.toggle-container` (`PEQ LOCK` label + checkbox switch, same markup pattern as the
  24dB Slope/Sync toggles) added as a sibling *outside* that wrapper, so it's the only
  thing left visible when locked - matches the user's literal "スイッチだけを残して".
- `ApplyPEQLock(locked)`: sets `#peqLockedContent`'s `display` to `none`/`''`, syncs
  the checkbox's own `checked` state (needed for the programmatic restore-on-load call
  below, which doesn't go through the checkbox's own `onchange`), and persists to
  `localStorage` (`firstSynthPEQLocked`) - same persisted-WebView-preference pattern as
  `kUIScaleStorageKey`/`kDarkModeStorageKey` right above it in the file.
- **Defaults to locked** when never set before (`localStorage.getItem(...) !== '0'`) -
  deliberately the opposite default from Dark Mode (which defaults off) since the user
  asked to lock the panel *right now*, over values they'd just finished tuning; first
  launch (or any launch after an explicit prior "unlocked" choice was never made) comes
  up locked.
- Restore call (`ApplyPEQLock(localStorage.getItem(kPEQLockStorageKey) !== '0')`) placed
  in the bottom post-body `<script>` block, alongside the wave-knob `user-change`
  listener wiring - not gated behind the host "params" round-trip (unlike
  `RestoreUIScale()`), since this touches no param/host state at all, same reasoning as
  Dark Mode's immediate-apply approach, just deferred to after `<body>` parses since
  (unlike Dark Mode's `<html>` root) the target elements are body content.
- Verified via the browser-harness technique: fresh load (no prior localStorage) comes
  up locked with the panel's visible text reduced to exactly `"PEQ LOCK"`; toggling via
  both the JS function directly and a real `change` event on the checkbox itself both
  correctly show/hide `#peqLockedContent` and persist the choice. **Not yet confirmed
  live in the native app** as of this write-up (rebuilt both Standalone/CLAP and
  relaunched Standalone right after implementing).

## Auto-persist all params across Standalone launches (2026-07-23)

Same session. User: "今後、すべてのパラメータの設定を次に開いたときに覚えているように
できますか" (remember every param's settings the next time the app is opened) -
distinct from the existing manual File > Save/Load Preset feature (2026-07-20,
Standalone only, user-chosen `.preset` file) - this is automatic, no menu action
needed, and always uses one fixed file rather than a chosen one.

**Standalone-only** (`#ifdef APP_API` throughout, both `FirstSynth.h`/`.cpp`) - a CLAP
instance's state is already persisted by the *host's* own project save/reload via the
same `SerializeState`/`UnserializeState` pair (already correct/shared per the Preset
section above), so this file-based mechanism would be redundant there; only Standalone
has no such host to lean on.

- **New file**: `C:\Users\a_wak\AppData\Local\FirstSynth\autosave.state` - same folder
  `settings.ini` already lives in (`INIPath(path, "FirstSynth")` from `IPlugPaths.h`,
  already included), so no folder-creation step was needed - confirmed that folder
  already existed from `settings.ini`'s own prior writes.
- **Save side**: `OnParamChange` sets a new `std::atomic<bool> mAutoStateDirty` on
  *every* param change (any param, any source - user drag, host automation, even the
  restore itself); `OnIdle()` (already ticking every `IDLE_TIMER_RATE`=50ms for the
  meter/looper-gauge) checks-and-clears that flag and calls `SaveAutoState()` if dirty -
  batches writes to at most once per idle tick rather than once per param-change
  (which could be many times a second while a knob is actively being dragged), same
  atomic-flag-plus-`OnIdle`-drain idiom already used for `mLooperStateDirty`.
  `SaveAutoState()` itself is just `SerializeState(chunk)` + a raw `fwrite` - identical
  mechanism to the manual Save Preset menu command, just a fixed path instead of a
  `GetSaveFileNameA` dialog result.
- **Load side, v1 (constructor-time) - reverted same session, see correction below**:
  first attempt called `LoadAutoState()` from the constructor, right after every
  `GetParam(...)->InitXXX(...)` call, reasoning that `OnWebContentLoaded()`'s later
  base-implementation param sync would naturally pick up the already-restored values,
  making a separate `OnRestoreState()` push (unlike Load Preset) unnecessary.
- On a truly first-ever launch (no `autosave.state` file yet), `LoadAutoState()`'s
  `fopen` simply returns null and it's a no-op - defaults stand as normal (still true
  after the correction below).
- **User follow-up same session**: "動かした箇所だけでなく、動かしてないパラメータも
  含めて全部再現できるようにしてほしいです" (reproduce not just the params I moved, but
  literally all of them). Investigated at the file level first (not a live-GUI bug
  report I could directly reproduce): decoded `autosave.state`'s raw bytes as 69
  little-endian doubles and confirmed the **save side was already fully correct** -
  dozens of distinct non-default values across many different param indices, not just
  one or two - because `IPluginBase::SerializeParams`/`UnserializeParams` (the
  framework code `SerializeState`/`UnserializeState` call into) unconditionally
  iterate *every* param each time, and `OnParamReset`'s default implementation (called
  once at the end of `UnserializeParams`) calls `OnParamChange` **and**
  `OnParamChangeUI` for literally every param index too - so nothing was ever being
  selectively skipped.
- **Real risk found on review, fixed regardless of whether it was the actual reported
  symptom**: the v1 constructor-time call could reach `UnserializeState`'s internal
  `OnParamChange` calls safely (all the effect/DSP member objects like `mBassBoost`/
  `mEQ`/etc. are plain non-pointer members, already fully constructed via the
  member-initializer phase before the constructor *body* runs) - but calling
  `OnRestoreState()` at that same point (which I'd initially reasoned was unnecessary
  and therefore hadn't added) would have been unsafe *if* added later without also
  moving the timing, since `OnRestoreState()`'s default implementation calls
  `EvaluateJavaScript` per param, and **no WebView/editor exists yet at
  constructor-time** - there's nothing for that call to reach.
- **Fixed by moving the restore entirely into `OnWebContentLoaded()`** (same method
  that already hosts the computer-keyboard-input feature, `#ifdef APP_API`-gated),
  right after the existing `EDITOR_DELEGATE_CLASS::OnWebContentLoaded();` base call:
  `LoadAutoState(); OnRestoreState();` - the exact same
  `UnserializeState()`-then-`OnRestoreState()` pair the already-proven-working manual
  Load Preset menu command uses, just triggered automatically instead of from a menu
  click. This is provably safe (WebView is guaranteed alive at this point - it's the
  "web content has loaded" callback) and removes any remaining doubt about whether the
  natural `OnWebContentLoaded()`-triggered `OnUIOpen()` timing alone was sufficient -
  the base call now sends one round of (compiled-default) values, then `LoadAutoState`
  + `OnRestoreState` immediately overwrite the UI with the real restored ones a moment
  later. The EQ's per-band coefficients still get computed correctly regardless of
  this timing change - computed once during `OnParamChange` using whatever sample rate
  is current (the default 44.1kHz placeholder, if `OnReset()` with the real rate
  hasn't landed yet), then correctly recomputed anyway once `OnReset()` does fire with
  the real rate - no audible glitch either way.
- Not yet re-confirmed live by the user after this correction (rebuilt both
  Standalone/CLAP, relaunched Standalone right after) - ask them to retest: move
  several different knobs across different panels/pages, close, reopen, confirm all
  of them (not just the last one touched) come back correctly. If anything still
  doesn't restore, get the specific param name(s) that failed - that's the concrete
  data needed to keep investigating, since I have no way to click the native window's
  controls myself to reproduce a live-GUI-only symptom directly.

**Confirmed working end-to-end by the user** ("できてます。確認しました。") right after
the `mAutoStateLoaded` race-condition fix above - the auto-persist feature is done.

**User instruction to remember going forward (2026-07-23):** the 5-band EQ (PEQ) and
the current overall tuned param state are not "just another synth setting" to the
user - they consider this a defining characteristic of 1st Synth's own character/
identity. They may adjust values somewhat over time, but the general direction is to
leave the EQ/current settings largely alone, not casually reset or rework them.
**Don't suggest changing the PEQ defaults or the current saved state unprompted, and
be conservative/ask first before any change that would alter the currently-tuned
sound.** This matters especially now that params auto-persist across launches (see
above) - the live `autosave.state` file IS this tuned identity; if it's ever
overwritten for testing purposes (as happened once already this session, for the
`mAutoStateLoaded` race-condition verification), restore the real values afterward,
don't leave test data in place.

## Master Gain knob mirrored onto the Effects page (2026-07-23)

User: "Synthページにあるゲインのつまみを、エフェクトページにもおいてください。この
ツマミは連動していて、片方で値を変えるともう片方も同じように動きます" - wanted the
existing Master Gain knob (param-id 0, Synth page's `panel-master`) also available on
the Effects page, kept in sync both ways. Reasoning given: adjusting effect Mix
amounts changes perceived loudness, so being able to nudge overall level without
leaving the Effects page is useful while dialing those in.

- Added a second `<knob-control id="effectsGainKnob" label="Gain" param-id="0">` in a
  small `.panel-master`-classed section (reusing that exact class name, not a new
  one) at the very start of the Effects page's first `.effects-row`, before Chorus -
  reusing `panel-master` means it automatically picks up the same orange
  section-color-coding as the Synth page's real Master panel (`--knob-pointer:
  #ea580c` etc.), for free, no new CSS needed. The original Synth-page knob gained a
  matching `id="masterGainKnob"` (previously unidentified).
- **Real bug found and fixed while implementing this**: `OnParamChange`'s knob lookup
  used `document.querySelector(...)` (singular) to find "the" knob-control for a
  given param-id and push host-driven updates to it - fine when every param has
  exactly one knob, but with two Gain knobs now sharing param-id 0, only whichever
  one happened to appear first in the DOM would ever receive host-driven syncs
  (initial load, preset recall, DAW automation) - the second would silently drift out
  of sync. **Fixed** by switching to `document.querySelectorAll(...)` +
  `.forEach(knob => knob.updateValueFromHost(value))` - updates every matching
  knob-control, not just the first. This is a general fix (harmless for every other
  param, which still only ever has one knob-control) that also makes any *future*
  duplicate-param-id knob "just work" without needing another special case.
- **Live-drag mirroring** (host-driven sync alone doesn't cover *dragging* one knob,
  since a host doesn't echo a self-originated change back to the UI mid-drag - same
  reasoning as the wave-knob/EQ-curve `user-change` mirroring elsewhere in this file):
  added two `user-change` listeners, one on each Gain knob, each pushing its own live
  `normValue` straight to the other via `updateValueFromHost(...)`.
  `updateValueFromHost`'s `fromHost=true` internally skips re-dispatching
  `user-change`/`SPVFUI` (see knob-control.js's own `updateValue()` comment), so this
  can't loop back and forth between the two listeners.
- Verified via the browser-harness technique: simulated dragging `masterGainKnob` to
  75% -> `effectsGainKnob` displayed "75.0 %" immediately (live-drag path); simulated
  dragging `effectsGainKnob` to 20% -> `masterGainKnob` displayed "20.0 %" (other
  direction); simulated a host-driven `OnParamChange(0, 0.5)` -> *both* knobs updated
  to "50.0 %" (confirms the querySelectorAll fix). Also confirmed the Effects-page
  copy inherits the orange `--knob-pointer` color automatically via the shared
  `panel-master` class.
- Not yet confirmed live in the native app as of this write-up (rebuilt both
  Standalone/CLAP and relaunched Standalone right after implementing).

## CC7 now drives Gain directly, mCCVolume removed (2026-07-23)

User: "今CC64（ボリューム？）は内部のボリュームをコントロールしているようですが、内
部のボリュームは常に最大値にしておいて、CC64の信号はGainとつながるようにできます
か". Worth noting: the actual implemented CC has always been **CC7** (`IMidiMsg::
kChannelVolume`), not CC64 (that's Sustain/Hold, a separate existing feature - see the
Hold button in the WebView UI section above) - the user's request clearly describes
CC7's real behavior ("seems to control internal volume"), so treated this as CC7
throughout, not a literal CC64 remap.

Previously (since early in the project, see the now-struck-through note above): CC7
scaled `mCCVolume`, a separate multiplier applied to the final output in
`ProcessBlock`, entirely independent of the visible Gain knob/param - two overlapping
"volume" controls, one of them invisible. User found this confusing and wanted CC7 to
move the *same* Gain knob instead, with the old hidden multiplier gone entirely (not
just frozen at 1.0 - dead code, removed outright per this project's usual convention).

- **Removed**: `mCCVolume` member (`FirstSynth.h`) and its two-line multiply at the
  top of `ProcessBlock`'s per-sample loop (`FirstSynth.cpp`) - Gain itself was already
  a fully-wired, audible param (applied per-voice inside `FirstSynth_DSP.h` via
  `mParamsToSmooth[kModGainSmoother]`), so nothing else needed touching for Gain
  itself to keep working exactly as before.
- **`ProcessMidiMsg`'s CC7 branch** now calls `SetParameterValue(kParamGain,
  msg.ControlChange(IMidiMsg::kChannelVolume))` instead of assigning `mCCVolume`.
  `IMidiMsg::ControlChange()` already returns `[0,1]`, which for Gain (a plain linear
  0-100% param, no shape curve) is exactly its own normalized value - no conversion
  needed. `SetParameterValue` (`IPlugAPIBase`) is the standard iPlug2 mechanism for
  "an internal/MIDI source changes a parameter" - it updates the param (audibly
  correct immediately, since `FirstSynth_DSP.h` reads the real param value) and calls
  the DSP-side `OnParamChange(int)`.
- Updated the meter's own comment (`ProcessBlock`) from "post CC7 volume" to "post
  Gain," since that's now the only volume-scaling stage feeding it.
- **User-reported bug, same session**: "Gainノブは動いていません" - the Gain param's
  real value *was* changing (audio behavior was correct), but the on-screen knob
  never moved. Root-caused by reading `IPlugAPIBase::SetParameterValue`'s actual
  implementation (`SetNormalized` + `InformHostOfParamChange` + `OnParamChange(idx,
  kUI)`) against `IPlugAPP.h`: for the **Standalone app specifically**,
  `InformHostOfParamChange` is a complete no-op (`{}` - no host/DAW exists to
  inform), and the 3-arg `OnParamChange(idx, source)`'s default implementation only
  calls the 1-arg `OnParamChange(idx)` (DSP-side, what `FirstSynth::OnParamChange`
  overrides) - it does **not** call `OnParamChangeUI`/`SendParameterValueFromDelegate`,
  which is the only thing that actually reaches the WebView (`EvaluateJavaScript`,
  `SPVFD(...)`). My original comment claiming "OnParamChange... pushes the new value
  to the WebView UI too" was simply wrong - traced and corrected via reading the
  actual framework source rather than assuming.
  - **Fixed** with the same atomic-flag-plus-`OnIdle()`-drain pattern already used
    for `mLooperStateDirty`/the meter: new `std::atomic<bool> mGainUIDirty`, set
    in `ProcessMidiMsg`'s CC7 handler right after `SetParameterValue`, consumed by
    `OnIdle()` which calls `SendParameterValueFromDelegate(kParamGain,
    GetParam(kParamGain)->GetNormalized(), true)` - this is the one call that
    actually reaches the WebView, and it must run on the main thread (WebView2's COM
    object isn't safe to touch from the audio thread, which is where
    `ProcessMidiMsg` runs) - `OnIdle()` already runs on the main thread (confirmed:
    it already does exactly this kind of push for the meter/looper gauge).
  - Left ungated (not `#ifdef APP_API`) since it's harmless for CLAP too: a real
    DAW host's own automation echo would likely reach the UI through its own proper
    channel already, making this redundant there, but redundant-and-idempotent
    (pushing the same already-correct value again) isn't a bug, and keeping one
    code path for both targets is simpler than special-casing it.
- **Confirmed working by the user** ("うまくいっています") right after this fix.

## App icon: the Looper walker on a gauge (2026-07-23)

User: "そろそろこのシンセのアイコンを作ろうと思います。ルーパーの人がゲージの上で歩
いている感じでお願いします" - wanted an actual icon for 1st Synth (previously just
iPlug2's generic default template icon, a plain "iPlug" wordmark), based on the
Looper page's hand-drawn walking-cursor character (see the "Looper" WebView UI
section above for that SVG's original design/history).

- **File**: `resources/FirstSynth.ico`, referenced by `resources/main.rc`'s
  `IDI_ICON1` (this is the Standalone `.exe`'s icon *and* feeds into whichever other
  formats reuse the same resource - confirmed via `resources/main.rc` grep, no other
  icon reference existed for this project besides the generic VST/AAX icons the
  postbuild script uses for those specific formats' own installers).
- **Design**: a dark rounded-square badge (`#161b22`) containing the exact same
  stick-figure silhouette used by the Looper gauge's walking cursor (head circle +
  spine + 2-segment legs mid-stride + 2 arms, coordinates directly adapted from that
  SVG's `<path>` data in `index.html`, scaled ~4.5x) rendered in off-white
  (`#f4f6fb`), standing on a rounded gauge-bar shape (dark track + blue `#2563eb`
  fill, matching the app's own `--accent`) - reusing the project's own established
  character/color language rather than inventing a new one.
- **Built with GDI+ primitives directly** (`System.Drawing` via PowerShell), not by
  rasterizing an SVG - the icon is just rounded rects, an ellipse, and round-capped
  lines, all cheap to reproduce with `GraphicsPath`/`Pen`/`Brush` at each target
  resolution (16/32/48/64/128/256), keeping every size crisp rather than scaling one
  bitmap. All coordinates are defined once in a 256-unit design space and scaled by
  `size/256.0`, so every resolution matches proportionally.
  - 16px is noticeably blurrier than the larger sizes (expected for a detailed
    multi-shape glyph at that resolution - most app icons simplify at 16px, this one
    doesn't, but the silhouette still reads as "a person + a bar" even blurred).
- **Packaged as a proper multi-resolution `.ico`** (not just one bitmap): wrote the
  ICONDIR + ICONDIRENTRY-per-size + raw-PNG-per-size container format by hand (a
  `System.Drawing.Icon` alone can't easily construct a *multi-size* `.ico` from
  scratch) - PNG-compressed frames inside `.ico` have been valid since Windows Vista,
  simpler than the legacy uncompressed-DIB approach.
- **Verified two ways**: loaded the new file back with `System.Drawing.Icon` to
  confirm it parses correctly, and (more importantly) extracted the icon directly
  from the *built* `FirstSynth.exe` after rebuilding to confirm the new artwork
  actually got embedded, not just present on disk - MSBuild's incremental build
  tracks `.rc`-file changes but a same-named `.ico` being silently overwritten on
  disk wasn't guaranteed to trigger a resource recompile, so this check mattered.
- Rebuilt both Standalone/CLAP and relaunched Standalone with the new icon.
  **Confirmed by the user** ("とりあえずこれで行きましょう") after being shown the
  256px/32px/16px previews before committing it to the actual build.
- **Follow-up same day**: user restarted Explorer but still saw the old icon -
  confirmed the built `.exe` itself had the correct new icon embedded (extracted it
  directly via `System.Drawing.Icon` and compared), so this was Windows' own icon
  cache being stale, not a build/embedding problem. Restarting `explorer.exe` alone
  doesn't always clear it - deleted the actual cache database files
  (`%LocalAppData%\IconCache.db` and every `%LocalAppData%\Microsoft\Windows\
  Explorer\iconcache_*.db`/`thumbcache_*.db`) and restarted Explorer again.
  **Confirmed fixed by the user** ("見れました").

## WASAPI added as a Standalone audio driver option (2026-07-23)

User: developing is paused for now ("これでいったん開発を止めます"), but asked about
ASIO sounding noticeably better than DirectSound, and that OBS can't loopback-record
audio sent out via ASIO on their setup (no loopback support), so they can't currently
make demo videos at good audio quality. Discussed two options (WASAPI vs an external
virtual-routing tool like VoiceMeeter/ASIO4ALL) with rough effort estimates for each -
**user chose WASAPI**, partly from prior experience that WASAPI tends to sound good
in other software too.

**Root cause of the DirectSound quality complaint**: DirectSound (`__WINDOWS_DS__`,
RTAudio's legacy Windows backend) goes through the older Windows audio mixer/kmixer
path, which can resample/process audio more aggressively than the modern WASAPI
engine - ASIO bypasses the OS mixer entirely (direct hardware access), which is why
it sounds best but can't be loopback-captured by OBS the way a normal Windows audio
device can.

**Investigated before implementing** (this is a shared iPlug2 framework gap, not a
FirstSynth-specific bug - affects [[project-suikinkutsu-plugin]] too once that project
rebuilds against the same checkout): confirmed via `RtAudio.h`
(`#if !(defined(__WINDOWS_DS__) || defined(__WINDOWS_ASIO__) || defined(__WINDOWS_WASAPI__)...`)
that RTAudio's WASAPI backend (`RtApiWasapi`) is already fully implemented in the
vendored RTAudio source - it was simply never enabled for this project's Standalone
build, and the driver-selection UI never had a slot for it either. Also confirmed the
WASAPI-specific linker libs (`ksuser`, `mfplat.lib`) are already `#pragma comment`'d
directly inside `RtAudio.cpp` itself, so no vcxproj/props linker changes were needed
beyond the compile-time macro.

**Four small changes, all in the shared `iPlug2` checkout** (none in FirstSynth's own
files - this benefits any iPlug2 Standalone app sharing this checkout):
- `common-win.props`: added `__WINDOWS_WASAPI__` to `APP_DEFS` (was
  `__WINDOWS_DS__;__WINDOWS_MM__;__WINDOWS_ASIO__` only).
- `IPlug/APP/IPlugAPP_host.h`: added `const int kDeviceWASAPI = 2;` - **appended**,
  not inserted before/renumbering `kDeviceASIO`(1), since `mAudioDriverType` is
  persisted as a raw int in each user's `settings.ini` (`GetPrivateProfileInt`) -
  renumbering would silently reinterpret existing users' saved driver choice as a
  different driver on next launch.
- `IPlug/APP/IPlugAPP_host.cpp`'s `TryToChangeAudioDriverType()`: added
  `else if (mState.mAudioDriverType == kDeviceWASAPI) mDAC =
  std::make_unique<RtAudio>(RtAudio::WINDOWS_WASAPI);` alongside the existing
  ASIO/DS branches.
- `IPlug/APP/IPlugAPP_dialog.cpp`'s `PopulatePreferencesDialog()`: added a third
  `CB_ADDSTRING` call ("WASAPI") to the driver dropdown - **must be the 3rd call**
  (combobox item index *is* the actual "enum" read back by `CB_GETCURSEL`/
  `CB_SETCURSEL`, no separate ID mapping exists) so its index (2) lines up with
  `kDeviceWASAPI`. Checked the two other `driverType == kDeviceASIO` special cases in
  this same file (disabling the input-device combo, enabling the ASIO control-panel
  button) - both correctly fall through to WASAPI's "else" branch unchanged, since
  WASAPI (like DirectSound) uses separate in/out device selection, not ASIO's
  single combined-device model.
- **Build note**: first attempt used `-t:FirstSynth-app -t:Rebuild` (two separate
  `-t:` flags), which MSBuild parsed as "build both of these top-level targets" and
  ended up trying (and failing) to rebuild the unused/unconfigured VST2/VST3/AAX
  projects too (missing SDKs, pre-existing and expected - see "Targets actively
  built/tested" in the Project basics section). Correct syntax for "rebuild just this
  one project" is a single colon-joined target: `-t:FirstSynth-app:Rebuild`. Used a
  full rebuild rather than an incremental build specifically because this change is a
  global preprocessor macro (`common-win.props`) affecting compilation across many
  source files (RTAudio, IPlugAPP_host, etc.) - safer to force a clean recompile than
  risk some translation units silently keeping the old macro set.
- Rebuilt both Standalone/CLAP and relaunched Standalone. **Not yet confirmed live by
  the user** as of this write-up - ask them to open Preferences/Audio Settings,
  select "WASAPI" as the driver, pick their audio interface, and check both (a)
  whether it sounds noticeably better than DirectSound and closer to ASIO, and (b)
  whether OBS's Desktop Audio (WASAPI loopback) capture now works with FirstSynth
  routed through it.

## Status as of last session (2026-07-23) — development paused here

User: "かなり仕上がってきたので、これでいったん開発を止めます" - pausing development
again after a long, productive session. Everything below was confirmed working live
by the user before the pause, **except** the WASAPI item (sound confirmed playing,
but a real quality comparison against DirectSound/ASIO is still pending - their audio
interface was unplugged at the time). Read this whole file top-to-bottom when
resuming, and re-check `private\todo.txt` fresh (it's a live file the user edits
between sessions).

**Confirmed working this session, in order:**
1. LFO Rate knob title trim ("Rate (Hz)"/"Rate (Tempo)" -> plain "Rate"), the
   rate-knob-slot centering fix, and the LFO Shape icon shrink (44x32 -> 38x27px)
2. Filter panel: Resonance relabeled "Q", Filter Type relabeled "LP-BP-HP", new
   **Key Follow** knob (param 55) added between them
3. New **5-band parametric EQ** (params 56-68) at the end of the effects chain -
   Low Shelf / 3x Peaking / High Shelf, with a live combined-response curve display
   (`eq-curve-display.js`)
4. **PEQ LOCK** toggle hiding/revealing the EQ panel (WebView-only, localStorage-persisted,
   defaults locked)
5. **Auto-persist every param across Standalone launches** (`autosave.state`, `#ifdef
   APP_API`) - including a real race-condition fix (`mAutoStateLoaded` gate) found
   and fixed after the first attempt reset to defaults on every launch
6. **Master Gain knob mirrored onto the Effects page** (`effectsGainKnob`), live-linked
   both ways with the Synth page's `masterGainKnob` - also fixed a real bug this
   surfaced (`OnParamChange`'s knob lookup was `querySelector`, singular; switched to
   `querySelectorAll` so host-driven updates reach every knob sharing a param-id, not
   just the first)
7. **MIDI CC7 now drives the Gain param directly** (`mCCVolume` multiplier removed
   entirely) - needed a second fix after the first attempt correctly changed the
   param's real value but never told the WebView UI (`SetParameterValue` alone
   doesn't reach `SendParameterValueFromDelegate`; added `mGainUIDirty` +
   `OnIdle()`-driven push, same pattern as the meter/looper)
8. **New app icon** (`resources/FirstSynth.ico`) - the Looper's hand-drawn walker
   standing on a gauge bar, replacing iPlug2's generic default icon. Also had to
   manually clear Windows' icon cache database (Explorer restart alone wasn't enough)
9. **WASAPI added as a Standalone audio driver option** - a shared `iPlug2`-checkout
   fix (see that section above), motivated by ASIO sounding best but being
   unusable with OBS's lack of loopback capture on the user's setup. **Fully
   confirmed working** ("うまくいっているようです") in the next session, once the
   audio interface was reconnected - both the quality (vs. DirectSound/ASIO) and
   OBS's WASAPI-loopback Desktop Audio capture picking up FirstSynth correctly.
   This item is done, no longer open.

**Important standing instruction, unrelated to any single feature (repeated here
since it governs how to treat items 2-4 above going forward):** the 5-band EQ and
the overall current tuned param state are, in the user's own words, a defining
characteristic of 1st Synth's identity, not just "another setting." Don't propose
changing the EQ defaults or the current saved state unprompted - see the dedicated
note earlier in this file (search "Important, ongoing instruction") for the full
wording, and [[project-firstsynth-clap-plugin]]'s memory file, which carries the
same instruction.

## Possible next steps

- ~~WASAPI verdict pending~~ - **done**, confirmed working (quality and OBS loopback
  both) the session right after it was added.
- **Continue down `private\todo.txt`** (re-read it fresh - the 5-band EQ item above
  already supersedes/expands its old "3バンドEQを ミキサーの下に" line, and the LFO
  Rate-knob-slot fix from 2026-07-22 is long since confirmed, so don't re-litigate
  either): remaining lower-priority items as of the last read were rounding the Amp
  LFO Shape's Square corners (reported to "crackle"), longer ADSR Release max values,
  more effects (feedback/distortion/bitcrush/lowpass/5-band parametric EQ - **the
  parametric EQ part of this is now done**, reorderable effects order is not), a
  sub-oscillator, and a Standalone tempo source for LFO sync.
- Preset save/load exists but Standalone-only (File menu, `.preset` files); VST3/CLAP
  host preset browsers aren't wired up (would need separate, format-specific work
  even though `SerializeState`/`UnserializeState` are already correct and shared).
  Note this is now somewhat superseded in spirit by the auto-persist-across-launches
  feature (item 5 above) for the Standalone-only case specifically.
- VST3/VST2 targets are patched to build but never actually tested — CLAP and
  Standalone are the only formats verified end-to-end.

## Amp + Filter envelope Attack changed from linear to ease-in curve (2026-07-26)

User picked development back up ("これでいったん開発を止めます" from the last session
no longer applies) and reported the sound feels "硬い" (hard/stiff). Asked what curve
shape A/D/R currently use - turned out FirstSynth uses iPlug2's stock `ADSREnvelope<T>`
class (`iPlug2\IPlug\Extras\ADSREnvelope.h`) completely unmodified, with no per-curve
customization (unlike [[project-suikinkutsu-plugin]]'s own bespoke envelope with its
`ShapedRamp`/Curve knob). That stock class's shapes: **Attack** is a straight linear
ramp (`CalcIncrFromTimeLinear`); **Decay** and **Release** are both the same
one-pole/RC-style exponential decay (`CalcIncrFromTimeExp`, targets ~-60dB by the set
time). Explained that Decay/Release's exponential shape is a normal analog-modeled
choice, but a **linear Attack** has a nonzero, constant slope from sample 1 - an
abrupt onset that's a plausible source of perceived hardness. User asked to try
changing it.

**Chose an ease-in power curve (slope starts near zero, accelerates toward the top)
over the RC-charge exponential (`1-e^-t/τ`) Decay/Release already use** - the RC-charge
shape actually has its *steepest* slope right at t=0 (capacitor-charging behavior),
which would if anything sound more abrupt at the very onset, not softer. An ease-in
power curve (`x^n`, n>1) is what actually produces a gentler start.

**Implementation** (shared `iPlug2\IPlug\Extras\ADSREnvelope.h` framework file, same
class [[project-suikinkutsu-plugin]] does *not* use, so blast radius is FirstSynth-only
today despite being a framework-level change):
- Added `mAttackShapeExponent` (default `1.` = linear, byte-identical to the old
  behavior since `pow(x, 1.) == x` exactly) and a public `SetAttackShape(T exponent)`
  setter - opt-in, doesn't change default behavior for anything not calling it.
- `Process()`'s `kAttack` case now computes `result = std::pow(mEnvValue, mAttackShapeExponent)`
  instead of `result = mEnvValue` directly; `mEnvValue` itself still drives the linear
  0-1 timing/stage-transition logic unchanged, only the *output* curve is reshaped -
  same pattern the class already uses elsewhere (e.g. Decay's `result` differs from
  its raw `mEnvValue`).
- `FirstSynth_DSP.h`'s `Voice` constructor: added `mAMPEnv.SetAttackShape(2.);` -
  applied to the **Amp envelope only** at first (narrowest change, to test the
  hypothesis in isolation).

Rebuilt Standalone+CLAP (Debug), relaunched. **Confirmed by the user** ("いい感じです
ね。急に良くなったような感じがあります。") - the ease-in Attack noticeably reduced the
perceived hardness.

**Follow-up same session**: user liked it enough to ask for the same treatment on the
Filter envelope's Attack (the cutoff sweep's onset) too - added
`mFilterEnv.SetAttackShape(2.);` right alongside the Amp one. **Confirmed by the user**
("いいですね。こちらの方がいいです。") - preferred with both envelopes eased in. Both
now use exponent `2.`; if more softening is ever wanted, raise the exponent above `2.`
on either/both.

## AMP/Filter Release max raised from 4s to 8s (2026-07-26)

User: 4 seconds was too short a ceiling for Release. `FirstSynth.cpp`: both
`kParamRelease` and `kParamFilterRelease`'s `InitDouble` max changed from `4000.` to
`8000.` (ms), keeping the same `ShapePowCurve(3.)` taper and 2ms minimum - well within
`ADSREnvelope::MAX_ENV_TIME_MS` (60000ms), so no framework-level constraint hit.
Rebuilt Standalone+CLAP, relaunched. Not yet explicitly re-confirmed by ear with the
new 8s ceiling reached, but the change is mechanical/low-risk (same shape, just a
larger max) - ask if it's ever in question.

## New "Yuragi" knob added to the Master panel (2026-07-26)

User asked for a per-note randomization knob combining two things from
[[project-suikinkutsu-plugin]]'s Yuragi concept: (1) the "width, not speed" per-trigger
random pitch jitter (same idea as SuiKinKutsu's Wind Harp Pitch Yuragi on its Line 3
panel - a fresh random draw each note, not a continuous LFO), plus (2) a new twist not
in SuiKinKutsu: the *same knob* also controls the width of a per-note random L/R pan
(SuiKinKutsu's Wind Harp pans every tone fully randomly with no width control; here
it's knob-scaled, 0=always center).

**Added `kParamYuragi`** (appended last in `EParams`, per this project's "never
renumber" convention for saved-preset/param-index stability), `InitDouble` 0-100% in
`FirstSynth.cpp`, wired via `ForEachVoice` in `FirstSynth_DSP.h` (same pattern as
`kParamFilterKeyFollow`) to a new per-voice `T mYuragiRate` (0-1).

**`Voice::Trigger()`** now draws two independent values from the existing `Rand()`
(per-voice LCG, already returns `[-1, 1]` - no new RNG needed) each note-on:
- Pitch: `mYuragiPitchOffsetOctaves = Rand() * mYuragiRate * (0.6/12.)` - up to ±0.6
  semitones at 100% width (**started at ±1.2, user asked to halve it** - see below).
  Added into `osc1Freq`/`osc2Freq`'s pitch term in `ProcessSamplesAccumulating()`, so
  it stays constant for the note's whole duration (drawn once per trigger, not
  continuously re-randomized).
- Pan: `mGainL`/`mGainR` computed via the same constant-power `cos/sin(angle)`
  technique as SuiKinKutsu's Wind Harp (`angle = panNorm * π/2`, `panNorm = Rand() *
  mYuragiRate * 0.5 + 0.5`) - at width=0, `panNorm` is always exactly 0.5 (dead
  center) regardless of the random draw, since the draw itself is scaled to zero.

**Output stereo handling had to change to support this**: previously
`ProcessSamplesAccumulating` did `outputs[0][i] += ...; outputs[1][i] = outputs[0][i];`
(copy-left-to-right, correct only because every voice was hard-panned dead center -
see the code comment removed in this change for why that trick worked at all). Now
`outputs[0][i] += voiceOut * mGainL; outputs[1][i] += voiceOut * mGainR;` - genuine
independent per-voice stereo accumulation, required once voices can have different pan
positions.

**Incidental fix bundled in**: `Voice`'s `mRandSeed` used to default to `0` for every
voice (fine for the audio-rate noise oscillator, which decorrelates within a few
samples regardless, but risked identical first-note Yuragi draws across freshly-
allocated voices in a chord). Seeded it uniquely per voice in the constructor
(`(uint32_t)(uintptr_t)this`, same technique SuiKinKutsu's own Yuragi RNG uses) so the
very first chord's random draws are already decorrelated.

**UI**: `resources/web/index.html`'s MASTER panel - added the knob right after Bass
Boost with a `margin-left: 20px` gap (same gap style already used between Gain and
Glide/Bass Boost), `param-id="69"` (matches `kParamYuragi`'s position, right after the
EQ params which end at 68).

Rebuilt Standalone+CLAP (Debug) after each change, relaunched each time. **Confirmed
working by the user** ("効いています"). Pitch width was originally ±1.2 semitones
(matching Wind Harp's rescaled max) - **user asked to halve it to ±0.6**, done and
reconfirmed. Pan width has not been separately adjusted/questioned. User ended the
feature here for this session ("いったんこの機能についてはこれで") - don't
assume further Yuragi tuning is wanted without being asked.

## Dragon line-art background watermark - infrastructure added, disabled pending a real asset (2026-07-26)

User asked for a faint dragon line-art watermark behind the UI ("透かしでドラゴンの
線画"). No existing dragon asset anywhere on the machine, so drew one by hand as
`resources/web/dragon-bg.svg` (stroke-only paths, no fill - serpentine body, horns,
whiskers, fin spikes) and iterated on it by serving `resources/web/` with
`python -m http.server` and screenshotting in the browser pane (the actual app wasn't
needed for this part - CSS/SVG renders fine standalone, `knob-control` self-inits
visually even without the C++ backend).

**Theme-adaptive technique**: rather than baking a fixed stroke color into the SVG
(which would look wrong in one of the two themes), the SVG is applied via CSS
`mask-image`/`-webkit-mask-image` (`mask-mode: alpha` forced explicitly, since the
default mode for an SVG-as-mask-image isn't reliably "alpha" across browsers) on a
`position: fixed` `#dragon-watermark` div whose `background-color` is `var(--text)` -
so it automatically tints correctly in both light and dark mode with zero JS. Stacking
order needed `z-index: -1`, not `0` or `auto` - per CSS2.1's painting order, a
positioned element with `z-index: 0`/`auto` paints *above* static in-flow content in
the same stacking context; only a *negative* z-index paints behind it. Verified
correct tinting in both themes via `document.documentElement.setAttribute('data-theme',...)`
in the browser before touching the real app.

**Result: rejected by the user** ("ぜんぜんドラゴンには見えない") - my hand-drawn
path reads as an abstract squiggle, not a recognizable dragon. Per their instruction,
kept the mechanism (the CSS mask/positioning, the SVG file itself, the div) but set
`opacity: 0` on `#dragon-watermark` so nothing shows for now - they may supply a
different illustration later. **To bring it back**: drop in a new `dragon-bg.svg`
(same viewBox 0 0 900 700, stroke-only, `fill="none"`) and raise `opacity` back to
around `0.06`. Don't assume the current `dragon-bg.svg` is worth reusing/tweaking
further without being asked - the user's framing suggests a full replacement, not an
iteration on this attempt.

## Computer-keyboard note range extended, then JIS-keyboard fix (2026-07-26)

User wanted the PC-keyboard-as-MIDI-keyboard range (`resources/web/index.html`'s
`kKeyToNoteOffset`, dev-only, Standalone-only) extended past L (offset 14). Extended
following the same white/black-key physical stagger to P/;/'/[/] (offsets 15-19,
reaching G an octave+fifth above the base). User reported "：キーが反応しません" -
root cause: the map was keyed by `event.key` (the *character* the OS layout produces),
and JIS keyboards type different characters than US at the same physical positions for
punctuation. Fixed properly, not just patched: switched the whole map (letters
included) to `event.code` (physical key identity, layout-independent) -
`KeyA`/`KeyW`/.../`Semicolon`/`Quote`/`BracketLeft`/`BracketRight`. User then found even
`event.code` didn't line up with felt physical position past Semicolon, because JIS
keyboards have physically more keys in that row than US (an extra key or two) - a real
hardware layout difference no software fix can fully paper over. Per their request,
dropped `Quote`/`BracketLeft`/`BracketRight` (the ones typing '@'/'['/']' on their JIS
board) and kept just the `KeyP`/`Semicolon` extension (offsets 15-16).

## Looper: rough waveform strip under the gauge, Yuragi knob, envelope/range tweaks, and a scissors cut tool (2026-07-26, one long session)

Several separate but related additions, in the order asked:

**Amp+Filter envelope Attack: linear -> ease-in curve.** User: sound felt "硬い"
(hard/stiff). Diagnosed FirstSynth uses iPlug2's stock `ADSREnvelope<T>`
(`iPlug2\IPlug\Extras\ADSREnvelope.h`) unmodified - Attack was a straight linear ramp,
Decay/Release both an RC-style exponential (`CalcIncrFromTimeExp`). Added an opt-in
`SetAttackShape(exponent)` to that shared class (default `1.`=linear, byte-identical
to old behavior via `pow(x,1.)==x`; framework-level but FirstSynth-only in practice
since [[project-suikinkutsu-plugin]] doesn't use this class), set to `2.` (ease-in,
not the RC-charge shape Decay/Release use - RC-charge is steepest right at note-on,
which wouldn't have helped) for both `mAMPEnv` and `mFilterEnv` in
`FirstSynth_DSP.h`'s `Voice` constructor. **Confirmed by the user**, both envelopes.

**AMP/Filter Release max: 4s -> 8s.** `FirstSynth.cpp`'s `kParamRelease`/
`kParamFilterRelease` `InitDouble` max changed `4000.`->`8000.` (ms), same
`ShapePowCurve(3.)` taper, well under `ADSREnvelope::MAX_ENV_TIME_MS` (60000ms).

**New "Yuragi" knob, Master panel** (right of Bass Boost, `margin-left:20px` gap,
`param-id="69"`, appended last in `EParams` per the "never renumber" convention).
Combines two ideas from [[project-suikinkutsu-plugin]]'s Yuragi concept: (1) the
"width, not speed" per-trigger random pitch jitter (like Wind Harp's Pitch Yuragi -
fresh draw each note, not an LFO), and (2) a new twist not in SuiKinKutsu: the same
knob also scales a per-note random L/R pan width (SuiKinKutsu's Wind Harp always pans
fully random with no width control). `Voice::Trigger()` draws two independent values
from the existing per-voice `Rand()` (already `[-1,1]`, no new RNG needed): pitch
offset up to ±0.6 semitones at 100% width (**started at ±1.2, user asked to halve
it**), and pan via the same constant-power `cos/sin(angle)` technique SuiKinKutsu's
Wind Harp uses. Had to change `ProcessSamplesAccumulating()`'s output stage from
`outputs[0]+=...; outputs[1]=outputs[0];` (a "mono copy" trick that only worked
because every voice was hard-panned center) to genuine independent
`outputs[0]+=voiceOut*mGainL; outputs[1]+=voiceOut*mGainR;`. Bundled fix: seeded
`mRandSeed` per-voice (`(uint32_t)(uintptr_t)this`, same as SuiKinKutsu) instead of
defaulting to 0 for every voice, so a freshly-allocated voice pool's very first chord
doesn't draw correlated Yuragi values. **Confirmed working.**

**Filter Env Amount range: ±6 -> ±8 octaves** (`FirstSynth_DSP.h`'s
`envAmountOctaves`; was ±4 before that, in an earlier session). User: "still weak."

**Looper `Stop()` bug: recording lost if Stop pressed directly (not via the transport
cycle).** Root cause: `Stop()` transitioned straight to `kStopped` without finalizing
`mLoopLengthSamples`/`StampRecordedRange()` the way `CycleTransport()`'s own Recording
branch does - so a first-time user hitting Stop right after Record kept `mLoopLength
Samples` at its old (often 0) value, orphaning the just-recorded audio. Fixed by
mirroring `CycleTransport()`'s finalization inside `Stop()` too, gated on
`state==kRecording`.

**Rough waveform strip under the gauge** (`resources/web/index.html`'s
`#looperGaugeWaveform` canvas, `LooperEffect::GetWaveformPeaks()`/`kWaveformBuckets`
=300). Buckets span the *full* `mMaxSamples` (60s) range, same denominator
`GetBarFraction()` uses, so bucket i lines up with the same x-fraction as the gauge -
buckets past the recorded range stay 0/blank. Pushed (`kMsgTagLooperWaveform`) on
every transport/stop/clear press, on cut-apply, and every OnIdle tick while actively
Recording. User asked for it to use much more vertical space (there's ~350px of empty
page below the gauge) - height 28px -> 240px, alpha 0.5 -> 0.75.

**Bug chain while building the above - three real, separate root causes, not one:**
1. *Waveform frozen/stale during active Recording*: `GetWaveformPeaks()` bounded
   against `mLoopLengthSamples` (stale, from the *previous* take) instead of
   `mWritePos` (the currently-growing take's real extent) while Recording - same
   split `GetBarFraction()` already handles correctly. Fixed by mirroring that split.
2. *Waveform update falling further behind the gauge the longer a take ran*
   ("だんだんずれていく" - user's first "still misaligned" report after fix #1):
   `GetWaveformPeaks()` used to rescan the *entire* 0..validLen range from scratch on
   every call - fine once, too expensive to also call every OnIdle tick during
   Recording (added for fix #1), since the scan cost grows with elapsed recording
   time. Rewrote it to be incremental: `mWaveformScanPos`/`mWaveformCache` remember
   how far it's already scanned, so each call only processes the *new* samples since
   last time (roughly constant work per tick, not growing). `ResetWaveformScan()`
   added and called wherever the buffer's content/length changes discontinuously - a
   new recording starting (`CycleTransport()`'s kEmpty case), `Clear()`/`ResetLoop()`,
   and after a cut applies (from the *main* thread in `FirstSynth.cpp`, since
   `ApplyPendingCut()` runs on the audio thread and can't safely touch the cache -
   `GetWaveformPeaks()` itself is main-thread-only by contract, unlike the atomic
   pending-cut fields it shares the class with).
3. **The actual root cause of the persistent ~25% gap** (user, after testing fix #2
   and insisting it was "not a timing issue, the ratio is fundamentally wrong" - and
   they were right): a **base64 length bug**, unrelated to anything above. The
   WebView message handler decoded `n = Math.floor(dataSize / 4)` to determine how
   many float buckets were in the payload - but `dataSize` here turned out to report
   the **base64-*encoded* string's length**, not the original raw byte count
   (1200 bytes -> a 1600-character base64 string; 1600/4=400 instead of the true
   300). This silently squeezed the entire waveform display to 300/400 = 75% of its
   correct width, *consistently*, regardless of how long the recording was - which is
   exactly why the ratio looked "fixed" across differently-sized tests rather than
   growing/shrinking with elapsed time. Found by adding a temporary on-screen debug
   readout (`bar=/wave=/n=/lastNZ=`) and asking the user to report the raw numbers -
   `n=400` immediately gave it away (`400 = 300 * 4/3`, the exact base64 expansion
   ratio). **Fix**: derive the bucket count from `bytes.length` (the *decoded* byte
   string, from `window.atob(data)`) instead of the `dataSize` parameter - confirmed
   by the user afterward (`n=300`, `bar`/`wave` matching). Debug readout fully
   removed afterward. **Worth remembering for any future variable-length binary
   payload from C++ to this WebView** (including in [[project-suikinkutsu-plugin]] if
   it ever sends one) - the fixed-size messages elsewhere in this codebase
   (`kMsgTagLooperProgress`, `kMsgTagMeterLevel`, etc.) never hit this because they
   only *gate* on `dataSize >= N` rather than using it to size a dynamic array, so
   this bug had never surfaced before.

**New scissors cut tool** (`.looper-cut-tools`, two buttons right of the Looper
knob-row, hand-drawn scissors+chevron SVG icons). Select left or right tool, then
click/touch the gauge or waveform to queue a cut: left deletes everything *before*
the touched position, right deletes everything *after* it. Never applied instantly -
queued (`LooperEffect::QueueCut()`/`mPendingCut`) and only actually applied
(`ApplyPendingCut()`) at the next loop-wrap, detected in `Process()`'s
kPlaying/kOverdubbing case (`wrapped` flag on the existing `while` loops there) - so
it can never introduce a mid-loop discontinuity, matching the user's spec ("次に人が
冒頭に戻る瞬間にカットされたゲージが表示され"). Left-cut shifts the retained tail
down to start at buffer index 0 (mWriteTime shifted alongside, *not* re-stamped via
`StampRecordedRange()` - a cut isn't a new recording, already-decaying Feedback
content shouldn't suddenly read as fresh); right-cut just shortens
`mLoopLengthSamples` in place. While pending, the region that will be removed blinks
red (`#looperCutPending`, spans from the gauge track's top to the waveform's bottom,
CSS `@keyframes cut-pending-blink`) via a new `kMsgTagLooperCutPending` push. Stop/
Clear both cancel a pending cut (nothing to apply it at once stopped/wiped) and push
the "no pending cut" state so the blink clears immediately rather than lingering.
New JS<->C++ messages: `kMsgTagLooperCut` (UI->C++, 1 byte side + 4-byte float
position) and `kMsgTagLooperCutPending` (C++->UI, 1 byte kind + 4-byte float
position) - both packed/unpacked via `DataView`, same convention as the existing
`kMsgTagLooperProgress`/`kMsgTagMeterLevel` messages. **Confirmed working by the user**
after the waveform-alignment bugs above were separately resolved (the cut feature's
own logic was correct throughout - the mismatch users kept reporting was in the
*display*, not in what actually got cut).

## Cut-tool follow-up polish (2026-07-26, same session)

Several small requests after the cut tool above was confirmed working:

- **Scissors icons enlarged**: SVG 26x20 -> 34x26, `.looper-cut-btn` 44x34px ->
  54x42px.
- **New Cancel button**, left of the two scissors, same size (⊗ icon in a circle).
  Cancels a pending cut before it lands - `LooperEffect::CancelPendingCut()`
  (public, just clears `mPendingCut`) + new `kMsgTagLooperCancelCut` (UI->C++, no
  payload) which calls it and pushes `kMsgTagLooperCutPending`(none) right after, same
  as Stop/Clear already did. Only enabled (`cutPendingActive`, tracked in
  `UpdateCutPendingOverlay()`) while a cut is actually pending/blinking - grayed out
  (`.looper-cut-btn:disabled`, `opacity:0.35`) the rest of the time so it doesn't look
  like a live no-op button.
- **Bug: a right-cut's blink region extended all the way to the gauge's 100% width**,
  including the unrecorded remainder past the actual recorded content - should only
  span up to the recorded end, matching what the gauge itself lights up. Fixed by
  tracking `currentBarFraction` (set from `UpdateLooperGauge()`, same value already
  driving the gauge fill's own width) and using `currentBarFraction - frac` instead of
  `1 - frac` for the right-cut's blink width in `UpdateCutPendingOverlay()`.
- **Waveform wasn't updating live during Overdubbing.** Root cause was structural,
  not a repeat of the earlier base64 bug: `GetWaveformPeaks()`'s incremental scan
  (added for the Recording-state fix) only ever looks at *new* indices past
  `mWaveformScanPos` - correct for Recording (which only ever writes forward once)
  but wrong for Overdubbing, which continuously *rewrites already-scanned* indices as
  the playhead cycles through the established loop (`Process()`'s kOverdubbing case).
  Fixed by forcing a full-but-bounded rescan (`mWaveformScanPos=0` + clear the cache)
  on every `GetWaveformPeaks()` call while `state==kOverdubbing` - safe to do every
  tick unlike the *original* always-full-rescan bug, because `mLoopLengthSamples` is
  fixed once Overdubbing starts, so this doesn't grow more expensive with elapsed
  time the way the Recording case did. `FirstSynth.cpp`'s OnIdle waveform-push
  condition extended from `state==kRecording` to `kRecording || kOverdubbing`.
  **Confirmed by the user** ("出来てます。バッチリです。").
- **New hover guide line** (`#looperCutHoverLine`, thin 2px accent-colored line):
  while a cut tool is selected, follows the mouse across the gauge/waveform
  (`OnGaugeAreaHover()`/`OnGaugeAreaLeave()`, same 0..1 fraction math as
  `OnGaugeAreaClick()` itself, just drawn instead of sent) so the exact position a
  click would land on is visible before committing. Hidden on mouse-leave or when the
  tool gets deselected mid-hover (`UpdateCutToolButtons()` calls `OnGaugeAreaLeave()`
  when `selectedCutTool` becomes null).
- **Verified (not changed)**: user asked that neither scissors button be selected by
  default on startup. Already true by construction - `selectedCutTool` initializes to
  `null` at script-parse time and neither button's HTML has the `active` class baked
  in - confirmed directly via a static-preview JS check
  (`selectedCutTool===null`, both buttons' `className` plain `"looper-cut-btn"`,
  `cutCancelBtn.disabled===true`) rather than just asserting it from reading the code.
  No code change was needed; recorded here per the user's request.

## Milestone: this session's build tagged v1.00 (2026-07-26)

User asked to mark the current build as v1.00. `config.h`'s `PLUG_VERSION_STR`
("1.0.0") and `PLUG_VERSION_HEX` (`0x00010000`) were already set to this - no code
change needed. Recording what "v1.00" actually covers, everything from this one long
session (in order): Amp+Filter envelope Attack linear->ease-in curve; AMP/Filter
Release max 4s->8s; new Yuragi knob (per-note random pitch+pan); Filter Env Amount
±6->±8 octaves; PC-keyboard note range extended (then partly reverted for the user's
JIS keyboard) and switched to layout-independent `event.code`; a real bug fix in
`LooperEffect::Stop()` (recording lost if Stop was pressed directly instead of
cycling through Play first); preset dialog remembering its last-used folder (shared
framework fix, ported from [[project-suikinkutsu-plugin]]); a dragon watermark
attempt (rejected by the user, mechanism kept but disabled - see its own entry); and
the Looper page's waveform-under-gauge display plus the full two-scissors cut tool,
including the extended debugging chain that found and fixed a real base64-length bug
in the WebView messaging layer (see "Looper: rough waveform strip..." entries above
for full detail on all of this). If picked up again later, this is the natural point
to diff against for "what changed since v1.00."

## Version label added to the GUI header (2026-07-26)

User asked to be able to check the version from the GUI (an "About" area or similar).
Added `#versionLabel` right after the `<h1>1st synth</h1>` title in
`resources/web/index.html` (small, muted text, e.g. "v1.0.0"). Rather than
hardcoding the string in the WebView (which could drift from `config.h` over time),
`FirstSynth.cpp`'s `OnWebContentLoaded()` pushes the real compiled-in value via
`EvaluateJavaScript("SetVersionLabel('" PLUG_VERSION_STR "')")` - `config.h`'s
`PLUG_VERSION_STR`/`PLUG_VERSION_HEX` stay the single source of truth for both the
plugin's actual metadata and this display. Not gated behind `#ifdef APP_API` (unlike
the keyboard-input push right next to it) - shows when hosted as CLAP too, not just
Standalone. Rebuilt both targets, **confirmed by the user**.

**Session paused here** ("少し休みますので") - this is a good resume point; read from
the top of this file's most recent entries (this v1.00 milestone section onward) if
picked back up, same caution as always about not assuming active development
continues automatically.

## Resumed 2026-07-27 - multi-thread confusion, then todo.txt cross-check

This session's conversation thread had fallen behind the real project state (this
same file already showed work through 2026-07-26's v1.00 tag that this thread's own
history had no record of) - the user confirmed this was because work had been
spread across multiple parallel chat threads and they'd lost track themselves too.
Resolved by treating this file (not conversation memory) as the single source of
truth going forward, matching the project's own established convention.

Cross-checked `private\todo.txt` against this file's actual recent entries and found
two items likely already resolved but not yet crossed off there: **ADSR Release max**
(done - 4s->8s, see the 2026-07-26 entry above) and **PC-keyboard range extension**
(done as far as reasonably possible - extended to P/Semicolon, but Quote/[/] had to
be dropped because the user's JIS keyboard physically doesn't align with US layout
past that point, a real hardware constraint not a bug). One item flagged as
*uncertain*, not assumed resolved: "AとDのカーブ、ちょっと極端すぎる？" - the Attack
ease-in curve change (2026-07-26, fixed a *different* complaint, "硬い"/stiff) may or
may not be the same underlying issue as "極端" (too extreme, likely about
`ShapePowCurve(3.)`'s knob-sensitivity feel) - Decay's curve was never touched at
all. Didn't mark this done without asking. User was shown this breakdown and said
the todo list is fine as-is for now, no further action taken on it this session.

## 5-band EQ defaults adopted from the user's tuned state (2026-07-27)

User: "5band eqの状態を少し修正したので、こちらをデフォルトで採用してください" - had
been tweaking the EQ knobs (presumably over the intervening sessions/days, given the
auto-persist-across-launches feature keeps whatever was last set) and wanted the
*current* tuned curve promoted from "just whatever's currently saved" to the actual
compiled-in default - directly relevant to the standing instruction that this EQ
defines 1st Synth's sound identity (see the "Important, ongoing instruction" note
near the top of this file / the project's memory file).

Read the live values directly from `C:\Users\a_wak\AppData\Local\FirstSynth\
autosave.state` (indices 56-68, matching `kParamEQLowFreq`..`kParamEQHighGain`) rather
than asking the user to read out each knob - decoded as 70 little-endian doubles
(70 params total now, confirms the Yuragi knob added 2026-07-26 brought the count up
from 69). Updated `FirstSynth.cpp`'s `InitFrequency`/`InitDouble` default arguments
for all 13 EQ params to match (rounded to whole Hz for frequencies, 2 decimals for
gain/Q - imperceptible precision loss, matches this file's usual convention for
these param types):

```
Low Shelf:  125 Hz,  +3.12 dB
Band 2:     317 Hz,  +1.92 dB,  Q 0.78
Band 3:    1000 Hz,  -3.36 dB,  Q 1.02
Band 4:    1726 Hz,  +5.04 dB,  Q 0.62
High Shelf: 4865 Hz, +3.60 dB
```

(previously: all gains 0dB/flat, frequencies spread evenly at 100/300/1000/3000/8000,
Q 0.7 on every peaking band - a neutral starting point, not the user's actual sound).

This only changes what a **brand-new** instance (no `autosave.state` yet - a fresh
install, a different machine, or the file being deleted) starts with, and what
double-clicking an EQ knob resets it to (`knob-control.js`'s
`default-value`/`realToNormalized`) - it does **not** retroactively touch the
already-running instance's live values or the existing `autosave.state` file, both of
which already held these exact values already (that's where they were read from).
Rebuilt both Standalone/CLAP and relaunched Standalone. Not yet explicitly
re-confirmed by the user as of this write-up, though the values are provably
identical to what they'd already tuned by ear and asked to be promoted.

## Bass Boost v2: real low-shelf + saturation on the boost, not just a lowpass-and-add trick (2026-07-27)

User asked for a stronger-sounding Bass Boost (frequency staying fixed at 150Hz),
mentioning secondhand/unsourced recollection that some vintage synths' fuller low end
came from an always-on bass boost circuit - specifically wondered whether the Juno-106
had one. **Didn't know of a specifically-named "bass boost circuit" in the Juno-106
and said so directly** rather than inventing plausible-sounding but unverified circuit
detail - the Juno-106's own signal path as best known (DCO+sub-osc -> HPF -> VCF
(80017A chip) -> VCA -> chorus) doesn't obviously include one. Offered three
generic directions instead (wider gain range on the existing trick; a real low-shelf
filter; add saturation to the boosted low band) - **user chose to combine the shelf
and saturation options.**

Old implementation (`BassBoostEffect`, see the struck-through description this
replaced): a one-pole lowpass copy of the signal added back on top, capped at roughly
+6dB at DC - simple but weak, and not a precisely-defined shelf.

**New implementation**, `FirstSynth_Effects.h`:
- **Real RBJ low-shelf filter** - reused `BiquadFilter<T>`/`CalcLowShelfCoeffs<T>`
  (already built for the 5-band EQ's own Low Shelf band) rather than duplicating shelf
  math a second time. This required **moving both** from their original location
  (just above `ParametricEQEffect`, later in the file) to **above** `BassBoostEffect`
  near the top of the file, since `BassBoostEffect` now needs them and C++ template
  two-phase lookup requires a non-ADL-findable dependent function name to be visible
  at the point the calling template is *defined*, not just where it's instantiated -
  pure reordering, no logic changes to either moved piece.
- **Gain range 0-15dB** (`gainDb = amount * 15.`) at Amount 0-100% - matches the
  5-band EQ's own gain range for consistency, a real ~2.5x stronger ceiling than the
  old trick's ~6dB.
- **Saturation confined to just the shelf's own boost, not the whole signal**: after
  running the shelf (`shelfL = mFilterL.Process(l)`), `boostL = shelfL - l` isolates
  exactly what the shelf *added* (concentrated at low frequencies, since a shelf
  leaves highs essentially untouched) - only *that* difference gets pushed through
  `tanh(boostL * driven) / driven` (driven = `1 + amount*3`, so higher Amount both
  boosts more *and* saturates harder) before being added back:
  `l += saturatedBoost`. This means the dry/original signal is never itself
  saturated, only the enhancement is - matches the "circuit driving into mild
  saturation adds low-end thickness" idea discussed, without coloring the whole mix.
  At Amount=0 the shelf's own gain is 0dB, so `boostL≈0` and `tanh(0)/1=0` - exact
  bypass, same as before.
- Frequency intentionally left fixed at 150Hz (not exposed as a param) per the user's
  own confirmation that it didn't need to be adjustable.
- Rebuilt both Standalone/CLAP and relaunched Standalone. Not yet confirmed live by
  the user as of this write-up - ask them to compare against how it sounded before
  (louder/thicker low end, and whether the added saturation is tasteful or too much
  at higher Amount settings - `driven`'s `*3` multiplier and the 15dB ceiling are
  both easy to retune if either extreme needs softening). **Confirmed by the user**
  ("いいんじゃないでしょうか").

## WASAPI occasional clicks/pops - root-caused to a sample-rate mismatch (2026-07-27)

User: "やはりときどき音がプツっと言う瞬間があり、それが気になります" - this turned out
to be the same WASAPI concern flagged (but not investigated) in an earlier session's
memory note ("slightly more dropout-prone than DirectSound"). Narrowed down via two
quick questions before investigating: happens **randomly** (not tied to knob moves or
note-on/off) and **WASAPI-specific**. Classic buffer-underrun symptom, so first
suggested the standard fix - raise the Buffer Size in Audio Settings. User tried 1024:
"だいぶ減りますが、それでもときどき出ます" (much better, but still occasional) -
confirmed buffers were part of it but not the whole story, worth digging further
rather than declaring it fixed.

**Root cause found by reading `RtAudio.cpp`'s WASAPI backend directly**: `RtApiWasapi`
constructs its own internal `WasapiResampler` whenever the stream's requested sample
rate doesn't match the target device's native WASAPI mix format - an extra
resampling stage runs on *every* audio callback in that case, which is exactly the
kind of added per-callback CPU cost that could occasionally blow a deadline even with
a generous 1024-sample buffer. Confirmed a real mismatch existed by querying live
WASAPI device formats directly via COM (`IMMDeviceEnumerator`/`IAudioClient::
GetMixFormat`, a small ad-hoc PowerShell+C# snippet, not a permanent tool) rather than
guessing:

```
FirstSynth's settings.ini:              sr=48000
Actual output device's native format:
  CABLE Input (VB-Audio Virtual Cable)  96000 Hz   <- what FirstSynth was set to output to
  (for reference, other devices on this same machine:)
  CABLE In 16ch (VB-Audio Virtual Cable) 48000 Hz
  スピーカー (Audient iD4)                48000 Hz
  スピーカー (Realtek(R) Audio)           48000 Hz
  ヘッドホン (iLoud Micro-Monitor)        44100 Hz
```

A genuine 48000 vs 96000 (exactly 2x) mismatch on the specific device FirstSynth's
`settings.ini` had configured as `outdev`. Confirmed the Standalone app's Preferences
dialog already exposes a Sample Rate dropdown (`IDC_COMBO_AUDIO_SR`, populated by
`IPlugAPPHost::PopulateSampleRateList` from the device's own supported-rate list) -
**no code change needed**, purely a settings fix. Two equally valid directions
offered: change FirstSynth's own SR to 96000 to match the device, or change the
device's own default format to 48000 via Windows' Sound Control Panel. **User chose
the former** - `settings.ini` now shows `sr=96000`. **Confirmed by the user**
("だいぶ減りました" - a big reduction) - not explicitly declared 100% eliminated, so
if any residual clicking is still reported, don't assume this alone was the complete
picture; there could be a second, smaller contributing factor still unaccounted for.

**Session paused here** ("とりあえずここまで記録してください") - resume point.
Everything above is confirmed by the user except the WASAPI click investigation's
last mile (reduced a lot, not explicitly all-the-way-gone - see its own entry for
what to check first if it comes up again: whether it's actually fully resolved now,
and if not, whether a *second* contributing factor exists beyond the sample-rate
mismatch already fixed). No other open threads from this stretch of the session -
the Bass Boost v2 rework and the tuned-EQ-defaults promotion were both explicitly
confirmed working.

## Looper: smarter cut timing, Cancel->Undo, transport order (2026-07-27)

User asked for three separate Looper behavior changes in one request. All three
touch `FirstSynth_Looper.h`, `FirstSynth.h`'s `EMsgTags`, `FirstSynth.cpp`'s
`OnIdle()`/`OnMessage()`, and `resources/web/index.html`.

**1. Cuts now apply the instant they're *safe*, not always at the next loop-wrap.**
User's exact spec: left-cut - if the playhead is already right of (past) the cut
point, cut immediately; if left of it, blink until the playhead passes through,
then cut. Right-cut - if the playhead is left of (before) the cut point, cut
immediately; if right of it, blink until the playhead returns to the start (wraps),
then cut. Worked out why this is musically correct: a left-cut shifts the retained
*tail* down to start at index 0 - safe the instant the playhead is already inside
that tail (`mPlayPos >= cutPos`), since the underlying sample it's on just gets a
new (shifted) index, no jump. A right-cut keeps the head in place and just shortens
the loop - safe the instant the playhead hasn't reached the doomed tail yet
(`mPlayPos < cutPos`), since an unaffected playhead needs no change at all. Neither
condition needs `wrapped` explicitly checked as a separate event - checking the
plain condition every sample already captures both "already safe" (fires
next sample, reads as instant) and "wait for it" (for right-cut, `mPlayPos < cutPos`
naturally can't go true again mid-cycle once violated - forward playback only
increases until it wraps - so it degrades to "wait for wrap" automatically, without
a separate special case).
- `Process()`'s kPlaying/kOverdubbing case: replaced `if (wrapped && mPendingCut...)`
  with a per-sample check of the condition above (removed the now-unused `wrapped`
  local entirely - the two `mPlayPos` wrap-around `while` loops stay, just don't
  track it as a flag anymore).
- `ApplyPendingCut()`: previously always did `mPlayPos = 0.` unconditionally after
  either cut type, which only ever sounded seamless because it used to only ever
  run exactly at a real wrap (where a jump-to-0 was already happening anyway). Now
  that it can run mid-loop too, replaced with type-specific remapping: left-cut
  computes `mPlayPos = oldPlayPos - cutPos` (same underlying sample, shifted index -
  genuinely continuous, not just "coincidentally timed to hide a jump"); right-cut
  leaves `mPlayPos` completely unchanged (the retained head never moved, so the
  old position is still valid). A defensive clamp guards against any stray
  out-of-range value reaching `Process()`'s indexing, though the "safe"
  precondition should make it unreachable in practice.
- Verified the exact continuity math by hand-tracing both cases against the
  "safe" precondition (left: `oldPlayPos - cutPos >= 0` always holds since safe
  requires `oldPlayPos >= cutPos`; right: `oldPlayPos < cutPos` = new
  `mLoopLengthSamples`, so it's already in-range) - not yet confirmed by ear that
  this reads as glitch-free in the real app, only reasoned through on paper.

**2. Cancel button repurposed into Undo** (round counter-clockwise-arrow icon,
Feather Icons' "rotate-ccw" shape, replacing the X-in-a-circle). User's reasoning:
with change 1 above, cuts now land almost immediately in the common case, so
"cancel before it lands" rarely has a meaningful window anymore - "undo what just
happened" is more useful. One-level undo, not a full history stack:
- New `LooperEffect` members: `mUndoBufferL/R`, `mUndoWriteTime`,
  `mUndoLoopLengthSamples`, `mUndoPlayPos` (the actual snapshot) and
  `mUndoRequested`/`mHasUndo`/`mUndoJustApplied` (atomics, same
  message-thread-sets/audio-thread-consumes/OnIdle-drains pattern already used for
  `mPendingCut`/`mCutJustApplied`).
- `SaveUndoSnapshot()`: called from the very top of `ApplyPendingCut()`, before any
  mutation - copies just the currently-valid `mLoopLengthSamples` range (not the
  full 60s-cap `mMaxSamples` buffer), a bounded and infrequent (one per user cut)
  cost, safe on the audio thread same as the shift/truncate that follows it.
- `PerformUndo()`: restores the snapshot, resets `mPlayPos` to 0 (deliberately not
  attempting continuous remapping the way `ApplyPendingCut()` now does - the
  pre-cut buffer shape doesn't correspond to any single coherent post-cut position
  in general, so this is a plain, predictable restart instead), and also cancels
  any still-queued pending cut (so it can't immediately re-cut the just-restored
  audio).
- `RequestUndo()`/`Process()`'s new top-of-function check mirror
  `QueueCut()`/the cut-apply check's message-thread/audio-thread split exactly.
- `Clear()` now also resets `mHasUndo` (nothing left to undo once the whole loop's
  wiped); `Stop()` deliberately does *not* (undoing should survive a Stop/Play
  toggle, unlike a still-pending cut, which genuinely can't ever land while Stopped).
- `EMsgTags`: `kMsgTagLooperCancelCut` renamed to `kMsgTagLooperUndoCut` (same tag
  number 10, repurposed meaning - msgTags aren't persisted anywhere, unlike
  `EParams`, so renumbering/renaming is safe within one synchronized rebuild) and a
  new `kMsgTagLooperUndoAvailable` (11, C++->UI) added for the button's
  enabled/disabled state, which is now driven independently of the pending-cut
  blink overlay (`cutPendingActive`, the old JS variable driving this, was removed
  entirely - genuinely dead code once the button stopped caring about pending-cut
  state).
- `FirstSynth.cpp`: `OnIdle()`'s existing `ConsumeCutJustApplied()` block now also
  pushes `kMsgTagLooperUndoAvailable`(1); a new `ConsumeUndoJustApplied()` block
  pushes `kMsgTagLooperUndoAvailable`(0) + clears the pending-cut overlay + forces
  a waveform rescan/push (mirrors the cut-applied block's own reasoning almost
  exactly). `OnMessage()`'s Clear branch also pushes `kMsgTagLooperUndoAvailable`(0).
- `index.html`: button id `cutCancelBtn`->`looperUndoBtn`, `CancelPendingCut()`->
  `UndoLastCut()`, new `UpdateUndoAvailable()`/`hasUndoAvailable`/
  `UpdateUndoButton()` replacing the old `UpdateCutCancelButton()`/
  `cutPendingActive`-driven logic. Verified via the browser-harness technique:
  button starts disabled, `UpdateUndoAvailable(1)`/`(0)` correctly
  enables/disables it, `UndoLastCut()` doesn't throw when disabled (no-op, matches
  the old Cancel button's same defensive convention), and
  `UpdateCutPendingOverlay()` still runs cleanly with the undo-button coupling
  removed.

**3. Transport cycle order: Record->Play->Overdub->Play->... changed to
Record->Overdub->Play->Overdub->Play->...** User: layering immediately after the
first take is the common case, so landing in Overdub (not Play) right after
Recording finishes saves an extra press. Minimal change - `CycleTransport()`'s
`kRecording` case now transitions to `kOverdubbing` instead of `kPlaying` when a
take has content; `kPlaying`/`kOverdubbing` already toggled each other correctly on
every subsequent press, so nothing else needed to change for the "then alternate
Overdub/Play" part of the request. Updated the JS label-order comment
(`kLooperStateLabels`) to match; `Stop()`'s own "resume into Play" behavior was
deliberately left alone (not requested, and arguably safer as a default resume
point than Overdub, which would silently start overwriting content again).

Rebuilt both Standalone/CLAP and relaunched Standalone. **Confirmed working by the
user** ("上手くいっています") for all three changes.

## Looper: cut tool disabled past the recorded region (2026-07-27, same session)

Follow-up request right after the above was confirmed: "ハサミスイッチを押しても
録音されてないゲージの場所では線を出さない（カットすることもできない）仕様にして
ください" - the gauge is always drawn at the full 60s-cap width (see
`GetBarFraction()`'s own comment), but only `currentBarFraction` of it is actually
recorded/lit; past that point was still clickable with the cut tools, which doesn't
mean anything (there's no audio there to cut).

`index.html` only - no C++ change needed, this is purely about what the WebView
lets the user attempt:
- `OnGaugeAreaClick()`: added an early return when `frac > currentBarFraction`
  (the same module-level variable `UpdateLooperGauge()` already tracks and
  `UpdateCutPendingOverlay()` already uses to cap the right-cut blink region - see
  that function's own 2026-07-26 fix). Deliberately rejected client-side rather
  than relying on C++'s `QueueCut()`, which would otherwise silently clamp the
  position to the last recorded sample instead of actually refusing - that would
  have landed a cut the user didn't ask for at a position they didn't click.
- `OnGaugeAreaHover()`: same cutoff, hides the guide line entirely past the
  recorded region instead of drawing it somewhere a click wouldn't do anything.
- Verified via the browser-harness technique: simulated `UpdateLooperGauge(0.3,
  0.1)` (30% recorded) then dispatched synthetic hover/click events at fractions
  both inside (0.2) and outside (0.6) that boundary - inside showed the guide line
  and sent exactly one `SAMFUI` call on click; outside hid the guide line and sent
  zero additional calls.

Rebuilt both Standalone/CLAP and relaunched Standalone. **Confirmed by the user**
("うまくいっています") live in the native app.

## Cut tool auto-deselects after 30s idle (2026-07-27, same session)

Follow-up: "ハサミスイッチを点灯したあと、カットが行われないまま30秒がたったら消える
（また押すまでは無効になる）ようにしてください" - a selected scissors tool now
auto-deselects (same visual/enabled-state change as clicking it again manually) if
30 seconds pass with no cut actually landing.

`index.html` only, pure `setTimeout`/`clearTimeout`, no C++ involved:
- New `RestartCutToolTimeout()`: unconditionally clears any existing timer first,
  then starts a fresh 30000ms one *only if* a tool is currently selected (so
  deselecting - selectedCutTool already null by the time this runs - just clears
  without scheduling a new one). The timeout callback deselects
  (`selectedCutTool = null`) and updates the buttons, identical to what
  `SelectCutTool()` already does for a manual toggle-off.
- Called from **two** places, both meaning "the 30s window should restart, not
  just start once": `SelectCutTool()` (a tool was freshly selected or switched -
  the obvious case), and `OnGaugeAreaClick()` right after a cut actually sends
  (**not** the no-op "clicked past the recorded region" early-return, or clicking
  with no tool selected at all) - a real cut counts as activity, so it extends the
  window rather than leaving a stale 30s-from-selection deadline that could expire
  moments after a still-useful cut just landed.
- Verified via the browser-harness technique (temporarily stubbing `setTimeout`/
  `clearTimeout` to count calls and capture the callback, since the whole point is
  a 30-second real-world delay this doesn't wait out): confirmed selecting sets
  exactly one timer (30000ms delay), manually invoking the captured callback
  correctly deselects the tool (`selectedCutTool` -> `null`, button loses
  `.active`), a real cut clears-then-resets the timer (restart, not just extend),
  and a manual deselect clears without scheduling a replacement.

Rebuilt both Standalone/CLAP and relaunched Standalone. **Confirmed live by the
user** ("いいですね") after actually waiting out the real 30s in the native app -
both the scheduling logic and the felt duration are good as-is.

## Session paused here (2026-07-27, continued)

All Looper changes from this stretch are confirmed: the smart immediate/deferred
cut timing, the Undo button (repurposed from Cancel), the Record->Overdub->Play
transport order, the recorded-region click/hover restriction, and the 30s cut-tool
idle timeout. No open threads from this part of the session. Resume by reading from
the top of this file's most recent dated entries if picked back up later - same
caution as always about not assuming active development continues automatically,
and about checking this file's true tail over any single conversation thread's own
memory (see the "multi-thread confusion" note earlier in this file for why that
matters specifically on this project).

## Looper max length: 60s -> 30s (2026-07-27, continued)

User, after actually using the looper for a while: "60秒は必要ないような気もして
きました。短くすると何か処理上で軽くなるとかメリットはありますか" (60s feels like
more than needed - is there a processing benefit to shortening it?). Answered with
the real trade-off before changing anything: **memory, not CPU**. `LooperEffect`'s
`mBufferL`/`mBufferR`/`mWriteTime` are all sized off `sampleRate * kMaxLoopSeconds`
and allocated unconditionally in `SetSampleRate()` - regardless of whether the
looper is ever touched, and per plugin instance (so N tracks in a DAW = N copies).
At this project's `sample` type (confirmed `double`, 8 bytes - `FirstSynth-win.props`
never overrides iPlug2's `SAMPLE_TYPE_DOUBLE` default) and the user's own current
96kHz setting, 60s was **~132MB per instance** (3 buffers x 8 bytes x 96000Hz x 60s);
halving to 30s roughly halves that. Explicitly **not** a realtime-processing win:
`Process()` (the audio-thread hot path) is O(1) per sample regardless of this
constant - only allocation size scales with it, not per-sample work - and the
length-scaling helpers (`StampRecordedRange`/`GetWaveformPeaks`/cut+undo snapshots)
already scale with *actual* recorded length, not the cap, so they're no cheaper at
a lower cap unless a recording would otherwise have run longer than the new limit.
User: "30秒でしばらく使ってみようと思います" (I'll try 30s for a while).

- `FirstSynth_Looper.h`: `kMaxLoopSeconds` `60.` -> `30.`, with a comment
  explaining the memory-vs-CPU trade-off above (marked easy to retune again).
- Bulk-updated every other "60s" mention across `FirstSynth_Looper.h`,
  `FirstSynth.cpp`, and `index.html` (all just explanatory comments describing "the
  cap" conceptually, e.g. "gauge fractions are relative to the full Ns cap, not the
  actual recorded length" - the concept itself needed no logic changes, just the
  number in the prose) to `30s` via `replace_all`, then had to fix one collateral
  edit: `replace_all` also mangled the *brand new* comment on `kMaxLoopSeconds`
  itself, which was deliberately describing the *historical* 60s value (a "was 60s
  originally" note) - that comment briefly read "was 30s originally... 30s was
  ~132MB" (nonsensical, since 30s obviously isn't ~132MB) until manually corrected
  back to accurately describe 60s as the *old* value and 30s as the *new* one.
  Worth remembering: a literal-string `replace_all` doesn't distinguish "describes
  the new value" from "describes old history" prose - always re-check freshly
  written comments specifically after a broad find-and-replace touches the same file.
- No other functional code depends on the literal `60` anywhere - `mMaxSamples` is
  always computed from the constant, never hardcoded separately.

Rebuilt both Standalone/CLAP and relaunched Standalone. Not confirmed as final -
see the very next entry, changed again minutes later.

## Looper max length: 30s -> 40s (2026-07-27, continued)

Immediate follow-up to the 60s->30s change above. User: "実際に使うのは15秒くらい
がほとんどなんですが、心理的に時間のリミットがせまっていると思うとプレッシャーに
なりますね。40秒にしてもらえますか" (actual takes are mostly ~15s, but *knowing*
the limit was approaching created psychological pressure regardless of not really
needing the room - please make it 40s instead). Notably a **different** reason than
the original 60->30 change (that one was about the user's own assessment that the
cap itself was more memory than needed) - this one is about the felt experience of
the cap being tight, independent of actual usage. Both are legitimate, separate
motivations worth keeping distinct in mind if this number gets revisited again.

- `FirstSynth_Looper.h`'s `kMaxLoopSeconds`: `30.` -> `40.`. Rewrote the constant's
  comment to track the *full* history (60 -> 30 -> 40) with both distinct reasons,
  rather than just overwriting the previous entry - useful context if asked "why
  is this 40 and not round-tripped back to 60" later.
- **Real mistake caught and fixed before it shipped**: initially planned to repeat
  the same `replace_all` "30s"->"40s" bulk approach used for the previous change,
  but `index.html` turned out to have a **second, unrelated** "30 seconds" concept
  in it - the cut-tool idle-timeout added earlier this session (see "Cut tool
  auto-deselects after 30s idle"). A blind file-wide `replace_all` would have
  incorrectly rewritten those comments (and only those - the actual
  `setTimeout(..., 30000)` call itself was never at risk, since "30000" doesn't
  contain the substring "30s") to claim a 40-second idle timeout that doesn't
  actually exist. Caught by grepping every remaining "30s" occurrence and reading
  each one *before* editing, rather than assuming the pattern was unambiguous
  within the file just because it had been in the previous file (`FirstSynth_Looper.h`/
  `FirstSynth.cpp` had no such collision - only `index.html` did, since that's
  the only file with two independently-introduced "N seconds" concepts). Updated
  the 8 genuinely loop-cap-related comments individually instead of via
  `replace_all`, leaving the 3 cut-tool-timeout ones (lines ~1297/1303/1375)
  untouched. **General lesson for this file going forward**: `index.html` now has
  two different "30-second-ish" timing concepts that can drift independently
  (the loop cap, currently 40s, and the cut-tool idle timeout, currently 30s) -
  don't assume a bare "30s"/"Ns" grep hit is about one or the other without
  reading it first.
- `FirstSynth_Looper.h` and `FirstSynth.cpp` had no such collision (their "30s"
  mentions were unambiguously all about the loop cap) - `FirstSynth.cpp` was safe
  to bulk `replace_all`; `FirstSynth_Looper.h`'s generic comments were also
  individually edited (matching the approach forced on `index.html`, for
  consistency) specifically to avoid re-mangling the historical comment on
  `kMaxLoopSeconds` itself the same way the previous change's `replace_all` did.

Rebuilt both Standalone/CLAP and relaunched Standalone. User: "これでしばらく使って
みます" (I'll live with this for a while) - a trial, not an explicit "confirmed
correct" verdict (same as how the 30s version was only ever a trial too, before it
got changed again minutes later) - don't assume 40s is settled/final if picked up
again later; check whether the user has said anything more specific about it since.

## Session paused here (2026-07-27, continued)

User: "ここまで記録しておいてください" (please record up to here). Everything in
this file's 2026-07-27 entries above is either explicitly confirmed by the user or
(for the just-made 40s loop-length change specifically) an active trial they asked
to be recorded as-is, not yet a final verdict. No other open threads. Resume by
reading this file's true tail first (see the "multi-thread confusion" and
"Resumed 2026-07-27" entries earlier for why that matters specifically on this
project) rather than trusting any single conversation thread's own memory of where
things stand.

## Looper cut tool: hover preview now shades the side that will be removed (2026-07-28)

User reported the existing hover guide line (a thin vertical line following the
cursor while a scissors tool is selected) made it hard to tell *which side* a
click would delete, only *where* it would land. Fix, `index.html` only, no C++:

- Added a new steady (non-blinking) shaded overlay, `#looperCutHoverArea`
  (`.looper-gauge-cut-hover-area` CSS: same red as the existing
  `.looper-gauge-cut-pending` blink overlay — that color already means "will be
  cut" in this UI — but dimmer, `opacity: 0.18`, and no animation, since it's a
  preview, not a confirmed queued cut) sitting alongside the existing hover line.
- Refactored the left/width math ("region before frac" for the left tool, "region
  after frac up to `currentBarFraction`" for the right tool) out of
  `UpdateCutPendingOverlay()` into a shared `SetCutRegionStyle(el, kind, frac)`,
  reused by both the confirmed-pending overlay and the new hover preview, so the
  two can't independently drift on which side gets shaded.
- `OnGaugeAreaHover()` now updates both the line and the new shaded area every
  mousemove (same frac math, same recorded-content cutoff as before); both hide
  together in `OnGaugeAreaLeave()` and when no tool is selected.

Rebuilt both Standalone/CLAP (link-only — no C++ changed, `index.html` loads from
disk at runtime in Debug) and launched Standalone. **Confirmed by the user**
("バッチリです").

## Tempo-synced LFOs ignored the host's actual tempo (2026-07-28)

User: "LFOをシンクにしたときの基準テンポ...CLAPの場合はDAWに同期するということにな
ります" (asked to fix the base tempo used by tempo-synced LFOs - in CLAP's case that
should track the DAW). Root-caused to `FirstSynth.cpp`'s `ProcessBlock()`: it called
`mDSP.ProcessBlock(nullptr, outputs, 2, nFrames, mTimeInfo.mPPQPos,
mTimeInfo.mTransportIsRunning)` - **never passing a tempo argument at all**, so it
silently fell back to `IPlugInstrumentDSP::ProcessBlock()`'s own default parameter
(`tempo = 120.`) on every host, always, regardless of the DAW's real BPM.
`mTimeInfo.mTempo` itself was already being populated correctly by every host format
(confirmed in iPlug2's `IPlugCLAP.cpp`, `IPlugVST3_ProcessorBase.cpp`,
`IPlugAU.cpp`/`IPlugAUAudioUnit.mm`, `IPlugAAX.cpp`, `IPlugVST2.cpp`) - the bug was
purely in FirstSynth's own call site never reading it. `LFO<T>::ProcessBlock()`
(iPlug2's `Extras/LFO.h`) uses the passed `tempo` directly to compute both the
free-running phase increment and the samples-per-beat scalar for transport-locked
phase, so this fully explains why tempo-synced LFO rate never tracked host BPM
changes.

Fix: `mDSP.ProcessBlock(..., mTimeInfo.mTempo)` - one line. Rebuilt both Standalone/
CLAP. CLAP verification (loading in REAPER, changing its BPM, confirming tempo-synced
LFO rate follows) deferred to the user's own later batch REAPER testing session, per
the usual workflow - not yet confirmed live in a real DAW as of this entry.

## Standalone-only manual Tempo control added (2026-07-28, continued)

Immediate follow-up: since `mTimeInfo.mTempo` is only ever real for an actual DAW
host - `IPlugAPP` never sets it, confirmed by grep, so Standalone silently sits at
`DEFAULT_TEMPO` (120) forever regardless of user intent - the user asked for a way
to set a base tempo for tempo-synced LFOs specifically in the Standalone app.

- New `EMsgTags` entry `kMsgTagSetStandaloneTempo` (`FirstSynth.h`): UI -> C++, 4-byte
  float BPM. Deliberately **not** a real automatable `IParam` - a DAW host already
  reports its own real tempo, so an automatable "Tempo" parameter would be a
  confusing no-op automation target there. Matches this project's existing
  "don't expose internals that don't apply to every host" instinct (see
  [[project_firstsynth_clap_plugin]]'s PEQ-visibility note for the same principle
  applied elsewhere).
- New member `std::atomic<double> mStandaloneTempo {120.}` (`FirstSynth.h`, inside
  the existing `#ifdef APP_API` block) - read from the audio thread in
  `ProcessBlock()`, written from the main thread in `OnMessage()`, same atomic idiom
  as `mMeterPeak`.
- `ProcessBlock()`: `#ifdef APP_API` now selects `mStandaloneTempo.load()` instead of
  `mTimeInfo.mTempo` for the tempo argument - Standalone gets the user's manual
  value, every other format keeps using the real host tempo from the fix above.
- `OnWebContentLoaded()`: added an `EvaluateJavaScript` call to
  `EnableStandaloneTempoControl()`, gated on `#ifdef APP_API` - exact same
  established pattern as the existing `EnableComputerKeyboardInput()` call right
  above it (dev/Standalone-only WebView feature, never invoked when hosted as CLAP).
- `index.html`: new hidden-by-default "Tempo" control in the Synth page's Master
  panel (next to Yuragi), revealed only by the JS call above. Deliberately a plain
  `<input>`, not a `knob-control` element, since `knob-control` wires directly to a
  real `param-id`/`IParam` and this value intentionally isn't one.
- Does **not** currently persist across Standalone relaunches (resets to 120 every
  launch) - the existing "remember every param" auto-persist system
  (`LoadAutoState`/`SaveAutoState`) only covers real `IParam`s via
  `SerializeState`/`UnserializeState`, and this deliberately isn't one. Not asked
  for; flagged here in case it comes up later.

**Crash bug found and fixed same session**: first version of the input used
`<input type="number">` with native `min`/`max`/`step` attrs. User reported the
native up/down spinner buttons visually overlapped the 3-digit value at the chosen
width - fixed once (widened + tried hiding the spinner via `-webkit-appearance`/
`-moz-appearance`) - but then the user reported the app **crashing** while typing a
number into the field. Root cause: iPlug2's Windows WebView host
(`IPlugWebView_win.cpp`) injects its own `document`-level `keydown`/`keyup`
listener that forwards every keystroke to the native side as an `SKPFUI` "global
shortcut" message whenever `document.activeElement.type != "text"` - intended to
let WebView-hosted knobs/buttons not swallow DAW keyboard shortcuts, but it only
special-cases `type="text"`, not `type="number"`. Typing digits into a
`type="number"` field therefore fired a flood of unintended `SKPFUI` messages to
the native host on every keystroke, which is what actually crashed the app - not
anything in this project's own new `kMsgTagSetStandaloneTempo` handling (that
message-passing path mirrors the already-working `kMsgTagLooperCut` exactly and was
never the problem). This is the **first free-text `<input>` in this UI** - every
prior `<input>` in `index.html` is a checkbox, which doesn't generate the same kind
of keystroke flood, so this framework quirk never surfaced before.

Fix: switched to `<input type="text" inputmode="numeric">`, with `oninput="this.value
= this.value.replace(/[^0-9]/g, '')"` doing the digit-only restriction that
`type="number"` used to provide for free, and `SetStandaloneTempo()`'s existing
20-300 clamp still runs on `onchange`. Removed the now-inert spinner-hiding CSS
(`-webkit-appearance`/`-moz-appearance` rules only ever applied to `type="number"`).
**Confirmed working by the user** ("動いています") after relaunching - no crash
typing digits, and they separately confirmed/noted the up/down spinner is
intentionally gone now (asked about it, not a bug report).

**Worth remembering for any future free-text `<input>` in this codebase**: always
use `type="text"` (with `inputmode`/`pattern`/JS-side filtering for
numeric-only cases), never `type="number"`/`type="range"`/etc., because of this
exact framework-level `activeElement.type != "text"` check in
`IPlugWebView_win.cpp` (and the macOS equivalent in `IPlugWebView_mac.mm`, same
`!= "text"` condition) - this is a shared framework file, so the same landmine
applies to [[project-suikinkutsu-plugin]] too if it ever adds a non-checkbox
`<input>`.

## Standalone Tempo control moved into the page header (2026-07-28, continued)

User: "テンポは一番上のところ（レベルメータの左）に置けますか。レイアウトが崩れる
ので" - the Master panel placement was crowding that row. Moved the whole
`#standaloneTempoContainer` control out of the Synth page's Master panel and into
`.page-header`, immediately before the Level meter.

Wrapped both the Tempo chip and the meter chip in a new `.header-right-group`
(`display:inline-flex`) rather than just moving `margin-left:auto` from
`.meter-container` onto the new Tempo chip directly - the Tempo chip is
`display:none` on every non-Standalone build (CLAP/VST3/etc., only ever revealed by
`EnableStandaloneTempoControl()`), and a `display:none` element takes zero part in
flex layout, so an auto margin living on it alone would vanish right along with it -
silently breaking the meter's right-alignment on every format except Standalone.
Putting the auto margin on the always-rendered wrapper instead keeps the meter
correctly right-aligned regardless of whether the Tempo chip inside it happens to be
visible. `AlignMeterToContentEdge()` (the separate JS logic that fine-tunes the
meter's right margin against the LFO Depth column) still works unchanged - it only
depends on `.meter-container` being the visually rightmost element flush with
`.page-header`'s own right edge, which still holds true nested one level deeper
inside the wrapper.

HTML/CSS only, no rebuild needed. Visually confirmed by the user.

## Switched primary DAW-hosted target to VST3; VST3 SDK fetched for the first time (2026-07-28, continued)

User: not comfortable with REAPER yet, wants to develop against VST3 instead and
test in a different DAW during development, planning to register for the (free)
Steinberg VST3 commercial distribution license later - only actually required
before public/closed-source distribution, not for local dev or testing, so this is
fine to defer.

- `Dependencies/IPlug/VST3_SDK` only had a placeholder `README.md` - the actual SDK
  had never been fetched for this project. Ran the project's own
  `download-vst3-sdk.sh`, which `git clone`s Steinberg's own public GitHub mirror
  (`steinbergmedia/vst3sdk`, no account/registration needed for this) plus its
  submodules (`pluginterfaces`, `base`, `public.sdk`, `cmake`, `vstgui4`).
- Built `FirstSynth-vst3` for the first time - succeeded on the first attempt, no
  code changes needed. Output bundle auto-copied to the shared
  `C:\Users\a_wak\AppData\Local\Programs\Common\VST3\` folder by the existing
  `postbuild-win.bat`, same as CLAP's own common-folder copy step.
- User's own two VST3 test hosts, both already installed and already confirmed
  VST3-capable (see this file's/[[project-firstsynth-clap-plugin]]'s DAW
  compatibility notes): Renoise and BespokeSynth. REAPER remains the CLAP test host
  whenever CLAP testing resumes later.
- **Build routine updated going forward**: every code change now builds all three
  of Standalone/VST3/CLAP (previously just Standalone+CLAP) - see
  [[feedback-firstsynth-build-workflow]] for the updated standing instruction.

Not yet tested live in Renoise/BespokeSynth as of this entry - the build is ready
and copied to the shared folder, but the user hasn't loaded it in either host yet.

## VST3 confirmed working in Renoise/BespokeSynth; output moved to the standard system folder (2026-07-28, continued)

User loaded the VST3 build in both Renoise and BespokeSynth - **both work**. Two
follow-up issues surfaced and were worked through:

**1. Plugin wasn't found by either host at first.** The previous entry's build had
copied `FirstSynth.vst3` to iPlug2's own default `VST3_X64_PATH`
(`$(LOCALAPPDATA)\Programs\Common\VST3`, chosen upstream specifically so builds
never need admin rights) - not the real Steinberg-standard system-wide folder
(`C:\Program Files\Common Files\VST3\`), which is what both hosts actually scan by
default, and neither host exposes a custom-search-path setting the user could
redirect instead. Fixed properly rather than working around it:
- `config/FirstSynth-win.props`: added a project-level override,
  `VST3_X64_PATH`/`VST3_ARM64EC_PATH` = `$(CommonProgramFiles)\VST3` - set before
  `common-win.props` is imported, so it wins over that file's own
  `Condition="...==''"` default. Doesn't touch the shared iPlug2 framework file
  itself, just this project's own config.
- Writing to that folder needs the current Windows account to already have write
  permission there (confirmed via `Program Files\Common Files\VST3` already being
  full of the user's other installed VST3 plugins, but a `touch` test as the
  current user failed with Permission Denied first). This is a one-time
  system/security-settings change, so - not something to run unprompted - the user
  ran it themselves in an elevated terminal:
  `icacls "C:\Program Files\Common Files\VST3" /grant "%USERNAME%:(OI)(CI)F"`.
  Confirmed working (`touch` test succeeded afterward), then `FirstSynth-vst3`
  rebuilt cleanly and landed in the standard folder with no further UAC prompts
  needed for future builds.

**2. Window opened too narrow in both hosts on first launch, cutting off some
params - fixed by manually resizing.** Root-caused to a real framework-level
bug, not anything specific to VST3 or to either host: `IPlugPaths.cpp`'s
`WebViewCachePath()` returns a single **hardcoded, unscoped** folder
(`%APPDATA%\iPlug2\WebViewCache`) shared by *every* iPlug2 WebView plugin and
*every* host format on this machine (Standalone/CLAP/VST3, any DAW) - confirmed by
directly grepping that folder's `Local Storage/leveldb/*.log` for
`firstSynthUIScalePercent` and finding a real write history (60/70/80/90/100, most
recent write 80) left over from earlier Standalone testing. The Zoom feature
(`ApplyUIScale()`/`RestoreUIScale()` in `index.html`) persists via `localStorage`
and, on restore, issues an actual native window resize (`kMsgTagSetUIScale` ->
`Resize()`) - so Renoise/BespokeSynth's brand-new sessions inherited that stale 80%
setting from an unrelated earlier Standalone session and opened shrunk accordingly.

Immediate fix applied: deleted the entire shared cache folder
(`C:\Users\a_wak\AppData\Roaming\iPlug2\WebViewCache`) - pure regenerable WebView2
profile/cache data (cookies, localStorage, GPU shader cache, etc.), not real user
data, so safe to wipe; every WebView-side pref this project has (UI Scale, Dark
Mode, PEQ Lock) resets to its compiled-in default and gets recreated fresh on next
launch. Had to ask the user to close BespokeSynth first (its own `msedgewebview2.exe`
helper processes were holding the cache files open, blocking the delete) - some
`msedgewebview2.exe` processes lingered briefly even after that but released the
lock shortly after.

**Not fully fixed - deferred by the user's own choice.** The real root cause (the
single unscoped shared cache path in `IPlugPaths.cpp`, which also means different
*plugins* - e.g. this and [[project-suikinkutsu-plugin]] - could collide on
`localStorage` keys/other profile state if such a collision were ever introduced)
was presented as a second, more invasive option (scope the cache path per plugin
+ per host executable, touching the shared iPlug2 framework file) - **user chose
to only do the immediate cache-wipe reset for now, not the structural fix.** This
means the exact same symptom (a stale persisted Zoom value bleeding into a
different host's first-ever session) **can recur** any time the user zooms in one
host/format and then opens a different one for the first time afterward. As of
this entry the user said "まだ問題はありますが" (there's still a problem) when
asking to record progress - not yet confirmed whether the reset fully resolved
this specific instance of the symptom, or whether some other issue remains. Check
in on this before assuming it's settled.

## PLUG_HOST_RESIZE experiment - tried and reverted, window-cropping issue still unsolved (2026-07-28, continued)

Follow-up investigation into the "window opens too narrow" issue from the previous
entry, continued in a fresh screenshot round: user confirmed Zoom showed 100% (so
the earlier stale-localStorage-bleed bug specifically was fixed), but Renoise and
BespokeSynth still cropped the bottom (Master/Amp ADSR/Amp LFO rows) and right edge
(Level meter, Pitch LFO's Sync toggle) of the UI - a *different*, still-open bug from
the same-looking symptom.

**Hypothesis tried**: `config.h`'s `PLUG_HOST_RESIZE 1` makes `IPlugVST3View::
canResize()` report `true` to the host; theory was that some hosts, told a window is
freely resizable, open it at their own smaller default size instead of trusting
`getSize()`'s declared `PLUG_WIDTH`/`PLUG_HEIGHT` (1352x694) - and that setting it to
`0` would force every host to always honor the declared size exactly, per spec, since
non-resizable views have no negotiation to do.

**Result: didn't fix it, and cost the workaround.** Rebuilt all three targets and
retested - Renoise and BespokeSynth still opened cropped, `PLUG_HOST_RESIZE=0` made
no visible difference to the initial sizing. Worse, disabling it also disabled
Renoise's window resize-grip entirely - the user could no longer manually drag the
window bigger to work around the cropping (confirmed: "広げられなくなりました"),
which had been the one working escape hatch. **Reverted back to `PLUG_HOST_RESIZE 1`
same session** - net regression, not a fix - rebuilt all three targets again and
confirmed manual resize works again in Renoise.

**Root cause still unidentified.** User then tested **Studio One** as a third,
independent, mainstream host - same cropping there too. Three unrelated hosts
(Renoise, BespokeSynth, Studio One) all showing the identical symptom, while
Standalone (same HTML/CSS, no host negotiation involved) renders perfectly at every
zoom level, rules out both "it's an HTML/WebView rendering limitation" (ruled out -
Standalone proves the same markup renders correctly once actually given the right
window size) and "it's one host's individual quirk" (ruled out by 3-for-3
reproduction). Points toward something in iPlug2's own VST3 window-size-negotiation
code (`IPlugVST3_View.h`'s `getSize()`/`attached()`/`canResize()`/`Resize()` - see
that file for the current implementation) not correctly asserting the plugin's
declared size to the host at the right moment, but the exact defect wasn't
pinned down. Web research found related-but-not-identical prior art: iPlug2 GitHub
issues [#769](https://github.com/iPlug2/iPlug2/issues/769) ("VST3 in Ableton Live
10 & 11 creates wrong Editor Window size on initial construction" - Ableton
specifically ignoring `EditorResize()` during construction) and
[#593](https://github.com/iPlug2/iPlug2/issues/593) (Ableton-specific resize-during-
open bug), plus an [iPlug2 forum thread](https://iplug2.discourse.group/t/unserializeeditorstate-vst3-win-version-is-not-resizing-correctly-on-ableton/700)
- all Windows VST3 sizing issues, but none an exact match for a bug reproducing
across Renoise/BespokeSynth/Studio One specifically (the GitHub issues are
Ableton-specific).

**Status: paused here by the user's own choice** ("いったんここで止めましょう") -
not fixed, not actively being pursued further right now. Current known-working
state: `PLUG_HOST_RESIZE` back at its original `1`; every tested host (Standalone,
Renoise, BespokeSynth, Studio One via CLAP or VST3) opens with the editor
potentially cropped on first show, but can be manually dragged larger as a working
(if mildly annoying, and per-session-recurring) workaround. Revisit
`IPlugVST3_View.h`'s size-negotiation code if this gets picked up again - don't
re-try the exact `PLUG_HOST_RESIZE=0` experiment without a new idea, it's already
confirmed to be a dead end that costs the workaround besides.

## CLAP tempo sync confirmed live in REAPER (2026-07-28, continued)

Closed out the one item deferred from the very first tempo-fix entry above: user
(new to REAPER, walked through it step by step) loaded the CLAP build in REAPER
7.78, put an Amp LFO into Sync mode (Rate 1/4, Depth 50.7%) for an audible tremolo,
looped a held MIDI note, then changed REAPER's project BPM live (120 -> ~240) while
it played. **Confirmed**: the tremolo speed audibly changed with the BPM change -
`mTimeInfo.mTempo` is really reaching the DSP in a real DAW now, not just in
theory. This closes out the original tempo-sync bug from earlier in this file
("Tempo-synced LFOs ignored the host's actual tempo").

Same REAPER session also re-confirmed the still-unsolved window-cropping issue
from the entry above - REAPER is now a **4th** independent host reproducing it
(after Renoise/BespokeSynth/Studio One), same workaround (manual drag-resize)
applied. Doesn't change that investigation's paused status, just further evidence
it's systemic rather than host-specific if picked up again later.

Practical REAPER walkthrough notes (user had never used REAPER before, needed
step-by-step guidance throughout - worth remembering for next time this comes up):
- Adding a plugin: right-click empty track panel area -> "Insert new track", then
  click that track's "FX" button and search/insert from there.
- **Creating a MIDI item to test with**: right-clicking directly in a track's own
  timeline lane did nothing for the user (unclear why - possibly a REAPER
  preference/mouse-modifier quirk on this install) - the reliable path that
  actually worked was the **top menu bar's "Insert" menu** (not right-click) ->
  New MIDI item, then double-click that item to open the piano roll and drag out
  a note there.
- **Looping playback**: drag across the ruler above the item's time range to set
  a time selection, enable the "Repeat" toggle in the transport bar, then Space to
  play.
- **No sound despite the meter moving**: REAPER's own audio device wasn't set -
  fixed via Options -> Preferences -> Audio -> Device, selecting the real audio
  interface (was presumably on the wrong device/None). Not a FirstSynth issue.

## Cross-format preset browser added (2026-07-28, continued)

User asked whether a preset saved in Standalone could be loaded from CLAP/VST3 -
answer was no (Standalone's existing "Save/Load Preset" File-menu dialog,
`IPlugAPP_dialog.cpp`, writes a raw `SerializeState()` chunk to a `*.preset` file
the user picks via a native Windows file dialog - Standalone-only tooling, and a
DAW's own native VST3/CLAP preset browser is a completely separate mechanism with
its own file format/convention). User wanted one that's actually shared, "like
commercial synths have a patch browser" - built one, working identically in every
format (Standalone/VST3/CLAP), not gated on `APP_API` unlike every other
preset/state mechanism in this file:

- **Storage**: one fixed shared folder, `GetPresetsDir()` = `AppData\Local\
  FirstSynth\Presets\` (same `INIPath()` helper the settings.ini/autosave.state
  already use, just a new `Presets` subfolder, created via `std::filesystem::
  create_directories` if missing - `common-win.props` already sets `stdcpp17`
  project-wide, confirmed via grep, so `<filesystem>` needed no toolchain changes).
  Deliberately the *same* raw-chunk-dump format Standalone's existing manual
  `*.preset` save/load already uses (`SerializeState()`/`UnserializeState()`, byte-
  for-byte) - not a new file format, just a new fixed location and a new
  in-WebView UI on top of the same mechanism, so it works identically regardless
  of host.
- **New messages** (`FirstSynth.h`'s `EMsgTags`): `kMsgTagPresetList` (C++->UI,
  preset names newline-joined, sent from `OnWebContentLoaded()` and again after
  every save), `kMsgTagPresetSave`/`kMsgTagPresetLoad` (UI->C++, UTF8 preset name).
- **New C++ methods** (`FirstSynth.cpp`, all cross-format, none `APP_API`-gated):
  `GetPresetsDir()`, `SendPresetList()`, `SavePresetAs()`, `LoadPresetByName()`,
  `SanitizePresetName()`.
- **Security note worth remembering**: preset names double as filenames and arrive
  as free-text from the WebView, so `SanitizePresetName()` is the real trust
  boundary between arbitrary UI text and a real filesystem path. First draft used
  an ASCII-only allow-list (alnum/space/-/_/parens) - caught before it shipped that
  this would silently strip out **any non-English preset name entirely**,
  including Japanese, which is a very real use case for this user specifically.
  Rewrote as a byte-*blacklist* instead: reject only actual path separators (`\`
  `/`), reserved Windows filename characters (`:`, `*`, `?`, `"`, `<`, `>`, `|`),
  and ASCII control bytes - every UTF8 multi-byte sequence (any non-ASCII
  character) now passes through untouched. The mandatory `.preset` suffix the
  callers always append already neutralizes `".."` as a traversal risk (the
  resulting filename is never literally `".."`), so no separate check for that
  was needed once no path separator can appear in the sanitized name at all.
- **WebView UI** (`index.html`): a new `.preset-bar` row (Prev ◀ / dropdown /
  Next ▶ / "Save As..." button) below `.page-header`, not folded into that
  already-crowded row (see the window-cropping entries above) - deliberately its
  own row since, unlike the Standalone-only Tempo control, this needs to be
  visible in every build. "Save As..." uses a plain `window.prompt()` for the
  name, matching the user's own chosen UX ("ボタン→テキスト入力欄で名前を入力").
  Preset names are UTF8-encoded (`TextEncoder`) going out and UTF8-decoded
  (`TextDecoder`) coming back in the `kMsgTagPresetList` handler - not the simpler
  charCodeAt-per-byte pattern the float-array messages elsewhere in this file use,
  since that would corrupt any multi-byte (non-English) name.
- **Bug found and fixed same session**: picking the *already-selected* preset
  again from the dropdown (to discard live knob tweaks and reload its saved
  values) silently did nothing - native `<select>`'s `onchange` simply never fires
  when the user re-picks the option that was already selected, since nothing
  changed from the browser's own perspective. Only worked by first picking a
  genuinely different preset, or via the Prev/Next buttons (which call
  `LoadPreset()` directly, not dependent on `onchange`). Fixed by clearing the
  select's `selectedIndex` on `mousedown` (right before the native dropdown pops
  up), so whatever gets picked afterward - even the same visible option - always
  registers as a real value change and fires `onchange` reliably. Accepted minor
  edge case: canceling the dropdown without picking (e.g. Escape) can leave it
  showing blank until the next pick - recoverable, not data-lossy.

Rebuilt all three targets (Standalone/VST3/CLAP), confirmed working in Standalone:
save-as, dropdown reselect (after the fix above), knob values round-tripping
correctly. Not yet tested from VST3/CLAP specifically, though the mechanism is
identical there - worth a quick check next time either is opened, mainly to
confirm a preset saved from one format really does show up when opening from a
different one (the whole point of this feature).

## Preset browser: layout bug fixed, cross-format list-enumeration bug still OPEN (2026-07-28, continued, session paused mid-investigation)

User tried the new preset browser from VST3 (host: Renoise, per this session's
earlier testing) and reported "スタンドアローンで保存したプリセットは見当たりま
せんね" (a preset saved in Standalone doesn't show up). Two distinct bugs found so
far investigating this - **one fixed, one still open**:

**Bug 1 - FIXED: `.preset-bar` markup was accidentally nested inside `.page-header`.**
When the preset-bar row was added, the `</div>` closing `.page-header` (a
no-wrap flex row) was misplaced - it ended up *after* `.preset-bar` instead of
*before* it, so the whole preset browser became one more flex item squeezed onto
that already-tight single row and got pushed off the visible window's right edge
entirely ("枠の外に出てしまっている", and widening the window didn't help since
the row just kept shifting everything rightward). Fixed by moving `.page-header`'s
closing `</div>` to right after the meter/tempo group, making `.preset-bar` a
proper sibling row below it, matching the original intent. HTML-only, no rebuild
needed - **confirmed fixed by the user** ("出ました").

**Bug 2 - STILL OPEN: `SendPresetList()` finds 0 presets from VST3, despite
resolving to the identical folder Standalone finds 2 in, with no error.** Added a
temporary diagnostic (`SendPresetList()` now also pushes `console.log('FirstSynth
presets dir: <dir> | ec=<n> (<msg>) | found=<n>')` via `EvaluateJavaScript` -
search `FirstSynth.cpp` for "temporary diagnostic" to find/remove this once
resolved) and compared DevTools console output side by side:
- **Standalone**: `FirstSynth presets dir: C:/Users/a_wak/AppData/Local/FirstSynth/Presets | ec=0 (The operation completed successfully.) | found=2` - correct.
- **VST3 (Renoise)**: `FirstSynth presets dir: C:/Users/a_wak/AppData/Local/FirstSynth/Presets | ec=0 (The operation completed successfully.) | found=0` - **identical path, identical no-error status, but zero matches**, despite `test21.preset`/`test22.preset` genuinely existing in that exact folder at the time (confirmed directly via filesystem listing outside either app).
- Ruled out: **write-side path virtualization/sandboxing** - asked the user to
  Save As a new preset ("vsttest") from within that same VST3 instance, and
  `vsttest.preset` **did** land in the real shared folder (confirmed directly).
  So writes from VST3 definitely reach the true folder; only the *enumeration*
  used by `SendPresetList()` fails to see files that are already there.
- **In progress when the session paused**: asked the user to reopen/reload that
  same VST3 plugin instance now that `vsttest.preset` exists, and check whether
  the diagnostic's `found=` count is 0, 1 (only `vsttest`, written from within
  that very process/session), or 3 (everything, meaning it self-corrected) - this
  would help distinguish "stale directory snapshot specific to that process" from
  something else. **Not yet answered as of this entry** - the user asked to pause
  and record progress here because a session time limit was approaching, right
  before actually reopening the plugin to check.

**Resume here**: re-run that check first (reopen the VST3 instance, read the new
`found=` count from DevTools console) before doing anything else with this
feature. Current file state: `test21.preset`, `test22.preset`, `vsttest.preset`
all genuinely exist in `C:\Users\a_wak\AppData\Local\FirstSynth\Presets\`. The
diagnostic logging is still live in the built binaries (all three targets were
last rebuilt with it present, `PLUG_HOST_RESIZE` still at its normal `1`, nothing
else changed since). Remove the diagnostic once this is solved -
[[project_firstsynth_clap_plugin]]'s memory doesn't need a separate note beyond
this entry existing.

## Preset browser Bug 2, continued investigation - root cause still NOT found, session paused again (2026-07-28, continued)

Picked back up after the previous pause. New evidence gathered, several
hypotheses tested and ruled out - **still unsolved**.

**New evidence**: enhanced the diagnostic to log every *raw* directory entry the
OS-level enumeration yields, before any of `SendPresetList()`'s own filtering
(`is_regular_file()`/extension check) - not just the post-filter count. Result
from a fresh VST3 instance (Renoise), opened right after a full DAW restart, with
all three files (`test21.preset`, `test22.preset`, `vsttest.preset`) genuinely
present on disk (confirmed via direct filesystem listing outside any app):

```
FirstSynth presets dir: C:/Users/a_wak/AppData/Local/FirstSynth/Presets | ec=0 (The operation completed successfully.) | rawCount=1 | rawEntries=[vsttest.preset:1:.preset] | found=1
```

**This is conclusive on one point**: `rawCount=1` means the OS-level enumeration
itself (`std::filesystem::directory_iterator`, i.e. `FindFirstFile`/`FindNextFile`
under the hood on Windows) only yields **one raw entry** to this process - it is
**not** a filtering bug in `SendPresetList()`'s own code (which correctly
processes whatever it's handed). Standalone, run right alongside for comparison,
sees `rawCount=3` from the identical resolved path string.

**Confirmed reproducing in two independent, unrelated hosts** (Renoise and
BespokeSynth) - both show the exact same pattern: a fresh VST3 instance only ever
sees the **one file that instance itself most recently wrote**, completely blind
to `test21.preset`/`test22.preset` (written earlier, by Standalone), even after a
**full restart of the host DAW itself** (ruling out an in-memory/stale-handle
explanation tied to a long-lived plugin-bridging subprocess).

**Hypotheses tested and ruled out this round:**
- **Not file-level corruption/hidden characters**: `xxd`/`Get-ChildItem` directly
  confirm `test21.preset`/`test22.preset`/`vsttest.preset` all have clean,
  expected filenames/extensions/attributes ("Archive" only, nothing unusual).
- **Not write-side path virtualization/sandboxing**: saving a new preset from
  within a live VST3 instance genuinely lands the file in the real, non-redirected
  shared folder (confirmed directly) - so if any redirection/virtualization is
  happening, it's asymmetric (writes reach the real folder; some reads don't see
  everything that's there), which is unusual and doesn't match typical Windows
  UAC-style file virtualization behavior (which is normally symmetric and doesn't
  even apply to `AppData\Local` in the first place - that's a per-user-writable
  location, not one of the legacy-app-targeted protected system folders).
- **Not Windows Defender Controlled Folder Access**: checked directly via
  `Get-MpPreference` - `EnableControlledFolderAccess` is `0` (disabled) on this
  machine, so this specific, otherwise-very-plausible-looking explanation is
  ruled out.
- **Not a stale in-memory/long-lived-subprocess cache**: reproduces identically
  even after a **full restart of the host DAW**, not just closing/reopening the
  plugin's own editor window.

**Not yet tried / worth trying next session:**
- Check Task Manager for any additional child/bridge/sandbox processes spawned
  specifically when Renoise or BespokeSynth loads a VST3 plugin (some hosts run
  plugins out-of-process for crash isolation) - if such a process exists and is
  itself short-lived/recreated per plugin-instance (not per DAW-session), that
  would explain "blind to anything not written by this exact instance" without
  needing a full DAW restart to reset it, and would point at that specific
  mechanism as the isolation boundary.
- Check whether any third-party antivirus/EDR product (beyond Windows Defender,
  already ruled out via Controlled Folder Access) is installed and has an
  equivalent "protect this folder" or per-application file-visibility feature.
- Try reproducing with the antivirus/real-time-protection temporarily disabled,
  as a blunt test, to see if the behavior changes at all.
- Consider testing whether the same bug reproduces in **REAPER** hosting **CLAP**
  (not yet tried for this specific bug - all testing so far was VST3 in Renoise/
  BespokeSynth) - if CLAP-in-REAPER behaves *differently* (sees all 3 files),
  that would narrow this down to something VST3-specific (or Renoise/
  BespokeSynth-specific) rather than a general "hosted plugin" issue.
- If truly stuck, consider whether this preset browser feature needs a different
  storage strategy entirely (e.g., writing through a mechanism less exposed to
  whatever isolation this is, if it can even be identified) - but don't reach for
  this until the actual mechanism is understood; changing storage strategy blind
  risks fixing the symptom without knowing why, and reintroducing it elsewhere.

**Status: paused again, unsolved, by necessity (user's session time limit).**
Current file/build state unchanged from the previous pause entry - diagnostic
logging still live in `SendPresetList()` (now the enhanced raw-entry version),
all three targets last built with it present. `test21.preset`, `test22.preset`,
`vsttest.preset` all still genuinely exist in the shared folder. Resume by reading
this entry and the previous one in full before touching this feature again - a
lot of ground has already been covered and ruled out; don't re-run the same
already-ruled-out checks.

## Title wrapping to two lines fixed (2026-07-28, continued)

Small, unrelated bug found in passing: "1st synth" (`<h1>`) started wrapping onto
two lines. `.page-header` is a `display:flex` row now carrying several more items
than it originally did (Zoom, Dark mode, the Tempo/Level group, the new preset
browser is a separate row so not this one, but the header itself has grown over
the course of this session) - a flex item can shrink below its own content's
natural width by default, and `h1` had no `flex-shrink`/`white-space` override, so
once the row got tight enough the title itself was what gave way and wrapped.
Fixed with `white-space: nowrap; flex-shrink: 0;` on `h1`. HTML/CSS only, no
rebuild needed. **Confirmed fixed by the user.**

## Preset browser Bug 2 - MAJOR REFRAME: not VST3/host-specific after all (2026-07-28, continued)

**Critical correction to the previous two "Bug 2" entries: the "VST3/CLAP-hosted
instances only see their own most recent write" framing was wrong, or at least
incomplete.** Shortly after the previous entry, the user reopened/refreshed
**Standalone** (the same build that had earlier and reliably reported
`rawCount=3`/`found=2` multiple times over the course of this session) and got:

```
FirstSynth presets dir: C:/Users/a_wak/AppData/Local/FirstSynth/Presets | ec=0 (The operation completed successfully.) | rawCount=1 | rawEntries=[vsttest.preset:1:.preset] | found=1
```

Same single-entry result as VST3/CLAP - **in Standalone, no host/DLL-hosting/
sandboxing involved at all.** Re-confirmed via direct filesystem listing
(outside any app, at that exact moment) that all three files
(`test21.preset`/`test22.preset`/`vsttest.preset`) genuinely still existed on
disk. So the "only VST3/CLAP-hosted instances are affected, Standalone always
works" pattern from the previous two entries **does not hold** - the same process
type (Standalone), with the same binary, previously showed 3 and later showed 1,
for the identical files on identical disk state at different points in time. This
means the bug is **not fundamentally about VST3-hosting/sandboxing/process
isolation** as previously suspected - something else, likely time/state-dependent
in a way not yet understood, occasionally causes `std::filesystem::
directory_iterator` (on *any* process type) to only enumerate 1 of the 3 files
genuinely present.

Checked Windows Defender's real-time protection status (distinct from Controlled
Folder Access, already ruled out): `Get-MpComputerStatus` shows
`RealTimeProtectionEnabled: True` (Defender's core AV scanning is active; no
third-party AV product found registered). Real-time file-scanning filter drivers
are a known category of things that can cause exactly this kind of transient/
inconsistent directory-enumeration behavior (a file recently written or
recently-scanned appears immediately; other files sitting untouched for a while
can occasionally be treated differently by some filter driver's own caching) -
**not confirmed as the cause, but newly relevant and not yet ruled out**, unlike
Controlled Folder Access which was concretely disabled.

**Reframed status for whoever resumes this**: this is looking less like "a VST3/
CLAP hosting quirk" and more like **either a genuine, if rare, intermittent
Windows/filesystem-driver quirk affecting `std::filesystem::directory_iterator`
enumeration generally, or a bug in this project's own code that's more subtle
than simple host-type-based, tied to some state or timing not yet identified**
(e.g., does it correlate with how long ago the directory was last modified? With
how many times `SendPresetList()`/`GetPresetsDir()`'s `create_directories()` call
has run recently? Worth testing deliberately next time: call the diagnostic
several times in a row from the same still-open instance and see if the count is
stable or flips between calls - that alone would distinguish "OS-level
intermittent flakiness" from "something in this specific call path only
sometimes goes wrong"). **Do not assume the previous two entries' "VST3-hosting-
specific" framing is still accurate** - re-read this entry first if resuming.

Paused again by necessity (user's session time limit, mentioned repeatedly this
session) - not resolved, reframed instead. Diagnostic logging still live in
`SendPresetList()`, unchanged since the previous entry.

## Knob "sometimes doesn't grab on the first attempt" - diagnostic added, not yet root-caused (2026-07-28, continued)

User reported (again - this has apparently come up before, though not previously
tracked in this file) that knobs occasionally don't respond to the very first
click/drag attempt, no clear pattern, and the second attempt always works. This
sounds related to but distinct from Known Issue #11 above ("Knob needed a second
touch to respond", fixed 2026-07-21 by switching `mousedown`/`touchstart` ->
`pointerdown` in `knob-control.js`) - that fix is confirmed still present, but
evidently doesn't cover every case.

Checked one concrete lead: a documented WebView2 Runtime regression (versions
around 134.x) where the first click on a page can get its focus routed to the
wrong element. Checked the installed WebView2 Runtime via the registry
(`HKLM\...\EdgeUpdate\Clients\{F3017226-...}`, `pv` value) - **150.0.4078.105**,
well past the version that bug was fixed in (134.0.3124.68), so **that specific
known issue is ruled out** as the cause here.

Added temporary diagnostic logging to `resources/web/knob-control.js` (both
projects sharing this file, i.e. also relevant to
[[project-suikinkutsu-plugin]] if it comes up there too):
- A module-level (not per-knob-instance - this file backs ~70 knob elements on
  one page, so a per-instance listener would spam ~70 duplicate lines per click)
  capturing-phase `document` `pointerdown` listener, logging every pointerdown
  anywhere on the page (target tag/class, pointerType, timestamp).
- A log line at the very top of `startDrag()` itself (label, button, pointerType,
  `document.hasFocus()`, timestamp).

**Next time a grab silently fails**, compare the two: if the document-level line
fires but `startDrag()`'s own line doesn't, the pointerdown never reached the
knob's own circle element at all (a hit-testing/focus-routing problem upstream of
this file, not something fixable in `startDrag()` itself); if neither fires, the
input never reached the page/WebView; if both fire but the knob still didn't
visibly respond, the bug is in the drag-update logic itself, not event delivery.
Not yet observed/diagnosed further as of this entry - purely instrumentation
added, waiting on the next live repro. Remove the diagnostic once root-caused
(search "Temporary diagnostic" in `knob-control.js`).

## Modulation Matrix page added (2026-07-28, continued) - major feature

User requested a mod-matrix page, like commercial synths have: pick a source, a
destination, and a bipolar amount, per slot. Planned via `EnterPlanMode` first
(plan saved to `C:\Users\a_wak\.claude\plans\glimmering-greeting-boole.md`) given
the scope - confirmed with the user beforehand: 4 slots (later expanded to 8, see
below), dropdown pickers for Source/Dest (not knobs), page tab placed between
Effects and Looper.

**Sources (fixed list, `EMatrixSource` in `FirstSynth_DSP.h`)**: None, 2 new free
LFOs (Mod LFO 1/2 - not hard-wired to any destination, unlike Pitch/Filter/Amp
LFO), 2 new free ADSR envelopes (Mod Env 1/2), Velocity (captured from `Trigger()`'s
own `level` param, which was already being received but ignored per an earlier
"key velocity removed" decision - that decision is untouched, this is only a new,
additional use of the same value), Key Follow (reuses the existing per-sample
`pitch` local, normalized `/4` for a typical playable range), Mod Wheel (CC1 -
implemented the previously-stub `Voice::SetControl()`, confirmed via
`VoiceAllocator.cpp`/`MidiSynth.cpp` that generic CCs reach it with the real CC
number and an already-normalized [0,1] value).

**Destinations (fixed list, `EMatrixDest`)**: None, Filter Cutoff (+-8oct, same
scale as Filter Env Amount), Filter Resonance (+-100 on the existing 0-100
normalized scale), Osc1/Osc2 Pitch (+-2oct, independent per oscillator - the
existing Pitch LFO still applies to both together), Amp Level (multiplicative,
same `1 + x` convention as the existing Amp LFO term), Pan (recombines with the
existing Yuragi base pan, only recomputes cos/sin when actually used - see below),
Wave Shape 1/2 (+-4, full morph range), Osc1/Osc2/Noise Level (+-1 on the existing
0-1 mix scale).

**Routing implementation** (`Voice::ProcessSamplesAccumulating`): gathers all 7
source values into an array once (envelopes' real `.Process()` calls happen here,
advancing their state), then for each active slot looks up
`sourceValue * (amount/100)` and accumulates into a per-destination array, applied
at each destination's existing computation point. Osc1/Osc2 Pitch (and later
Pitch Fine) are a special case: computed once per *block*, not per sample, matching
this file's own pre-existing convention for `kModPitchLFO` (osc frequency/phaseInc
are themselves only block-rate) - `ADSREnvelope::GetPrevOutput()` (doesn't advance
time, unlike `Process()`) is used there specifically so the two envelope sources
can be read for that block-start snapshot without desyncing their real per-sample
advancement used everywhere else. Pan's cos/sin recompute is gated behind
`matDest[kMatDstPan] != 0`, so using the matrix for anything else costs nothing
extra there and existing per-note Yuragi-only pan behavior is unchanged.

**Anti-zipper smoothing**: Mod Env 1/2's Sustain params go through the same
`LogParamSmooth` buffer mechanism (`kModModEnv1SustainSmoother`/
`kModModEnv2SustainSmoother`, two new `EModulations` entries) that `mAMPEnv`/
`mFilterEnv`'s own Sustain already use - without this, twisting a Mod Env's
Sustain knob while a held note is actively using it as a matrix source would
click/jump at whatever destination it's routed to.

**Params**: 28 new (`kParamModLFO1*`/`ModLFO2*`/`ModEnv1*`/`ModEnv2*`/
`kParamMatrix1..4 Source/Dest/Amount`), all appended at the very end of `EParams`
per this project's established "never renumber" convention. UI: new "Matrix" page
tab between Effects and Looper, `<select>` dropdowns for Source/Dest (the WebView's
first use of `<select>` for enum params rather than a knob - added a
`kMatrixEnumParams`/`OnParamChange` branch and `data-param-id` attributes so
host-driven changes, e.g. preset recall, actually update the dropdowns - the
generic `knob-control[param-id=...]` fallback silently does nothing for elements
that aren't knob-controls).

Built all three targets, **confirmed working live by the user** (Mod LFO 1 ->
Filter Cutoff audibly sweeping cutoff on a held note).

### Follow-up 1: old presets load safely (2026-07-28, continued)

User asked whether presets saved before this feature existed would load with the
new Matrix params implicitly at None/None/0 (no conflict). Confirmed by reading
`IPluginBase::UnserializeParams()` (`IPlugPluginBase.cpp`): it reads params
strictly in index order and simply **stops** once the saved chunk runs out of
bytes (`chunk.Get()` returns -1, loop condition `pos >= 0` fails) - so an old,
shorter preset leaves every appended-but-unread param (here, all 28 new ones) at
its compiled-in default. Since every new Matrix param's default is inert
(Source/Dest = None, Amount = 0%), old presets load with the matrix harmlessly
empty automatically - no manual reset needed. This is exactly why the project's
"append-only, never renumber" params convention exists.

### Follow-up 2: Pitch destinations felt too strong - added a Fine pair (2026-07-28, continued)

User: Osc1/Osc2 Pitch's +-2 octave range felt "わりと強い" (rather strong) even
at modest Amount for subtle vibrato-style use. Added `kMatDstOsc1PitchFine`/
`kMatDstOsc2PitchFine` - **appended at the very end of `EMatrixDest`**, not
inserted next to the existing Osc1Pitch/Osc2Pitch entries, since a Dest param's
*value* is this enum and already-saved Matrix routings must keep meaning the same
thing (same append-only reasoning as the params themselves). +-1 semitone
(1/12 octave) at 100% amount - 24x narrower than the existing pair. Confirmed
working by the user.

### Follow-up 3: 4 -> 8 slots (2026-07-28, continued)

User asked to double the slot count. Mechanically straightforward given the
existing design: `Voice::mMatrixSource/Dest/Amount` arrays sized via a new
`static const int kNumMatrixSlots = 8` (was a bare `4` in two places), 12 more
appended params (`kParamMatrix5..8 Source/Dest/Amount`), matching `InitEnum`/
`InitDouble` calls and `SetParam` case labels (the existing
`(paramIdx - kParamMatrix1Source) / 3` slot-index arithmetic already generalized
correctly - slots 5-8 are just further along the same contiguous run). Confirmed
this doesn't meaningfully add CPU cost when discussed with the user: the 7 source
values are gathered **once per sample** regardless of slot count (each slot just
looks the already-computed value up from an array) - the real fixed per-sample
cost (2 extra always-on `ADSREnvelope::Process()` calls for Mod Env 1/2,
regardless of whether any slot actually uses them - a known, not-yet-optimized
"tax") doesn't change with slot count either. Confirmed working live by the user.

**Layout follow-up**: 8 slots in one long vertical list felt too tall - split into
2 columns of 4 (`.matrix-columns` flex row wrapping two `.matrix-slots-col`
columns), then widened the gap between columns from 24px to ~96px (~one
knob-container's width) per user feedback ("だいぶ良くなりました" - confirmed).

**Not yet done / known follow-ups if this comes up again**: the "always-on Mod
Env 1/2 evaluation regardless of use" CPU tax mentioned above could be optimized
(skip `.Process()` when no active slot references that source) if it ever
matters in practice - not implemented, flagged only.

## OPEN ISSUE: Mod Env 1/2 always evaluated regardless of Matrix use (2026-07-28, continued)

User explicitly asked to keep this tracked as an open/unresolved item, not just a
passing footnote. **Not fixed, not urgent, just tracked.**

`Voice::ProcessSamplesAccumulating()`'s Modulation Matrix source-gather step calls
`mModEnv1.Process(...)`/`mModEnv2.Process(...)` **unconditionally, every sample,
for every active voice** - regardless of whether any of that voice's 8 matrix
slots actually has its Source set to Mod Env 1 or Mod Env 2. This means turning
the whole Matrix feature "off" (all 8 slots at None) still pays the cost of 2 full
extra ADSR envelope evaluations per voice per sample, on top of the pre-existing
`mAMPEnv`/`mFilterEnv` evaluations (i.e. envelope-processing cost is unconditionally
2x what it was before this feature, not conditional on actual matrix usage).

**Possible future fix** (not implemented): compute once per block (not per
sample) whether any of the 8 slots' `mMatrixSource[]` equals `kMatSrcModEnv1`/
`kMatSrcModEnv2`, and skip that envelope's `.Process()` call for the block when
unused - `ADSREnvelope` would need to either tolerate being skipped without state
corruption (need to check its own internal timing/stage-advance assumptions
before relying on this) or use `GetPrevOutput()` as a substitute when idle, similar
to how the block-start Pitch destination snapshot already uses it elsewhere in
this same function.

Not measured with a real profiler - see the earlier "how much CPU" discussion in
this same session for the qualitative reasoning (this is likely small relative to
the pre-existing `pow()`/`tan()`-heavy filter cutoff math, but it's real and
unconditional, unlike Pan's modulation which is already correctly gated). Revisit
if CPU ever becomes a concern, using the `CPU_Monitor.bat` tool mentioned in
[[feedback-firstsynth-build-workflow]] to get real numbers before/after any fix.

## Preset browser Bug 2 - ROOT CAUSE FOUND (environmental, not a code bug) + a real product-level issue it exposed (2026-07-29)

Picked back up the still-open "some presets invisible to enumeration" mystery
from the earlier "MAJOR REFRAME" entries, with much stronger evidence this time
(36 real preset files instead of 3, after the user asked to consolidate their
~29 old presets - originally saved via Standalone's separate native "File > Save
Preset" dialog, in `C:\Users\a_wak\CLAP_plugin\FirstSynth\presets\` - into the
new shared browser's folder).

**Root cause, found via Process Monitor (Sysinternals)**: captured every file
operation `FirstSynth_x64.exe` performs around the Presets folder. The trace
shows it querying **two different paths for the same logical folder**:
- `C:\Users\a_wak\AppData\Local\FirstSynth\Presets` (the real, intended path)
- `C:\Users\a_wak\AppData\Local\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local\FirstSynth\Presets`
  (a Windows container/AppContainer-redirected alias - `Claude_pzs8sxrjxfjjc` is
  this Claude Code session's own package identity)

Confirmed these two paths are **the same physical files on disk** - copying a
file from one into the other reported `cp: ... are the same file` (Bash detected
identical file IDs, not two separate copies). This matches Windows' `bindflt.sys`
container-binding mechanism (used by Windows Sandbox/container features): named
file access (open/read/write by exact filename) passes through identically via
either path, but **directory *enumeration* (`QueryDirectory`/`FindFirstFile`)
through the bind is filtered independently** - apparently to only files "known"
to have been created within the active container/session context. This exactly
explains every previous observation in this investigation: individual saves/loads
always worked (same underlying file, opened directly by name), but the *listing*
shown by `SendPresetList()` only ever included files the app itself had written
during an active Claude Code session, regardless of host format, regardless of
Standalone vs. VST3, regardless of full process restarts, regardless of launching
via a clean Explorer double-click (still affected, since the bind/filter is tied
to the machine-wide active session, not to which process launched the .exe) -
tested and ruled out: `Get-Item`'s `LinkType`/`Target` show the real folder is a
plain directory, not an NTFS junction/symlink, so this isn't a simple reparse
point either.

**Conclusion: this is not a bug in FirstSynth's code, and not something fixable
by changing it.** It's an artifact of the sandboxed environment this specific
Claude Code session runs in on this Windows machine - very likely tied to the
session's own lifetime (once this session ends, the container binding may no
longer apply, and a real end user's machine - never running inside this kind of
sandbox at all - should never exhibit this in the first place). Not something to
keep chasing with more code changes.

### The real, separate, product-level issue this exposed: two independent, disconnected preset-save mechanisms

Regardless of the sandbox explanation above, the user raised the actual
important point this investigation surfaced: **this project currently has two
completely separate ways to save a preset, and they don't share state**:

1. **The old, Standalone-only native "File > Save Preset" dialog**
   (`IPlugAPP_dialog.cpp`, `#ifdef APP_API`/`ID_SAVE_PRESET`) - a native Windows
   file picker, saves anywhere the user chooses (historically, the user's own
   `CLAP_plugin\FirstSynth\presets\` folder, accumulated ~29 presets there over
   many past sessions), remembers the last-used folder. Standalone-only - VST3/
   CLAP hosts have no equivalent, since this dialog is APP_API-gated.
2. **The new cross-format preset browser** (added earlier this session, `.preset-bar`
   in `index.html`, `SavePresetAs()`/`LoadPresetByName()`/`SendPresetList()` in
   `FirstSynth.cpp`/`FirstSynth_DSP.h`) - works in every format, but only ever
   looks at one fixed folder (`GetPresetsDir()`, `AppData\Local\FirstSynth\Presets\`).

Both write the *same underlying byte format* (a raw `SerializeState()` chunk) -
confirmed compatible by directly copying old dialog-saved files into the new
browser's folder and having them load correctly - but a user has no way to
discover or reach the old dialog's presets from the new browser (or vice versa)
without manually moving files around outside the app, exactly as happened here.
**This needs a real decision before this project is ever distributed** - the user
suggested eventually retiring the old dialog-based Save Preset entirely (the new
browser being a strict superset - same file format, but discoverable, cross-
format, and with Prev/Next browsing the old dialog never had) so there's only one
preset system going forward. Not implemented yet - **explicitly flagged by the
user as something that must be resolved before release**, this session just
recorded the decision context; no code changes made for this specific item.

**File relocation done this session** (unrelated to the sandbox bug specifically,
just consolidating for convenience): the user's ~29 old presets were moved from
`CLAP_plugin\FirstSynth\presets\` into the new browser's shared folder
(`AppData\Local\FirstSynth\Presets\`) - **a backup copy was restored at the
original location** at the user's request, so both locations currently have a
full copy. The (session-scoped, environmental) enumeration bug means not all of
these are currently visible in the browser's dropdown *while this Claude Code
session is active* - re-verify once a fresh, non-Claude-Code-session context is
available (e.g., next time the user opens the app entirely on their own,
unrelated to any active coding session).

**Next steps for whoever resumes this**:
1. Re-test the preset browser's dropdown listing in a context with **no active
   Claude Code session running** - if the sandbox theory is right, this should
   just work correctly with zero code changes.
2. Separately, and regardless of #1's outcome: **decide and implement the
   consolidation of the two preset-save mechanisms before distribution** - the
   user's own suggested direction is retiring the old Standalone-only dialog in
   favor of the new cross-format browser exclusively.

## Horizontal scrollbar reappeared after Matrix/Tempo/Preset-bar header changes - PARTIALLY diagnosed, fix attempt did NOT work, session paused (2026-07-29)

User asked to set the default window size (`PLUG_WIDTH`/`PLUG_HEIGHT` in
`config.h`) to match their own currently-open, manually-resized window, to
eliminate a horizontal scrollbar that appeared after today's session's changes
(Tempo control, Preset bar, Matrix page all added to the header/layout this
session). **Not resolved by session's end - resume from here.**

**What was tried, in order:**
1. Measured the user's open window via `GetClientRect` (PowerShell/user32.dll
   P/Invoke, the same technique used for this project's original window-size
   calibration) - got 1395x750, set `PLUG_WIDTH`/`PLUG_HEIGHT` to that, rebuilt.
   Scrollbar still present.
2. Asked the user to manually widen further until the scrollbar visually
   disappeared, re-measured: 1402x780. Set that, rebuilt. Scrollbar still present
   (user reported "まだ出てます").
3. User hypothesized the real problem might be excess *right margin*, not
   insufficient width. Got a screenshot confirming: large empty space visible on
   the right side of the window, yet a scrollbar still showing at the bottom -
   confirming excess scrollable width, not insufficient visible space.
4. Measured precisely via DevTools:
   `document.getElementById('scroll-container').scrollWidth` (1544) vs
   `clientWidth` (1403) - content genuinely needs ~1544px, not just what
   GetClientRect had measured. Set `PLUG_WIDTH=1550` (1544 + 6px safety margin),
   rebuilt. **Scrollbar still present** - and now the window looked visibly
   smaller on screen than before.
5. Checked the actual monitor resolution: `[System.Windows.Forms.Screen]::
   PrimaryScreen.WorkingArea` = **1536x864** - the requested 1550px width
   *exceeds the physical monitor's own width*, so Windows was clamping/shrinking
   the actual window to fit the screen, undermining the fix. This is the same
   class of issue as the project's older "Known Issue #12" (2026-07-21, window
   larger than the display).
6. Asked the user how to handle the monitor-width ceiling; **user pushed back**:
   didn't think the content should actually need that much width in the first
   place - suspected the 1544px `scrollWidth` measurement itself was inflated by
   something wrong, not a genuine content requirement.
7. Investigated via targeted DevTools queries (`getBoundingClientRect().right`
   across all header/page-synth descendants) and **found the real bug**:
   `.header-right-group` (wraps the Tempo chip + Level meter, added earlier this
   session) had `right: 1692.2`, while its own actual children only reached
   `right: 1334.5` - roughly **360px of invisible, unaccounted-for margin**.
   Traced this to `AlignMeterToContentEdge()` (existing function, aligns the
   meter's right edge with the Synth page's rightmost real content column): it
   computes `gap = header.getBoundingClientRect().right - targetRect.right` (where
   `header` was `.page-header`) and applies that as `marginRight` directly on
   `.meter-container`. Since `.meter-container` is now nested inside
   `.header-right-group` (a restructuring from earlier this session, for the
   Tempo chip), and the `meterAlignObserver` `ResizeObserver` watches
   `.page-header` itself, writing a margin onto a `.page-header` descendant grows
   `.page-header`'s own intrinsic width - which is exactly what the observer
   watches - creating a self-referential feedback risk. This lines up with the
   user's own hypothesis ("右マージンを大きく取り過ぎてるんじゃないでしょうか")
   almost exactly.

**Fix attempted** (in `index.html`): changed `AlignMeterToContentEdge()`'s
`header` reference from `.page-header` to `#scroll-container` (the actual
viewport-bound scrolling box - `width:100%`, `overflow:auto`, shouldn't grow just
because a descendant's margin changes), and changed
`meterAlignObserver.observe(...)` from watching `.page-header` to watching
`#scroll-container` instead (kept observing `.panel-amplfo` for Zoom-driven
changes, unchanged).

**Result: no change at all.** Relaunched Standalone (HTML-only change, no
rebuild needed) and re-measured - `scrollWidth`/`clientWidth` and the
`.header-right-group` vs `.meter-container` `right` values came back **exactly
identical** to before the fix (1696.2 vs 1334.5, essentially the same numbers as
the pre-fix 1692.2 vs 1334.5, modulo the different window width in between
attempts). Confirmed the edit genuinely landed in the file (grepped
`index.html` for the new `const header = document.getElementById(...)` line -
present). **This means either**: (a) the `#scroll-container` reference doesn't
actually resolve to a stable/different value than `.page-header` did (possible if
`<body>`/`<html>` themselves are *also* growing to accommodate the overflow,
making `#scroll-container`'s own `100%` resolve to the same inflated number `.page-header`
was already giving), or (b) `AlignMeterToContentEdge()` isn't actually the
(sole) source of the extra margin at all, and the real cause is still
unidentified. **Was about to ask the user to directly inspect
`getComputedStyle(...).marginRight` on both `.header-right-group` and
`.meter-container`, plus `#scroll-container`'s own `getBoundingClientRect().right`,
to disambiguate - session ended before getting that result.**

**Resume here**: get that `getComputedStyle` diagnostic result first (the exact
JS is in this session's own transcript, search for "meter marginRight" in the
conversation, or just re-derive: check whether `.meter-container`'s *computed*
`marginRight` alone accounts for the ~360-560px gap, and separately confirm
whether `#scroll-container`'s `getBoundingClientRect().right` is genuinely
different from `.page-header`'s own value on this window size - if they're the
*same* number, the reference-point fix was a no-op by construction and a
different fix is needed, e.g. applying the alignment margin to
`.header-right-group` instead of `.meter-container`, or reconsidering whether
`AlignMeterToContentEdge()` should exist in its current form at all now that
`.header-right-group` already carries `margin-left:auto` for its own
right-alignment (the two mechanisms may simply be fighting each other).
`PLUG_WIDTH`/`PLUG_HEIGHT` are currently `1550`/`780` in `config.h` - almost
certainly **not the final correct values**, don't treat them as settled; the
real fix is in the CSS/JS layout bug above, not the window-size constant, and
the window size should be revisited only *after* the margin bug is actually

## Horizontal scrollbar - second fix attempt: margin moved to `.header-right-group` (2026-07-29, new session)

Resumed from the entry above without the queued `getComputedStyle` DevTools
result (new session, previous session's remaining diagnostic never got an
answer). Instead of waiting on another live measurement, reasoned through the
CSS statically (confirmed by re-reading `.header-right-group`'s own rule,
`index.html` ~line 735: `display: inline-flex; ... margin-left: auto;`) and
found the actual bug:

`.header-right-group` is an auto-sized flex container - its own width hugs its
children's content, and **a child's margin counts toward that content-fit
width calculation**. The previous fix (and the original code before it) both
wrote `marginRight` onto `.meter-container`, which is nested *inside*
`.header-right-group`. Every px of margin added to the meter therefore
inflated `.header-right-group`'s own intrinsic width by that same px - and
since `.header-right-group` is already flush-right via its own
`margin-left: auto` within `.page-header`, that growth had nowhere to go but
overflow `.page-header`/`<main>`/`#scroll-container` to the right. This
explains exactly why the 2026-07-29 (earlier session) reference-point fix
(`.page-header`→`#scroll-container`) measured "no change at all" - the
reference point was never the actual problem; the write target was.

**Fix**: moved the `marginRight` write from `.meter-container` to
`.header-right-group` itself (`AlignMeterToContentEdge()` in `index.html`, now
selects `.header-right-group` instead of `.meter-container`). A margin on a
flex item lives outside that item's own border-box and only affects its
position within *its own parent's* flex row (`.page-header`) - it does not
feed into the item's own content-width calculation the way a margin on a
*descendant* does. So `margin-left: auto` on `.header-right-group` simply
gives up `gap` px of the free space it would otherwise have claimed, shifting
the whole group (Tempo chip + meter, as one unit) left by that amount, with no
self-inflation possible.

**Not yet verified live** - this was reasoned through statically because the
file lives outside the sandboxed project folder used by this session's browser
tool (loading it there renders as a static, script-disabled snapshot, so the
JS couldn't be exercised directly). HTML-only change, no rebuild needed -
**next step: relaunch Standalone and confirm the scrollbar is gone.** If it
persists, re-run
`document.getElementById('scroll-container').scrollWidth + ' vs ' + document.getElementById('scroll-container').clientWidth`
in DevTools to see whether any overflow remains, and if so get
`getComputedStyle(document.querySelector('.header-right-group')).marginRight`
plus its `getBoundingClientRect()` to see whether the new margin is even being
applied/read back as expected. Only after the scrollbar is confirmed gone
should `PLUG_WIDTH`/`PLUG_HEIGHT` (currently 1550/780) be re-measured and
finalized.

**Confirmed working (2026-07-29, same new session)**: user relaunched
Standalone and confirmed the scrollbar was gone - the `.header-right-group`
fix above was correct. However the window was now genuinely too wide (empty
space on the right), since `PLUG_WIDTH`/`PLUG_HEIGHT` were still the old
1550/780 values measured against the buggy, overflow-inflated layout. User
manually narrowed the window to the snug size; measured via `GetClientRect`
(same PowerShell/user32.dll P/Invoke technique as before, found by process ID
this time since `FindWindow(null, "1st synth")` doesn't match - the actual
Win32 window title is "FirstSynth", not the `PLUG_NAME`/display name "1st
synth"): **1387 x 780**. Set `PLUG_WIDTH=1387`, `PLUG_HEIGHT=780` in
`config.h` (no artificial safety buffer added this time, unlike the earlier
+6px - the previous over-measurement came from the CSS bug, not from
measurement imprecision). Rebuilt both Standalone and CLAP successfully.
**Re-verified and CONFIRMED by the user (2026-07-29)** after the rebuild: no
scrollbar, no excess right-side empty space, at the default launch size.
**This bug is fully resolved.** `PLUG_WIDTH`/`PLUG_HEIGHT` = 1387/780 in
`config.h` is final.

## Display name unified to "FirstSynth" (was "1st synth") (2026-07-29)

While investigating the window-title mismatch above (`FindWindow(null, "1st
synth")` failed because the actual Win32 window title was "FirstSynth"), the
user asked whether the split between `PLUG_NAME "1st synth"` (config.h - used
for the WebView UI title, DAW plugin-list name, native dialog titles/filters)
and the hardcoded `"FirstSynth"` in `resources/main.rc` (Standalone window
CAPTION, exe FileDescription/InternalName/ProductName - written once by
iPlug2's project-duplication script, never synced with `PLUG_NAME`) causes any
real conflict. Answer: no functional/registration conflict (VST3/CLAP identity
is `PLUG_UNIQUE_ID`/`PLUG_MFR_ID` = 'hnve'/'EZAN', not the display string),
just a visible inconsistency across surfaces.

User decided to unify on **"FirstSynth"**, not "1st synth" - reasoning:
BespokeSynth has a known problem calling up/searching plugins whose names
start with a digit. Since `main.rc` already said "FirstSynth", this only
needed two edits: `config.h`'s `PLUG_NAME` → `"FirstSynth"`, and
`resources/web/index.html`'s `<h1>1st synth</h1>` → `<h1>FirstSynth</h1>`
(line ~1999; a comment referencing the old title in an unrelated bug-report
quote, ~line 127, was left as-is - historical record, not user-facing).
Rebuilt both Standalone and CLAP (Debug, both succeeded, 0 errors) - user
confirmed Standalone now shows "FirstSynth" consistently (window title + UI
`<h1>`).

**Not yet re-verified inside a real DAW** - `PLUG_UNIQUE_ID`/`PLUG_MFR_ID`
didn't change, so this should NOT register as a new/duplicate plugin, but a
DAW's plugin list may have the old name cached and need a manual rescan to
show "FirstSynth" instead of "1st synth". Check next time CLAP is tested in
REAPER/BespokeSynth (per this project's usual batched-DAW-testing workflow).

Also fixed the same session: `CLAP_DESCRIPTION` in `config.h` still said
`"1st synth - a simple polyphonic synth"` (found via a raw byte-string search
of the built `.clap`, since `strings` isn't available in this shell -
`[System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($path))`
+ regex works fine as a substitute) - changed to `"FirstSynth - a simple
polyphonic synth"`.

### DAW rescan results + a real VST3 install-path bug found along the way

**REAPER**: after rescanning, still showed "1st synth" - fixed by deleting
REAPER's CLAP cache ini (Options → Show REAPER resource path in explorer →
delete `reaper-clap-win64.ini`) and relaunching for a full fresh scan. Plain
"Re-scan" alone wasn't enough - it apparently only diffs the plugin directory
for new/removed files, not re-reads metadata of already-known plugin IDs.
**Confirmed fixed by the user.**

**Studio One**: also showed "1st synth" after rescanning, but investigation
revealed a genuinely different, more serious cause than DAW caching - **a
real packaging bug, not a DAW-side quirk**:

`config\FirstSynth-win.props` (added 2026-07-28, an override for
Renoise/BespokeSynth's need for the real system-wide VST3 folder) set
`VST3_X64_PATH`/`VST3_ARM64EC_PATH` to `$(CommonProgramFiles)\VST3`.
**`$(CommonProgramFiles)` resolves based on the bitness of the MSBuild.exe
process evaluating it, not the target platform being built.** The MSBuild.exe
this project's build workflow uses (found via `vswhere -find
MSBuild\**\Bin\MSBuild.exe`, i.e. `...\MSBuild\Current\Bin\MSBuild.exe`) is
the **32-bit** MSBuild - under it, `$(CommonProgramFiles)` silently means `C:\
Program Files (x86)\Common Files`, NOT the real 64-bit `C:\Program Files\
Common Files` that 64-bit DAWs (Studio One included) actually scan for VST3.
So every VST3 rebuild this whole session (and, near as can be told, possibly
since 2026-07-28 when this override was added) was being copied to the wrong,
wrong-bitness-labeled folder, while the real scanned folder
(`C:\Program Files\Common Files\VST3\FirstSynth.vst3`) kept a stale copy
(dated 2026-07-28) that predates today's renaming - explaining exactly why
Studio One kept showing "1st synth" no matter how many times it rescanned: it
was correctly reading a file that genuinely still said "1st synth", because
that file was never being updated at all.

iPlug2's own `common-win.props` already gets this right for the *other*
per-format paths - `VST2_X64_PATH` uses `$(ProgramW6432)\VstPlugins` and
`AAX_X64_PATH` uses `$(CommonProgramW6432)\Avid\Audio\Plug-Ins` -
`$(ProgramW6432)`/`$(CommonProgramW6432)` always resolve to the true 64-bit
paths regardless of the evaluating process's own bitness. The project-level
VST3 override just hadn't followed that same convention when it was written.

**Fix**: changed `FirstSynth-win.props`'s `VST3_X64_PATH`/`VST3_ARM64EC_PATH`
from `$(CommonProgramFiles)\VST3` to `$(CommonProgramW6432)\VST3`. Rebuilt
VST3 (`FirstSynth-vst3` target) - postbuild log confirmed
`VST3_X64_PATH "C:\Program Files\Common Files\VST3"` (no `(x86)`) this time,
and the file at that real path was verified byte-for-byte to now contain
"FirstSynth" (11 occurrences) and zero occurrences of "1st synth". Deleted the
stray, now-orphaned copy at the wrong `C:\Program Files (x86)\Common
Files\VST3\FirstSynth.vst3` path (user confirmed OK to delete) so it can't
cause confusion later. **Confirmed - user was able to see "FirstSynth"** in
Studio One after this fix (no separate rescan-cache workaround needed here,
unlike REAPER - the underlying file itself was simply wrong before).

**Worth remembering**: any other project-level `.props` override that uses
`$(CommonProgramFiles)`/`$(ProgramFiles)` directly (rather than the
`...W6432` variants) for an x64 install path has this same latent bug -
worth grepping for if similar "DAW won't pick up my rebuilt x64 plugin"
symptoms show up again elsewhere.

**BespokeSynth**: confirmed fixed with no extra work needed (loads via CLAP,
same install path REAPER uses, which was already correct).

### Renoise + Studio One (VST3): a THIRD copy of FirstSynth.vst3 was the real culprit

Both Renoise and Studio One load FirstSynth via VST3, and both kept showing
"1st synth" even after rescanning. Investigated each host's on-disk plugin
cache directly (with the user's permission, since these are live app data
files outside the project folder):

- **Renoise** (`AppData\Roaming\Renoise\V3.5.0\CachedVST3s_x64.db`, binary,
  UTF-16LE strings inside) had `EASYANDNICE: 1st synth` cached 3x.
- **Studio One 5** (the active installed version - confirmed via file
  mtimes; `Studio One 6`'s files were untouched since 2026-07-19)
  `AppData\Roaming\PreSonus\Studio One 5\x64\Plugins-ja.settings` (XML) had a
  `<ClassDescription classID="{F2AEE70D-...}" name="1st synth">` entry.

Closed both apps (verified via `Get-Process` first), renamed both cache files
to `.bak` (reversible, not deleted) to force a full rebuild. **Renoise fixed
immediately.** Studio One did NOT - after relaunch, the freshly-regenerated
`Plugins-ja.settings` now had **two** `<Section>` entries for FirstSynth under
*different* path-hash keys: one at `46C5CB9E/FirstSynth.vst3`
(`DateTime ... 2026/07/28 13:57:11`, `name="1st synth"`) and one at
`B8A53E68/FirstSynth.vst3` (`DateTime ... 2026/07/29 12:28:14`, matching
today's real rebuild, `name="FirstSynth"`) - i.e. Studio One was genuinely
finding **two different files on disk**, not just serving a stale cache.

Manually deleted the stale `<Section>` block from the settings file (same
"close app first, edit directly" approach) - **did not fix it**; user
confirmed still "1st synth" after another launch. Checking the file
afterward showed my edit had been silently overwritten - Studio One rewrites
this settings file from its own in-memory scan state on exit (mtime updated
to app-close time, "1st synth" entry back, byte-for-byte the same as before my
edit). This proved the stale entry was being *rediscovered on every launch*,
not just cached - meaning a real, second, genuinely-stale `FirstSynth.vst3`
file still existed somewhere on disk.

Full-system search (`find /c -iname FirstSynth.vst3`) found it:
**`C:\Users\a_wak\AppData\Local\Programs\Common\VST3\FirstSynth.vst3`**,
timestamped **2026/07/28 13:57:11** - matching the stale cache entry's
`DateTime` *exactly*. This is iPlug2's **original default** VST3 install
location (`common-win.props`: `$(LOCALAPPDATA)\Programs\Common\VST3`), from
*before* the 2026-07-28 `FirstSynth-win.props` override (see the
`$(CommonProgramFiles)` vs `$(CommonProgramW6432)` bug entry above) redirected
builds elsewhere. It was never cleaned up when the override was added, and
apparently is still one of Studio One's configured VST3 search locations, so
every scan kept re-finding this genuinely stale binary and re-adding its old
name - no amount of cache-clearing alone could fix that, since the cache was
being correctly rebuilt *from a real stale file*, not from stale metadata.

**Fix**: deleted that leftover `AppData\Local\Programs\Common\VST3\
FirstSynth.vst3` folder (user confirmed OK), then re-applied the same manual
`<Section>` removal to `Plugins-ja.settings` once more (this time it stuck,
since the source file triggering its regeneration was gone). **User confirmed
fixed in Studio One after this.**

**Takeaway for future "DAW won't show my updated plugin" issues on this
project**: check for stray old copies at iPlug2's *original* default install
paths (`$(LOCALAPPDATA)\Programs\Common\{VST3,CLAP}`) in addition to the
project's current override paths - the 2026-07-28 VST3 path override left
exactly this kind of orphan behind, and it can persist for a long time since
most hosts don't warn about or auto-prune newly-empty search locations.

## Looper gauge overflowing past the visible frame in Studio One (not Standalone) (2026-07-29)

User reported the Looper page's recording/playback gauge looked like it could
"record infinitely" in Studio One (VST3) - the recording state visually never
seemed to finish, gauge always looked cut off past the right edge. Standalone
never showed this.

**Ruled out via careful reading of `FirstSynth_Looper.h`'s `Process()`**: the
40-second cap (`kMaxLoopSeconds`) logic is sound and host-agnostic - `mWritePos`
is bounds-checked every sample and correctly transitions Recording->Playing.
Confirmed via user testing that the actual loop DOES complete and repeat
correctly at 40s even in Studio One - only the on-screen gauge looked wrong.

**Real cause, found via a live DevTools session inside Studio One's embedded
WebView** (right-click -> Inspect works there) plus two screenshots comparing
a wide vs. narrow docked panel width: `.panel-looper-gauge` uses plain
`width: auto` CSS with a fixed `margin-right: 56px` (compensating for `<main>`'s
asymmetric 60px-left/4px-right padding) - unlike *every other* page/element in
this UI (`.layout-grid`'s max-content columns, the header's
`AlignMeterToContentEdge()`-aligned meter), which deliberately do NOT stretch
to fill a wider-than-content window. Standalone's window is now sized snugly
to content (1387x780, see the earlier window-size entry above), so there was
never any "extra" width for the gauge to stretch into - the bug was latent but
invisible. Studio One's dockable Instrument-editor panel can be resized much
wider than that snug size, and the gauge - being the only element that
literally fills 100% of the raw available width instead of capping at the
same "content width" everything else respects - stretched out past where all
other content visually stops, looking like it "escapes the frame" (though it
never actually exceeded its own CSS box - `getBoundingClientRect()` measurements
confirmed no real overflow, e.g. gauge 1303px < main's content 1399px at the
time it was checked, yet still visibly wrong on-screen - the mismatch was
between the user's *visual* frame of reference, defined by every other page
element's narrower content-width, and the gauge's own honestly-wider raw box).

**Fix**: reused the same "gap" (how much wider the window is than the Synth
page's natural content, already computed in `AlignMeterToContentEdge()` for
the header meter) and applied it as extra `margin-right` on
`.panel-looper-gauge` too (on top of the original 56px baseline), so the gauge
now stops at the same content edge as everything else instead of stretching
to fill an arbitrarily wide DAW-hosted panel. Since Standalone's window keeps
gap≈0, its appearance is provably unchanged (`56 + 0 = 56`, the original
value) - user confirmed no visible change there after the fix.

**Follow-up bug found immediately after the first fix**: the gap can only be
*measured* while the Synth page (`.panel-amplfo`) is actually visible (it's
`display:none`-hidden on other pages, collapsing its rect to zero - see
`AlignMeterToContentEdge()`'s own long-standing comment on this). A session
that reopens the editor directly on a non-Synth page (this project's editor
persists last-active page, and the user habitually leaves it on Looper) would
initially show the plain CSS fallback (56px, uncorrected) until the user
happened to click through to Synth and back - user confirmed exactly this:
"最初に新しく開けたときはゲージが見切れている...何度か他のページに行き、また
戻ってくると枠内に収まっている". Fixed by persisting the last-computed gap to
`localStorage` (`firstSynthContentGapPx`, mirroring the existing
`firstSynthUIScalePercent` UI-zoom-preference pattern) and applying that cached
value immediately at startup (`ApplyCachedContentGap()`, called right before
`AlignMeterToContentEdge()`'s own real-measurement calls) - so a returning
session in the same host/panel-size context gets a correct margin from the
very first frame, refined immediately for real once/if the user visits Synth.
A genuinely first-ever launch (nothing cached yet) just keeps the original
CSS-fallback behavior, no worse than before either of these fixes.

**Confirmed working by the user**: gauge now correctly bounded on first open
in Studio One (no page-visiting dance needed), and Standalone's appearance is
unchanged. All HTML/JS-only changes (`resources/web/index.html`), no rebuild
needed for either fix.

## BEFORE DISTRIBUTION CHECKLIST (added 2026-07-29, read before first real release)

**VST3 licensing (2026-07-29 research, see conversation with user around this
date for full detail):** `THIRD-PARTY-NOTICES.txt` (project root) has the
Steinberg VST3 SDK's MIT license text + copyright notice, verbatim-copied from
`iPlug2/Dependencies/IPlug/VST3_SDK/LICENSE.txt`. **This file must actually be
included in whatever installer/zip ships to users** — having it sit in the repo
does not satisfy the license on its own; the obligation is only met once it's
bundled with the distributed package. Check this every time a release package
is built. If the VST3 SDK is ever updated to a newer version, quickly diff its
`LICENSE.txt` against this file's copy in case Steinberg changes the terms
(unlikely, but cheap to check).

**ASIO licensing — submission process now known (2026-07-29), still needs to
actually be done:** the real ASIO SDK zip (`www.steinberg.net/asiosdk` →
`ASIO-SDK_2.3.4_2025-10-15.zip`, no login needed) contains
`Steinberg ASIO Licensing Agreement.pdf` (Version 2.3.4, 2025-10-15). Its
last page (8/8) is a signature block: Steinberg's side is pre-filled with
**Email: reception@steinberg.de / Fax: +49 40 21035 300**; the Licensee side
just needs Company/Organization, Represented by, Address, and a Technical
Contact filled in, then signed. **Submission = fill in that page, sign, and
email the PDF to reception@steinberg.de** (fax as an alternative) — no web
portal, no forum post needed. (Note: Steinberg's own
`steinberg.net/developers/prorietary-sdk/` page has a broken/mislabeled "ASIO
Licensing Agreement" link that actually points to the *GameAudioConnect* SDK's
agreement, not ASIO's — don't trust that link, use the copy bundled inside the
actual ASIO SDK zip instead.) Company info to use: the
[[project-easyandnice-instruments-brand]] legal/business name. **Do not ship
an ASIO-enabled build to customers until this signed agreement is actually
sent and countersigned.**

**Status update 2026-07-29: agreement signed and emailed to
reception@steinberg.de.** Filled fields: Company/Organization "EASYANDNICE
INSTRUMENTS", Represented by / Licensee By "Akifumi Wakisaka", Title
"Representative", Date executed "29 July 2026", handwritten Printed
Signature, Technical Contact (same name, Kyoto address, phone
+81 80-6760-8198, email easyandnice@ymail.ne.jp). **Now waiting on
Steinberg's countersigned copy** — dev/test use of ASIO is fine in the
meantime, but still don't ship an ASIO-enabled build to customers until the
countersigned copy actually comes back. Followed up once (~2 weeks later,
same email thread) - still no reply as of the last check.

**2026-08-30 update: this delay is very likely a systemic Steinberg-side
backlog, not a mistake on our end.** Checked Steinberg's forums
(`forums.steinberg.net`) and found two independent threads confirming the
same pattern: "No response to Proprietary ASIO SDK License Agreement
submission" (submitted July 25, followed up Aug 6, still no reply as of that
post) and "ASIO SDK Licensing Agreement Question" (a different developer who
also emailed Steinberg directly and got no response, ~1 day before this
check). Also checked Steinberg's Help Center
(`helpcenter.steinberg.de`) — its "Contact support" ticket flow is scoped to
registered end-user product support (Cubase/Nuendo/hardware, requires a
MySteinberg-registered product) and has no SDK/developer-licensing category,
so filing a ticket there is unlikely to reach the right team - **not
recommended as a next step**. Best current plan: keep waiting (plausible
cause is the post-Oct-2025 relicensing surge overwhelming a small licensing
team), periodically check that forum thread for signs the backlog is
clearing, and only send another follow-up email after a longer gap rather
than escalating channels.

**RESOLVED 2026-08-19/2026-08-30: ASIO proprietary license is now fully
executed.** Steinberg's reply had actually arrived 2026-08-19 (sent by
"Svantje", Management Assistant, from a personal address rather than
reception@steinberg.de - landed in the spam folder and was missed until this
check) with the countersigned PDF attached. Verified by rendering the
countersigned copy's page 8: Steinberg's side now shows **Date executed
13.08.2026** and a handwritten signature from Clyde Sendke (Managing
Director); Licensee side unchanged from what was sent. **The ASIO proprietary
SDK license agreement between Steinberg and EASYANDNICE INSTRUMENTS is
complete — ASIO-enabled builds can now be distributed to customers.** Final
countersigned copy saved at
`C:\Users\a_wak\OneDrive\デスクトップ\easyandnicewaki\private\VSTとASIOについて\ASIO_LicensingAgreement_EASYANDNICE_NSTRUMENTS_Japan_August2026  Approved.pdf`
- keep this file permanently, it's the actual legal record. **Lesson for any
future Steinberg correspondence**: their reply may come from a named staff
member's personal address rather than the department alias you emailed
(reception@steinberg.de here) - check spam/junk folders by content, not just
by expected sender, if a reply seems overdue.

**IMPORTANT — mandatory ASIO attribution requirement found in the signed
agreement's §3 (2026-08-30), stricter than the general SDK README's "optional"
framing:** now that this proprietary agreement is signed and FirstSynth is
distributed as an "ASIO Driver Compliant Product", §3.1 makes displaying
"ASIO" + Steinberg's copyright notice **mandatory**, not optional. Must appear
in at least one of: an About Box, a startup/splash screen, or bundled
documentation. Exact required copyright notice text (§3.1.k, verbatim):
**"ASIO is a trademark of Steinberg Media Technologies GmbH, registered in
Europe and other countries."** Also: any webpage mentioning "ASIO" (e.g. a
feature list on easyandnicewaki.com) needs the same notice; "ASIO" must never
appear in the product/company's own name itself (only descriptive phrases
like "ASIO compatible" in plain, unstyled text); don't claim ASIO support
where it doesn't exist; don't redistribute the raw SDK. **DONE (2026-08-30): satisfied via the "bundled documentation" option**
(least visible of the 3 compliant placements - matches this project's own
"don't surface internal plumbing in the UI" convention, see the
PEQ-not-user-facing precedent in [[project-firstsynth-clap-plugin]]) - added
an "ASIO" section to `THIRD-PARTY-NOTICES.txt` right alongside the existing
VST3 MIT section, with the exact required copyright notice text and a
reference to the signed agreement. No UI or website change was needed. If
this product's website (easyandnicewaki.com) copy ever explicitly advertises
"ASIO support" as a feature, that specific page would separately need the
same notice too (§3.1.c) - not yet an issue since the site doesn't currently
call out ASIO by name, but keep this in mind if that copy changes.
fixed and the true required content width is known.

## Preset browser: Save (overwrite) + Delete added, ported back from SuiKinKutsu (2026-07-30)

[[project-suikinkutsu-plugin]] ported this project's cross-format preset browser
(2026-07-28 entry above) on 2026-07-30, and while testing it there the user asked
for delete + overwrite-in-place too - implemented there first, then ported back
here the same day since it's the same underlying feature and code.

**C++**: new `kMsgTagPresetDelete` in `FirstSynth.h`'s `EMsgTags` (appended as
16, after the existing `kMsgTagPresetList`=13/`kMsgTagPresetSave`=14/
`kMsgTagPresetLoad`=15 - per this project's "never renumber" convention). New
`DeletePresetByName()` (`FirstSynth.cpp`) - same sanitize-then-build-path pattern
as Save/Load, `std::filesystem::remove()` (silently no-ops if the file's already
gone), refreshes the list. `OnMessage()` now also handles `kMsgTagPresetDelete`.
Overwrite needed no new C++ - `SavePresetAs()` already overwrites in place when
given an existing name.

**WebView UI**: two new buttons in `.preset-bar` - "Save" (overwrites the
selected preset directly, no prompt) and "Delete" (removes it, `confirm()`-
guarded since it's destructive with no undo). Refactored the UTF8-encode-to-
base64 logic shared by Save/Save As/Load/Delete into one
`SendPresetNameMessage(msgTag, name)` helper.

**Also fixed a real, previously-just-cosmetic bug in `PrepareForPresetReselect()`
while porting this**: that function (added 2026-07-28, blanks the `<select>`'s
`selectedIndex` on every dropdown-open so re-picking the same option still fires
`onchange`) was already known to leave the dropdown showing blank if the user
opened it and backed out without picking anything - at the time this was
explicitly called out as an "accepted edge case" since nothing depended on
`select.value` staying populated. That stopped being true once Save/Delete were
added (both need to know "what preset is currently loaded"), so it would have
silently broken them here too, exactly as it did in SuiKinKutsu before being
caught and fixed there. Fixed the same way: a new `currentPresetName` JS variable
tracks the actually-loaded/saved preset independently of the `<select>`'s own
transient blanked state; `SaveCurrentPreset()`/`DeletePreset()` key off that
instead of `select.value`; a new `onblur` handler
(`RestorePresetSelectionIfBlank()`) re-syncs the dropdown's visible selection
back to `currentPresetName` if the user closes it without picking anything,
fixing the visual blank too.

Rebuilt all three formats (Standalone/CLAP/VST3), compiled clean, no errors.
**Not yet tested live in this project** - only confirmed working in SuiKinKutsu
so far, since that's where this was actually developed/verified this session;
worth a quick round-trip test here (Save overwrite, Delete, and the
open-dropdown-without-picking scenario) next time this project is opened.

## VST3 editor crop/scrollbar investigation, 2026-07-30 - paused, one hypothesis ruled out

Distribution-prep review picked this project's long-paused "editor opens cropped"
thread back up, using the DPI-clamp fix landed in [[project-suikinkutsu-plugin]]'s
`IPlugWebView_win.cpp` earlier the same day (this machine's own display is at 125%
scaling, same condition that caused SuiKinKutsu's bug, so it was a reasonable guess
that this was the same root cause). Live-tested in Renoise.

**Real symptom, confirmed via screenshot**: at a narrow-ish window size (~900-1000px
CSS width), PITCH LFO's rightmost knobs are genuinely off-screen with no way to
reach them - not "doesn't need a scrollbar," an actual missing horizontal
scrollbar despite real content overflow (`#scroll-container`'s CSS is plain
`overflow: auto`, which should show one). Separately, the Zoom dropdown showing 80%
on a fresh Renoise open is NOT this bug - that's the already-documented
shared-WebView-cache Zoom-bleed issue (see this file's "VST3 confirmed working"
2026-07-28 entry) reproducing again, unrelated.

**Repro pattern the user found**: reopen the editor -> both scrollbars present
(content reachable). Manually widen the window to where everything fits -> both
scrollbars disappear (as expected, no overflow at that size). Shrink the window
back down again -> scrollbars **stay gone** even though the content should overflow
again at the smaller size - some content becomes genuinely unreachable until the
editor is closed and reopened, which resets it back to "both bars present."

**Diagnostic logging added temporarily** (`SetWebViewBounds` in the shared
`IPlugWebView_win.cpp`, writing every call's inputs/parentRect/clamped-output to
`%TEMP%\vst3_size_debug.log`) and the exact repro replayed. **Key finding: at the
moment the user observed the stuck-missing-scrollbar bug, the log showed the C++
clamp code had already computed and applied the correct, fully up-to-date shrunk
bounds (1139x622) to WebView2** - so this is NOT the DPI-clamp-race hypothesis
(that race does occur in the log a few times - `GetClientRect` occasionally reads a
stale, mid-transition parent rect that gets corrected by a near-simultaneous second
call - but it self-corrects and isn't the same bug as this one). The real bug is
downstream: **WebView2/Chromium itself isn't repainting/recomputing scrollbars for
its new, correctly-set, smaller viewport** after a shrink - the content's own CSS
overflow state goes stale relative to the control's actual bounds.

**One experimental fix tried and reverted (didn't help, confirmed by the user
live)**: nudging `SetBoundsAndZoomFactor` by setting bounds 1px narrower then
immediately back to the real target, on the theory that forcing two distinct native
resize calls (rather than one) would break a coalesced/dropped layout pass - a
known workaround for this class of embedded-Chromium staleness in other apps.
Rebuilt, retested live in Renoise with the same open/widen/shrink sequence - **no
change**, still reproduces. Reverted immediately (both the nudge and the temp
diagnostic logging) rather than leave ineffective/debug-only code in this shared
framework file - `IPlugWebView_win.cpp`'s `SetWebViewBounds` is back to exactly its
post-DPI-clamp-fix state, no diagnostics, all three FirstSynth targets rebuilt
clean against that baseline afterward.

**Status: paused by the user's own choice** ("これはちょっと置いておきましょう"),
same category as the earlier `PLUG_HOST_RESIZE` experiment - a real, reproducible,
still-open bug, not fixed. **Next time this is picked up**: the WebView2-side
staleness angle is the confirmed-correct direction (ruled out: DPI-clamp race, the
1px-nudge trick) - worth researching WebView2-specific APIs for forcing a
layout/paint refresh after `put_Bounds`/`SetBoundsAndZoomFactor` (e.g.
`ICoreWebView2Controller::NotifyParentWindowPositionChanged`, or toggling
`put_IsVisible` briefly) before trying another blind experiment. Also worth trying
whether this reproduces in CLAP/REAPER too (not yet tested) - if it's VST3-only
like SuiKinKutsu's DPI bug was, that narrows the search further.

## Quick-start manual (Japanese + English) drafted, 2026-08-01

Same distribution-prep task as [[project-suikinkutsu-plugin]]'s manual (built the
same day that project's was finished). Built with `python-docx` (see
[[feedback-docx-generation-no-node]]), quick-start scope only (install, basic
operation, one-line-per-section for all 4 pages, presets - not a full parameter
reference). Deliberately does **not** mention the 5-band PEQ or its PEQ LOCK
toggle anywhere, per this file's own earlier "PEQ is not user-facing marketing/UI
content" entry - confirmed via a fresh Standalone screenshot that the EQ knobs
themselves are no longer shown in the Effects page UI at all now (only the PEQ
LOCK toggle remains visible) - consistent with the user's stated eventual goal of
hiding it entirely.

Four real Standalone screenshots (Synth/Effects/Matrix/Looper pages, saved by the
user directly into `resources\` as `sc_synth.png`/`sc_fx.png`/`sc_mtrx.png`/
`sc_looper.png`, copied into a new `manual\` folder) were embedded, one per page.
Files: `FirstSynth 取扱説明書.docx` and `FirstSynth User Manual.docx` (English,
translated after incorporating the user's edit to the Japanese draft - Yuragi
randomizes both tone *and* stereo position/pan, not tone alone). No PDF produced -
no LibreOffice/pandoc on this machine, user will export PDF themselves from Word.

This closes out both planned manuals from the 2026-07-30/31 distribution-prep
sessions (SuiKinKutsu done first, FirstSynth here) - see
[[project-easyandnice-instruments-brand]] for the broader distribution-prep
sequencing (packaging/installer work still waits on Ito's subscription-auth
implementation; preset-mechanism consolidation still waits on the user finishing
the factory preset set).

## Preset dropdown bug: playing a note right after picking a preset could spam-reload presets - FOUND AND FIXED (2026-08-01)

**User report**: right after selecting a preset from the dropdown, holding down a
computer-keyboard note key (e.g. `S`) while moving the mouse made "the GUI go
haywire" - many knobs visibly spinning/changing on their own.

**Investigation**: added temporary diagnostic logging to
`IPlugWebView_win.cpp`'s `SetWebViewBounds` first (wrong track, see the crop-
investigation entry above - reverted, unrelated to this bug). Then added a
document-level capturing `pointerdown` logger plus a `startDrag`-fired logger to
`knob-control.js` (this project's own copy under `resources/web/`, which already
had leftover 2026-07-28 diagnostic logging from an unrelated "first grab needs a
second touch" investigation) and had the user reproduce with DevTools Console
open (`SetEnableDevTools(true)` in `FirstSynth.cpp` already enables F12/Inspect
in Standalone). The captured console log showed **no `startDrag` ever fired** -
so no knob was ever actually being dragged - ruling out a stuck-drag-listener
theory. The log did show many distinct presets' full ~110-param dumps repeating,
but this initial capture also just reflected the user's own prior manual preset
browsing (`console.log` has no timestamps and Console retains history since
opened), so it wasn't conclusive on its own.

**Root cause, found by the user themselves through careful observation**: a
native HTML `<select>` keeps keyboard focus after a selection is made, and while
focused it independently implements browser-native "type-ahead" - pressing a
letter key jumps the selection to the next option whose text starts with that
letter, completely separately from any page-level keydown listener. This
project's `EnableComputerKeyboardInput()` (`index.html`) plays a note on `KeyS`
(among others) via a `document`-level keydown listener, but never moves focus
off the preset `<select>` after `LoadPreset()` runs - so every subsequent press
of a note key that also matched the new preset's first letter **both played a
note AND cycled the browser's native select to the next same-starting-letter
preset**, firing `onchange` -> `LoadPreset()` again. Holding a note key while
"playing" therefore spammed rapid, repeated preset loads - every knob's value
jumping between whatever those presets contained, on every letter-key repeat -
which is what actually looked like "the GUI running wild." Not related to mouse
movement at all in the end (the user's earlier framing that mouse movement was
required was a red herring from how they were testing, not a real requirement).

**Fix**: `LoadPreset(name)` in `index.html` now calls
`document.getElementById('presetSelect').blur()` right after sending the load
message, handing keyboard focus back to the page so only the note-key listener
sees subsequent keystrokes. Confirmed fixed by the user
("うまくいきました"). Rebuilt all three targets (Standalone/CLAP/VST3) with the
fix.

**Same bug existed verbatim in [[project-suikinkutsu-plugin]]** (identical
`LoadPreset()` ported from here 2026-07-30) - ported the same one-line fix there
too the same day, all three of its targets rebuilt. SuiKinKutsu has no
note-triggering keydown listener of its own, so it's less likely to have been
noticed there, but the underlying native-`<select>`-typeahead hazard is
identical and now closed in both projects.

## Old native Standalone Save/Load Preset dialog retired, File menu renamed to Options (2026-08-01)

User confirmed all sounds they'd made via the old native File>Save Preset
dialog have now been migrated into the cross-format preset browser, so the old
dialog can close out. This is the same "two disconnected preset mechanisms"
concern flagged back on 2026-07-24 as needing resolution before distribution -
now resolved here, same day [[project-suikinkutsu-plugin]] had this exact same
cleanup done first (same request, applied there first this session, this
project second using that as the recipe).

**Removed** `ID_SAVE_PRESET`/`ID_LOAD_PRESET` from `resources/resource.h` (their
handler code in the shared `iPlug2\IPlug\APP\IPlugAPP_dialog.cpp` is gated on
these macros being defined, so removing the defines here is enough - no shared
framework file touched) and removed the two corresponding `MENUITEM`s from
`resources/main.rc`'s File menu. With only Preferences/Quit left, renamed
`POPUP "&File"` -> `POPUP "&Options"` in the same file (matching SuiKinKutsu's
same rename). Rebuilt Standalone (the only format with this native menu) -
compiles and links clean.

Note: same tradeoff as SuiKinKutsu applies here - the native dialog let you
save/load to any arbitrary file location (useful for sharing a preset file
directly), the cross-format browser only reads/writes its own fixed
`AppData\Local\FirstSynth\Presets\` folder. Not raised as a concern this
session; presets there are plain files, so Explorer copy/paste covers the same
use case.

**New packaging requirement noted for later (2026-08-01, not yet actioned)**:
user wants the presets now living in the cross-format browser's folder
distributed as factory/initial presets when FirstSynth ships - i.e. the
installer should seed `AppData\Local\FirstSynth\Presets\` with these files
before first run, rather than a new install starting with an empty preset list.
No technical blocker (they're plain files in a fixed folder already), but there
is no installer/packaging pipeline at all yet - see this file's "BEFORE
DISTRIBUTION CHECKLIST" and [[project-easyandnice-instruments-brand]]'s
distribution-prep sequencing notes: packaging work itself is on hold pending
Ito's subscription-auth implementation. Revisit this preset-seeding requirement
once that packaging work actually starts. Same requirement almost certainly
applies to [[project-suikinkutsu-plugin]] too, same architecture.

## New Bend Range knob added to Mixer panel (2026-08-01)

User asked for a knob under the Mixer panel to set pitch bend range in semitone
steps up to an octave. The underlying `iPlug2` `MidiSynth` (shared framework,
`Extras/Synth/MidiSynth.h`) already has `SetPitchBendRange(int)` for exactly
this (default `kDefaultPitchBendRange = 12`, i.e. this synth has always
silently bent a full octave - just never had a control for it), so this was a
small, low-risk addition rather than new DSP.

**Added**: `kParamPitchBendRange` (appended to `EParams` per this project's
"never renumber" convention - lands at param index 110, the 111th param).
`GetParam(...)->InitInt("Bend Range", ..., 0, 12, "st", ..., "MIX")` in the
constructor (`FirstSynth.cpp`) - same "MIX" group as the other Mixer-panel
knobs. `IPlugInstrumentDSP::SetParam()` (`FirstSynth_DSP.h`) forwards it
straight to `mSynth.SetPitchBendRange(static_cast<int>(value))` - no new
per-sample DSP needed, the framework already applies it correctly. New knob in
`index.html`: a second `.knob-row` inside `.panel-mixer`, appearing below the
existing Osc1/Osc2/Noise row (`integer-display step="1"`, matching the
Octave/Semi knobs' style).

Initial default was set to 12 (matching the framework default / prior silent
behavior, so adding the knob wouldn't change any existing preset's sound) -
confirmed working live by the user, then **changed to 2 semitones** (the
common whole-tone convention) per their follow-up request, once they'd verified
the control itself worked correctly.

**Follow-up same day: Bend Range's top edge aligned with Oscillator 2's.** User
asked for the Bend Range box's top edge to line up with Oscillator 2's. The
first attempt bundled it as a second `.knob-row` inside `.panel-mixer` itself,
which didn't line up with anything in particular. Restructured properly: the
Synth page's `.layout-grid` used named `grid-template-areas` where `mixer` (and
`pitchlfo`) spanned *both* the osc1 and osc2 rows as one merged cell - split
that so `mixer` now occupies only the osc1 row, and a new `bendrange` area
takes the osc2 row in that same column (new `.panel-bendrange` div, moved out
of `.panel-mixer`). That correctly aligned the two *boxes'* top edges to the
same grid line, but the user still reported "not aligned" - the real remaining
gap was that Oscillator 2 has a real `<h2>` heading before its knob-row (the
shared `h2` rule adds `margin: 14px 0 6px`), while the new Bend Range panel had
no heading at all, so its knob-row started right at the grid line while
Oscillator 2's visually started ~37px lower (after its heading). Fixed by
adding an invisible spacer `<h2 style="visibility: hidden;">&nbsp;</h2>` to
`.panel-bendrange` - reuses the real h2 element (just hidden) so the two
panels' visible content lines up exactly, and stays in sync automatically if
the shared h2 styling is ever changed later. **Confirmed aligned by the user.**
Pure HTML/CSS changes, no rebuild needed (WebView loads `index.html` from disk
in Debug).

## Initial Patch preset created (2026-08-01)

User asked for one "Initial Patch" preset. Since presets are just a raw
`SerializeState()`/`SerializeParams()` dump - `NParams()` consecutive native
little-endian `double`s (8 bytes each, no header), in exact `EParams` enum
order (confirmed by cross-checking a real saved preset's bytes against the
param list, each value landing sensibly within its param's declared range) -
built it directly with a Python script that regex-parses every
`GetParam(kParamX)->InitXxx("...", DEFAULT, ...)` call out of the constructor
in `FirstSynth.cpp`, maps `EParams` enum order from `FirstSynth.h`, resolves
the few symbolic defaults (`LFO<>::kTriangle`->0, `LFO<>::k1`->11, `true`/
`false`->1.0/0.0), and packs the result as a `.preset` file - i.e. this is
genuinely every param's *declared default value*, not a hand-tuned sound.
Saved as `Initial Patch.preset` in the shared presets folder. **Confirmed
appearing in the preset list and loading correctly by the user**, who then
edited it further themselves (twice) and will formally designate their edited
version as the "real" Initial Patch once satisfied - this file was just the
starting point.

**Reusable technique**: if a "build/inspect a preset file directly" need comes
up again (here or in [[project-suikinkutsu-plugin]], same file format), this
regex-based constructor-parsing script is the reliable way to get every
param's true default without manually transcribing 100+ values by hand (error-
prone) or needing to drive the actual app's UI (not possible from this
session). Whenever a new param is added, remember to regenerate any existing
`Initial Patch.preset` too - it silently falls one param short of `NParams()`
otherwise (harmless on load, `UnserializeParams` just stops early, but stays
technically stale). Regenerated once already, right after adding
kParamVelocityCurve (896 bytes now, was 888).

## Velocity Curve knob added, right of the Matrix panel (2026-08-01)

User asked for a single continuous knob morphing the Modulation Matrix's
Velocity source's response from exponential through linear/proportional to
logarithmic. Added `kParamVelocityCurve` (appended, param index 111 - the
112th param), range -100..100%, in the "MATRIX" group.

**Curve math** (`FirstSynth_DSP.h`'s `Voice::Trigger()`): a continuous power
curve, `exponent = 4^-mVelocityCurve` (where `mVelocityCurve` is the param
value/100, i.e. -1..1) applied as `mVelocity = pow(level, exponent)`. At -100%
(exponential), exponent=4 (soft notes disproportionately quiet); at 0%
(linear, matches the old plain `mVelocity = level` behavior exactly); at
+100% (logarithmic), exponent=0.25 (soft notes disproportionately loud). Per-
voice member `mVelocityCurve`, pushed via the same `mSynth.ForEachVoice(...)`
pattern this project already uses for Yuragi/Filter Key Follow/etc. Since
`mVelocity` is Matrix-source-only (doesn't scale the Amp/Filter envelopes
directly - see that field's own long-standing comment), this curve only
affects however the Velocity source is actually routed in the Matrix (e.g.
the Velocity->Amp Level fix from earlier today), not envelope depth directly.

**UI**: new `.panel-velocitycurve` div, placed as a sibling to `.panel-matrix`
inside the same `.effects-row` (a flex row) on the Matrix page - lands
immediately to the right of the Matrix grid, matching the user's request. One
knob, "Curve", `signed-display` (param-id 111).

Confirmed working live by the user (soft-touch loudness response audibly
changed with the knob). Default changed from the initial 0 (linear) to
**-37.6%** (leaning toward exponential) per the user's own tuning, once they'd
confirmed the full range worked. `Initial Patch.preset` was regenerated to
include this param at 0.0 (its pre-tuning default at the time) - note this is
now stale relative to the -37.6% constructor default; not re-regenerated a
second time since the user said they're still editing Initial Patch by hand
anyway.

## Matrix Env Time destinations added: Amp/Filter/Mod Env 1/Mod Env 2 (2026-08-01)

User asked for a Matrix destination scaling each envelope's Attack/Decay/
Release together (Sustain untouched, it's a level not a time) - confirmed all
four envelopes (Amp, Filter, Mod Env 1, Mod Env 2) should get one. Appended 4
new `EMatrixDest` values (`kMatDstAmpEnvTime`/`kMatDstFilterEnvTime`/
`kMatDstModEnv1Time`/`kMatDstModEnv2Time`, `FirstSynth_DSP.h`) and their
option strings across all 8 Matrix slots' Dest `InitEnum` lists
(`FirstSynth.cpp`) and `<select>` dropdowns (`index.html`, `replace_all` since
the list is identical in all 8 places).

**Implementation**: `ADSREnvelope::Start()`/`Retrigger()` already accept a
`timeScalar` argument (comment: "for key-follow scaling" - built for exactly
this, previously unused in this project). `Voice::Trigger()` now does a small,
separate matrix accumulation pass (NOT the same per-sample one every other
destination uses - envelope timing is only meaningful once, at
Start()/Retrigger(), not re-readable per-sample) using whatever source values
are actually available at that instant, and passes the resulting scalar into
each envelope's Start()/Retrigger() call. `+-100%` amount = 4 octaves (16x
slower/faster) - started at 2 octaves (4x), widened same session (see below).

**Known limitation, documented not silent**: Mod LFO1/2 are DSP-level (shared
across all voices, rendered into per-block buffers), not reachable from this
per-voice `Trigger()` without new plumbing this project doesn't have - routing
either to an Env Time destination silently contributes 0. Velocity, Key
Follow, Mod Wheel, and Mod Env 1/2 (via their own `GetPrevOutput()`) all work.

**Follow-up, same session - real stale-pitch bug found and fixed via
Key Follow -> Env Time.** User reported the same key produced a different
Decay time on repeated presses. Root-caused by reading
`VoiceAllocator.cpp`: `StartVoice()` only sets a *glide target* for the pitch
control (`mVoiceGlides[voiceIdx]->at(kVoiceControlPitch).SetTarget(...)`)
before calling `pVoice->Trigger(velocity, retrig)` - the actual
`mInputs[kVoiceControlPitch]` value isn't written until the glide is advanced
during the next `ProcessBlock`. So reading `mInputs[kVoiceControlPitch]`
inside `Trigger()` (as the initial Key Follow implementation did) got
whatever a pooled voice's *previous* note left behind, not this note's own
pitch - which stale value survives depends on which voice got
recycled/stolen, explaining the per-press inconsistency. **Fix**: compute
pitch directly from `mKey` (base `SynthVoice` member, already set correctly
right before `Trigger()` is called every time - see the same `StartVoice()`)
using the same default key->pitch mapping `VoiceAllocator` itself uses
(`(key - 69) / 12`, confirmed in `VoiceAllocator.cpp`'s constructor - would
need revisiting only if this project ever calls `SetKeyToPitchFn()` to
override that default, which it doesn't currently). This bug was specific to
this new Trigger()-time code path - every *other* existing Key-Follow-routed
destination reads `mInputs[kVoiceControlPitch]` continuously inside
`ProcessSamplesAccumulating` (every block, for the life of the note), so any
one-block staleness self-corrects inaudibly there; only a one-shot read at
Trigger() (as Env Time needs) bakes a wrong value in for the whole note.
**Confirmed fixed by the user** ("大丈夫です").

**Follow-up, also same session - Key Follow -> Env Time still felt too
subtle even once correct**, before the stale-pitch bug above was found (user
wanted piano-like low/high decay-time contrast). Widened
`kEnvTimeRangeOctaves` from 2 to **4 octaves** (`+-100%` amount now = 16x,
was 4x) - deliberately only this constant, not `kMatSrcKeyFollow`'s own
shared `/4` scaling, so every other Key-Follow-routed destination (Filter
Cutoff, Pitch, etc.) is unaffected. **User confirmed** the combination of
this widened range plus the stale-pitch fix above finally made Key Follow's
effect clearly audible.

**Follow-up, same session - Mod LFO 1/2's known no-op with Env Time
destinations turned into a real UI constraint instead of a silent trap.**
User asked: rather than let a slot silently do nothing when Mod LFO 1/2 is
picked as Source and one of the 4 Env Time destinations is picked as Dest,
just don't let that combination be selected in the first place. Implemented
in `index.html` only (no C++ change): new `UpdateTimeDestAvailability(
sourceSelect)` hides+disables the 4 Env Time `<option>`s (values 14-17) in
that slot's Dest `<select>` whenever Source is Mod LFO 1 or Mod LFO 2 (`"1"`/
`"2"`), and resets Dest back to None (propagating the change via the existing
`SetMatrixEnum()`) if it was already set to one of them. Hooked in two
places: each of the 8 Source `<select>`s' `onchange` (live edits), and
`OnParamChange()`'s existing `kMatrixEnumParams` branch (covers initial load/
preset recall/DAW automation - the same branch already restores Matrix
Source/Dest `<select>`s from host-driven changes, see its own comment).
Confirmed working by the user.

## Initial Patch finalized as the user's own edited version (2026-08-02)

User asked to formally save their own hand-edited Initial Patch (built on top
of the constructor-defaults version created earlier) as the real "Initial
Patch" going forward. Checked `Presets\Initial Patch.preset` directly (unpacked
and inspected every value) - it already reflects their edits, saved via the
in-app preset browser's own "Save" button, not anything I needed to do: Gain
56.8%, Osc 1 Level 41.6%, Filter Cutoff ~11.2kHz, Filter Key Follow 103.2%
(notably only possible after this same session's Key Follow max-raise to
150% - confirms the save genuinely postdates that change, not stale). No
further action needed - this file is the finalized Initial Patch.

## Matrix Osc1/Osc2 Pitch range widened to +-4 octaves (2026-08-02)

Was +-2 octaves at +-100% amount (`matOsc1PitchOct`/`matOsc2PitchOct` in
`FirstSynth_DSP.h`'s per-block Matrix pitch computation) - user asked to
double it. Changed both `* (T) 2.` multipliers to `* (T) 4.`. Pitch Fine
destinations (+-1 semitone at +-100%) are untouched - separate, deliberately
narrow knobs for vibrato-style use, not part of this change. Rebuilt all
three targets clean.

## PEQ LOCK toggle nudged down slightly (2026-08-02)

User asked for a bit more space above the PEQ LOCK toggle on the Effects page
(`.panel-eq`'s own `.effects-row`, sitting right under the Master/Chorus/
Delay/Reverb row with no gap before). Added `margin-top: 16px` inline on that
row's `.knob-row`. Pure HTML/CSS, no rebuild. Confirmed fine by the user.

## Preset dropdown now remembers the last-selected preset across close/reopen (2026-08-02)

User noticed: the actual sound/params already survive close-and-reopen (the
existing auto-persist-across-launches mechanism, `autosave.state`), but the
Preset dropdown itself always fell back to showing the first entry in the
list on relaunch - because "which preset was selected" isn't an `IParam`, so
it was never part of that restore.

**Fix** (`index.html` only): new `kLastPresetStorageKey` localStorage key
(`'firstSynthLastPreset'`, same pattern as UI Zoom/Dark Mode/PEQ Lock).
`LoadPreset(name)` now writes it on every load. `UpdatePresetList(names)`'s
existing `targetValue` fallback logic (already used to keep the current
selection through a Save/Delete-triggered list refresh) now also falls back
to this persisted value when `select.value` is genuinely empty - i.e. only on
the very first sync after launch, before the user has touched the dropdown
this session; every other call already has a real `select.value` and takes
priority unchanged. Also sets `currentPresetName` when this fallback path
restores the dropdown, so Save/Delete's own "what's currently loaded"
bookkeeping (which deliberately doesn't just read `select.value`, see that
variable's own comment) stays in sync too.

**First test looked like it failed** ("リストの一番上の名前が出ます") -
re-tested and it worked correctly the second time, both the dropdown display
and `localStorage.getItem('firstSynthLastPreset')` in DevTools confirmed
correct. Likely just a stale-reload timing artifact (HTML loads from disk in
Debug, but the first test may have run before a full app restart actually
picked up the edited file) rather than a real bug - user confirmed working,
not investigated further since it wasn't reproducible on retry.

## New "ISO" alternate computer-keyboard-to-MIDI mapping added (2026-08-02)

User asked for a second computer-keyboard note-input layout alongside the
existing default one, switchable via a UI toggle. New mapping (their own
design): 4 rows, each a whole-tone (2-semitone) run on its own, cross-anchored
to the row below - Z-row (Z,X,C,V,B,N,M) as the baseline (Z=0), A-row
(A,S,D,F,G,H,J,K,L) anchored one semitone below Z (A=-1), Q-row
(Q,W,E,R,T,Y,U,I,O,P) anchored so W sits exactly one octave above Z (W=12,
so Q=10), and the number row (1-0) anchored one semitone below Q (1=9).
Verified by hand this covers 30 consecutive semitones (-1 to 28) with zero
gaps, several notes reachable from either of two keys (e.g. N and Q both hit
offset 10) - confirmed correct with the user before implementing.

**Implementation** (`index.html` only, no C++): new `kIsoKeyToNoteOffset` map
alongside the existing `kKeyToNoteOffset`; new `keyboardMode` ('aw'/'iso'),
persisted via localStorage (`firstSynthKeyboardMode`, same pattern as Dark
Mode/PEQ Lock/UI Zoom) with a `ToggleKeyboardMode()`/`ApplyKeyboardModeLabel()`
pair. New header button `#keyboardModeBtn` ("Keyboard: AW"/"Keyboard: ISO") -
hidden by default, only revealed from `EnableComputerKeyboardInput()` itself
(the existing Standalone-only entry point), so it never appears when hosted
as a plugin (computer-keyboard-as-MIDI is Standalone-only already).
`EnableComputerKeyboardInput()`'s keydown handler now branches on
`keyboardMode` for both the octave-shift keys and which offset map to use.

**Real crash found and avoided**: Z/X are real note keys in ISO mode (no room
left for them to also do octave shift), so Left/Right arrow keys were tried
first for that instead. **This crashed the Standalone app outright** (window/
process disappears entirely) specifically when an arrow key was pressed while
a note key was already held down - letter/digit keys alone were fine, arrows
alone were reportedly not tested in isolation. Checked Windows' Application
event log (WER) for a fresh crash entry to get a real exception
code/faulting-module instead of guessing - **found nothing newer than an old
unrelated 2026-07-23 crash**, meaning this new crash didn't generate a normal
WER report at all (unusual - a genuine access violation/stack overflow
usually does). Given no diagnostic evidence to chase and a real crash risk
confirmed reproducible, **did not keep investigating** - switched the two
octave-shift keys to Minus/Equal instead (physically unused by this layout,
presumably safe) rather than root-causing the arrow-key interaction. If this
ever needs revisiting: reproduce again with Windows Reliability Monitor or a
debugger attached before the crash, since the WER log alone didn't capture
enough to diagnose from.

**Reverted, same day.** Once working (Minus/Equal confirmed fine), the user
felt the new header toggle button made the header too cluttered and asked to
move the switch into the native "Options" menu instead. Checked: that native
menu (`main.rc`/`IPlugAPP_dialog.cpp`, shared framework) has no existing
precedent for a menu command reaching into the WebView's JS state - every
current menu item (Screenshot, Live Edit Mode, Preferences, etc.) is either
native-only or opens a separate native dialog, never calls into the page.
Given that would mean building new native-to-JS plumbing in a shared file (as
opposed to reusing something already there), flagged the size/risk of that to
the user before starting - **user decided to drop the feature entirely**
rather than pursue either approach. Fully reverted: `kIsoKeyToNoteOffset`,
`keyboardMode`/`kKeyboardModeStorageKey`/`ToggleKeyboardMode()`/
`ApplyKeyboardModeLabel()`, the `#keyboardModeBtn` header button, and
`EnableComputerKeyboardInput()`'s mode-branching all removed from
`index.html` - verified no leftover references. Back to exactly the original
single AW keyboard mapping. **The Left/Right-arrow-plus-held-key crash finding
above is still worth remembering even though the feature itself is gone** -
see this project's memory file.

## Wicki-Hayden alternate keyboard mode added (2026-08-02, done properly this time)

Same day, user came back wanting the alternate-keyboard-mapping idea again,
this time specifically matching **Hex MIDI**'s "Wicki-Hayden" isomorphic layout
(a real, standard, well-documented system - not a custom one-off design like
the earlier reverted attempt). Researched via WebSearch/WebFetch first: Hex
MIDI (hexmidi.com) is a real isomorphic-keyboard web app supporting QWERTY
computer-keyboard play across several standard layouts (Wicki-Hayden, Jankó,
Harmonic Table, C-System chromatic accordion, etc.) - confirmed via its own
page text ("Use Keyboard & Arrows to play"). Its actual keyboard grid renders
via Canvas, so its exact per-key mapping couldn't be read through the browser
tool's accessibility tree - the user sent a real screenshot with "Show QWERTY
Labels" enabled instead, which is what actually nailed down the anchors.

**Structure** (confirmed against the Wicki-Hayden Wikipedia article's own stated
smallest intervals - major second (2 semitones) and perfect fourth (5
semitones) - and cross-checked against the user's own screenshot/live
testing): +2 semitones per key moving right within a row; +5 semitones per row
moving **up** physically (ZXCV row lowest -> ASDF -> QWERTY -> digit row
highest). Went through two real corrections before landing here, both driven
by the user directly reading values off Hex MIDI rather than guesswork:
1. First attempt read the row direction backwards (had QWERTY row lowest,
   ZXCV highest) - the user caught this ("Z>A>Qで4度上がり") and confirmed the
   correct anchor is **Z = A#2 (MIDI 46)**, ascending Z->A->Q by a fourth each
   row.
2. Octave-shift mechanic: initially implemented as a flat +-12-per-press
   (matching the AW mode's Z/X convention), but the user clarified Hex MIDI's
   own Up/Down transpose is **not** a flat octave jump - each press moves the
   grid by one row-step, alternating +5/+7 semitones (landing back on a plain
   octave only every 2 presses - their own example: Z's note C2->F2->C3->F3).
   Implemented as `WickiTransposeOffset(n)`: `sign(n) * (6*|n| - (|n| mod 2))`,
   first bound to **ArrowUp/ArrowDown** (not Minus/Equal, since the user
   specifically wanted to match Hex MIDI's own key choice) - genuinely untested
   combo given the earlier Left/Right-arrow crash, user was warned and asked to
   test the keys alone first. **Confirmed crashed too** ("矢印キーでやはり
   クラッシュしますね") - Up/Down arrows crash exactly like Left/Right did when
   pressed together with a held note key. This settles it as a **general
   arrow-key incompatibility in this project's WebView/native keydown-forwarding
   path**, not something specific to one axis - still not root-caused, but now
   confirmed broad enough that arrow keys should simply be avoided here going
   forward (Hex MIDI's own use of arrow keys is unaffected, since it's a
   completely separate app/architecture). Tried **PageUp/PageDown** next, but
   the user didn't have easy physical access to those keys on their keyboard,
   so settled on **F1 (down)/F2 (up)** instead - no native menu accelerator in
   this project uses a bare F-key (Debug menu's shortcuts are all Ctrl+letter),
   so no conflict expected. Same `WickiTransposeOffset` math throughout, just
   different trigger keys each time. Not yet confirmed safe by the user as of
   this entry - test the same way (keys alone first, then with a held note)
   before trusting it.

**Row extents, including a real JIS-keyboard gotcha revisited**: user asked to
extend each row rightward to specific additional keys. Three were
straightforward (Minus/Equal on the digit row, Semicolon on the ASDF row,
Comma/Period/Slash on the ZXCV row - all ordinary keys, same on JIS/US).
The QWERTY row's extension revisited a **previously-documented JIS-keyboard
quirk** (see `kKeyToNoteOffset`'s own comment, from 2026-07-26): the two keys
right after P sit in a different place on JIS vs US keyboards, and specifically
the JIS key typing '@' corresponds to event.code **`Quote`**, not `BracketLeft`
as a US-keyboard-centric guess would assume - confirmed directly with the user
("アットキーです") rather than assumed, given this exact confusion was flagged
as a real risk once already in this project. Final QWERTY-row extension:
`Quote`(@) then `BracketLeft`([). Final full row extents: ZXCV up through
Slash, ASDF up through Semicolon, QWERTY up through BracketLeft, digit row
through Equal.

**UI**: new `kWickiHaydenKeyToNoteOffset` map, `keyboardMode`
('aw'/'wicki') persisted via localStorage (`firstSynthKeyboardMode`, same
pattern as Dark Mode/PEQ Lock/UI Zoom), `ToggleKeyboardMode()`/
`ApplyKeyboardModeLabel()`. New `#keyboardModeBtn` button ("Keyboard: AW" /
"Keyboard: Wicki-Hayden") - hidden by default, only revealed from
`EnableComputerKeyboardInput()` (Standalone-only), placed in the **preset bar**
(after Save/Save As/Delete, `margin-left: 24px` for visual separation) per the
user's specific placement request - not the header, which they'd found too
cluttered on the earlier attempt. All pure HTML/JS, no C++ change, no rebuild
needed for any of this.

**Reusable lesson**: for external reference layouts (unlike a from-scratch
custom design), get ground-truth anchors directly from the user reading a real
screenshot/live app rather than deriving from written descriptions or search
summaries alone - saved real rework here once the row-direction and octave-
shift-mechanic misunderstandings were caught early via direct confirmation
rather than being discovered after a full implementation.

## 2026-08-03 — C-System (chromatic button accordion) keyboard mode + dropdown UI

Added a **third** computer-keyboard mapping, again matching a real Hex MIDI
layout ("C System"), per user request: "もう一つのキー配列、Cシステムも追加
できますか。プルダウン方式で。" Same ground-truth-anchor methodology as
Wicki-Hayden — asked the user to read Q/A/Z/W directly off Hex MIDI rather
than deriving from theory alone. One self-correction along the way: user's
first answer ("A2、G2、F2") implied Q highest/descending, but they caught their
own error immediately ("すみません、間違えました。どうも下から考えるクセが
ついてまして"): actual values are **Q=F2, A=G2, Z=A2** (Q lowest, ascending
Q→A→Z, +2 semitones per row) — the opposite row direction from Wicki-Hayden,
where Z was lowest. W=G#2 confirmed the horizontal step is +3 semitones (a
minor third — matches the standard C-system accordion property of rising an
octave every 4 buttons, 12/4=3).

New `kCSystemKeyToNoteOffset` map, anchored on Q=F2 (MIDI 41). Row extents
(Minus/Equal, Quote/BracketLeft, Semicolon, Comma/Period/Slash) added to match
Wicki-Hayden's for visual/behavioral consistency — not individually
re-confirmed by the user for this layout specifically, worth double-checking
if anything sounds off at the row edges. Octave control is **F1(down)/F2(up)**
like Wicki-Hayden, but C-System uses a **flat ±12 semitones per press** (no
alternating-interval quirk to replicate here, unlike Wicki-Hayden's Hex-MIDI-
matched transpose) — reuses the same `keyboardOctaveShift` variable as AW mode.

**UI overhaul**: converted the keyboard-mode control from a 2-way toggle
button to a proper 3-way `<select>` dropdown (`#keyboardModeSelect`,
`SetKeyboardMode(mode)`) offering AW / Wicki-Hayden / C-System, replacing
`#keyboardModeBtn`/`ToggleKeyboardMode()`. Same localStorage key
(`firstSynthKeyboardMode`), now validated against a `kKeyboardModes` allowlist
instead of a binary check. Same placement (preset bar, after Save/Save
As/Delete).

**AW mode octave keys changed Z/X → F1/F2** (user request, "AWのときのオク
ターブアップ・ダウンをF2 F1に変更してください", for consistency with the other
two modes' key choice). Confirmed safe to reuse F1/F2 here: AW's own
`kKeyToNoteOffset` note map never used KeyZ/KeyX in the first place (its
lowest row starts at KeyA), so no note-key collision either before or after
the change.

All pure HTML/JS (`resources/web/index.html` only), no C++/param change, no
rebuild needed. User confirmed working: "OKです。"

## 2026-08-03 — Manual updated with F1/F2 octave note; confirmed AW is the default mode

User asked to check both quick-start manuals for any Z/X octave-shift
description that would need correcting to F1/F2 (following the AW-mode key
change above). Checked both `manual\FirstSynth 取扱説明書.docx` and
`manual\FirstSynth User Manual.docx` paragraph-by-paragraph — neither ever
documented an octave-shift key at all (the only PC-keyboard-play sentence just
says notes start at the A key with alternating white/black keys), so there was
nothing to correct. User then asked to add the F1/F2 note explicitly: appended
a sentence to that same paragraph in both docs via python-docx (single run
each, straightforward append) —
JP: "F1／F2 キーでオクターブを上下に切り替えられます。"
EN: "Use F1/F2 to shift the octave down/up."

Also confirmed the **default keyboard mode is already 'aw'** in code —
`resources/web/index.html`'s `keyboardMode` init falls back to `'aw'` whenever
`localStorage`'s stored value isn't one of the three valid modes (covers both
a fresh install and any corrupted/unexpected stored value). Since this is a
persisted-per-machine preference, this dev machine's own WebView profile may
still be sitting on `'wicki'`/`'csystem'` from this session's testing — asked
the user to reselect "Keyboard: AW" from the dropdown once, live, to reset
their own local state (not something I can do remotely; a fresh install
elsewhere is unaffected and already defaults to AW). User confirmed: "了解
です。"

## 2026-08-09 — Filter Type "LP" swapped to a Moog ladder (ported from Chaoscape)

User request: replace the LP side of the filter with the Moog-type ladder
filter from [[project_chaoscape_chainreaction_synths]] ("このchaoscapeで使った
ムーグタイプのフィルターに置き換えてみたいです"), with resonance raised.
Clarified scope via two quick questions since this filter is a lot more
feature-rich here than Chaoscape's (LP/BP/HP type blend, 12/24dB slope
toggle, Filter Env, Filter LFO, Key Follow, Matrix mod destinations for
Cutoff/Resonance) - none of which Chaoscape's always-on drone filter needed
to deal with:

1. **Scope**: full replacement, not an additional selectable option -
   confirmed after first proposing "add Moog as a 3rd Filter Type choice"
   as an alternative, but the ladder can't blend against the SVF's Band/High
   outputs the way the old LP (also SVF) could, so...
2. **Filter Type behavior**: `Filter Type` was a continuous 0-2 knob blending
   LP-BP-HP; since Moog LP can't be continuously crossfaded against SVF's
   BP/HP, it became a **discrete 3-way choice** instead (user's pick, over
   trying to preserve the old sweep). `kParamFilterType` changed from
   `InitDouble(0,2)` to `InitEnum("Filter Type", 0, {"LP (Moog)", "BP",
   "HP"})` in `FirstSynth.cpp` - same 0/1/2 value range, so old presets that
   had it at exactly LP/BP/HP still load correctly; only presets that had it
   mid-sweep (a blended value) will now snap to the nearest of the three.
   Deliberately did **not** remove/renumber `kParamFilterType`/
   `kParamFilterSlope` from `EParams` even though Slope is now a no-op for
   LP - would have shifted every later param's index and corrupted existing
   `.preset` files and host automation (`SerializeState` is positional, not
   name-keyed) - same reasoning as Chaoscape's `kParamEngineMode` being
   appended rather than inserted.

**DSP, `FirstSynth_DSP.h`**: added a **per-voice** Moog ladder (ported from
`ChaoscapeEngine::ProcessMoogLadder` almost verbatim - same Stilson/Smith-
style 4-stage cascade + tanh-saturated resonant feedback - just templated on
`T` and made a private `Voice` method/members instead of engine-level state,
since this synth is polyphonic and Chaoscape's was a single always-on
voice). New members `mMoogOut1-4`/`mMoogIn1-4`, reset alongside
`mFilterStage1/2.Reset()` in `Trigger()`. **Resonance raised per request**:
`kMoogResonanceScale = 6.0`, matching Chaoscape's own boosted scale (its
Mode 2), not Chaoscape's milder Mode 1 default (4.2) - untuned for this
synth specifically, just reused directly.

**Filter branch** in `ProcessSamplesAccumulating`: `mFilterType < 0.5` (i.e.
LP) now calls `ProcessMoogLadder(dry, cutoffHz, qNorm/100.)` - reuses the
exact same fully-modulated `cutoffHz`/`qNorm` already computed above for the
SVF path (Filter Env, Filter LFO, Key Follow, Matrix Cutoff/Resonance
destinations all carry over automatically, no separate modulation plumbing
needed). BP/HP (`mFilterType >= 0.5`) still runs the original
`mFilterStage1`/`mFilterStage2` + `BlendFilterOutputs` exactly as before,
including the `24dB Slope` toggle - that toggle is now a no-op when LP is
selected (Moog ladder is a fixed 4-pole/24dB design), documented in a
comment on `mFilterSlope24` rather than hidden/disabled in the UI.

**UI**: `index.html`'s Filter Type knob (`param-id="24"`) relabeled from
"LP-BP-HP" to "Filter Type", added `integer-display step="1"` so it visibly
snaps to 0/1/2 instead of showing decimals (matches the pattern used for
Octave/Semi's integer-stepped knobs elsewhere in this file).

Builds clean (Standalone + CLAP, 0 errors both). Verified via screenshot:
Filter Type knob shows a clean integer, loaded an existing preset without
issue, app stable for several seconds idle. **Not yet verified by ear** -
didn't play a note this session (no MIDI input available here) - ask the
user to check LP sounds like the Moog ladder (fuller resonance, can push
toward self-oscillation) and that BP/HP still sound unchanged from before.

## 2026-08-09 — Filter Type: knob -> 3-way switch; Slope toggle removed; BP/HP resonance range capped

Same-day follow-up to the Moog LP swap above, three related requests: (1)
Filter Type should be a 3-way switch, not a knob, with the current selection
visibly labeled; (2) BP/HP should be unconditionally 24dB (remove the Slope
toggle entirely); (3) BP/HP's resonance was too strong at the top of its
range - cap it at roughly what the old ~40% knob position used to sound like.

**Filter Type UI (`index.html`)**: replaced the `knob-control` with a 3-button
group reusing the existing LFO-Shape-picker pattern almost verbatim -
`.shape-selector-container` (the same bordered box every LFO Shape control
uses) wrapping a `<span class="toggle-label">Filter Type</span>` and a
`.shape-selector` grid (its `repeat(3, 38px)` CSS already fits exactly 3
buttons, no new grid rules needed) holding 3 `.shape-btn` buttons labeled
"LP"/"BP"/"HP". New `SetFilterType(btn)`/`UpdateFilterTypeSelection(idx)` JS
functions, modeled directly on `SetShape()`/`UpdateShapeSelection()` but
hardcoded to param 24 / 3 options (there's only ever one Filter Type control,
unlike the LFO shape pickers which repeat per LFO). `OnParamChange`'s old
`param === 25` (Slope toggle) branch replaced with a `param === 24` branch
calling `UpdateFilterTypeSelection()`, so host-driven changes (preset
recall, DAW automation) keep the button highlight in sync. One small
CSS addition (`.filter-type-selector .shape-btn { font-size:12px;
font-weight:700; }`) since `.shape-btn` was only ever styled for svg icons
before. **Turned out to automatically inherit the Filter panel's existing
green accent color** (same as the Cutoff/Q knobs) purely by reusing
`var(--accent)` the same way `.shape-btn.selected` already did - no extra
theming work needed, confirmed via screenshot.

**Slope toggle removed**: `filterSlopeToggle`/`SetFilterSlope()` deleted
from `index.html`. In `FirstSynth_DSP.h`, `mFilterSlope24` member and its
`case kParamFilterSlope` wiring in `SetParam()` are gone; the BP/HP branch
in `ProcessSamplesAccumulating` now runs `mFilterStage2.Process()`
unconditionally (always 24dB) instead of behind an `if`. `kParamFilterSlope`
itself is still registered in `FirstSynth.cpp` (`InitBool`, now genuinely a
no-op) rather than removed - same "never renumber, would corrupt saved
presets" reasoning as `kParamFilterType`'s own change earlier this session.

**BP/HP resonance range capped**: new `kSvfMaxQResonanceScale = 0.4`
constant in `FirstSynth_DSP.h`, multiplied into the existing `modulatedQ`
formula (`0.5 + (qNorm/100)*19.5*kSvfMaxQResonanceScale`) - this only feeds
`damp`, which only the SVF (BP/HP) branch uses, so the Moog LP branch (which
reads `qNorm` directly as a 0-1 fraction, not through this Q-widening
formula) is completely unaffected. Old max Q was 20.0 (near self-oscillating,
user found too strong); new max is ~8.3, matching roughly where the old
100%-scale knob sat at ~40%.

Builds clean (Standalone + CLAP). Verified live in Standalone via a real
mouse click (not just screenshot this time): clicking "LP" correctly moved
the highlight from "BP" to "LP" and the engine switched accordingly (visual
confirmation only, still no MIDI input available to verify the resonance cap
by ear - ask the user). **Environment note, not a code bug**: the Standalone
process disappeared without any Windows Application-Error log entry once,
seemingly just from being left idle between automation calls in this
session's sandboxed testing environment (no user interaction, no crash log)
- relaunching made it stable again and the click test then worked fine
immediately after; same class of unexplained idle-exit was seen once before
with Chaoscape earlier this session, still unresolved but doesn't seem tied
to any specific code change.

## 2026-08-09 — Ctrl+S overwrites the currently-selected preset

User request: overwrite the current preset via Ctrl+S, same effect as
clicking the existing "Save" button (`SaveCurrentPreset()`, added
2026-07-30 - overwrites in place, or falls back to the Save As prompt if
nothing is currently loaded). Pure `index.html` addition, no C++/param
change, no rebuild needed - added a page-level
`document.addEventListener('keydown', ...)` right after
`SaveCurrentPreset()`'s own definition, checking `(e.ctrlKey ||
e.metaKey) && e.code === 'KeyS'`, calling `e.preventDefault()` then
`SaveCurrentPreset()`. Registered unconditionally at script-parse time
(not inside `EnableComputerKeyboardInput()`, which is Standalone-only), so
it should work in every host format, not just Standalone.

**Not confirmed working this session** - repeated attempts to verify via
synthetic keyboard input (`keybd_event`, then `SendKeys` after focusing a
button via UI Automation) never showed the target preset file's mtime
change, and the Standalone process itself kept disappearing mid-automation
(no crash log, same unexplained-idle-exit pattern noted in the entry just
above) before a clean test could complete. Given real *mouse* clicks
reliably worked earlier in this same session (the Filter Type LP/BP/HP
switch test), and this project's own known framework gotcha
(`IPlugWebView_win.cpp` injects its own document-level keydown listener
that forwards keys to native code, documented in
[[project-firstsynth-clap-plugin]]) doesn't call `preventDefault`/
`stopPropagation` so shouldn't block a second listener from also firing,
the leading theory is this sandboxed session's synthetic-keyboard-input
path is unreliable (already flagged once before, see Chaoscape's own
"foreground/keyboard focus" note this session), not a bug in the new code.
**Ask the user to test Ctrl+S directly** - press it after loading/editing a
preset in Standalone, and confirm the preset file's contents/timestamp
actually update.

## 2026-08-09 — VST3 was stale (predated all of today's filter work); rebuilt

User reported the filter sounded different between Standalone and VST3 after
today's Moog-ladder-LP work. Root cause: **VST3 hadn't been rebuilt at all
today** - `build-win\vst3\x64\Debug\FirstSynth.vst3` was still dated
2026-08-02, and the actually-loaded copy at the real install path
(`C:\Program Files\Common Files\VST3\FirstSynth.vst3`) was even older
(2026-07-28) - both predate every change from today (Moog LP swap, 3-way
Filter Type switch, Slope removal, resonance cap, Ctrl+S). Standalone
picked up the changes immediately (loads `resources/web/*` from disk and
gets rebuilt every time per this project's normal routine); VST3 simply
hadn't been in the build rotation today. First rebuild attempt failed at the
postbuild install-copy step (`postbuild-win.bat` exit code 4) - traced to
**BespokeSynth running with the old FirstSynth.vst3 loaded**, file-locking
the install path; asked the user to close it (didn't force-close another
app's process myself), then rebuilt successfully once closed - confirmed via
the installed bundle's nested binary
(`...\FirstSynth.vst3\Contents\x86_64-win\FirstSynth.vst3`) showing today's
timestamp. **Lesson for this project**: after a filter/DSP-only session like
today's, remember VST3 needs its own explicit rebuild too, not just
Standalone+CLAP - the normal per-change routine
([[feedback-firstsynth-build-workflow]]) may need rechecking if VST3 usage
becomes routine again (it was deprioritized for CLAP back on 2026-07-28 per
this project's own memory, but the user is clearly testing VST3 again now).
User hasn't yet re-tested the filter in VST3 with the fresh build - ask next
time.

## 2026-08-09 — LP (Moog) vs BP/HP (SVF) energy mismatch noted; ladder-derived BP/HP discussed, not implemented

After the Moog LP swap, user noticed a real **energy/level difference**
between LP (Moog ladder) and BP/HP (SVF) - said they'll tune this later, not
asked to fix yet. Separately asked a genuine technical question (not a
request): real Moog hardware has BP/HP too, can that be implemented here?

**Answer given, not acted on**: yes, technically possible, two known real-
world approaches - (1) a separate one-pole HP stage bolted on alongside the
ladder (how e.g. Moog Matriarch/Grandmother do it - their HP isn't derived
from the ladder itself), or (2) weighted differencing of the ladder's own
intermediate stage outputs (`mMoogOut1`-`mMoogOut4`) to approximate BP/HP,
used by some ladder-clone multimode designs. Flagged the real trade-off:
ladder-derived BP/HP would be gentler/differently-voiced than the existing
precise SVF BP/HP, so it's a "new sound option" question, not a fix for the
energy-balance issue just raised - recommended settling the LP/BP/HP energy
balance first, treating a ladder-derived multimode as a separate, later,
not-yet-agreed-to idea. **Nothing implemented for either of these** - purely
a discussion, recorded per user request ("記録しておいてください").

## 2026-08-16 — Studio One crash while genuinely idle (no notes, no interaction) - investigation started, root cause NOT yet found

User report: while using FirstSynth as a VST3 instrument in Studio One, left it
completely idle for a while (no notes held, no UI interaction - "本当に放置")
and an error dialog appeared; clicking the dialog's "無視" (Ignore) button
crashed Studio One entirely. User confirmed the dialog showed detailed text
(not just a generic Windows crash box) but didn't have a screenshot from that
occurrence - **will screenshot it next time it happens** (this is the key next
step, see below).

**Confirmed via Windows Event Viewer** (`Get-WinEvent -LogName Application`,
event IDs 1000/1001) - this happened **twice today**, at 11:43 and 13:06, both
with **identical** fault signature:
- Faulting module: `FirstSynth.vst3` (the real installed copy,
  `C:\Program Files\Common Files\VST3\FirstSynth.vst3\...`, module timestamp
  `0x6a780026` = 2026-08-09 13:20:54, i.e. the **current, already-installed**
  build from the Moog-ladder-filter session - confirmed the matching PDB at
  `build-win\pdbs\FirstSynth-vst3_x64.pdb` has the exact same timestamp).
- Exception code `0xc0000409` = `STATUS_STACK_BUFFER_OVERRUN`, exception data
  `5` = `FAST_FAIL_INVALID_ARG` - this is the Windows "fail fast" mechanism a
  Debug-CRT build's own runtime checks invoke when they detect something
  unsafe to continue past (this project ships Debug-config builds even for
  real DAW use - see this file's own Build commands section - so these checks
  are active in the shipped binary, unlike a Release build).
- Fault offset `0x4ec4fc`, **identical both times** - a deterministic,
  reproducible code path, not a rare race.

**Symbolicated the fault offset** against the matching PDB (no WinDbg/cdb
installed on this machine - used raw `dbghelp.dll` P/Invoke from PowerShell
instead: `SymInitialize`/`SymLoadModuleEx`/`SymGetLineFromAddr64`, loading the
PDB directly by path with a synthetic base address purely for symbol lookup,
not memory-mapping the real DLL. **Caution for next time**: an early attempt
using an incorrectly-marshaled `IMAGEHLP_LINE64` struct - declaring its
`PCHAR FileName` field as a managed `string` instead of `IntPtr` - crashed the
PowerShell process outright; fixed by marshaling it as `IntPtr` and converting
via `Marshal.PtrToStringAnsi` afterward). Result: **resolves to
`minkernel\crts\ucrt\src\appcrt\misc\invalid_parameter.cpp:237`**, inside the
Universal CRT's own internal invalid-parameter/fail-fast handler - i.e. this
is where the runtime *detected and reported* a problem, not where the actual
bug is. Some earlier call - almost certainly an "_s" (secure) CRT function
call or a `/RTC1` runtime-check-instrumented local variable somewhere in our
own code (or a statically-linked dependency, since this iPlug2 project
compiles everything into one module per format) - is what actually triggered
this. Getting the real culprit requires either (a) a full crash dump with a
walkable call stack (Windows only kept the lightweight `Report.wer` files for
these two crashes, in `C:\ProgramData\Microsoft\Windows\WER\ReportArchive\`,
no attached `.dmp` - full local dump collection isn't enabled on this
machine), or (b) the dialog's own text, which Debug-CRT runtime-check dialogs
(`_RTC_Failure`/`_invalid_parameter`) typically print with the exact file,
line, and often the variable/function name directly - **user will screenshot
this next time it reproduces, that's the fastest path to the real fix**.

**Working theory, NOT confirmed** - narrowed down by the "genuinely idle, no
interaction" condition, which rules out anything gated on note-on or UI
clicks: something that keeps running continuously in the background
regardless of notes is the likely trigger. Prime suspect considered:
`LooperEffect::mCurrentTime` (`FirstSynth_Looper.h`) advances every single
`Process()` call *unconditionally*, including while
Empty/Stopped/idle - "continuously-advancing wall clock" per its own class
comment - so after a long enough session it becomes a very large `double`. Its
only consumer, `DecayFactor()` (`std::pow(mFeedback, elapsed/kDecayTimeScale)`
with `mFeedback` in `[0,1]`), was checked and is IEEE754-well-defined for
arbitrarily large `elapsed` (smoothly underflows to 0, never NaN/Inf/throws) -
so this specific path was **ruled out** as the direct cause, but the general
"something that only misbehaves after long unattended runtime" shape still
feels like the right category to keep investigating (Mod LFO1/2, which per
this file's earlier "OPEN ISSUE: Mod Env 1/2 always evaluated regardless of
Matrix use" entry also run unconditionally every block regardless of notes,
haven't been checked yet).

**Next steps**:
1. Get the dialog's exact text next time it reproduces (user's own action item).
2. Consider enabling Windows' full local crash-dump collection
   (`HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps`) so a
   future occurrence keeps a complete, stack-walkable `.dmp` automatically -
   **not done this session, this touches system-wide settings so it's the
   user's own call to enable, not something to do unprompted**.
3. If/when a real dump or dialog text is available, re-run the same
   `dbghelp.dll` P/Invoke symbolication approach documented above (now with a
   corrected, crash-safe struct definition) to pin down the actual buggy call.

## Studio One crash - ROOT CAUSE FOUND AND FIXED (2026-08-16, same day, continued)

User got a real error dialog this time and screenshotted it: **"Debug
Assertion Failed! ... File: ...\include\vector Line: 1941 Expression: vector
subscript out of range"** - confirms this is `std::vector::operator[]`'s own
debug bounds check firing, i.e. genuine out-of-bounds indexing somewhere in
our code (or a statically-linked dependency).

Enabled Windows' full local crash-dump collection per the user's own choice
(`HKLM\...\Windows Error Reporting\LocalDumps`, `DumpType=1` mini-dump,
`C:\CrashDumps` - user ran this themselves via an elevated PowerShell, not
done by Claude, since it's a system-wide setting). **The crash reproduced
again and a real `.dmp` was captured** - unlike the earlier occurrences, this
one still had usable memory (stack) to work with.

**Wrote a minimal minidump parser in Python** (no WinDbg/cdb available on this
machine, and raw `dbghelp.dll` P/Invoke from PowerShell had already proven
fragile for anything beyond a single-address lookup - see the entry above).
Parsed the MDMP header/stream directory by hand, pulled the exception
stream's thread context (RIP/RSP/RBP straight out of the raw x64 `CONTEXT`
struct at its documented byte offsets - Rsp@152, Rbp@160, Rip@248), the
module list (to get `FirstSynth.vst3`'s load base/size), and the memory list
(to get the actual stack bytes near RSP). Confirmed RIP matched the earlier
Event-Viewer-derived fault offset (`0x4ec4fc`) exactly - good cross-check.
Then scanned every 8-byte-aligned QWORD in the captured stack region for
values falling inside `FirstSynth.vst3`'s address range - each one is a
plausible return address, i.e. an approximate (not perfectly unwound, but
good enough) call stack, in order.

**Resolved every candidate address to source file:line** via the same
`dbghelp.dll` `SymGetLineFromAddr64` P/Invoke technique validated earlier
(loading the matching PDB by path at a synthetic base, not touching the real
process). Walking outward from the crash: `invalid_parameter.cpp:237` ->
`<vector>:1940` (the actual bounds-check assert site) -> ... ->
**`FirstSynth_Effects.h:281`** (inside `ChorusEffect::ReadInterp()`) ->
**`FirstSynth_Effects.h:261`**-ish (`ChorusEffect::Process()`'s call into
`ReadInterp`) -> `MidiSynth.cpp:475` (iPlug2 framework, expected - this is
just the processing call chain) -> `FirstSynth.cpp`/`IPlugProcessor.cpp`/
`IPlugVST3_ProcessorBase.cpp` (normal VST3 `process()` entry chain). **Real
bug confirmed inside `ChorusEffect::ReadInterp()`.**

**Root cause**: `ReadInterp()` only wrapped `readPos` at the *lower* bound
(`while (readPos < 0) readPos += size`) - there was no matching upper-bound
wrap, and no clamp on the final `(int) readPos` cast before indexing
`buf[i0]`/`buf[i1]`. Under the Chorus Rate/Depth params' normal ranges,
`delaySamples` should always stay comfortably under the buffer's size (60ms
buffer, ≤20ms+2-sample max modulated delay) - so this was never expected to
be hit in ordinary use, but Chorus runs unconditionally on the master bus
(after all voices, always active regardless of notes - matches "crashed even
though nothing was playing" *and* "crashed while a sequencer was actively
playing notes", since neither report was really about note activity at all).
Combined with this project's own earlier finding that DAWs call `OnReset()`
(which reallocates `mBufferL`/`mBufferR` via `SetSampleRate()`) far more often
than Standalone does, the exact trigger is still not 100% pinned down
(parameter-range edge case vs. floating-point rounding at a buffer boundary
vs. a possible race between `SetSampleRate()`'s reallocation and `Process()`'s
concurrent read were all considered - not conclusively distinguished) - but
the fix closes the hole regardless of which of those it actually was.

**Fix**: added the missing symmetric upper-bound wrap (`while (readPos >=
size) readPos -= size`, matching the pattern this codebase already uses
correctly elsewhere - e.g. the Looper's own `mPlayPos` wrap in
`FirstSynth_Looper.h`), a hard clamp (`if (i0 >= size) i0 = size - 1`) as a
final backstop against any residual floating-point edge case, and an early
`return 0` guard for a genuinely empty (`size == 0`) buffer (defensive - was
already unlikely to be reachable, but would previously have hung or divided
by zero rather than crash cleanly). Rebuilt Standalone, CLAP, and VST3 (VST3's
postbuild copy failed once with the file locked - same class of issue as the
2026-08-09 entry above, Studio One still had the old build loaded; asked the
user to close it, then it succeeded - confirmed via the installed binary's
timestamp).

**Not yet re-verified live by the user** - next step: use FirstSynth normally
in Studio One (with Chorus active, since that's the only path through this
code) for a while, including both idle periods and sequencer playback, and
confirm no repeat of the crash. If it *does* recur despite this fix, that
would point toward the race-condition possibility mentioned above rather than
a pure value-range/rounding issue, and would need actual thread-synchronization
work around `OnReset()`/`SetSampleRate()` vs. `ProcessBlock()`.

**Reusable technique for future FirstSynth crashes**: the whole
minidump-parsing + `dbghelp` symbolication approach above worked well and is
fully self-contained (Python for the binary parsing, PowerShell+`dbghelp.dll`
P/Invoke for symbol resolution against the matching PDB) - no WinDbg/cdb
install needed. Keep `LocalDumps` enabled (already done, user's own machine)
so any future crash has a `.dmp` ready to go in `C:\CrashDumps`.

## 2026-08-16 — "Single note sounds detuned with Yuragi up" - NOT a FirstSynth bug, diagnosed to BespokeSynth sending duplicate MIDI note-ons

User reported a single held note (Osc2 off) intermittently sounding detuned
when the Yuragi knob is up. Explained the mechanism from the code
(`Trigger()` in `FirstSynth_DSP.h` draws a fresh random pitch offset - up to
±0.6 semitones at 100% - once per note-trigger, identically applied to both
oscillators, so Yuragi itself can't detune Osc1 against Osc2): the only way a
single musical note could audibly "detune" is if **two voices are actually
sounding at once** (legato overlap, sustain pedal, or a duplicate/retriggered
note-on), each having independently drawn a different random Yuragi offset -
the slight pitch difference beats like a detuned unison. This matches the
reported *intermittent* nature (depends on whether an overlap happens to
occur).

User tested in Standalone (computer-keyboard input, no host in between) and
confirmed the detune does NOT happen there - **isolated to BespokeSynth**,
almost certainly BespokeSynth sending a duplicate MIDI note-on for the same
note (a host/routing issue, not something FirstSynth's code did wrong -
triggering a new voice on every note-on it receives is correct behavior).
**No FirstSynth code change needed or made.** If this comes up again, the fix
(if any) belongs in BespokeSynth's own MIDI routing (duplicate cable/Thru/Echo
settings, or multiple modules feeding the same channel), not here.

## 2026-08-16 — Moog ladder LP: raised the normalized-cutoff safety clamp 0.45 -> 0.49 (highs still cut at max Cutoff + dead zone near the knob's top)

User reported two things about the Moog-ladder LP filter (swapped in
2026-08-09): (1) even with Cutoff turned fully right (20000Hz), highs still
sound slightly rolled off - never truly "open"; (2) the knob's top stretch
feels disproportionate - turning down just slightly from max closes the
filter a lot more than expected.

**Both traced to the same line**: `ProcessMoogLadder()`
(`FirstSynth_DSP.h`) computes `T fc = std::max(0., std::min(0.45, cutoffHz /
mSampleRate));` - a hard safety ceiling on the ladder's normalized cutoff, at
`0.45`. At 44.1kHz that's only ~19845Hz, so (1) the filter's real internal
cutoff was capped below what the knob claimed even at 20000Hz, and (2) every
Cutoff value from ~19845Hz up to 20000Hz clamps to the *identical* 0.45,
i.e. that whole top stretch of the knob was a dead zone doing nothing at
all - turning down past it is what made the filter suddenly start audibly
closing, reading as "a small movement near the top closes it a lot."

Explained the trade-off (this classic Stilson/Smith-style ladder topology's
coefficient approximations, ported from Chaoscape, become
unstable/self-oscillate if this ceiling is raised too close to 0.5/Nyquist)
and asked the user to choose between a conservative bump (~0.48) or a more
aggressive one (~0.49, more open but more risk). **User chose the more
aggressive option.** Changed the clamp to `0.49`. Rebuilt all 3 targets
(Standalone/CLAP/VST3), all succeeded cleanly.

**User confirmed no stability problems** with the 0.49 clamp. But then raised
a related, much more concrete complaint: **the Cutoff knob showing "1000Hz"
sounds almost inaudible**, and raising Resonance around there makes it sound
like the resonant peak is at ~80Hz, not 1000Hz.

## 2026-08-16 — Moog ladder LP, continued: the *real* bug was normalizing against the full sample rate instead of Nyquist

User provided a spectrum-analyzer screenshot (white noise through the filter,
Cutoff at max/20000Hz, Q=0) showing the response already down substantially
by ~5kHz and falling off a cliff well before 20kHz - i.e. **"fully open" was
nowhere near actually open**. This was a much bigger finding than the earlier
0.45->0.49 clamp tweak (which only shrank the top-of-knob dead zone without
fixing the underlying mismatch).

Checked `ChaoscapeEngine.h` (this filter's origin, per its own porting
comment) - confirmed its `ProcessMoogLadder` uses the exact same
`fc = cutoffHz / mSampleRate` convention, so this wasn't a copy/paste
mistake introduced during the port; it's an inherited miscalibration from
the source project. Chaoscape's own file also had a directly relevant comment
nearby (about its Mode 4 "click" filter) explaining this exact ladder
formula's `0.35013*fSq*fSq` gain-compensation term is very sensitive to `fc`
- useful context, though the actual root cause here turned out to be
simpler.

**Verified numerically** with a standalone Python simulation of the exact
formula (run outside the plugin, feeding white noise through the same
difference equations and measuring the FFT magnitude at several
frequencies) - confirmed the spectrum-analyzer finding precisely: at
"cutoff=20000Hz" the simulated filter was already -12.6dB down at 5000Hz and
-58dB down at 20000Hz. Tried normalizing against **Nyquist**
(`sampleRate * 0.5`) instead of the full sample rate, with the clamp raised
to `0.98` (just short of Nyquist) - re-simulated and got dramatically closer
to genuinely open at max: -0.9dB at 5000Hz, -17dB at 20000Hz, for the same
"20000Hz" label. Also improved (though didn't perfectly fix) the low/mid
range: "1000Hz" label's true -3dB point moved from an extremely muffled
~150-200Hz (old normalization) to a still-low-but-much-better ~250-300Hz.
Explained plainly to the user that this classic Stilson/Smith-style
approximation's fixed coefficients (`1.16`/`0.35013`/`0.3`) inherently can't
be made perfectly 1:1 accurate to the Hz label without a real filter
redesign - this fix is a large, verified improvement, not a perfect one.

**Fix applied** (`ProcessMoogLadder` in `FirstSynth_DSP.h`): `fc` now computed
as `cutoffHz / (mSampleRate * 0.5)` (Nyquist-normalized) instead of
`cutoffHz / mSampleRate`, clamp raised from `0.49` to `0.98` accordingly
(supersedes the earlier 0.45->0.49 change - same clamp variable, new
meaning since the denominator changed). Confirmed the *outer* `cutoffHz`
clamp just above this function (`cutoffHz = max(20., min(mSampleRate*0.49,
cutoffHz))`) is numerically consistent with the new inner clamp
(`0.98 * 0.5 * sampleRate == 0.49 * sampleRate` - same ceiling, no conflict).
Rebuilt all 3 targets, all succeeded cleanly.

**Not yet re-verified live by the user** - next steps: (1) re-check the
spectrum analyzer at max Cutoff/Q=0 to confirm it now looks genuinely flat/
open up near 20kHz instead of rolling off from ~2kHz; (2) re-check the
"1000Hz sounds inaudible" complaint - should be meaningfully better, though
per the simulation above, don't expect the knob's Hz label to be perfectly
1:1 accurate anywhere in its range, that's an inherent limit of this filter
model, not a bug; (3) re-confirm no instability/self-oscillation at extreme
Cutoff+Resonance now that the effective usable range of `fc` is much wider
than before (`0.98` vs the old `0.49`, which itself was vs `0.45` - the
*headroom* before hitting Nyquist changed meaning between these, worth a
fresh stability check rather than assuming the earlier "no problems" result
still applies unchanged).

Separately, flagged the same normalization mistake in
[[project_chaoscape_chainreaction_synths]] (`ChaoscapeEngine.h`'s own
`ProcessMoogLadder` has the identical `cutoffHz/mSampleRate` convention, per
the user's request to note it there too) - not fixed there, just recorded.
Checked GrainField too - it only *mentions* `ProcessMoogLadder` in a comment
for comparison, doesn't actually have a copy of the function, so nothing to
flag there.

## 2026-08-16 — Filter Type (LP/BP/HP switch) retired; fixed HPF added in series after the Filter

User request: stop the selectable Filter Type - Filter is LP-only now (the
Moog ladder). Add a **separate, fixed second filter stage in series after
it**: a plain highpass with **only a cutoff knob, no resonance/Q control**,
placed in the UI exactly where the old LP/BP/HP 3-way switch used to be.

**Params** (`FirstSynth.h`): appended `kParamHPFCutoff` at the very end (per
this project's "never renumber" convention) - param-id 112. `kParamFilterType`
and `kParamFilterSlope` (already-retired 2026-08-09) are both left registered
but now fully vestigial/unused, same reasoning as before - deleting them would
shift every later param's index and break old saved presets/DAW automation
lanes that might reference them.

**DSP** (`FirstSynth_DSP.h`): removed the `if (mFilterType < 0.5) {...} else
{...}` branch in `ProcessSamplesAccumulating` - now unconditionally
`T filtered = ProcessMoogLadder(dry, cutoffHz, qNorm/100.);`. Removed the
BP/HP-only `mFilterStage1`/`mFilterStage2` `SVFStage` members,
`kSvfMaxQResonanceScale`, and the now-fully-dead `BlendFilterOutputs()`
helper (confirmed via grep it had no other callers). Added the new stage:
a single `SVFStage<T> mHPFStage` member (reusing the existing, proven TPT
state-variable filter class rather than writing a new filter type from
scratch), driven by a **fixed** Butterworth damp constant
(`kHPFDamp = 1/sqrt(2)`, not exposed as a param, matching "cutoff knob only")
and its own `mHPFCutoff` member (default 20Hz, i.e. the range's own minimum -
so a preset that never saved a value for this brand-new param starts
effectively inert rather than silently thinning the bottom end). Applied
after the Moog ladder's own output, taking just the SVF's `high` output
(`filtered = hpfHigh;`). Added `case kParamHPFCutoff:` to `SetParam()`.
`GetParam(kParamHPFCutoff)->InitFrequency("HPF Cutoff", 20., 20., 20000.)` in
`FirstSynth.cpp`'s constructor, same Hz range/log-shape convention as the
main Cutoff knob.

**UI** (`resources/web/index.html`): removed the old 3-way switch markup
(`.filter-type-selector`, its 3 `.shape-btn`s) and its dedicated CSS rule,
replaced with `<knob-control label="HPF Cutoff" param-id="112" units="Hz"
shape="log" integer-display>` in the exact same grid slot. Removed
`SetFilterType()`/`UpdateFilterTypeSelection()` JS functions and the
`if (param === 24) {...}` special-case branch in `OnParamChange` (param 24 -
`kParamFilterType` - now has no UI element at all; the generic
`knob-control[param-id]` fallback safely no-ops on an empty NodeList for it,
confirmed no JS error risk).

Rebuilt all 3 targets clean (0 errors). Launched Standalone as a smoke test -
process stayed running, no crash. **User confirmed working**: the HPF Cutoff
knob shows up in the Filter panel where the old switch was, and it audibly
removes low end in series after the LP.

## 2026-08-16 — Filter LFO Random/Square shapes causing big momentary volume spikes - fixed with cutoff smoothing

User reported large, momentary volume spikes when the Filter LFO's **Random**
or **Square** shape modulates Cutoff - not reported for the other (continuous)
LFO shapes.

**Diagnosis**: Random (sample-and-hold) and Square are the only Filter LFO
shapes that change value *instantaneously* rather than ramping continuously.
Feeding a true instant jump in `cutoffHz` into `ProcessMoogLadder()` hits its
strongly nonlinear gain-compensation term (`0.35013*fSq*fSq`, a 4th power of
the normalized cutoff) - a sudden large change there, while the ladder's own
internal state (`mMoogOut1-4`) is still "tuned" to the previous cutoff, can
produce a momentary transient/spike before the state catches up. This is a
known general characteristic of time-varying resonant filters, not specific
to this implementation, just most audible with genuinely discontinuous
modulation sources.

**Fix**: added a fast (~3ms) one-pole smoother on the *final* summed
`cutoffHz` (after Env Amount/Filter LFO/Key Follow/Matrix are all combined),
applied right before it reaches `ProcessMoogLadder` - `mSmoothedCutoffHz`/
`mCutoffSmoothCoeff` (new `Voice` members in `FirstSynth_DSP.h`), coefficient
computed once in `SetSampleRateAndBlockSize()` (`1 - exp(-1/(0.003*sampleRate))`),
applied per-sample in `ProcessSamplesAccumulating` via
`mSmoothedCutoffHz += (cutoffHz - mSmoothedCutoffHz) * mCutoffSmoothCoeff`.
3ms is short enough that fast LFO sweeps and other modulation should still
sound essentially as sharp as before, but long enough to turn a true
zero-time jump into a fast ramp the filter's state can track without a
transient. Only applied to the main Filter's cutoff (the new HPF stage
added earlier this session isn't LFO/Matrix-modulated, so wasn't in scope).

Rebuilt all 3 targets clean (0 errors). **User confirmed improved** -
Random/Square Filter LFO modulation sounds better, no more report of the
volume spikes.

## 2026-08-18 — Standalone "abort() has been called" dialog after closing the window - first seen, NOT reproduced, unresolved

User reported a Debug CRT dialog ("Microsoft Visual C++ Runtime Library -
Debug Error! ... abort() has been called", Abort/Retry/Ignore) appearing
**after closing** the Standalone window (`FirstSynth_x64.exe`) - not during
active use. First time seen ("今日初めて見た"), nothing unusual was
happening right before closing ("特に何もしていなかった").

**Investigated, found nothing**: unlike the earlier Chorus crash this same
session (which had a proper `0xc0000409` WER event + eventually a full
minidump), this one left **no trace at all** - no `Get-WinEvent` Application
log entry (checked both a targeted ID 1000/1001 filter and a broad
last-30-minutes scan), no new file in `C:\CrashDumps` (LocalDumps is enabled,
see the earlier Chorus crash entry - didn't catch this one), no WER
ReportArchive/ReportQueue entry newer than 2026-07-23. `abort()`/SIGABRT
apparently doesn't reliably route through the same WER unhandled-exception
capture path a structured exception (like the earlier vector-subscript one)
does, at least not for however this one was dismissed.

**Tried to reproduce**: launched and closed Standalone (`CloseMainWindow()`
via PowerShell) 2x in a row - both exited cleanly, no dialog. Not
reproducible on demand from a simple launch-then-immediately-close sequence.

**Not connected to today's changes as far as can be told** - nothing changed
today (Moog ladder normalization, Filter Type retirement/HPF, cutoff
smoothing) touches shutdown/destructor/audio-device-teardown code at all;
this could be a pre-existing, rare, possibly heap-corruption-triggered issue
that just happened to surface today, not a regression from today's work.
This project's own history has at least one earlier mention of a similar
unexplained pattern (see the 2026-08-09 "Ctrl+S" entry's "same
unexplained-idle-exit pattern noted in the entry just above").

**Status: unresolved, low-information.** Asked the user to click "再試行"
(Retry) instead of Abort/Ignore next time it appears - this normally tries
to attach a JIT debugger, though none is configured on this machine (no
WinDbg/cdb, see the Chorus crash investigation's own notes on tooling), so it
may not actually help; otherwise just dismiss with Ignore as before. No
actionable next step until it reproduces again with more context (what was
open/in use, whether it's *always* on close or intermittent, any pattern in
timing).

## 2026-08-24 — eni_auth 購読ライセンスゲートをUI接続、3ターゲットともビルド確認・ロック画面確認済み

伊藤さん(mako)が用意した`eni_auth/`ライブラリ（PR、既にmainへマージ済み・commit
`1922160`）を、Standalone/CLAP/VST3の3ターゲット全てに接続した。プラン:
`C:\Users\a_wak\.claude\plans\replicated-launching-book.md`。

**やったこと**（プランの Step 1-10 通り）:
- `FirstSynth-app/clap/vst3.vcxproj` に `eni_auth/eni_auth.cpp` /
  `eni_http_win.cpp` / `eni_json.cpp` / `vendor/monocypher/*.c` の5ファイルを
  `<ClCompile>` に追加。`winhttp.lib`のリンクは`eni_http_win.cpp`自身の
  `#pragma comment(lib, ...)`任せで、vcxproj側の追加設定は不要だった
- `FirstSynth.h`: `EMsgTags`に`kMsgTagLicenceState`/`LoginRequest`/
  `DeviceCode`/`LoginResult`(17-20)を追記。`mLicence`(構造体、非atomic)と
  `mLicenceValid`(atomic、ProcessBlockが読む唯一のフィールド)を分けて持つ設計
  - `mLicence`はコンストラクタとログイン成功後の`OnIdle()`の2箇所でしか書かない
    ことで、非trivial構造体をスレッド間で受け渡す必要をなくした
- `FirstSynth.cpp`: コンストラクタ先頭で`eni::CheckLicence()`、
  `ProcessBlock()`先頭で`mLicenceValid`ゲート(無効なら出力を0埋めしてreturn)、
  `OnMessage()`にログインボタン処理(`RunDeviceFlow()`を`std::thread(...)
  .detach()`で実行、この既存パターンの唯一の先例は`eni_auth.cpp`自身の
  `RefreshInBackground()`)、`OnIdle()`にdirty flag経由のUI push、
  `OnWebContentLoaded()`に初期state push
- `resources/web/index.html`: `#licence-lock-screen`オーバーレイ
  (`SetPage()`と同じ`style.display`切替の流儀)、3メッセージタグのJS側配線、
  `SAMFUI()`でのログインボタン送信

**ビルド時に見つかった実バグ、修正: `eni_auth.cpp`が生のUTF-8日本語文字列
リテラル(`"有効なサブスクリプションがありません"`等)を含んでおり、この
プロジェクトの通常のMSVCビルド(コードページ932想定)だとC2001/C2143の構文
エラーで**コンパイルが通らなかった**。FirstSynth.cpp自身の日本語はコメント
内だけなので今まで顕在化しなかった問題。`/utf-8`コンパイラオプションを3つの
vcxprojの全6設定×3ファイル=18箇所の`<ClCompile>`に`AdditionalOptions`として
追加して解決(ライブラリ自体のファイル内容は一切変更していない)。共有の
`iPlug2\common-win.props`は触っていない(他プロジェクトへの影響を避けるため、
FirstSynth固有の3つのvcxprojだけに限定)。

**確認済み**: Standalone/CLAP/VST3の3ターゲットとも警告0・エラー0でビルド成功。
このマシンには`%APPDATA%\easyandnice\license.json`が存在しないため、
Standaloneを起動してそのまま「未ライセンス状態」の実地確認になった
(`PrintWindow`でユーザーの別作業(SunVox)の邪魔にならない形でキャプチャ) -
ロック画面(「FIRSTSYNTH はロック中です」+ ログインボタン)が
ヘッダー/プリセットバー/演奏画面ごと正しく覆っていることを確認。

**未確認(ユーザー側での確認が必要)**: ログインボタンのクリック
(WebView2内のUI Automationがこの環境では効かず、`ブラウザでログイン`
ボタンが見つからなかった - 過去のセッションでも記録されているWebView2の
既知の制約と同じ)、実際のDevice Flowログイン完了、CLAPでのREAPER動作確認、
音声ゲート(ロック中に鍵盤を弾いても無音であること)の実地確認。
プランファイルの「検証」セクション参照。

## 2026-08-18 — VST3 editor cropping in real hosts (BespokeSynth) - ROOT CAUSE FOUND AND FIXED, plus two related bugs found along the way

Picked back up the long-paused "editor opens cropped in real VST3 hosts, user
has to manually drag it bigger every time" issue (last touched 2026-07-30,
"PLUG_HOST_RESIZE experiment" and "VST3 editor crop/scrollbar investigation"
entries above), this time reproducing live in **BespokeSynth (VST3)**.

**Root cause, found via a research subagent + live diagnostic logging**:
`IPlugVST3_View::getSize()` (shared iPlug2 framework code,
`IPlug/VST3/IPlugVST3_View.h`) returns `GetEditorWidth()/GetEditorHeight()`
(i.e. `PLUG_WIDTH`/`PLUG_HEIGHT`, currently 1387x780) completely unscaled. Per
the VST3 spec, `ViewRect` on Windows is *physical* pixels - so on this
machine's 125%-scaled display, the host was being told "make the window
1387x780" and building a parent that's *physically* 1387x780, which is only
~1110x624 in the 96-DPI-equivalent terms the content actually needs. Standalone
never showed this because it explicitly multiplies by the display scale itself
(`IPlugAPP_dialog.cpp`'s `ClientResize`) before ever sizing its own window -
VST3's `getSize()` path had no equivalent.

**First fix attempt (reverted)**: tried returning `kResultFalse` from
`setContentScaleFactor()` (telling the host "I don't handle scaling, please
scale for me"). Symptom changed but didn't resolve: the window would flash to
the correct larger size then immediately snap back small. Added temporary
diagnostic logging (`%TEMP%\vst3_size_debug.log`, same convention as the
2026-07-30 investigation) at every relevant call site
(`setContentScaleFactor`/`getSize`/`attached`/`onSize`/`SetWebViewBounds`/the
async WebView2-controller-created callback) and got a full, timestamped trace
of BespokeSynth's actual sequence. It proved the *opposite* of the working
theory: BespokeSynth doesn't scale `getSize()`'s value itself at all - it just
uses whatever `getSize()` returns *directly* as the target window size, once
it's finished processing `setContentScaleFactor`. So `kResultFalse` was wrong;
reverted to `kResultOk`.

**Real fix**: `WebViewEditorDelegate::SetScreenScale()` (`IPlug/Extras/WebView/
IPlugWebViewEditorDelegate.h`) was the base `IEditorDelegate`'s no-op default -
added a real override that captures the *un-scaled* base size once (from
whatever `GetEditorWidth()/GetEditorHeight()` held before any scaling, i.e.
the raw `PLUG_WIDTH`/`PLUG_HEIGHT`) and calls `SetEditorSize(baseW*scale,
baseH*scale)` every time the host reports a scale factor - added a
`GetScreenScale()` getter alongside it. Confirmed via the same diagnostic
logging that the *next* `getSize()` call (BespokeSynth re-queries it right
after each `setContentScaleFactor`) then returned the correctly-scaled
1734x975, and the window settled there - **the original cropping bug is
fixed**.

**Second bug found while verifying**: after the fix, opening at the correct
size worked, but manually toggling the WebView's own "Zoom" dropdown to 100%
expanded the window yet still didn't show the full GUI ("拡大しますが、GUI
全部は見えません"). Cause: `FirstSynth.cpp`'s `kMsgTagSetUIScale` handler
computed its resize target from the *raw* `PLUG_WIDTH`/`PLUG_HEIGHT` macros
(`newW = round(PLUG_WIDTH * scalePercent/100)`), with no DPI awareness at
all - a second, independent path to the same root problem. Fixed by
multiplying through the new `GetScreenScale()` too:
`newW = round(PLUG_WIDTH * GetScreenScale() * scalePercent/100)`. This
project-specific handler likely has twins in SuiKinKutsu/GrainField/Compost
(same UI-zoom-feature pattern) - not checked/fixed there yet.

**Third bug found while verifying** (unrelated to DPI, a real separate
finding): after both fixes above, the editor still visually "shrank" on
open - traced to the WebView's own **UI Zoom localStorage value reading 80%**
that the user never set for BespokeSynth. Root cause: `IPlugPaths.cpp`'s
`WebViewCachePath()` (**shared framework code**) returned a single **hard-
coded path**, `%APPDATA%\iPlug2\WebViewCache`, used as WebView2's user-data-
folder (and therefore its `localStorage`/cookie store) by *every* iPlug2
WebView plugin, in *every host*, on the whole machine - confirmed live: an
80% Zoom set earlier this session while debugging FirstSynth in **Studio
One** had bled into this completely separate **BespokeSynth** session, purely
because both happened to load a WebView2 environment under the same shared
folder. Fixed by namespacing the path with `BUNDLE_NAME` (per-project
config.h macro - separates *products*, e.g. FirstSynth vs. SuiKinKutsu) and
the current process's own executable name via `GetModuleFileNameA(nullptr,
...)` (separates *hosts*, e.g. Studio One vs. BespokeSynth vs. Standalone vs.
REAPER) - `%APPDATA%\iPlug2\WebViewCache\<BUNDLE_NAME>\<hostExeName>`. Needed
`#include "config.h"` added to `IPlugPaths.cpp` (same pattern already used by
`IPlugAPP_dialog.cpp`/`IPlugAPP_main.cpp` for the same reason). This is a
**real, previously-undiagnosed bug affecting every iPlug2 WebView-based
project on this machine**, not just today's symptom - SuiKinKutsu, GrainField,
Compost, Chaoscape (if WebView-based) all share this same framework file and
would all need rebuilding to pick up the fix. Side effect (expected, harmless,
not migrated): every existing saved WebView preference (Zoom, Dark Mode, PEQ
Lock, etc.) for every project/host combo resets to defaults once, the first
time each is rebuilt against this fix, since the old shared cache folder is
simply no longer read.

**Two default changes, per explicit user request** (`resources/web/index.html`):
- Dark Mode now defaults to dark when nothing's saved yet (was light) -
  `localStorage.getItem(kDarkModeStorageKey) !== '0'` instead of `=== '1'`.
- UI Zoom now defaults to 80% when nothing's saved yet (was 100%) -
  `RestoreUIScale()` treats an unset value as 80 instead of doing nothing.
Both still fully respect an explicit saved preference either way - these only
change what happens on a genuinely first-ever launch (or, incidentally, right
after the cache-path fix above resets everyone once).

**Diagnostic logging fully removed** after root-causing (`LogVstSize` in
`IPlugWebView_win.cpp`, `LogVstViewSize` in `IPlugVST3_View.h`, and their
call sites) - was explicitly temporary, matching the 2026-07-30
investigation's own convention.

**Files touched, all shared iPlug2 framework code except the one noted**:
`IPlug/VST3/IPlugVST3_View.h`, `IPlug/Extras/WebView/
IPlugWebViewEditorDelegate.h`, `IPlug/Extras/WebView/IPlugWebView_win.cpp`,
`IPlug/IPlugPaths.cpp`, and `FirstSynth.cpp` (project-specific: the
`kMsgTagSetUIScale` handler fix) + `resources/web/index.html` (project-
specific: the two default changes).

**Confirmed working end-to-end by the user** in BespokeSynth (VST3): editor
opens at the correct size with no manual resize needed, in Dark mode, at 80%
Zoom, first try, no leftover cropping. Rebuilt and reconfirmed with the
diagnostic logging removed. Standalone/CLAP were rebuilt too (share the
touched framework files) but not yet independently re-verified live - should
be low-risk (same code paths, scale=1.0 no-ops correctly through all of this
on a 100%-scale target, and Standalone already had its own correct DPI
handling that this doesn't change).

**Not yet done**: confirmed via `find` that Chaoscape/SuiKinKutsu/GrainField/
GrainKit(Compost)/UeberLooper have no `iPlug2` folder of their own - they all
reference this exact same shared checkout (`C:\Users\a_wak\CLAP_plugin\
iPlug2`) via relative paths, same as FirstSynth. So the 3 shared-framework
fixes above (getSize DPI scaling, SetScreenScale/GetScreenScale,
WebViewCachePath namespacing) already apply to all of them automatically -
**just rebuilding each project picks up the fix, no porting needed**. Still
worth doing (and checking each project's own UI-zoom-feature code, if any,
for the same `kMsgTagSetUIScale`-style raw-PLUG_WIDTH pattern FirstSynth had)
next time any of them is worked on - not done proactively this session since
it wasn't asked for.

## 2026-08-25 — Preset dropdown showed a leftover name ("5th Strings") while the actual sound was the init patch

**User report**: loading FirstSynth fresh in a DAW showed "5th Strings" in
the preset dropdown, but the actual sound was close to the init patch - name
and sound disagreed.

**Root cause**: the WebView preset dropdown (`index.html`) fell back to a
browser-wide `localStorage` key (`kLastPresetStorageKey`, written by
`LoadPreset()` every time *any* preset was loaded, *anywhere*) whenever the
`<select>` had no value of its own yet - i.e. every fresh WebView page load,
which happens on every editor GUI open (VST3/CLAP hosts fully tear down and
recreate the WebView on each open/close - confirmed via `IPlugWebView_win.cpp`'s
`CloseWebView()`/`mWebViewCtrlr->Close()`). That guess was purely cosmetic -
it set the dropdown's displayed selection but never told C++ to actually load
anything. A brand-new VST3/CLAP instance has no restore mechanism at all
(`LoadAutoState()` is `#ifdef APP_API` - Standalone only), so a fresh insert
into a DAW project genuinely starts at the init patch while the label showed
whatever preset was last loaded in *any* FirstSynth instance on that host
(possibly a completely different project, possibly hours earlier).

**Considered and rejected**: auto-loading the guessed name for real (so label
and sound would always match) - this looked like the obvious fix but is
actually dangerous. Since closing/reopening the editor GUI on an *existing*,
already-correctly-loaded instance also re-triggers this same fresh-WebView-page
code path, auto-loading on that path would have silently discarded the real
current state (host-restored params, or live tweaks since the GUI was last
open) and replaced it with some old preset file's contents - a much worse bug
than a mislabeled dropdown, just from clicking to reopen the plugin window.

**Actual fix**: stopped guessing. `mCurrentPresetName` (new `WDL_String`
member, `FirstSynth.h`/`.cpp`) is now the single source of truth, updated only
by a real `LoadPresetByName()`/`SavePresetAs()`/`DeletePresetByName()` on
*this* instance - never by a WebView reload. Pushed to the UI via a new
`kMsgTagCurrentPresetName` message (C++ -> UI, UTF8, sent from
`OnWebContentLoaded()` via `SendCurrentPresetName()`), appended to `EMsgTags`
per this project's "never renumber" convention. `index.html`'s
`UpdatePresetList()` no longer reads `localStorage` at all - the dropdown
shows nothing selected until this authoritative name arrives, so it can never
disagree with the real sound again. `kLastPresetStorageKey` removed entirely
(dead code once nothing read it).

Since `mCurrentPresetName` lives only in memory (deliberately *not* added to
`SerializeState()`'s own chunk - that chunk's binary layout is shared
byte-for-byte with real DAW project state and every `*.preset` file, and this
project has been bitten by exactly that kind of layout mismatch before), it
correctly:
- stays blank on a genuinely fresh instance (matches the init-patch sound - the
  original bug, fixed)
- survives a GUI close/reopen on an already-running instance (the C++ object
  isn't destroyed when the WebView is, so the label stays honest across that
  too, not just on first open)
- has no way to leak across instances/projects/hosts, unlike the old
  localStorage approach

To keep the one working part of the old behavior - a Standalone relaunch
recalling its last preset's *name*, not just its sound - `SaveAutoState()`/
`LoadAutoState()` (Standalone-only, `#ifdef APP_API`) now also write/read a
tiny sidecar text file, `autosave_presetname.txt`, right next to
`autosave.state`. Deliberately a separate file rather than appending to
`autosave.state`'s own chunk, same byte-layout-safety reasoning as above.

**Build**: Standalone, VST3, and CLAP (Debug|x64) all rebuilt clean, 0
errors, after closing BespokeSynth to release the file lock.

**Not yet done**: not re-verified live in a DAW yet (or Standalone) - next
step is inserting a fresh instance and confirming the dropdown shows nothing
selected (not a wrong name), then Save/Load/Delete and a GUI close+reopen to
confirm the label still tracks correctly through those.

## 2026-08-25 — "Factory" preset marking, for developing with shipped and work-in-progress presets mixed together

**User request** (prompted by the previous entry's discussion of what a new
subscriber's preset list would look like today - currently empty, since
nothing seeds `%APPDATA%\FirstSynth\Presets\` on first run): be able to keep
developing with finished ("1軍"/factory) and work-in-progress ("2軍") presets
mixed in the same folder/dropdown, while being able to tell them apart, so a
future packaging step can pick out just the factory ones.

**Design**: a small sidecar text file, `<GetPresetsDir()>\_factory.txt`, one
sanitized preset name per line - deliberately not touching the `.preset`
files' own binary format (same reasoning as `mCurrentPresetName`'s own sidecar
file two entries up). The leading underscore keeps it out of
`SendPresetList()`'s enumeration (already filtered to the `.preset`
extension), so it lives right alongside the real presets without ever
appearing as one itself.

**C++** (`FirstSynth.h`/`.cpp`): `GetFactoryMarksPath()`, file-local
`ReadFactoryMarks()`/`WriteFactoryMarks()` helpers, `SendFactoryPresetList()`
(pushes the sidecar's contents via new message `kMsgTagFactoryPresetList`),
`TogglePresetFactoryMark()` (sanitizes, flips membership, persists, refreshes
UI - wired to new UI->C++ message `kMsgTagPresetToggleFactory`). Both messages
appended to `EMsgTags` per this project's "never renumber" convention.
`DeletePresetByName()` now also strips a deleted preset from the marks file if
present, same spirit as its existing `mCurrentPresetName` cleanup right next
to it.

**UI** (`resources/web/index.html`): new "☆ Factory" / "★ Factory" toggle
button in the preset bar, next to Delete - click sends the currently-selected
preset's name via `kMsgTagPresetToggleFactory`. `factoryPresetNames` (a `Set`,
populated by the new message's handler) drives `ApplyFactoryMarkers()`, which
prefixes marked entries with "★ " in the dropdown's option labels - called
after `UpdatePresetList()` rebuilds the list and after the factory-list
message itself arrives (either can land first, both call it). The button's own
label/disabled state is kept in sync via `UpdateFactoryToggleButton()`, called
from every place `currentPresetName` changes (`LoadPreset()`, `DeletePreset()`,
the `kMsgTagCurrentPresetName` handler, and the empty-list early-return path in
`UpdatePresetList()`).

**Not yet done**: the actual packaging/installer step that would read
`_factory.txt` and seed a new user's Presets folder from just those files -
out of scope for this request (see the previous entry's "BEFORE DISTRIBUTION
CHECKLIST" - no installer pipeline exists yet at all). This just adds the
marking mechanism a future packaging step will read from.

**Build**: Standalone, VST3, and CLAP (Debug|x64) all rebuilt clean, 0 errors.

**Not yet verified live**: needs a manual check in Standalone - open, pick a
preset, click the Factory toggle, confirm the label/star updates and
`_factory.txt` gets written in `%APPDATA%\FirstSynth\Presets\`, reload the
list (e.g. via Save As on a different preset) and confirm the mark survives.

## 2026-08-25 — Release-mode GUI never opened on Windows, root cause fixed (shared iPlug2 framework)

**Context**: while discussing the previous entry's "Factory preset" feature
(only meant to be visible during development, not in what ships), realized
hiding it for a real distributed build would need a working Release build to
even test against - and this project's own [[feedback_iplug2_webview_windows_release.md]]
memory said Release-mode WebView Standalone builds don't load their GUI at
all on Windows. User confirmed this bug itself was the more urgent thing to
fix first.

**Root cause** (`IPlug/Extras/WebView/IPlugWebViewEditorDelegate.h`'s
`LoadIndexHtml()`, shared iPlug2 framework, affects every WebView-based
project on this machine, every format): Debug builds resolve `index.html`
from a path baked in at compile time via `__FILE__` (fine on the compiling
machine only). Release builds fell through to `LoadFile("index.html",
bundleid)` - a bare filename with no directory. Windows' own
`IWebViewImpl::LoadFile()` (`IPlugWebView_win.cpp`) never actually uses the
`bundleID` parameter at all (that's a macOS/iOS-only resolution path) - it
derives the resources folder purely from whatever directory is embedded in
the filename string itself. A bare filename has no directory, so the
resolved folder was empty and WebView2 showed ERR_FILE_NOT_FOUND - the GUI
never opened, in any Release-configuration build, on every format
(Standalone/VST3/CLAP/...), confirming and root-causing what the earlier
memory only worked around by "always build Debug."

**Fix**:
- New `GetCurrentModuleDirWin()` (`IPlugWebView_win.cpp`) - resolves the
  folder containing whichever module (.exe for Standalone, the .vst3/.clap
  DLL for the others) this code is actually compiled into, at runtime, via
  `GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, ...)` on its
  own function address - no per-format special-casing needed, unlike the
  old dev-machine-only `__FILE__` approach.
- `LoadIndexHtml()` now has a third branch (`#elif defined OS_WIN`, between
  the existing Debug and non-Windows-Release branches): builds a real path,
  `<moduleDir>\Resources\web\index.html`, from that and passes it to
  `LoadFile()` - same mechanism the Debug branch already relies on, just
  computed at runtime instead of compile time.
- `scripts/postbuild-win.bat` now actually copies `resources\web\*` next to
  every built binary - `Resources\web\` alongside the Standalone .exe in
  `%BUILD_DIR%`, inside the VST3 bundle's `Contents\x86_64-win\Resources\web\`
  (before the bundle gets xcopied to the real VST3 install path, so it comes
  along automatically), and next to the installed .clap in
  `%CLAP_X64_PATH%\Resources\web\`. This was the other missing half - the
  code fix alone had nothing to copy from without this. Mirrored for the
  ARM64EC platform block too (untested on this machine, but kept consistent).

**Verified**: rebuilt all 3 formats in Release|x64 - 0 errors, and each
postbuild run logged its new "copying WebView resources..." line. Confirmed
`Resources\web\index.html` actually landed in all 3 real locations
(`build-win\Resources\web`, the VST3 bundle at both the staging path and
`C:\Program Files\Common Files\VST3\FirstSynth.vst3\...`, and
`%LOCALAPPDATA%\Programs\Common\CLAP\Resources\web`). Launched the Release
Standalone .exe and captured its window via `PrintWindow` (no focus-stealing,
same technique as the 2026-08-24 eni_auth lock-screen check) - **GUI renders
correctly**: full Synth page, Zoom 80%, Preset "Organ2" correctly recalled,
the new Factory toggle button visible. VST3/CLAP weren't visually confirmed
in a real DAW this session (no GUI automation available for that), but the
underlying mechanism is identical and the resource files are confirmed
correctly placed - low risk.

Rebuilt all 3 formats back in Debug|x64 afterward to leave the normal dev
config as the current build-win state (Debug still works unaffected - it
still takes the original `__FILE__`-based branch, untouched by this fix).

**Not yet done**: [[feedback_iplug2_webview_windows_release.md]] should be
updated/superseded now that a real fix exists (currently still says "build
Debug, not Release" as the only workaround). Same shared-framework situation
as the 2026-08-18 DPI fix - SuiKinKutsu/GrainField/Compost/Chaoscape/
UeberLooper all share this exact iPlug2 checkout, so this fix already applies
to them too, but each project's own `postbuild-win.bat` still needs the same
resource-copying addition (that part is project-specific, not shared) before
any of them could actually ship a working Release build.

## 2026-08-25 — Factory toggle button hidden from Release builds

Follow-up to the Release-mode GUI fix above, completing the original request:
the "Factory" preset-marking button is a developer-only tool, not something a
real end user's copy should show.

**Implementation**: `index.html`'s `#factoryToggleBtn` now starts
`display: none` in the markup itself (not just disabled) - starting hidden
and only ever being *revealed* is safer than starting visible and hiding it
later, since a Release build can then never show it even briefly if the
reveal call were ever slow/failed. New JS function `SetDevBuild(isDev)` sets
`style.display`. `FirstSynth.cpp`'s `OnWebContentLoaded()` calls it right
after `SendFactoryPresetList()`, with the boolean baked in via `#ifdef
_DEBUG` (true) / `#else` (false) - so it's the actual MSBuild Configuration
that decides, same signal `LoadIndexHtml()` itself already keys off.

**Verified live** (Standalone, `PrintWindow` screenshots, same technique as
above): Debug build shows the button in the preset bar; the real packaged
Release build (`build-win\FirstSynth_x64.exe` - not the raw MSBuild output
copy at `build-win\app\x64\Release\FirstSynth.exe`, which has no
`Resources\web` next to it and correctly shows a WebView2 network-error page
if launched directly instead) shows a normal preset bar with no Factory
button, GUI otherwise fully working. VST3/CLAP Release also rebuilt clean
(0 errors) with the same change, not re-screenshotted (same code path as
Standalone, already covered above). All 3 formats rebuilt back to Debug
afterward to leave the normal dev config in place.

## 2026-08-25 — Debug and Release Standalone builds were silently overwriting each other

**User report**: launched Standalone themselves, saw no Factory button, asked
whether that meant this was actually a Release build, and asked for a way to
switch between the two.

**Root cause**: `postbuild-win.bat` copied both Debug and Release Standalone
builds to the exact same filename, `build-win\FirstSynth_x64.exe` - whichever
config was built most recently silently overwrote the other. Earlier the same
session, verifying the Release-mode GUI fix meant Release was built last,
leaving that overwriting the Debug copy the user then double-clicked -
confirmed via `tasklist` that the process the user had running was that stale
Release copy.

**Fix**: `config/FirstSynth-win.props`'s postbuild `CALL` now passes
`"$(Configuration)"` as an extra trailing argument; `postbuild-win.bat` reads
it (`CONFIGURATION`) and copies Release to a distinctly-named
`%NAME%_%PLATFORM%_Release.exe` instead, leaving Debug's filename exactly as
it always was (`FirstSynth_x64.exe`) so the user's existing muscle memory and
tooling (`CPU_Monitor.bat`) keep working unchanged. Both `.exe` blocks
(ARM64EC and x64 platform sections) updated identically. Shared by all 5
formats' vcxproj (`config/FirstSynth-win.props` is imported by all of them),
but only `.exe` (Standalone) actually branches on it - VST3/CLAP/etc. still
just install to one fixed path each, matching how any real DAW plugin folder
already works (no equivalent ambiguity to resolve there).

**Verified**: rebuilt Debug then Release - confirmed via the postbuild log's
own echo line that each copies to the right distinct filename, and via
`PrintWindow` screenshots of each launched separately (both exes exist
simultaneously in `build-win\`, but only one can run at a time - some kind of
single-instance/audio-device lock, unrelated to this fix) - Debug
(`FirstSynth_x64.exe`) shows the Factory button, Release
(`FirstSynth_x64_Release.exe`) doesn't, GUI otherwise identical and working
in both.

**How to apply going forward**: after testing a Release build for any
reason, no need to rebuild Debug back over it anymore - both filenames now
coexist, so the user's normal `FirstSynth_x64.exe` double-click always stays
whatever was last built *as Debug*, regardless of any Release builds done in
between.

Two desktop shortcuts added (user request, "使い分けできるように"):
"FirstSynth（開発版）" -> `build-win\FirstSynth_x64.exe`, "FirstSynth（Release版）"
-> `build-win\FirstSynth_x64_Release.exe`. Point at the build output paths
directly, so they always reflect whatever's most recently built there - no
need to recreate them after a rebuild.

`tools\cpu_monitor.ps1` also updated (follow-up question: which one is the
CPU meter tied to - turned out neither, confirmed via `Get-Process -Name
"FirstSynth"` returning nothing for either renamed exe). Was doing an exact
`-Name "FirstSynth"` match, which stopped working the moment Debug/Release
split into different filenames (Windows process names always match the
launched exe's own filename). Changed to a `"FirstSynth*"` wildcard picking
whichever one is actually running (`Select-Object -First 1`), and the label
now shows the real matched process name (e.g. "FirstSynth_x64" vs
"FirstSynth_x64_Release") instead of a hardcoded "FirstSynth", so it's
visible at a glance which build is being watched. Also resets the running
CPU-delta tracking when the matched process's PID changes between ticks
(e.g. closing Debug and opening Release) - comparing `TotalProcessorTime`
across two unrelated processes would have produced a meaningless number
otherwise. Verified live: launched Release, confirmed the monitor showed
"FirstSynth_x64_Release : N%"; closed it, launched Debug, confirmed it
switched to "FirstSynth_x64 : N%".

## 2026-08-25 — EQ Bypass switch, draggable curve points, bigger points

Three related user requests about the 5-band parametric EQ panel.

**Bypass switch**: new `kParamEQBypass` param (appended to `EParams`, real
enum value 113 - confirmed by counting entries, since none of them have
explicit numbers), `mEQBypassed` bool in `FirstSynth.h`/`.cpp` set directly
from `OnParamChange` (same non-atomic convention this file's other simple
effect toggles already use) and checked in `ProcessBlock()`:
`if (!mEQBypassed) mEQ.Process(...)`. UI: a "Bypass" toggle switch
(`index.html`, `SetEQBypass()`) placed next to the "5-Band EQ" heading inside
`#peqLockedContent` - i.e. only reachable once PEQ LOCK is opened, per the
user's own framing of the request. `param === 47 || param === 51` generic
checkbox-sync branch in `OnParamChange` (JS) extended to also cover 113.

**Draggable + bigger curve points** (`eq-curve-display.js`): the per-band
markers were a pure readout before - `POINT_RADIUS` bumped 3->6 (bigger, per
request), doubling as `POINT_HIT_RADIUS` (11, in the same canvas-internal
600x150 coordinate space the points are drawn in) for a pointerdown hit-test.
Dragging updates the curve's own `freq[band]`/`gainDb[band]` and redraws
live, clamped to the chart's `MIN_FREQ/MAX_FREQ` (20-20000, matches the real
`InitFrequency` range exactly) and a separate `PARAM_MIN_DB/MAX_DB` (-15/15,
the real `InitDouble` Gain range - narrower than the chart's own -18/18 axis,
which is intentionally wider so a maxed-out band doesn't render right at the
edge). Dispatches `point-drag-start`/`point-drag`/`point-drag-end` (all on
`this`, the host element, so no `composed:true` needed - same as
knob-control.js's own 'user-change' event).

**Reverse wiring** (`index.html`): new `kEQBandToParamIds` (band -> {freq,
gain} paramId, built once by inverting the existing `kEQParamMap`), new
`KnobRealToNormalized()` (exact inverse of the existing `KnobNormalizedToReal()`,
mirroring knob-control.js's own private `realToNormalized()` formula) - the
`point-drag` listener converts the drag's real Hz/dB back to normalized,
sends it via `SPVFUI()` (guarded with `typeof === 'function'`, unlike this
file's plain onclick handlers - matters for the file:// test harness below),
and calls the corresponding knob-control's `updateValueFromHost()` so the
knob's own dial/label stay in sync without re-triggering `UpdateEQCurve()`
in a redundant loop. `point-drag-start`/`-end` pair `BPCFUI`/`EPCFUI` around
the whole gesture (both freq and gain), matching the same "begin/end gesture"
contract knob-control.js's own drag already follows, so DAW automation-lane
recording sees one gesture per point-drag, not a value-per-pixel storm.

**Verification**: C++ compiled clean (all 3 formats, Debug). For the JS
(can't be exercised by a C++ build, and this repo lives outside the current
session's working directory so the Browser pane's `file://` preview would
only render a static, non-executing snapshot - see
[[feedback_cross_sandbox_file_delivery]]-adjacent caveat) - started a
throwaway local HTTP server (`python -m http.server`, via a temporary
`.claude/launch.json`, removed after) rooted at `resources/web`, opened it in
the Browser pane, and drove it directly:
- Unlocked PEQ LOCK via a real click - confirmed `#peqLockedContent` becomes
  visible and the Bypass checkbox (`data-param-id="113"`) exists with the
  right wiring.
- Confirmed `EQCurveDisplay.POINT_RADIUS === 6` / `POINT_HIT_RADIUS === 11`.
- Dispatched real `PointerEvent` sequences (down/move/up) at a band's exact
  on-canvas position, for two different bands - confirmed `freq[band]`/
  `gainDb[band]` moved in the correct direction each time (right/up = higher
  freq/gain, left/down = lower) and landed within the clamped range, with
  zero console errors once `SPVFUI`/`BPCFUI`/`EPCFUI` were stubbed to no-ops
  (this bare-HTTP-server test page has no real WebView2 host, so those
  bridge functions exist but throw internally calling the host-injected
  `IPlugSendMsg` - confirmed that's exactly what threw before stubbing, not
  a bug in this feature).
- Couldn't fully number-verify the knob-mirroring side (`updateValueFromHost`)
  in this harness specifically, because the knobs' real `min`/`max`
  attributes are only ever set by the host's initial "params" JSON message,
  which never arrives outside a real WebView2 session - confirmed the
  function does get called with no error, just couldn't confirm the exact
  displayed number is right without that missing host round-trip. The
  conversion math itself is an exact mirror of `KnobNormalizedToReal()`
  (already proven correct - it's what drives the existing knob->curve
  direction) and of knob-control.js's own private formula, so this is a
  format/attribute-availability gap in the test harness, not a reason to
  doubt the logic.
- Not yet re-confirmed live inside the real Standalone/VST3/CLAP host by the
  user themselves - worth a quick live check next time it's convenient.

User confirmed the above works live ("できてます") and asked one follow-up:
double-click any PEQ knob to reset it to default, with Gain specifically
resetting to 0dB.

## 2026-08-25 — EQ knob double-click-to-default: Gain resets to 0dB, not the tuned patch default

**Discovery**: double-click-to-default already exists for *every* knob-control
in the whole app (`knob-control.js`'s `onDblClick()`, resets to
`this.defaultValue` - generic, not EQ-specific), and `default-value` is
already populated for every knob from the real host param info
(`OnMessage()`'s `case "params"`, `paramInfo["default"]`). So this wasn't
missing plumbing - the actual gap was that EQ Gain's real C++ default (e.g.
`kParamEQLowGain`'s `InitDouble` default, 3.12dB) is a *tuned patch value*
(this synth's baseline sound, see that InitDouble call's own comment), not a
musically neutral "flat EQ" value - the user wants double-click to reach the
latter specifically for Gain, not reproduce whatever the patch happened to
start at.

**Fix** (`index.html`, `OnMessage()`'s `case "params"`): right after the
existing generic loop sets every knob's `default-value` from the host,
a second pass overrides it back to `"0"` for exactly the 5 EQ Gain param IDs
(57/59/62/65/68 - derived by filtering `kEQParamMap` for `kind === 'gain'`,
not hand-listed). Freq/Q knobs untouched - their own tuned defaults (125Hz,
0.78 Q, etc.) are still musically meaningful reset targets, unlike Gain.
Only the WebView-side double-click *reset target* changes - the real
param's actual default (initial patch value, DAW automation's own "reset to
default", `.preset` files, etc.) is completely unaffected.

**No C++ changes, no rebuild needed** - purely `index.html`/JS, and Debug
builds of all 3 formats already read `resources/web/index.html` directly
from source at runtime (`LoadIndexHtml()`'s `__FILE__`-relative path, see
the "Release-mode GUI" entry above) - takes effect on next launch with no
rebuild step. (A Release build, if ever made again, would need its postbuild
resource-copy re-run to pick this up - not done now, not urgent.)

**Verified** via the same throwaway local-HTTP-server Browser-pane technique
as above: fed a synthetic "params" host message with realistic non-zero Gain
defaults (3.12/1.92/3.60) - confirmed the 3 Gain knobs' `default-value`
became exactly `"0"` while Freq/Q knobs kept their real tuned defaults
untouched. Then simulated an actual `dblclick` on a Gain knob's circle
element after moving it away from default - landed exactly on `+0.000 dB`.
Same for a Freq knob - landed on its real tuned default (125 Hz), confirming
that path was correctly left alone.

## 2026-08-25 — Delay tempo-sync

User request: "ディレイにシンクモードを作れますか" (add a sync mode to Delay).
Mirrors the existing LFO Rate(Hz)/Rate(Tempo)/Sync pattern as closely as
Delay's own architecture allows.

**New params** (`FirstSynth.h`, appended after `kParamEQBypass` per the
"never renumber" convention): `kParamDelayTimeTempo` (114, enum division,
`LFO_TEMPODIV_VALIST`) and `kParamDelayTimeMode` (115, bool Sync toggle).
**Sync defaults to `false`**, unlike the LFOs (which default `true`) -
existing presets/patches saved before this param existed must keep sounding
exactly as they did (plain ms-based Delay Time), not silently switch to
synced the moment they're reloaded.

**`DelayEffect<T>` (`FirstSynth_Effects.h`) needed real new plumbing**, not
just param wiring - unlike the LFOs, it isn't an `LFO<T>`/`IOscillator<T>`
subclass, so it had no rate/phase/tempo concept to hook into at all. Added:
- `GetQNScalar(division)` - an exact copy of iPlug2's own
  `LFO<>::GetQNScalar()` table (`Extras/LFO.h`), kept in sync manually the
  same way this project's other DSP-mirroring code already is (e.g.
  eq-curve-display.js mirrors `ParametricEQEffect`'s coefficient math).
- `SetTempo(double)`, `SetSyncMode(bool)`, `SetDivision(int)` - all funnel
  into a new `UpdateSyncedTimeMs()` that computes `mEffectiveTimeMs` (the one
  value `Process()` actually reads, replacing its old direct `mTimeMs` read)
  - either the synced value (`60000/tempo / GetQNScalar(division)`, clamped
    to `[10, 2000]` ms) or the plain manual `mTimeMs`, depending on
    `mSyncMode`. Clamped to the *param's own declared range* (10-2000ms,
    `kParamDelayTime`'s `InitDouble`), not the buffer's real ~2100ms
    capacity - a slow-tempo+long-division combo (e.g. 8/1 at 40 BPM =
    48000ms) must be caught here, or the displayed knob position and the
    actually-audible delay would silently disagree (the buffer-capacity
    clamp already inside `Process()` would still catch it, but silently,
    with no matching UI feedback).

**`FirstSynth::ProcessBlock`**: added one `mDelay.SetTempo(tempo);` call,
once per block, right where `mDSP.ProcessBlock(...)` already receives that
same local `tempo` (the existing CLAP/VST3/host-tempo vs. Standalone-tempo
selection, unchanged) - `mDelay` lives in `FirstSynth` itself, not
`FirstSynth_DSP.h`, and only needs the current value (not per-sample phase
like the LFOs), so a plain once-per-block setter was enough; no need to
thread it through `IPlugInstrumentDSP::ProcessBlock`'s own signature.

**`FirstSynth::OnParamChange`**: two new cases, `mDelay.SetDivision((int)
value)` / `mDelay.SetSyncMode(value > 0.5)` - added directly in
`FirstSynth.cpp`'s own switch (like Delay's other 4 params already are),
not routed through `FirstSynth_DSP.h`'s `SetParam` like the LFOs are, since
`mDelay` isn't part of the per-voice/DSP object.

**`index.html`**: reused the *existing generic* `SetSync()` /
`UpdateRateKnobVisibility()` / `.rate-knob-slot` machinery as-is, just
extending `kLFOSyncParams` (+= 115) and `kLFORateKnobsBySync` (+= `115: [44,
114]`) - no new JS functions needed, despite the "LFO"-named constants (they
were never actually LFO-specific in implementation, only in the params
they'd been used for so far - noted with a comment rather than renaming
them, to avoid touching a lot of already-working code for no functional
gain). Delay panel gained a `rate-knob-slot`-wrapped Time(ms)/Time(Tempo)
pair plus a "Sync" toggle, positioned before Feedback/Mix/Ping Pong.

**Verified**: all 3 formats compiled clean (0 errors). UI wiring confirmed
live via the same throwaway-local-HTTP-server Browser-pane technique as the
EQ work above: both Time knobs and the Sync checkbox exist with the right
param ids, `kLFOSyncParams`/`kLFORateKnobsBySync` correctly extended, and
toggling Sync on/off via the real `SetSync()` handler correctly swaps which
knob is visible each time (ms hidden+tempo shown when on, reversed when
off) - exactly mirroring the LFOs' own already-proven behavior. The
tempo-division math itself (`GetQNScalar`/`UpdateSyncedTimeMs`) was verified
by hand (traced scalar values against note-division names, e.g. "1/16"
scalar=4 correctly yields 1/4 of a beat's duration, "1/1" scalar=0.25
correctly yields 4 beats) rather than run live, since it's pure C++ with no
JS-side equivalent to test in the same browser-harness way the EQ curve
math was. **Not yet confirmed audibly** - worth a live check (e.g. turn Sync
on, set a slow BPM + long division, confirm the repeats actually land on
the beat and the knob doesn't silently hit the 2000ms ceiling for divisions
that would want to go longer, like 8/1 at very slow tempos).

## 2026-08-26 — Envelope Shape diagram, oscillator phase question, and a dev-only A/B test of Pigments'/Sylenth1's time-knob curves

User wasn't satisfied with the current Amp/Filter ADSR "feel" and asked to
see the actual curve shape as a diagram first (published as an Artifact,
mirroring `ADSREnvelope.h`'s real math: linear-timed attack reshaped by
`pow(x, 2)` since `FirstSynth_DSP.h` calls `SetAttackShape(2.)` on both
envelopes, then true exponential decay/release calibrated the same way
`CalcIncrFromTimeExp` is - not committed to the plugin itself, just a
one-off visualization tool). That prompted two follow-ups.

**Oscillator phase question**: "発音時のphaseはどうなってますか" - answered by
reading `Voice::Trigger()` (`FirstSynth_DSP.h`): `mPhase1 = mPhase2 = 0.;` is
the unconditional first line, every note-on, regardless of `isRetrigger`
(which is itself always `false` in the actual current voice-allocation code
- `VoiceAllocator.cpp` has a `// TODO retrig / legato` with `bool retrig =
false;` hardcoded at every call site). So phase is already hard-reset to 0
on literally every trigger - not a source of inconsistent attack feel: if
anything, every note starts from the exact same waveform point every time.

**Reference-synth comparison**: user measured each reference synth's own
knob's real value at 12-o'clock/50% rotation - Sylenth1: 0-10000ms, center
exactly 5000ms (linear). Pigments: 0-20000ms, center 1300ms (a power curve,
solving `pow(0.5, exp) = 1300/20000` gives exponent ≈3.9434 - the same
`ShapePowCurve` family FirstSynth's own knobs already use, just steeper and
over one wide shared range instead of FirstSynth's current per-stage
different max, 1000/4000/8000ms). At FirstSynth's own current knob-center
(exponent 3): Attack≈126ms, Decay≈501ms, Release≈1002ms - notably more
front-loaded toward short times than Pigments, especially for Attack (max
only 1000ms vs Pigments' effective 20000ms for every stage).

**Dev-only A/B tool built** (user request: "開発用のみで切り替えで両方試せる
ように") to actually try both reproduced curves live rather than just
compare numbers on paper:
- `FirstSynth.h`/`.cpp`: new `#ifdef _DEBUG`-only mechanism -
  `EEnvTimeCurvePreset` (`kEnvTimeCurveFirstSynth`/`Pigments`/`Sylenth1`),
  `ApplyDevEnvTimeCurve()` (reinterprets a param's raw *normalized* [0,1]
  value through the chosen preset's curve instead of that param's own real
  declared `ShapePowCurve(3.)`/range), `SetEnvTimeCurvePreset()` (switches
  and immediately re-applies all 6 Attack/Decay/Release x Amp/Filter params'
  *current* normalized values through the new curve, so switching takes
  effect at once, not just on the next knob nudge). `OnParamChange` overrides
  its local `value` for exactly these 6 params when a non-default preset is
  active - they already fell through to `mDSP.SetParam(paramIdx, value)`
  unchanged, so nothing else needed to change. New dev-only message
  `kMsgTagSetEnvTimeCurvePreset` (24) - the enum slot is always declared
  (never-renumber convention preserved) but its C++ *handling* and the
  WebView selector that sends it are both `#ifdef _DEBUG` / hidden-until-
  `SetDevBuild(true)` - never reachable in a Release build at all, matching
  this project's other dev-only tools (Factory preset marking).
  Real param declarations (used for host automation/preset save) are
  completely untouched - this only changes how `OnParamChange` *interprets* a
  change to one of these 6 params while testing.
- `index.html`: new `<select id="envTimeCurveSelect">` next to the Factory
  button (same hidden-by-default/`SetDevBuild` pattern). `SetEnvTimeCurvePreset()`
  (JS) sends the message via `SAMFUI`'s `ctrlTag` (matching the existing
  `kMsgTagSetUIScale` convention for a single small integer, no base64
  payload needed) *and* overrides the 6 knobs' `min`/`max`/`shapeExponent` to
  match, so what's displayed/how dragging feels stays consistent with what
  the C++ side actually does with the same shared normalized value - the
  normalized value is the single source of truth on both sides, never the
  real declared param range. Each knob's true original min/max/shape-exponent
  is cached once (from the host's own initial "params" message) so switching
  back to preset 0 restores them exactly.
- `knob-control.js`: found mid-implementation that `shape-exponent` isn't in
  `observedAttributes` (only `min`/`max`/etc. are) - `setAttribute()` on it
  silently does nothing after construction. Fixed by setting the instance
  property directly instead (`knob.shapeExponent = ...` - a plain public
  field, not private, so this works from outside the class) rather than
  touching `observedAttributes` for every knob project-wide. Also added
  `refreshDisplay()` (re-renders from the knob's own current normalized value
  without changing it) - needed because changing min/max/shapeExponent alone
  doesn't retroactively refresh an already-rendered label/pointer position.

**Verified**: both Debug and Release compile clean (0 errors) - confirms the
`#ifdef _DEBUG` gating has no stray unconditional references. Live JS
verification via a throwaway local-HTTP-server Browser-pane session (same
technique as the EQ/Delay work above) - hit a real caching bug along the way
(the Browser tool's networking layer was serving a stale cached response for
a `localhost:8934` URL reused across many earlier test sessions this
conversation, even from a brand-new tab with a hard navigate - moving to a
fresh port immediately fixed it; noted here in case it recurs). Once past
that: set the Attack knob to normalized 0.5, confirmed it showed "125.9 ms"
under the FirstSynth preset, exactly **"1300 ms" under Pigments** and exactly
**"5000 ms" under Sylenth1** - both landing exactly on the user's own
measured reference points - then confirmed switching back to preset 0
restored the original min/max/shape-exponent exactly.

**Not yet done**: live-heard in the real Standalone/DAW app by the user -
this was only verified in the JS test harness (accurate for the display/
curve-math side) and by code inspection for the C++ side (accurate for the
compile/logic side), but the two were never exercised together as one real
running plugin instance this session.

## Amp/Filter ADSR: adopted Pigments curve as the real, permanent Attack/
## Decay/Release curve (2026-08-25)

After A/B testing (above), the user tried reproducing the Sylenth1 reference
too and found the numbers didn't match what they expected - "sylenth1です
が、どうやら数値は秒ではないようです。もっと短く感じます". I started
adding diagnostic logging to chase it, but the user clarified this wasn't a
FirstSynth bug at all: they had misread Sylenth1's own UI and transcribed
non-second values as if they were seconds ("表記をそのまま秒だと思って書
き写したのですが、それは秒ではなかった"). All diagnostic logging (`LogEnvTime`
in `FirstSynth.cpp`/`FirstSynth_DSP.h`) was fully reverted - confirmed via
grep no references remained, rebuilt clean.

Getting an accurate Sylenth1 reference turned out to be more work than it was
worth, so the user decided to just adopt the **Pigments** curve outright as
FirstSynth's real curve: "どうやら思ったよりも複雑に出来ています。とりあ
えず、pimentsのエンベロープタイムを採用します。" I flagged that this would
change the sound of existing saved presets' ADSR (since it changes the real
declared param curve, not just a dev overlay) - user confirmed via
AskUserQuestion: "気にしない（推奨）", proceed regardless.

**Changes**:
- `FirstSynth.cpp`: all 6 ADSR time params (`kParamAttack/Decay/Release`,
  `kParamFilterAttack/Decay/Release`) changed from their old individual
  ranges/`ShapePowCurve(3.)` to a single unified curve: `min=0.`, `max=20000.`,
  `ShapePowCurve(3.9434164716336326)` - the exponent solving
  `pow(0.5, exp) = 1300/20000` (Pigments' own 12-o'clock/1300ms reference
  point), computed via Python (`math.log(1300/20000) / math.log(0.5)`). Each
  param's real-value default (`10.`) unchanged.
- `index.html`: the same 6 knobs' `shape-exponent` attribute updated from
  `"3"` to `"3.9434164716336326"` to match (verified via grep that unrelated
  knobs still on `shape-exponent="3"` - Note Glide Time, Mod Env 1/2 - were
  left untouched).
- Dev A/B tool simplified from 3-way to 2-way: since preset 0 ("FirstSynth")
  is now identical to the old "Pigments" option, that option was removed -
  `EEnvTimeCurvePreset` collapsed to `{kEnvTimeCurveFirstSynth, kEnvTimeCurveSylenth1}`,
  `ApplyDevEnvTimeCurve()`'s Pigments case removed (now redundant with
  `default`), `envTimeCurveSelect`'s `<option>` list and `kEnvTimeCurvePresets`
  JS map both trimmed to just Sylenth1. This enum is a pure runtime UI
  selector (never persisted/saved), so renumbering it is safe - unlike
  `EParams`, which must never be renumbered.

**Verified**: all 3 formats (Standalone/VST3/CLAP) rebuilt Debug|x64, 0
errors each. Launched Standalone, PrintWindow screenshot confirmed the
Filter ADSR panel renders correctly under the new curve (Attack 16.25 ms,
Decay 1321 ms, Release 507.8 ms shown for knobs at their default/varied
positions - no visual corruption, no crash). Test process closed after
verification.

**Not yet done**: live-heard/felt by the user in their own testing - this
was only visually verified via screenshot this session, not played.

## Mod Env 1/2: also adopted Pigments curve (2026-08-26)

Follow-up to the above - user asked "2つのMODENVも同じように出来ますか" (can
the two Mod Envs get the same treatment). `kParamModEnv1Attack/Decay/Release`
and `kParamModEnv2Attack/Decay/Release` (6 params, `FirstSynth.cpp`) changed
from their old individual ranges (`1-1000`/`1-4000`/`2-8000`, `ShapePowCurve(3.)`)
to the same unified curve as the main Amp/Filter ADSR: `min=0., max=20000.,
ShapePowCurve(3.9434164716336326)`. `index.html`'s matching 6 `<knob-control>`
elements (`param-id="78/79/81"` Mod Env 1, `"82/83/85"` Mod Env 2) updated
`shape-exponent` from `"3"` to `"3.9434164716336326"` to match, `max-digits="4"`
left as-is (same as the main ADSR knobs, sufficient for 20000ms display).
Sustain params (`80`/`84`) untouched. Confirmed via code read that
`FirstSynth_DSP.h`'s `SetParam()` for these params just forwards the real
`value` (ms) into `ADSREnvelope::SetStageTime()` - no other hardcoded range
dependency, so no DSP-side change needed. The dev-only A/B curve tool
(`ApplyDevEnvTimeCurve`, still Amp/Filter-ADSR-only, 6 params) was
intentionally left un-extended to Mod Env - out of scope, user didn't ask for
it.

**Verified**: all 3 formats rebuilt Debug|x64, 0 errors each (had to
`taskkill` a leftover `FirstSynth_x64.exe` from the prior test session first -
it was locking the Standalone exe during copy). Tried to reach the Matrix
page via native-window mouse-automation (PowerShell `mouse_event`/`SendInput`
at the tab button's exact pixel coordinates, verified against a PrintWindow
screenshot) to screenshot-confirm the Mod Env knobs, but the click never
registered - tried 3 times (legacy `mouse_event`, `SendInput` with virtual-
desktop-aware absolute coords, then a fresh atomic re-check of window rect +
immediate click) and the page never left "Synth". Likely cause: this app's
UI is hosted in a WebView2 child window/render surface, and global synthetic
mouse input to the top-level HWND doesn't reliably reach Chromium's own input
pipeline - would need to target the child window specifically (or real
hardware input) to work reliably. Abandoned the automation as not worth
the effort for a low-risk, attribute-only change identical in kind to the
already-screenshot-verified main ADSR knobs. **User confirmed by their own
manual testing in the Standalone app: "出来てると思います"** (looks like
it's working) - test process closed after.

## Matrix Filter Cutoff destination: scale corrected from +-8 to +-4 octaves
## (2026-08-26)

User noticed/asked whether Filter LFO patched via the dedicated Filter LFO
section vs. via the Matrix (Mod LFO 1/2 -> Filter Cutoff) sweeps the cutoff
by noticeably different amounts. Investigated - confirmed a real
inconsistency, not a misperception: the dedicated Filter LFO's Depth knob
scales to **+-4 octaves at 100%** (`FirstSynth_DSP.h`, `kParamFilterLFODepth`
case: `(value/100.)*4.`), but the Matrix's Filter Cutoff destination was
scaling its accumulated modulation by a hardcoded **+-8 octaves** (matching
Filter Env Amount's own range instead, `envAmountOctaves` a few lines above -
apparently copied from the wrong dedicated knob's range when the Matrix
destinations were first built). So the same LFO waveform at matching "100%"
settings swept twice as far via the Matrix as via the dedicated Filter LFO
section.

User confirmed the fix direction: "そうですね。LFOと合わせてみてください"
(align it with the [Filter] LFO). Changed the `+- 8` to `+- 4` in the single
line computing `cutoffHz` in `ProcessSamplesAccumulating`
(`FirstSynth_DSP.h`, the `matDest[kMatDstFilterCutoff] * (T) 4.` term) -
comment above updated to explain the new rationale (matches dedicated Filter
LFO Depth, not Filter Env Amount). Filter Env Amount's own +-8 octave range
is untouched - that's a different dedicated knob, not compared to LFOs by the
user.

**Verified**: all 3 formats rebuilt Debug|x64, 0 errors (same locked-exe
`taskkill` needed as above, then rebuilt clean).

**Not yet done**: not yet heard live by the user - this was a code-level fix
based on identifying the scale mismatch, not yet A/B'd by ear in a running
instance.

## Oscillator phase: dev-only "Fixed" vs "Free" A/B toggle (2026-08-26)

Follow-up to the earlier oscillator-phase investigation (this session, see
above - confirmed `mPhase1`/`mPhase2` unconditionally reset to 0 on every
note-on). User asked: "オシレータの位相の話ですが、固定とそうでないのと開
発用に切り替えて使ってみたいです" (want a dev-only toggle between fixed and
non-fixed phase, to try both). Built following the exact same pattern as the
Env Time Curve A/B tool (`kMsgTagSetEnvTimeCurvePreset`) - a new always-
declared, `#ifdef _DEBUG`-only-handled message.

**Two reset sites found and both gated** (there are two, not one - easy to
miss the second): `Voice::Trigger()`'s own unconditional `mPhase1 = mPhase2
= 0.;`, and a *second* reset via `ADSREnvelope`'s `mResetFunc` callback
(passed as `mAMPEnv`'s constructor arg, fires specifically on a mid-envelope
legato/mono retrigger, i.e. voice-stealing while still sounding - a different
code path than a fresh `Trigger()` on a free voice). Both now read a new
per-Voice flag, `mFixedPhase` (default `true` = today's existing behavior,
unchanged).

**Changes**:
- `FirstSynth_DSP.h`: `mFixedPhase` (`#ifdef _DEBUG` bool) added to `Voice`'s
  `public:` section (not `private:` where `mPhase1`/`mPhase2` themselves
  live) - it has to be reachable from `IPlugInstrumentDSP`'s own
  `ForEachVoice` broadcast, same as `mMatrixSource`/`mVelocity`/etc. above it,
  none of which are private either. Both reset sites wrapped in `#ifdef
  _DEBUG if (mFixedPhase)`. New `SetOscPhaseMode(bool fixed)` on
  `IPlugInstrumentDSP` (`#ifdef _DEBUG`), mirrors `SetParam()`'s own
  `ForEachVoice` pattern for per-voice state.
- `FirstSynth.h`: `kMsgTagSetOscPhaseMode` appended to `EMsgTags` (after
  `kMsgTagSetEnvTimeCurvePreset`, value 25) - always declared, `#ifdef
  _DEBUG`-only handling, same convention as its sibling. `mOscPhaseFixed`
  (bool, mirrors current state) + `SetOscPhaseMode(bool)` declaration added
  to the same `#ifdef _DEBUG` member block as `mEnvTimeCurvePreset`.
- `FirstSynth.cpp`: `OnMessage()` case forwards `ctrlTag != 0` to the new
  `SetOscPhaseMode()`, which just stores `mOscPhaseFixed` and calls
  `mDSP.SetOscPhaseMode(fixed)`. Much simpler than the Env Time Curve tool -
  no knob/param reinterpretation needed, this is a plain per-voice runtime
  flag, not tied to any host-visible param.
- `index.html`: new `<select id="oscPhaseModeSelect">` (`"Phase: Fixed"` /
  `"Phase: Free"`) next to `envTimeCurveSelect`, same hidden-by-default/
  `SetDevBuild(true)` reveal pattern. `SetOscPhaseMode(fixed)` (JS) just
  sends `SAMFUI(25, fixed, 0)` - no knob display to keep in sync this time.

**Verified**: all 3 formats (Standalone/VST3/CLAP) rebuilt Debug|x64, 0
errors each; also rebuilt Standalone in **Release|x64** to confirm the
`#ifdef _DEBUG` gating has no stray unconditional references outside it (also
0 errors) - same double-check this project always does for a dev-only tool
before calling it done.

**Not yet done**: not yet exercised live (toggled and A/B'd by ear) by the
user - this was a build-only verification pass (Debug+Release compile clean).
Did not attempt another native-window mouse-automation screenshot pass this
time, given the Mod Env entry above already established it's unreliable for
this WebView2-based UI - left it to the user's own manual testing instead.

## Oscillator phase dev toggle: fixed a display-only bug on GUI reopen
## (2026-08-26)

User tried the new toggle live in BespokeSynth (VST3) - "GUIをもう一度開く
とFIXEDに戻ってしまいます" (reopening the GUI shows Fixed again). Root cause:
`mOscPhaseFixed`/each `Voice`'s `mFixedPhase` are plain C++ members on the
plugin instance/DSP - untouched by closing just the editor window (only the
WebView itself gets torn down and recreated). But the WebView's HTML always
restarts showing each `<select>`'s hardcoded default option on reload, and
nothing was pushing the real current state back to a freshly (re)loaded page
- so the dropdown *looked* like it reverted to Fixed even though the DSP was
still correctly applying whichever mode had actually been selected. Same
latent bug existed for the Env Time Curve tool's `envTimeCurveSelect` too
(never reported, since the user hadn't reopened the GUI mid-A/B on that one
yet) - fixed both at once since they share the identical root cause.

**Fix**: `FirstSynth.cpp`'s `OnWebContentLoaded()` (`#ifdef _DEBUG` block,
right after the existing `SetDevBuild(true)` call) now pushes both tools'
real current state to the UI on every WebView load via
`EvaluateJavaScript()`: `SetEnvTimeCurveDisplay(mEnvTimeCurvePreset)` and
`SetOscPhaseModeDisplay(mOscPhaseFixed ? 1 : 0)`. Two new JS functions in
`index.html` (display-only - set the `<select>`'s `.value`, and for Env Time
Curve also reapply the knob min/max/shapeExponent overrides, same logic
`SetEnvTimeCurvePreset()` already has for that part - but neither one
re-sends the message back to C++, since that state's already correct there,
only the UI needed catching up).

**Verified**: all 3 formats rebuilt Debug|x64, 0 errors - VST3 rebuild hit a
"共有違反" (sharing violation) build error on the first attempt because
BespokeSynth still had the previous `FirstSynth.vst3` loaded; user closed
BespokeSynth ("とじました") and the rebuild succeeded cleanly on retry.
Standalone/CLAP had already built clean before that (they don't lock the
same file BespokeSynth was holding).

**Not yet done**: not yet re-confirmed live by the user that reopening the
GUI in BespokeSynth now correctly shows the last-selected mode.

## WebView2 environment reuse across GUI open/close (2026-08-26, iPlug2 shared
## framework change - local commit only, not pushed upstream)

User report: using FirstSynth as a VST3 in BespokeSynth feels noticeably
heavier than Standalone, and specifically "再生中にGUIを開くと、その瞬間に
CPU消費がすさまじくなります" (opening the GUI during playback causes a
severe CPU spike right at that moment). Confirmed root cause is NOT
FirstSynth's own code but shared `iPlug2/IPlug/Extras/WebView/IPlugWebView_win.cpp`:
`IWebViewImpl::OpenWebView()` called `CreateCoreWebView2EnvironmentWithOptions()`
completely fresh on *every* call, and `CloseWebView()` dropped the only
reference to it (`mWebViewEnvironment = nullptr;`) on every close. Spinning
up a WebView2 "environment" (the underlying shared Chromium browser/GPU/
renderer process machinery) is expensive - in a host that tears down and
recreates the editor window on every GUI open/close (confirmed: BespokeSynth
- there's already an older comment in this same file, 2026-08-18, noting
BespokeSynth's parent-resize timing quirks, so this host is known to behave
this way), that full cost was being paid live, during playback, on every
single GUI open. Standalone doesn't show this because its window normally
opens once and stays open through a whole session.

User's call after being shown the tradeoff (this is shared framework code,
past convention here has been local-commit-only, not pushed to the upstream
iPlug2 checkout): "改修は避けて通れないでしょう" (the fix can't be avoided) -
proceed.

**Fix** (per Microsoft's own WebView2 guidance: reuse one environment object
across multiple opens/controls rather than recreating it - only the much
cheaper *controller* needs to be created fresh per open):
- Added `IPlugWebView_win.cpp`: `static wil::com_ptr<ICoreWebView2Environment>
  gWebViewEnvironment` - process-wide (this .cpp is one translation unit per
  plugin binary, so shared across every instance of *this* plugin in the host
  process, never across different plugins). Plus `gWebViewEnvironmentPending`
  (bool) and `gPendingControllerCreations` (`vector<function<void()>>`) to
  correctly queue a second `OpenWebView()` call that arrives while the first
  environment creation is still in flight (async, its own completion
  callback), instead of racing a second competing environment.
- `OpenWebView()` restructured: the old nested "create environment -> create
  controller" callback chain is now extracted into a standalone
  `createController` lambda, invoked either immediately (environment already
  cached), queued (creation in flight), or from inside that creation's own
  completion callback (draining the queue too, for any other instances that
  piled up waiting). The giant unchanged controller-setup body (event
  handlers, script injection, bounds clamping, etc, ~230 lines) didn't need
  to move or reindent - it stays at the same brace-nesting depth as before.
- `CloseWebView()`: no longer nulls `mWebViewEnvironment` - that's just this
  instance's own reference to the shared static now; letting it go on
  eventual destruction is harmless, the static keeps the real environment
  object alive regardless of how many instances currently hold a reference.
- Dangling-`this` safety: `IWebViewImpl` gained `std::shared_ptr<bool> mAlive`
  (a separate heap allocation, not embedded in `this`, so a `weak_ptr` to it
  correctly reports "expired" even after `this` itself is freed). Every
  deferred/queued continuation captures a `weak_ptr<bool>` and checks
  `.lock()` before touching `this` via the captured `createController` -
  covers the edge case of a GUI being closed while its own environment-creation
  request is still queued behind another instance's in-flight one.

**Verified**: rebuilt Standalone/VST3/CLAP Debug|x64 (0 errors) and also
Standalone Release|x64 (0 errors, this fix is unconditional - not `#ifdef
_DEBUG`-gated like the other recent dev tools, it's a real behavior change
for every build). Launched Standalone, PrintWindow screenshot confirmed the
GUI still renders correctly (all knobs/panels intact, "Phase: Fixed"/"Env
Time: FirstSynth" dev selectors both showing correctly) - no visual
corruption, no crash on the modified WebView creation path.

**Confirmed live by the user in BespokeSynth**: "Bespokesynth上でGUI開閉し
ました。スパイクはなくなりました。" (opened/closed the GUI in BespokeSynth -
the spike is gone). The fix works as intended for the actual reported
problem. Not separately exercised: the multi-instance-racing-for-the-same-
environment path (two FirstSynth instances both opening their editor for the
first time nearly simultaneously) - reasoned through carefully but not
directly reproduced/tested this session; no reason to expect it's needed
given the straightforward single-instance case is confirmed fixed.

Also worth noting for future sessions: this fix lives in the *shared*
`C:\Users\a_wak\CLAP_plugin\iPlug2\` checkout, which every sibling project
(SuiKinKutsu, GrainField, GrainKit/Compost, UeberLooper, Chaoscape) compiles
directly from via the same `..\..\iPlug2\...` relative path (confirmed via
grep on their own `.vcxproj` files) - unlike the earlier "iPlug2 WebView
Windows Release bug" fix (which lived in each project's own copied
`postbuild-win.bat` and needed manual porting per-project), this one applies
to every sibling project automatically the next time each is rebuilt, no
manual porting needed. User asked about this specifically (worried the
local-commit-only convention meant other users wouldn't get the fix) - see
this file's own explanation above for why that convention only means "not
pushed to the public upstream iPlug2 GitHub project," not "excluded from
FirstSynth's own distributed builds" (iPlug2 is compiled directly into the
binary, not a separate runtime dependency).

## Env Time Curve dev tool removed; Osc Phase Mode dev tool kept (2026-08-26)

User's final calls on the two active dev-only A/B tools: "エンベロープのカ
ーブですが、pigmentsカーブを採用しますので、sylenth1の切り替えスイッチは
削除してください。オシレータのphaseに関しては、もう少し様子をみたいです。"
(Envelope curve: adopting the Pigments curve, so remove the Sylenth1 switch.
Oscillator phase: still want to keep watching that one a while longer.) So
the Env Time Curve tool (`kMsgTagSetEnvTimeCurvePreset`/`EEnvTimeCurvePreset`/
`ApplyDevEnvTimeCurve`/`SetEnvTimeCurvePreset`, plus its `envTimeCurveSelect`
UI and all supporting JS state) was fully deleted; the Osc Phase Mode tool
(`kMsgTagSetOscPhaseMode`/`mFixedPhase`/`oscPhaseModeSelect`, from earlier
this session) stays exactly as-is, still under active evaluation.

**Removed, across all 4 files**:
- `FirstSynth.h`: the `kMsgTagSetEnvTimeCurvePreset` `EMsgTags` entry, and the
  `#ifdef _DEBUG` block's `EEnvTimeCurvePreset` enum/`mEnvTimeCurvePreset`/
  `ApplyDevEnvTimeCurve()`/`SetEnvTimeCurvePreset()` declarations. Since this
  slot was never persisted/saved anywhere (dev-only, `#ifdef _DEBUG`-gated,
  pure live WebView<->plugin IPC - unlike a real host-automatable param),
  actually deleting it and letting `kMsgTagSetOscPhaseMode` shift down a
  number (25 -> 24) was judged safe, rather than leaving a permanently
  unused/reserved slot the way `EParams`' "never renumber" convention would
  require for a real param.
- `FirstSynth.cpp`: the `OnMessage()` case, both function bodies
  (`ApplyDevEnvTimeCurve`/`SetEnvTimeCurvePreset`), `OnParamChange()`'s 6-param
  override block, and the `OnWebContentLoaded()` `SetEnvTimeCurveDisplay` push
  (added just one turn earlier this session for the GUI-reopen display bug -
  removed along with everything else now that the tool itself is gone). The
  historical comment on `kParamAttack`'s `InitDouble()` explaining *why* the
  Pigments curve was chosen was kept (still accurate/useful design-decision
  record) but its now-dangling reference to the deleted symbol was swapped
  for a pointer to progress.md instead.
- `resources/web/index.html`: the `envTimeCurveSelect` `<select>` element and
  its comment, `SetDevBuild()`'s handling of it, `SetEnvTimeCurvePreset()`/
  `SetEnvTimeCurveDisplay()`, `kEnvTimeCurveParamIds`/`kEnvTimeCurveOriginals`/
  `kEnvTimeCurvePresets`, and the "params"-handler block that populated
  `kEnvTimeCurveOriginals`. `SetOscPhaseMode()`'s `SAMFUI()` call updated from
  `25` to `24` to match the C++ enum's shift.

**Verified**: all 4 targets rebuilt clean, 0 errors - Standalone/VST3/CLAP
Debug|x64, plus Standalone Release|x64 (confirms no stray reference to a
deleted symbol survived anywhere, in either configuration). Not yet
re-launched/screenshotted this round - the changes are a straightforward
deletion of an already-verified-working tool, following the same edit
pattern used throughout this file's own earlier removals.

## Shared iPlug2 checkout published as a private GitHub fork (2026-08-29)

Context: Ito tried to build FirstSynth on a Mac and hit the fact that the
`CLAP_plugin\iPlug2` checkout on this PC is modified, not stock upstream, and
existed nowhere else. The Mac build (and adding an AU target) needs this
exact checkout. Ito's suggestion: just share it on GitHub. Done.

**What was done:**
- Committed two previously-uncommitted framework fixes that were sitting in
  the iPlug2 working tree (both already verified working in sibling projects
  earlier this month, just never committed here): the WebView2
  environment-reuse perf fix (BespokeSynth GUI-reopen CPU spike, 2026-08-26)
  and the Release-build `index.html` path resolution fix
  (`GetCurrentModuleDirWin`, 2026-08-25). New commit `39c6341d4`.
- `git remote rename origin upstream` (upstream stays `iPlug2/iPlug2` for
  pulling framework updates).
- Created **private** repo `github.com/waki-loveburger/iPlug2` (private
  because commit `49bd00eb7`'s message names unreleased products), added it
  as `origin`, pushed `master`. `master` now tracks `origin/master`.
  Private because the auth token needed a one-time `workflow` scope added
  (`gh auth login -h github.com -s workflow -w`) - upstream's own
  `.github/workflows/*.yml` in history were being rejected without it.
- Full local delta vs upstream = 3 commits: `4d90482dc` (VST3 DPI +
  per-product WebView2 cache isolation), `49bd00eb7` (WASAPI, Standalone
  Save/Load Preset, ADSR `SetAttackShape`, LFO S&H, earlier DPI/bounds
  fixes), `39c6341d4` (this session's two WebView fixes).

**Collaborator access:** not added yet - Ito's GitHub username wasn't on
hand. Either add him at
`github.com/waki-loveburger/iPlug2/settings/access`, or he requests access /
self-invites the way he did for the SuiKinKutsu repo.

**Still open (separate follow-up):** pull the ADSR `SetAttackShape` (~16 lines)
and LFO S&H (~50 lines) into FirstSynth's own tree as `FirstSynth_ADSR.h` /
`FirstSynth_LFO.h` so upstream's `Extras/ADSREnvelope.h` / `Extras/LFO.h` can
go back to stock. FirstSynth is the only consumer of the modified versions
(grep-checked all 5 siblings - none use `SetAttackShape` or `kSampleHold`).
This is hygiene only; it does NOT remove the need for the fork (the IPlugAPP/
WebView framework changes can't live in a project). Not done this session -
waiting on the go-ahead.

## Band-limited oscillators - PolyBLEP/PolyBLAMP in Morph() (2026-09-01)

User: "音がどうしても全体的に硬く感じる" - the synth sounds hard/brittle overall
and they couldn't place why. Diagnosis: `FirstSynthOsc::Morph()` generated every
non-sine shape naively (`Saw` = raw ramp, `Pulse` = raw step, `AsymTriangle` =
raw corners, the saw->square segment = a literal `std::max(-1,min(1,Saw*k))`
hard-clip). No band-limiting anywhere and no oversampling, so all the
above-Nyquist energy folded back as inharmonic aliasing - the classic "digital
hardness", worst toward the Saw/Square/Pulse end of Wave Shape and worst on
high notes. Contributing but secondary: the Moog ladder's 2026-08-16 recalibration
opened it up (used to secretly roll off ~2 octaves low, was masking the alias
hash); `mFixedPhase=true` phase-locking every note-on; no osc drift/detune.

**Fix (this session): 2-sample PolyBLEP + PolyBLAMP corrections inside Morph().**
- New helpers in `namespace FirstSynthOsc` (`FirstSynth_DSP.h`, above `Morph`):
  `Wrap01`, `PolyBlep` (unit-step residual), `PolyBlamp` (slope-discontinuity
  residual, = integral of blep).
- `Morph()` gained a 3rd arg `T dt` (per-sample phase increment = freq/sampleRate),
  **defaulted to 0** so `dt<=0` reproduces the exact pre-change naive shape (the
  JS `waveform-display.js` mirror is left untouched on purpose - a static preview
  should draw the ideal shape, not BLEP ripple). `dt` is clamped to 0.49 inside.
- Per segment: Sine->Tri = BLAMP on the two triangle vertices x blend amount;
  AsymTri = BLAMP on both vertices, slope-diff `2/r + 2/(1-r)` with `r` clamped
  to 0.98 for the magnitude only (positions exact); pure Saw = BLEP on the
  size-2 down-step at phase 0.5; saw->square clip = Saw BLEP + BLAMP on the two
  clip shoulders at phase `0.5/k` and `1-0.5/k` (Δslope ∓2k) - collapses onto the
  pure-Saw BLEP as k->1, continuous across the seam; Pulse = BLEP on both edges
  (rising +2 at phase 0, falling -2 at phase = duty).
- Call sites in `Voice::ProcessSamplesAccumulating` pass `(T)phaseInc1` /
  `(T)phaseInc2`.

**Verified:**
- Builds: app/vst3/clap x Debug/Release x64 all 0 errors / 0 warnings.
- Standalone launches, process stays Responding.
- Numerical spectral check (Python re-impl of the exact Morph math, f0≈1760 Hz /
  MIDI 93, alias-energy vs harmonic-energy ratio, naive vs band-limited):
  pure saw -12.9 -> -28.7 dB, saw>sq clip -15.8 -> -31.8 dB, square -14.7 ->
  -30.6 dB, narrow pulse -13.2 -> -30.2 dB (all ≈ -16 dB better); sine>tri and
  asymtri already clean, ~3-4 dB better from the vertex BLAMP. -30 dB is roughly
  where 2-sample PolyBLEP tops out; minBLEP or 2x oversampling would go further
  if the user still wants more after listening.

**Not done / open:** user hasn't A/B'd by ear yet in a host. If still too bright,
next levers (in order): ~~phase free-run~~ (tried + removed, see the 2026-09-01
entry below - inaudible here), Osc Drift (added 2026-09-01), re-check Moog Hz
calibration, unison (on hold), then consider oversampling / minBLEP.

## Factory presets: repo set + first-run seeding + dev-tool polish (2026-09-04)

User finalized the shipping preset set (37, ★-marked in-app via `_factory.txt`)
and asked for all five of: (1) ship only those 37, (2) an automatic filter from
a working folder, (3) commit them to the repo, (4) make the Factory button
dev-only, (5) all of the above. Also renamed two of the 37 to Title Case:
`seq`→`Seq`, `synth2`→`Synth2` (filename *is* the preset name - no name stored
in the chunk).

- **Repo `presets/`** now holds exactly the 37 `.preset` + a normalised
  `_factory.txt` (LF, no BOM - PowerShell `Set-Content -Encoding utf8` had
  earlier written it BOM+CRLF, which `ReadFactoryMarks` tolerates for `\r` but
  not the BOM on line 1). The 29 orphaned 2026-07-29 `testNN`/`perc`/`synth_test`
  files (never referenced by any build) were removed. New **`.gitattributes`**:
  `*.preset binary` (raw double dump - autocrlf would corrupt it) and
  `presets/_factory.txt text eol=lf`.
- **`scripts/collect-factory-presets.ps1`** (item 2) - reads a working folder's
  `_factory.txt`, copies just the marked `.preset` into `presets/`, prunes any
  no-longer-marked ones, writes a normalised `_factory.txt`. Run it after
  changing marks in-app. `-WhatIf` supported.
- **`postbuild-win.bat`** (item 1) - new `RESOURCES_PRESETS_SRC=%BUILD_DIR%\..\presets`,
  xcopied to `Resources\presets\` next to every built binary (app / vst3 bundle /
  clap), same 6 spots as `resources\web`. Verified: all 4 install locations get
  38 files.
- **`SeedFactoryPresets()`** (`FirstSynth.cpp`, called from the ctor) - resolves
  the bundled dir the same way `LoadIndexHtml` does (Debug: project `presets\`
  via `__FILE__`; Release-Win: `GetCurrentModuleDirWin()+"\Resources\presets"`),
  copies any `.preset`/`_factory.txt` not already in `GetPresetsDir()`
  (`%LOCALAPPDATA%\FirstSynth\Presets`), guarded by a one-time
  `.factory_seeded_v1` marker so a user's later deletion sticks (bump the suffix
  if the shipped set ever changes). Verified live: removed 3 presets + cleared
  the marker → relaunch re-seeded exactly those 3, no dupes, no crash.
- **Factory button** (item 4) - was already `#ifdef _DEBUG` + `SetDevBuild()`
  gated (never shown in a Release build). Added belt-and-braces: label is now
  "★/☆ Factory (dev)", dashed border, 0.75 opacity, DEV-ONLY title text.
- ★ badge on dropdown options (`factoryPresetNames`) is deliberately *not*
  dev-gated - end users see ★ on the 37 factory presets vs none on their own
  saved presets, a useful factory-vs-mine distinction.

Build: app/vst3/clap Debug x64 all clean. **Follow-up for the user:** the 37
presets have mixed param counts (some predate HPF Cutoff / EQ Bypass / Delay
tempo-sync / Osc Drift); the 2026-09-01 preset-load-resets-to-default fix means
those load fine with the newer params defaulted, but re-saving all 37 on the
current build would make them full-length/consistent.

## Before release - open TODO (2026-09-01)

Not blocking, but must not ship without these (user's call, noted here so it
isn't forgotten):
- ~~Factory-preset switch~~ - resolved 2026-09-04: already Release-hidden
  (`#ifdef _DEBUG`); additionally relabelled "Factory (dev)" with a dashed
  dev-tool style. See the 2026-09-04 entry above.
- ~~Phase-free switch~~ - resolved 2026-09-01: the dev A/B toggle was **removed**
  entirely (user confirmed the fixed/free difference is inaudible in this synth -
  phase 0 is a zero crossing for the Morph waveforms and there's no unison to
  make it matter). Phase now always resets to 0 on note-on, as before the toggle.

## Phase-mode dev switch removed; Osc Drift param added (2026-09-01)

Follow-up to the band-limiting work. User A/B'd the `#ifdef _DEBUG` fixed/free
oscillator-phase toggle "何度も" and still couldn't hear a difference, unlike
Sylenth1's equivalent. Reason it's a non-lever *here*: FirstSynth resets phase
to **0**, which the Morph waveforms are deliberately built to make a zero
crossing (no onset step/click), and there's no unison stacking to turn phase
relationships into audible comb/width effects the way Sylenth1's does.

**Removed** (`kMsgTagSetOscPhaseMode` / `mFixedPhase` / `mOscPhaseFixed` /
`SetOscPhaseMode` / `oscPhaseModeSelect` + its two JS fns + the
`OnWebContentLoaded` display re-push), same "never persisted anywhere, safe to
delete outright" reasoning as the Env Time Curve tool removal (2026-08-26).
`kMsgTagSetOscPhaseMode` was the last `EMsgTags` entry so nothing renumbered.
Phase-reset-to-0-on-note-on is now unconditional (Voice ctor `mAMPEnv` resetFunc
+ `Trigger()`), exactly the pre-toggle behavior.

**Added `kParamOscDrift`** ("Osc Drift", param id 116, appended per never-
renumber; knob placed right of Bend Range in the Mixer area, `shape-exponent=2`,
0-100%, **default 0** = no change to existing presets). Per-voice, per-oscillator
slow analog-style pitch wander: two incommensurate slow sines summed per osc
(osc1 rates 0.11/0.17 Hz, osc2 0.13/0.19 Hz - different so a 2-osc patch slowly
beats/thickens *without* needing unison), peak +-12 cents at 100%. Evaluated
once per block in `ProcessSamplesAccumulating` (wander >> block length), added
into the `osc1Freq`/`osc2Freq` `pow(2, ...)` exponent. Phase accumulators
free-run, seeded per-voice from `Rand()` in the ctor so pooled voices don't
drift in lockstep; never reset on Trigger.

**Verified**: app/vst3/clap x Debug/Release x64 all 0 errors / 0 warnings;
standalone launches, PrintWindow screenshot confirms the "Osc Drift" knob shows
next to Bend Range and no phase dropdown remains. Not yet A/B'd by ear.

**Related idea parked** (user mentioned, not implemented): bx_oberhausen's
"Spread" - makes a single oscillator act like a stereo oscillator. User
corrected the first guess: there is **no L/R pitch difference and no beating**,
so it is NOT a detune/frequency-offset. It's a pure *phase* relationship - L and
R are the same oscillator at the same frequency, offset only in phase, and the
width comes from interaural phase decorrelation (no Δf ⇒ no beat by definition).
A plain fixed phase offset would comb-filter the mono sum (each harmonic n
scaled by cos(n·θ/2)); the fact that Oberhausen's Spread stays roughly
mono-neutral points to a **90°/quadrature (Hilbert) decorrelation** - R = the
oscillator's cosine/quadrature version, so mono-sum scales every partial by a
uniform cos(45°)≈0.707 (just ~-3 dB, no comb). If implemented here: evaluate the
osc twice per voice (`Morph(phase)` + a quadrature partner), and to keep the
width past the filter the Moog ladder would have to run L/R separately (the
expensive part) - or accept mono-after-filter. Lighter than unison (which is on
hold).

### Preset load now resets params to default first (2026-09-01)

User evaluating Osc Drift in Standalone: switching to a preset saved *before*
Osc Drift existed left the knob at the previously-loaded preset's value instead
of returning to 0. Root cause: a `.preset` (and `autosave.state`) file is a
headerless positional dump of `NParams()` little-endian doubles, and iPlug2's
`IPluginBase::UnserializeParams` loops `for (i=0; i<n && pos>=0; ++i)` - as soon
as the chunk runs out (`IByteChunk::Get` returns -1) it stops, so every param
appended since that file was saved is simply left untouched, keeping whatever
the last-loaded preset set it to.

Fix (`FirstSynth.cpp`): both load paths - `LoadPresetByName()` and
`LoadAutoState()` - now do `for (int i=0;i<NParams();++i) GetParam(i)->Set(GetParam(i)->GetDefault());`
right before `UnserializeState()`. Full-length (current-build) files are then
byte-for-byte identical in result (every param gets overwritten anyway); short
(older-build) files leave the missing trailing params at their compiled-in
default (0 for Osc Drift). `UnserializeParams`' own `OnParamReset(kPresetRecall)`
still fans the final values out to the DSP; `OnRestoreState()` still does the UI.
This also future-proofs the next appended param. Verified: builds clean;
standalone relaunches fine (screenshot).

## AW keyboard mode: octave shift back to Z/X (2026-09-02)

User: in the AW computer-keyboard mode *only*, octave shift should be Z (down) /
X (up) again. This reverses the AW half of the 2026-08-03 "move all 3 modes to
F1/F2 for consistency" change - Wicki-Hayden (F1/F2 grid transpose, Z/X are note
keys there) and C-System (F1/F2 octave) are untouched. `resources/web/index.html`
`EnableComputerKeyboardInput()` keydown handler, the `else` (AW) branch: keys are
now `KeyZ`/`KeyX`, with `F1`/`F2` kept as aliases so the interim muscle memory
still works. Z/X are not in `kKeyToNoteOffset` so there's no clash with note
input. Debug Standalone loads index.html from disk at runtime (no rebuild needed
to test); VST3/CLAP bundles need a rebuild for their copied `resources/web`.

**Band-limiting + Osc Drift confirmed adopted by the user (2026-09-01).** After
evaluating in Standalone the user said keep both. Rebuilt all real targets
(app/vst3/clap, Debug) once BespokeSynth was closed - the installed VST3 at
`C:\Program Files\Common Files\VST3\` and CLAP at `...\Programs\Common\CLAP\`
are now refreshed with everything from this session (earlier the VST3
install-copy had failed code 4 purely because BespokeSynth held the target file
open - binary itself always compiled fine).

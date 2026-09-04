#include "FirstSynth.h"
#include "IPlug_include_in_plug_src.h"
#include "IPlugPaths.h"
#include "LFO.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <filesystem>
#include <thread>

FirstSynth::FirstSynth(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Subscription licence gate (eni_auth, 2026-08-24). Offline/cheap per eni_auth.h's
  // own contract, safe to call here on the message thread at instantiation - must run
  // before ProcessBlock ever gets called (a DAW host may call it before any editor
  // opens), and before any WebView exists (the initial UI push happens later, in
  // OnWebContentLoaded() - see that function's own comment for why).
  mLicence = eni::CheckLicence();
  mLicenceValid.store(mLicence.valid, std::memory_order_relaxed);
  if (mLicence.valid && eni::ShouldRefresh(mLicence.exp))
    eni::RefreshInBackground();

  // Populate a fresh install's preset folder from the bundled factory set, once.
  // Pure filesystem work, no editor/WebView needed - safe here, and this runs
  // before OnWebContentLoaded()'s first SendPresetList() in every format.
  SeedFactoryPresets();

  GetParam(kParamGain)->InitDouble("Gain", 100., 0., 100.0, 0.01, "%");
  GetParam(kParamNoteGlideTime)->InitDouble("Note Glide Time", 0., 0., 2000., 0.1, "ms", IParam::kFlagsNone, "", IParam::ShapePowCurve(3.));
  // 2026-08-26 user request: adopted Pigments' own Attack/Decay/Release knob
  // curve/range wholesale, after directly A/B-comparing it (and a Sylenth1
  // reproduction, abandoned - see progress.md - once the user's own
  // transcribed Sylenth1 reference numbers turned out not to actually be in
  // seconds after all) via a dev-only tool - removed once this decision was
  // finalized, see progress.md's 2026-08-26 entries for the full story.
  // Was ShapePowCurve(3.), min 1/1/2, max 1000/4000/8000 (a different range
  // per stage) - now one shared 0-20000ms range/exponent for all three,
  // exponent solved from Pigments' own measured 12-o'clock/50%-rotation
  // value (1300ms, i.e. pow(0.5, exp) = 1300/20000). User confirmed this is
  // fine to change even though it changes already-saved presets' ADSR
  // sound (same knob position now means a different time) - still early
  // development, not a concern yet.
  GetParam(kParamAttack)->InitDouble("Attack", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "ADSR", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamDecay)->InitDouble("Decay", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "ADSR", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamSustain)->InitDouble("Sustain", 50., 0., 100., 1, "%", IParam::kFlagsNone, "ADSR");
  GetParam(kParamRelease)->InitDouble("Release", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "ADSR", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamLFOShape)->InitEnum("Pitch LFO Shape", LFO<>::kTriangle, {LFO_SHAPE_VALIST});
  GetParam(kParamLFORateHz)->InitFrequency("Pitch LFO Rate", 1., 0.01, 40.);
  GetParam(kParamLFORateTempo)->InitEnum("Pitch LFO Rate", LFO<>::k1, {LFO_TEMPODIV_VALIST});
  GetParam(kParamLFORateMode)->InitBool("Pitch LFO Sync", true);
  GetParam(kParamLFODepth)->InitDouble("Pitch LFO Depth", 0., 0., 100., 0.01, "%", IParam::kFlagsNone, "PITCH LFO", IParam::ShapePowCurve(2.));
  GetParam(kParamOsc1Wave)->InitDouble("Osc 1 Wave", 0., 0., 4., 0.01, "", IParam::kFlagsNone, "OSC1");
  GetParam(kParamOsc1Octave)->InitInt("Osc 1 Octave", 0, -4, 4, "", IParam::kFlagsNone, "OSC1");
  GetParam(kParamOsc1Semi)->InitInt("Osc 1 Semitone", 0, -12, 12, "", IParam::kFlagsNone, "OSC1");
  GetParam(kParamOsc1Fine)->InitDouble("Osc 1 Fine", 0., -100., 100., 0.1, "ct", IParam::kFlagsNone, "OSC1");
  GetParam(kParamOsc2Wave)->InitDouble("Osc 2 Wave", 0., 0., 4., 0.01, "", IParam::kFlagsNone, "OSC2");
  GetParam(kParamOsc2Octave)->InitInt("Osc 2 Octave", 0, -4, 4, "", IParam::kFlagsNone, "OSC2");
  GetParam(kParamOsc2Semi)->InitInt("Osc 2 Semitone", 0, -12, 12, "", IParam::kFlagsNone, "OSC2");
  GetParam(kParamOsc2Fine)->InitDouble("Osc 2 Fine", 0., -100., 100., 0.1, "ct", IParam::kFlagsNone, "OSC2");
  GetParam(kParamMixOsc1)->InitDouble("Osc 1 Level", 100., 0., 100., 0.01, "%", IParam::kFlagsNone, "MIX");
  GetParam(kParamMixOsc2)->InitDouble("Osc 2 Level", 0., 0., 100., 0.01, "%", IParam::kFlagsNone, "MIX");
  GetParam(kParamMixNoise)->InitDouble("Noise Level", 0., 0., 100., 0.01, "%", IParam::kFlagsNone, "MIX", IParam::ShapePowCurve(2.));
  GetParam(kParamFilterCutoff)->InitFrequency("Cutoff", 10000., 20., 20000.);
  // pow-curve so the knob's low/left end (where Q, mapped linearly from this value in
  // FirstSynth_DSP.h, is most audibly sensitive) gets more turning range - same fix
  // already applied to Noise Level for the same "too strong near the left" complaint
  GetParam(kParamFilterResonance)->InitDouble("Resonance", 0., 0., 100., 0.01, "%", IParam::kFlagsNone, "FILTER", IParam::ShapePowCurve(2.));
  // Was a continuous 0-2 LP-BP-HP blend, then a discrete 3-way choice
  // (LP/BP/HP). 2026-08-16: BP/HP retired entirely per user request - the
  // filter is LP-only now (always the Moog ladder), with a separate fixed
  // highpass stage in series after it instead (kParamHPFCutoff below) rather
  // than a selectable filter type. This param is no longer wired to anything
  // and its UI switch was removed - left registered, not deleted, to preserve
  // every later param's index for old saved presets/DAW automation lanes that
  // may still reference it (same reasoning as kParamFilterSlope just below,
  // which was already retired the same way back on 2026-08-09).
  GetParam(kParamFilterType)->InitEnum("Filter Type", 0, {"LP (Moog)", "BP", "HP"}, IParam::kFlagsNone, "FILTER");
  // No longer wired to anything (BP/HP is unconditionally 24dB now, LP's Moog
  // ladder always was) - the UI toggle was removed per user request. Left
  // registered, not deleted, to preserve every later param's index (see
  // kParamFilterType's own comment above for why that matters for presets).
  GetParam(kParamFilterSlope)->InitBool("24dB Slope", false, "", IParam::kFlagsNone, "FILTER");
  // Fixed second filter stage, always in series after the main Filter (Moog LP) -
  // just a plain highpass with only a cutoff knob (no resonance control), see
  // FirstSynth_DSP.h's Voice::mHPFStage. Default 20Hz (the range's own minimum)
  // so a freshly-added param on an old preset that never saved a value for it
  // starts effectively inert rather than silently thinning out the bottom end.
  GetParam(kParamHPFCutoff)->InitFrequency("HPF Cutoff", 20., 20., 20000.);
  // 0-100%: scales how much the note's own pitch shifts the cutoff (in the same
  // octave-additive way Env Amount/Filter LFO already do) - at 100%, cutoff tracks
  // the keyboard 1:1 (up an octave in pitch = cutoff up an octave), at 0% (default)
  // the filter behaves exactly as before (fixed cutoff regardless of note played)
  // Max raised 100->150% per user request - lets the filter over-track the
  // keyboard (cutoff rises faster than 1:1 with pitch) for an extra-bright
  // high end, not just the normal 1:1-at-100% tracking.
  GetParam(kParamFilterKeyFollow)->InitDouble("Key Follow", 0., 0., 150., 0.01, "%", IParam::kFlagsNone, "FILTER");
  GetParam(kParamFilterEnvAmount)->InitDouble("Env Amount", 0., -100., 100., 0.01, "%", IParam::kFlagsNone, "FILTER");
  // Same Pigments-curve adoption as the Amp ADSR above (kParamAttack's own
  // comment has the full story) - identical range/exponent, applied here too.
  GetParam(kParamFilterAttack)->InitDouble("Filter Attack", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "FILTER ADSR", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamFilterDecay)->InitDouble("Filter Decay", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "FILTER ADSR", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamFilterSustain)->InitDouble("Filter Sustain", 50., 0., 100., 1, "%", IParam::kFlagsNone, "FILTER ADSR");
  GetParam(kParamFilterRelease)->InitDouble("Filter Release", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "FILTER ADSR", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamFilterLFOShape)->InitEnum("Filter LFO Shape", LFO<>::kTriangle, {LFO_SHAPE_VALIST});
  GetParam(kParamFilterLFORateHz)->InitFrequency("Filter LFO Rate", 1., 0.01, 40.);
  GetParam(kParamFilterLFORateTempo)->InitEnum("Filter LFO Rate", LFO<>::k1, {LFO_TEMPODIV_VALIST});
  GetParam(kParamFilterLFORateMode)->InitBool("Filter LFO Sync", true);
  GetParam(kParamFilterLFODepth)->InitDouble("Filter LFO Depth", 0., 0., 100., 0.01, "%", IParam::kFlagsNone, "FILTER LFO", IParam::ShapePowCurve(2.));
  GetParam(kParamAmpLFOShape)->InitEnum("Amp LFO Shape", LFO<>::kTriangle, {LFO_SHAPE_VALIST});
  GetParam(kParamAmpLFORateHz)->InitFrequency("Amp LFO Rate", 1., 0.01, 40.);
  GetParam(kParamAmpLFORateTempo)->InitEnum("Amp LFO Rate", LFO<>::k1, {LFO_TEMPODIV_VALIST});
  GetParam(kParamAmpLFORateMode)->InitBool("Amp LFO Sync", true);
  GetParam(kParamAmpLFODepth)->InitDouble("Amp LFO Depth", 0., 0., 100., 0.01, "%", IParam::kFlagsNone, "AMP LFO", IParam::ShapePowCurve(2.));
  GetParam(kParamChorusRate)->InitFrequency("Chorus Rate", 0.5, 0.05, 5.);
  GetParam(kParamChorusDepth)->InitDouble("Chorus Depth", 50., 0., 100., 0.01, "%", IParam::kFlagsNone, "CHORUS");
  GetParam(kParamChorusMix)->InitDouble("Chorus Mix", 30., 0., 100., 0.01, "%", IParam::kFlagsNone, "CHORUS");
  GetParam(kParamDelayTime)->InitDouble("Delay Time", 300., 10., 2000., 0.1, "ms", IParam::kFlagsNone, "DELAY", IParam::ShapePowCurve(2.));
  // was capped at 95 (a leftover caution from when this project's other feedback-style
  // params, like the filter, could genuinely blow up at extreme values) - a plain delay
  // feedback loop at exactly 1.0 is perfectly stable (each repeat stays at constant
  // amplitude forever - a "freeze"/infinite-sustain effect, not a runaway), so 100 is
  // safe. User noticed the knob's max didn't actually reach true 100% feedback.
  GetParam(kParamDelayFeedback)->InitDouble("Delay Feedback", 30., 0., 100., 0.01, "%", IParam::kFlagsNone, "DELAY");
  GetParam(kParamDelayMix)->InitDouble("Delay Mix", 30., 0., 100., 0.01, "%", IParam::kFlagsNone, "DELAY");
  GetParam(kParamDelayPingPong)->InitBool("Delay Ping Pong", false, "", IParam::kFlagsNone, "DELAY");
  // 2026-08-25 user request: tempo-sync, same InitEnum/InitBool shape as the
  // LFOs' own Rate(Tempo)/Sync pair (kParamLFORateTempo/kParamLFORateMode) -
  // see kParamDelayTimeTempo's own comment in FirstSynth.h for the naming.
  // Sync defaults to false (unlike the LFOs, which default true) - existing
  // presets/patches saved before this param existed must keep sounding exactly
  // as they did (plain ms-based Delay Time), not silently switch to synced.
  GetParam(kParamDelayTimeTempo)->InitEnum("Delay Time", LFO<>::k1, {LFO_TEMPODIV_VALIST});
  GetParam(kParamDelayTimeMode)->InitBool("Delay Sync", false, "", IParam::kFlagsNone, "DELAY");
  GetParam(kParamReverbDecay)->InitDouble("Reverb Decay", 50., 0., 100., 0.01, "%", IParam::kFlagsNone, "REVERB");
  GetParam(kParamReverbDamping)->InitDouble("Reverb Damping", 50., 0., 100., 0.01, "%", IParam::kFlagsNone, "REVERB");
  GetParam(kParamReverbMix)->InitDouble("Reverb Mix", 30., 0., 100., 0.01, "%", IParam::kFlagsNone, "REVERB");
  GetParam(kParamLooperReverse)->InitBool("Looper Reverse", false, "", IParam::kFlagsNone, "LOOPER");
  // Speed removed 2026-07-22 - see FirstSynth_Looper.h's class comment and FirstSynth.h's
  // kParamLooperSpeed comment for why (variable-speed overdub-recording aliases/loses
  // information no matter how it's filtered).
  GetParam(kParamLooperFeedback)->InitDouble("Looper Feedback", 100., 0., 100., 0.01, "%", IParam::kFlagsNone, "LOOPER");
  GetParam(kParamLooperMix)->InitDouble("Looper Mix", 100., 0., 100., 0.01, "%", IParam::kFlagsNone, "LOOPER");
  GetParam(kParamBassBoost)->InitDouble("Bass Boost", 0., 0., 100., 0.01, "%", IParam::kFlagsNone, "");
  // 0-100%: width of a per-note random draw (fresh each NoteOn, not a continuous LFO -
  // same "width, not speed" Yuragi concept as SuiKinKutsu) applied to both pitch
  // (+-1.2 semitones at 100%) and stereo pan (full L-R field at 100%, dead center at 0%)
  GetParam(kParamYuragi)->InitDouble("Yuragi", 0., 0., 100., 0.01, "%", IParam::kFlagsNone, "");
  // 5-band EQ, last in the master effects chain - see FirstSynth_Effects.h's
  // ParametricEQEffect. Band gains default to 0dB (no coloration until touched);
  // freqs spread roughly evenly on a log scale across the audible range.
  // Defaults below are the user's own tuned EQ curve, adopted 2026-07-27 as 1st
  // Synth's baseline sound identity (was flat/0dB on every band before) - see
  // progress.md's "5-band EQ defaults adopted from the user's tuned state" entry.
  GetParam(kParamEQLowFreq)->InitFrequency("EQ Low Freq", 125., 20., 20000.);
  GetParam(kParamEQLowGain)->InitDouble("EQ Low Gain", 3.12, -15., 15., 0.01, "dB", IParam::kFlagsNone, "EQ");
  GetParam(kParamEQBand2Freq)->InitFrequency("EQ Band2 Freq", 317., 20., 20000.);
  GetParam(kParamEQBand2Gain)->InitDouble("EQ Band2 Gain", 1.92, -15., 15., 0.01, "dB", IParam::kFlagsNone, "EQ");
  GetParam(kParamEQBand2Q)->InitDouble("EQ Band2 Q", 0.78, 0.1, 10., 0.01, "", IParam::kFlagsNone, "EQ");
  GetParam(kParamEQBand3Freq)->InitFrequency("EQ Band3 Freq", 1000., 20., 20000.);
  GetParam(kParamEQBand3Gain)->InitDouble("EQ Band3 Gain", -3.36, -15., 15., 0.01, "dB", IParam::kFlagsNone, "EQ");
  GetParam(kParamEQBand3Q)->InitDouble("EQ Band3 Q", 1.02, 0.1, 10., 0.01, "", IParam::kFlagsNone, "EQ");
  GetParam(kParamEQBand4Freq)->InitFrequency("EQ Band4 Freq", 1726., 20., 20000.);
  GetParam(kParamEQBand4Gain)->InitDouble("EQ Band4 Gain", 5.04, -15., 15., 0.01, "dB", IParam::kFlagsNone, "EQ");
  GetParam(kParamEQBand4Q)->InitDouble("EQ Band4 Q", 0.62, 0.1, 10., 0.01, "", IParam::kFlagsNone, "EQ");
  GetParam(kParamEQHighFreq)->InitFrequency("EQ High Freq", 4865., 20., 20000.);
  GetParam(kParamEQHighGain)->InitDouble("EQ High Gain", 3.60, -15., 15., 0.01, "dB", IParam::kFlagsNone, "EQ");
  GetParam(kParamEQBypass)->InitBool("EQ Bypass", false, "", IParam::kFlagsNone, "EQ");

  // Modulation Matrix (2026-07-28) - 2 free LFOs + 2 free ADSR envelopes (not
  // hard-wired to any single destination, unlike Pitch/Filter/Amp LFO above) plus
  // 4 matrix slots routing a fixed source list to a fixed destination list at a
  // bipolar amount. See FirstSynth_DSP.h's EMatrixSource/EMatrixDest for the
  // routing logic - the string lists below must stay in that exact same order.
  GetParam(kParamModLFO1Shape)->InitEnum("Mod LFO 1 Shape", LFO<>::kTriangle, {LFO_SHAPE_VALIST});
  GetParam(kParamModLFO1RateHz)->InitFrequency("Mod LFO 1 Rate", 1., 0.01, 40.);
  GetParam(kParamModLFO1RateTempo)->InitEnum("Mod LFO 1 Rate", LFO<>::k1, {LFO_TEMPODIV_VALIST});
  GetParam(kParamModLFO1RateMode)->InitBool("Mod LFO 1 Sync", true);
  GetParam(kParamModLFO2Shape)->InitEnum("Mod LFO 2 Shape", LFO<>::kTriangle, {LFO_SHAPE_VALIST});
  GetParam(kParamModLFO2RateHz)->InitFrequency("Mod LFO 2 Rate", 1., 0.01, 40.);
  GetParam(kParamModLFO2RateTempo)->InitEnum("Mod LFO 2 Rate", LFO<>::k1, {LFO_TEMPODIV_VALIST});
  GetParam(kParamModLFO2RateMode)->InitBool("Mod LFO 2 Sync", true);
  // Pigments curve (min=0, max=20000ms, exponent solves pow(0.5,exp)=1300/20000) - matches Amp/Filter ADSR, see progress.md 2026-08-25
  GetParam(kParamModEnv1Attack)->InitDouble("Mod Env 1 Attack", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "MATRIX", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamModEnv1Decay)->InitDouble("Mod Env 1 Decay", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "MATRIX", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamModEnv1Sustain)->InitDouble("Mod Env 1 Sustain", 50., 0., 100., 1, "%", IParam::kFlagsNone, "MATRIX");
  GetParam(kParamModEnv1Release)->InitDouble("Mod Env 1 Release", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "MATRIX", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamModEnv2Attack)->InitDouble("Mod Env 2 Attack", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "MATRIX", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamModEnv2Decay)->InitDouble("Mod Env 2 Decay", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "MATRIX", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamModEnv2Sustain)->InitDouble("Mod Env 2 Sustain", 50., 0., 100., 1, "%", IParam::kFlagsNone, "MATRIX");
  GetParam(kParamModEnv2Release)->InitDouble("Mod Env 2 Release", 10., 0., 20000., 0.1, "ms", IParam::kFlagsNone, "MATRIX", IParam::ShapePowCurve(3.9434164716336326));
  GetParam(kParamMatrix1Source)->InitEnum("Matrix 1 Source", 0, {"None", "Mod LFO 1", "Mod LFO 2", "Mod Env 1", "Mod Env 2", "Velocity", "Key Follow", "Mod Wheel"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix1Dest)->InitEnum("Matrix 1 Dest", 0, {"None", "Filter Cutoff", "Filter Resonance", "Osc1 Pitch", "Osc2 Pitch", "Amp Level", "Pan", "Wave Shape 1", "Wave Shape 2", "Osc1 Level", "Osc2 Level", "Noise Level", "Osc1 Pitch Fine", "Osc2 Pitch Fine", "Amp Env Time", "Filter Env Time", "Mod Env 1 Time", "Mod Env 2 Time"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix1Amount)->InitDouble("Matrix 1 Amount", 0., -100., 100., 0.1, "%", IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix2Source)->InitEnum("Matrix 2 Source", 0, {"None", "Mod LFO 1", "Mod LFO 2", "Mod Env 1", "Mod Env 2", "Velocity", "Key Follow", "Mod Wheel"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix2Dest)->InitEnum("Matrix 2 Dest", 0, {"None", "Filter Cutoff", "Filter Resonance", "Osc1 Pitch", "Osc2 Pitch", "Amp Level", "Pan", "Wave Shape 1", "Wave Shape 2", "Osc1 Level", "Osc2 Level", "Noise Level", "Osc1 Pitch Fine", "Osc2 Pitch Fine", "Amp Env Time", "Filter Env Time", "Mod Env 1 Time", "Mod Env 2 Time"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix2Amount)->InitDouble("Matrix 2 Amount", 0., -100., 100., 0.1, "%", IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix3Source)->InitEnum("Matrix 3 Source", 0, {"None", "Mod LFO 1", "Mod LFO 2", "Mod Env 1", "Mod Env 2", "Velocity", "Key Follow", "Mod Wheel"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix3Dest)->InitEnum("Matrix 3 Dest", 0, {"None", "Filter Cutoff", "Filter Resonance", "Osc1 Pitch", "Osc2 Pitch", "Amp Level", "Pan", "Wave Shape 1", "Wave Shape 2", "Osc1 Level", "Osc2 Level", "Noise Level", "Osc1 Pitch Fine", "Osc2 Pitch Fine", "Amp Env Time", "Filter Env Time", "Mod Env 1 Time", "Mod Env 2 Time"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix3Amount)->InitDouble("Matrix 3 Amount", 0., -100., 100., 0.1, "%", IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix4Source)->InitEnum("Matrix 4 Source", 0, {"None", "Mod LFO 1", "Mod LFO 2", "Mod Env 1", "Mod Env 2", "Velocity", "Key Follow", "Mod Wheel"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix4Dest)->InitEnum("Matrix 4 Dest", 0, {"None", "Filter Cutoff", "Filter Resonance", "Osc1 Pitch", "Osc2 Pitch", "Amp Level", "Pan", "Wave Shape 1", "Wave Shape 2", "Osc1 Level", "Osc2 Level", "Noise Level", "Osc1 Pitch Fine", "Osc2 Pitch Fine", "Amp Env Time", "Filter Env Time", "Mod Env 1 Time", "Mod Env 2 Time"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix4Amount)->InitDouble("Matrix 4 Amount", 0., -100., 100., 0.1, "%", IParam::kFlagsNone, "MATRIX");
  // 4 -> 8 slots (2026-07-28 user request) - same source/dest lists as slots 1-4 above.
  GetParam(kParamMatrix5Source)->InitEnum("Matrix 5 Source", 0, {"None", "Mod LFO 1", "Mod LFO 2", "Mod Env 1", "Mod Env 2", "Velocity", "Key Follow", "Mod Wheel"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix5Dest)->InitEnum("Matrix 5 Dest", 0, {"None", "Filter Cutoff", "Filter Resonance", "Osc1 Pitch", "Osc2 Pitch", "Amp Level", "Pan", "Wave Shape 1", "Wave Shape 2", "Osc1 Level", "Osc2 Level", "Noise Level", "Osc1 Pitch Fine", "Osc2 Pitch Fine", "Amp Env Time", "Filter Env Time", "Mod Env 1 Time", "Mod Env 2 Time"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix5Amount)->InitDouble("Matrix 5 Amount", 0., -100., 100., 0.1, "%", IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix6Source)->InitEnum("Matrix 6 Source", 0, {"None", "Mod LFO 1", "Mod LFO 2", "Mod Env 1", "Mod Env 2", "Velocity", "Key Follow", "Mod Wheel"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix6Dest)->InitEnum("Matrix 6 Dest", 0, {"None", "Filter Cutoff", "Filter Resonance", "Osc1 Pitch", "Osc2 Pitch", "Amp Level", "Pan", "Wave Shape 1", "Wave Shape 2", "Osc1 Level", "Osc2 Level", "Noise Level", "Osc1 Pitch Fine", "Osc2 Pitch Fine", "Amp Env Time", "Filter Env Time", "Mod Env 1 Time", "Mod Env 2 Time"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix6Amount)->InitDouble("Matrix 6 Amount", 0., -100., 100., 0.1, "%", IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix7Source)->InitEnum("Matrix 7 Source", 0, {"None", "Mod LFO 1", "Mod LFO 2", "Mod Env 1", "Mod Env 2", "Velocity", "Key Follow", "Mod Wheel"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix7Dest)->InitEnum("Matrix 7 Dest", 0, {"None", "Filter Cutoff", "Filter Resonance", "Osc1 Pitch", "Osc2 Pitch", "Amp Level", "Pan", "Wave Shape 1", "Wave Shape 2", "Osc1 Level", "Osc2 Level", "Noise Level", "Osc1 Pitch Fine", "Osc2 Pitch Fine", "Amp Env Time", "Filter Env Time", "Mod Env 1 Time", "Mod Env 2 Time"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix7Amount)->InitDouble("Matrix 7 Amount", 0., -100., 100., 0.1, "%", IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix8Source)->InitEnum("Matrix 8 Source", 0, {"None", "Mod LFO 1", "Mod LFO 2", "Mod Env 1", "Mod Env 2", "Velocity", "Key Follow", "Mod Wheel"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix8Dest)->InitEnum("Matrix 8 Dest", 0, {"None", "Filter Cutoff", "Filter Resonance", "Osc1 Pitch", "Osc2 Pitch", "Amp Level", "Pan", "Wave Shape 1", "Wave Shape 2", "Osc1 Level", "Osc2 Level", "Noise Level", "Osc1 Pitch Fine", "Osc2 Pitch Fine", "Amp Env Time", "Filter Env Time", "Mod Env 1 Time", "Mod Env 2 Time"}, IParam::kFlagsNone, "MATRIX");
  GetParam(kParamMatrix8Amount)->InitDouble("Matrix 8 Amount", 0., -100., 100., 0.1, "%", IParam::kFlagsNone, "MATRIX");
  // Default changed to 2 semitones (whole tone, the common convention) per user
  // request after confirming the knob works - the framework's own default of 12
  // (MidiSynth.h's kDefaultPitchBendRange) was only kept as this param's initial
  // default while first verifying the control, not a deliberate choice.
  GetParam(kParamPitchBendRange)->InitInt("Bend Range", 2, 0, 12, "st", IParam::kFlagsNone, "MIX");
  // -100 = exponential, 0 = linear/proportional, +100 = logarithmic - see
  // FirstSynth_DSP.h's Voice::Trigger() for the actual curve math. Default -37.6
  // (leaning toward exponential) per user's own tuning, after confirming the
  // knob's full range worked correctly.
  GetParam(kParamVelocityCurve)->InitDouble("Velocity Curve", -37.6, -100., 100., 0.1, "%", IParam::kFlagsNone, "MATRIX");
  // 2026-09-01: slow per-oscillator analog-style pitch drift, depth only (rate
  // is fixed, like Yuragi). 0 = off. Osc1 and Osc2 drift independently and at
  // different rates, so a 2-osc patch slowly thickens/beats even without a
  // unison feature. See FirstSynth_DSP.h's Voice for the actual wander math.
  GetParam(kParamOscDrift)->InitDouble("Osc Drift", 0., 0., 100., 0.01, "%", IParam::kFlagsNone, "MIX");

#ifdef WEBVIEW_EDITOR_DELEGATE
  SetEnableDevTools(true);

  // The initial params-sync message (every param's name/min/max/default, base64-
  // encoded, sent as one JS call - see IPlugWebViewEditorDelegate::OnWebContentLoaded)
  // is capped by this buffer (default 8192 chars) and got silently TRUNCATED once this
  // project passed ~50 params - a truncated base64 string fails window.atob()/
  // JSON.parse() in index.html's OnMessage(), which silently aborted the *entire*
  // params-sync case with no visible error, leaving every single knob's min/max stuck
  // at knob-control.js's constructor fallback (0/100) forever. Symptom: DSP/audio
  // behavior was completely unaffected (ProcessBlock reads real IParam values
  // directly, never touches this JS-side cache), but every displayed number was
  // silently wrong against the true param range - most knobs happened to still
  // look plausible with a 0-100 fallback, but a bipolar knob like Filter Env Amount
  // showing "50%" at center (instead of 0%) is what finally made it obvious. Raised
  // generously past any size this project is likely to reach.
  SetMaxJSStringLength(32768);

  mEditorInitFunc = [&]()
  {
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
  };
#endif
}

#if IPLUG_DSP
void FirstSynth::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  // Subscription licence gate (eni_auth, 2026-08-24). mLicenceValid is set from the
  // constructor (always runs before this, even in a DAW that never opens the editor)
  // and re-set from OnIdle() after a successful login - see FirstSynth.h's comment on
  // why ProcessBlock reads this atomic mirror rather than mLicence.valid directly.
  if (!mLicenceValid.load(std::memory_order_relaxed))
  {
    memset(outputs[0], 0, nFrames * sizeof(sample));
    memset(outputs[1], 0, nFrames * sizeof(sample));
    return;
  }

  // mTimeInfo.mTempo is a real host-reported tempo for every other target (CLAP/VST3/
  // AU/AAX/VST2), but the Standalone app has no host transport to report one - it just
  // sits at IPlugStructs.h's DEFAULT_TEMPO (120) forever, regardless of user input.
  // mStandaloneTempo (settable via kMsgTagSetStandaloneTempo, see OnMessage() below) is
  // the Standalone-only substitute.
#ifdef APP_API
  const double tempo = mStandaloneTempo.load(std::memory_order_relaxed);
#else
  const double tempo = mTimeInfo.mTempo;
#endif
  mDSP.ProcessBlock(nullptr, outputs, 2, nFrames, mTimeInfo.mPPQPos, mTimeInfo.mTransportIsRunning, tempo);

  // 2026-08-25: Delay's tempo-sync (kParamDelayTimeMode) needs the current tempo,
  // same as the LFOs above already get via mDSP.ProcessBlock()'s own tempo arg -
  // but mDelay lives here in FirstSynth, not in FirstSynth_DSP.h, and only cares
  // about the current value (not per-sample phase), so a plain once-per-block
  // setter is enough - no need to route it through ProcessBlock's own signature.
  mDelay.SetTempo(tempo);

  for (int s = 0; s < nFrames; s++)
  {
    mBassBoost.Process(outputs[0][s], outputs[1][s]);
    mChorus.Process(outputs[0][s], outputs[1][s]);
    mDelay.Process(outputs[0][s], outputs[1][s]);
    mReverb.Process(outputs[0][s], outputs[1][s]);
    if (!mEQBypassed)
      mEQ.Process(outputs[0][s], outputs[1][s]);

    if (mLooper.Process(outputs[0][s], outputs[1][s]))
    {
      mLooperStateDirty.store(true, std::memory_order_relaxed);
      mLooperWaveformDirty.store(true, std::memory_order_relaxed); // only fires on the 40s auto-stop, which just finalized a recording
    }

    // Level/clip meter (user request: "歪んでるかどうか知るためのメーターが必要") -
    // track the true final output's peak (post Gain, post all effects/looper)
    // as a lock-free running max, read and reset once per OnIdle tick below. A CAS
    // loop is used (not a plain store) since this can race with OnIdle()'s exchange
    // on the main thread; the loop only needs to retry when a *larger* peak lands
    // concurrently, which is rare and cheap.
    double peak = std::max(std::abs(outputs[0][s]), std::abs(outputs[1][s]));
    double cur = mMeterPeak.load(std::memory_order_relaxed);
    while (peak > cur && !mMeterPeak.compare_exchange_weak(cur, peak, std::memory_order_relaxed))
      ;
  }
}

void FirstSynth::OnReset()
{
  mDSP.Reset(GetSampleRate(), GetBlockSize());
  mBassBoost.SetSampleRate(GetSampleRate());
  mChorus.SetSampleRate(GetSampleRate());
  mDelay.SetSampleRate(GetSampleRate());
  mReverb.SetSampleRate(GetSampleRate());
  mEQ.SetSampleRate(GetSampleRate());
  mLooper.SetSampleRate(GetSampleRate());
}

// Cross-format preset browser - see kMsgTagPresetList's comment in FirstSynth.h for
// why this exists alongside the Standalone-only autosave/manual-preset-dialog
// mechanisms rather than replacing them: same underlying SerializeState()/
// UnserializeState() chunk, just stored in one fixed shared folder and exposed
// through the WebView UI in every build.
void FirstSynth::GetPresetsDir(WDL_String& path)
{
  INIPath(path, "FirstSynth");
  path.Append("\\Presets");

  std::error_code ec;
  std::filesystem::create_directories(path.Get(), ec); // no-op if it already exists; ec deliberately ignored, matches this file's other fopen()-and-check-for-null style rather than throwing
}

// Where postbuild-win.bat put the bundled factory presets, resolved the same way
// IPlugWebViewEditorDelegate::LoadIndexHtml() resolves its own Resources\web:
// Debug reads the project source tree directly (compile-time __FILE__), Release
// on Windows looks next to this binary's own module. Anything else -> "".
#if defined OS_WIN && !defined _DEBUG
extern bool GetCurrentModuleDirWin(WDL_String& outDir); // IPlugWebView_win.cpp, decl mirrors IPlugWebViewEditorDelegate.h
#endif

void FirstSynth::GetBundledPresetsDir(WDL_String& path)
{
  path.Set("");
#if !defined OS_WIN
  return; // Windows-only for now, matching the rest of this file's packaging path assumptions
#elif defined _DEBUG
  std::filesystem::path p = std::filesystem::path(__FILE__).parent_path() / "presets";
  path.Set(p.string().c_str());
#else
  WDL_String moduleDir;
  if (GetCurrentModuleDirWin(moduleDir))
  {
    moduleDir.Append("\\Resources\\presets");
    path.Set(moduleDir.Get());
  }
#endif
}

void FirstSynth::SeedFactoryPresets()
{
  WDL_String src;
  GetBundledPresetsDir(src);
  if (src.GetLength() == 0)
    return;

  std::error_code ec;
  if (!std::filesystem::is_directory(src.Get(), ec))
    return;

  // One-time guard: once seeded, a user's later deletion of a factory preset
  // must stick. Suffix is a version - bump it if the shipped factory set is
  // ever changed/expanded so the new ones get seeded on the next launch.
  WDL_String marker;
  INIPath(marker, "FirstSynth");
  marker.Append("\\.factory_seeded_v1");
  if (std::filesystem::exists(marker.Get(), ec))
    return;

  WDL_String dstDir;
  GetPresetsDir(dstDir); // also creates it
  std::filesystem::path dst(dstDir.Get());

  for (const auto& entry : std::filesystem::directory_iterator(src.Get(), ec))
  {
    if (ec)
      break;
    if (!entry.is_regular_file(ec))
      continue;
    const std::filesystem::path& p = entry.path();
    const std::string ext = p.extension().string();
    const std::string name = p.filename().string();
    if (ext != ".preset" && name != "_factory.txt")
      continue;

    std::filesystem::path target = dst / p.filename();
    if (!std::filesystem::exists(target, ec))
      std::filesystem::copy_file(p, target, ec); // best-effort; a failed copy just means that preset isn't seeded, not a crash
  }

  FILE* fp = fopen(marker.Get(), "wb");
  if (fp)
  {
    fputc('1', fp);
    fclose(fp);
  }
}

// Preset names double as filenames and arrive from free-text WebView input (UTF8,
// e.g. Japanese preset names) - this is the actual boundary between "arbitrary UI
// text" and "a real filesystem path". A byte-blacklist, not an ASCII allow-list:
// only reject actual path separators, reserved Windows filename characters, and
// ASCII control bytes - every UTF8 multi-byte sequence (any non-ASCII character,
// high bit set) passes through untouched, so non-English preset names work. The
// mandatory ".preset" suffix appended by the callers already neutralizes ".." as a
// traversal risk (the resulting filename is never literally ".."), so no separate
// check for it is needed as long as no path separator can appear here.
void FirstSynth::SanitizePresetName(const char* rawName, WDL_String& outSafeName)
{
  outSafeName.Set("");

  if (!rawName)
    return;

  for (int i = 0; rawName[i] != '\0' && outSafeName.GetLength() < 128; i++)
  {
    unsigned char c = (unsigned char) rawName[i];
    bool rejected = c < 0x20 || c == 0x7f ||
                    c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
                    c == '"' || c == '<' || c == '>' || c == '|';
    if (!rejected)
      outSafeName.AppendFormatted(2, "%c", (char) c);
  }

  // trim any trailing spaces left over from a rejected trailing character
  while (outSafeName.GetLength() > 0 && outSafeName.Get()[outSafeName.GetLength() - 1] == ' ')
    outSafeName.DeleteSub(outSafeName.GetLength() - 1, 1);
}

void FirstSynth::SendPresetList()
{
  WDL_String dir;
  GetPresetsDir(dir);

  int foundCount = 0;
  int rawCount = 0; // every entry the OS-level enumeration yields, before any filtering
  WDL_String joined;
  WDL_String rawEntries; // one "<filename>:isRegularFile:<ext>" per raw entry, semicolon-joined
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir.Get(), ec))
  {
    rawCount++;
    auto p = entry.path();
    if (rawEntries.GetLength() > 0)
      rawEntries.Append("; ");
    rawEntries.AppendFormatted(300, "%s:%d:%s", p.filename().string().c_str(), (int) entry.is_regular_file(), p.extension().string().c_str());

    if (ec || !entry.is_regular_file())
      continue;

    if (p.extension() != ".preset")
      continue;

    foundCount++;
    if (joined.GetLength() > 0)
      joined.Append("\n");
    joined.Append(p.stem().string().c_str());
  }

  // temporary diagnostic (2026-07-28) - a VST3/CLAP-hosted instance only ever finds
  // whatever preset *it itself* most recently saved, never presets other processes
  // wrote to the same folder earlier (confirmed: Standalone finds 2, a fresh VST3
  // instance in either Renoise or BespokeSynth finds only the 1 it just saved
  // itself, even after a full DAW restart) - logging every *raw* entry the OS-level
  // enumeration yields (rawCount/rawEntries), not just the post-filter count, to
  // tell apart "the OS itself only shows this process 1 file" from "my own
  // is_regular_file()/extension filtering is incorrectly rejecting the other 2".
  WDL_String debugDir;
  for (int i = 0; i < dir.GetLength(); i++)
  {
    char c = dir.Get()[i];
    debugDir.AppendFormatted(2, "%c", c == '\\' ? '/' : c); // avoid JS string-escaping the backslashes
  }
  WDL_String debugMsg;
  debugMsg.SetFormatted(1200, "console.log('FirstSynth presets dir: %s | ec=%d (%s) | rawCount=%d | rawEntries=[%s] | found=%d');",
                        debugDir.Get(), ec.value(), ec.message().c_str(), rawCount, rawEntries.Get(), foundCount);
  EvaluateJavaScript(debugMsg.Get());

  SendArbitraryMsgFromDelegate(kMsgTagPresetList, joined.GetLength(), joined.Get());
}

void FirstSynth::SendCurrentPresetName()
{
  SendArbitraryMsgFromDelegate(kMsgTagCurrentPresetName, mCurrentPresetName.GetLength(), mCurrentPresetName.Get());
}

// forward declarations - defined further down, alongside FirstSynth::SendFactoryPresetList()/
// TogglePresetFactoryMark(), but also needed here by DeletePresetByName()
static void ReadFactoryMarks(const WDL_String& path, std::vector<std::string>& outNames);
static void WriteFactoryMarks(const WDL_String& path, const std::vector<std::string>& names);

void FirstSynth::SavePresetAs(const char* rawName)
{
  WDL_String safeName;
  SanitizePresetName(rawName, safeName);
  if (safeName.GetLength() == 0)
    return;

  WDL_String dir;
  GetPresetsDir(dir);

  WDL_String path;
  path.SetFormatted(MAX_WIN32_PATH_LEN, "%s\\%s.preset", dir.Get(), safeName.Get());

  IByteChunk chunk;
  if (SerializeState(chunk))
  {
    FILE* fp = fopen(path.Get(), "wb");
    if (fp)
    {
      fwrite(chunk.GetData(), 1, (size_t) chunk.Size(), fp);
      fclose(fp);

      mCurrentPresetName.Set(safeName.Get());
      SendCurrentPresetName();
    }
  }

  SendPresetList(); // refresh the UI's dropdown so the just-saved preset shows up immediately
}

void FirstSynth::LoadPresetByName(const char* rawName)
{
  WDL_String safeName;
  SanitizePresetName(rawName, safeName);
  if (safeName.GetLength() == 0)
    return;

  WDL_String dir;
  GetPresetsDir(dir);

  WDL_String path;
  path.SetFormatted(MAX_WIN32_PATH_LEN, "%s\\%s.preset", dir.Get(), safeName.Get());

  FILE* fp = fopen(path.Get(), "rb");
  if (!fp)
    return;

  fseek(fp, 0, SEEK_END);
  long fileSize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fileSize > 0)
  {
    std::vector<uint8_t> buf((size_t) fileSize);
    fread(buf.data(), 1, buf.size(), fp);

    // Reset every param to its compiled-in default before applying the preset's
    // bytes. A .preset file is a headerless positional dump of NParams() doubles
    // (see this file's preset-format comment / progress.md) - a preset saved by
    // an older build simply has fewer doubles, and iPlug2's UnserializeParams
    // stops as soon as the chunk runs out, leaving every param appended since
    // that preset was saved at whatever the *previously loaded* preset left it
    // (user report 2026-09-01: Osc Drift carrying over between presets). Reset
    // first so those params land on their default (0 for Osc Drift) instead.
    // UnserializeParams' own OnParamReset(kPresetRecall) then pushes every param
    // - restored or defaulted - to the DSP; OnRestoreState() does the same for
    // the WebView UI.
    for (int i = 0; i < NParams(); ++i)
      GetParam(i)->Set(GetParam(i)->GetDefault());

    IByteChunk chunk;
    chunk.PutBytes(buf.data(), (int) buf.size());
    UnserializeState(chunk, 0);
    OnRestoreState(); // pushes every restored param's new value to the WebView UI - same combo LoadAutoState() below uses

    mCurrentPresetName.Set(safeName.Get());
    SendCurrentPresetName();
  }

  fclose(fp);
}

// 2026-07-30 user request (ported from SuiKinKutsu, same day): delete a preset
// from the GUI. Reuses the same sanitize-then-build-path pattern as Save/Load -
// std::filesystem::remove() silently returns false (no exception) if the file
// doesn't exist, which is fine here (nothing to do).
void FirstSynth::DeletePresetByName(const char* rawName)
{
  WDL_String safeName;
  SanitizePresetName(rawName, safeName);
  if (safeName.GetLength() == 0)
    return;

  WDL_String dir;
  GetPresetsDir(dir);

  WDL_String path;
  path.SetFormatted(MAX_WIN32_PATH_LEN, "%s\\%s.preset", dir.Get(), safeName.Get());

  std::error_code ec;
  std::filesystem::remove(path.Get(), ec); // ec deliberately ignored, matches this file's other filesystem calls

  // A deleted preset can't stay marked "factory" - clean up the sidecar file
  // too, same spirit as the mCurrentPresetName cleanup right below.
  {
    WDL_String marksPath;
    GetFactoryMarksPath(marksPath);
    std::vector<std::string> names;
    ReadFactoryMarks(marksPath, names);
    auto it = std::find(names.begin(), names.end(), std::string(safeName.Get()));
    if (it != names.end())
    {
      names.erase(it);
      WriteFactoryMarks(marksPath, names);
      SendFactoryPresetList();
    }
  }

  if (mCurrentPresetName.GetLength() > 0 && !strcmp(mCurrentPresetName.Get(), safeName.Get()))
  {
    mCurrentPresetName.Set(""); // it's gone - nothing is "currently loaded" from a preset's perspective any more
    SendCurrentPresetName();
  }

  SendPresetList(); // refresh the UI's dropdown so the deleted preset disappears immediately
}

// File-local helpers for the factory-marks sidecar file (see
// GetFactoryMarksPath()'s own comment) - one sanitized preset name per line,
// plain text, matching this project's existing '\n'-joined convention rather
// than JSON (no other part of this file's preset I/O needs a parser either).
static void ReadFactoryMarks(const WDL_String& path, std::vector<std::string>& outNames)
{
  outNames.clear();

  FILE* fp = fopen(path.Get(), "rb");
  if (!fp)
    return;

  fseek(fp, 0, SEEK_END);
  long fileSize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fileSize > 0)
  {
    std::vector<char> buf((size_t) fileSize);
    fread(buf.data(), 1, buf.size(), fp);

    std::string content(buf.data(), buf.size());
    size_t start = 0;
    while (start <= content.size())
    {
      size_t nl = content.find('\n', start);
      std::string line = (nl == std::string::npos) ? content.substr(start) : content.substr(start, nl - start);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (!line.empty())
        outNames.push_back(line);
      if (nl == std::string::npos)
        break;
      start = nl + 1;
    }
  }

  fclose(fp);
}

static void WriteFactoryMarks(const WDL_String& path, const std::vector<std::string>& names)
{
  FILE* fp = fopen(path.Get(), "wb");
  if (!fp)
    return;

  for (const auto& name : names)
  {
    fwrite(name.data(), 1, name.size(), fp);
    fwrite("\n", 1, 1, fp);
  }

  fclose(fp);
}

void FirstSynth::GetFactoryMarksPath(WDL_String& path)
{
  GetPresetsDir(path);
  path.Append("\\_factory.txt");
}

void FirstSynth::SendFactoryPresetList()
{
  WDL_String path;
  GetFactoryMarksPath(path);

  std::vector<std::string> names;
  ReadFactoryMarks(path, names);

  WDL_String joined;
  for (const auto& name : names)
  {
    if (joined.GetLength() > 0)
      joined.Append("\n");
    joined.Append(name.c_str());
  }

  SendArbitraryMsgFromDelegate(kMsgTagFactoryPresetList, joined.GetLength(), joined.Get());
}

void FirstSynth::TogglePresetFactoryMark(const char* rawName)
{
  WDL_String safeName;
  SanitizePresetName(rawName, safeName);
  if (safeName.GetLength() == 0)
    return;

  WDL_String path;
  GetFactoryMarksPath(path);

  std::vector<std::string> names;
  ReadFactoryMarks(path, names);

  std::string target(safeName.Get());
  auto it = std::find(names.begin(), names.end(), target);
  if (it != names.end())
    names.erase(it);
  else
    names.push_back(target);

  WriteFactoryMarks(path, names);
  SendFactoryPresetList();
}

#ifdef APP_API
// user request: "今後、すべてのパラメータの設定を次に開いたときに覚えているように"
// (remember every param's settings the next time the app is opened) - a CLAP
// instance's state is already persisted through the host's own project save/reload
// (SerializeState/UnserializeState, already correct and shared - see the Preset
// save/load section elsewhere in this file/progress.md), so this file-based
// mechanism is Standalone-only, mirroring the existing manual Save/Load Preset
// feature (IPlugAPP_dialog.cpp) but automatic and using one fixed path instead of a
// user-chosen file. Lives in the same AppData\Local\FirstSynth folder settings.ini
// already uses (INIPath(), from IPlugPaths.h), so no separate folder-creation step
// is needed - that folder already exists by the time this synth has ever been run
// once (the framework's own settings.ini write already created it).
void FirstSynth::GetAutoStatePath(WDL_String& path)
{
  INIPath(path, "FirstSynth");
  path.Append("\\autosave.state");
}

void FirstSynth::GetAutoStatePresetNamePath(WDL_String& path)
{
  INIPath(path, "FirstSynth");
  path.Append("\\autosave_presetname.txt");
}

// called from OnWebContentLoaded() (below), same timing as the existing
// computer-keyboard-input feature - i.e. only once the WebView has actually
// finished loading and is ready to receive EvaluateJavaScript calls. Restoring
// earlier (e.g. directly from the constructor, before any WebView/editor exists at
// all) was considered and rejected: OnRestoreState()'s push would have nothing to
// call EvaluateJavaScript on yet at that point.
void FirstSynth::LoadAutoState()
{
  WDL_String path;
  GetAutoStatePath(path);

  FILE* fp = fopen(path.Get(), "rb");
  if (!fp)
    return;

  fseek(fp, 0, SEEK_END);
  long fileSize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (fileSize > 0)
  {
    std::vector<uint8_t> buf((size_t) fileSize);
    fread(buf.data(), 1, buf.size(), fp);

    // Same reason as LoadPresetByName()'s own reset loop: autosave.state is the
    // identical headerless positional dump, so one written by an older build is
    // short and leaves params appended since then untouched. On a fresh launch
    // they'd already be at their defaults, but resetting explicitly keeps this
    // correct even if LoadAutoState() is ever called at some other time.
    for (int i = 0; i < NParams(); ++i)
      GetParam(i)->Set(GetParam(i)->GetDefault());

    IByteChunk chunk;
    chunk.PutBytes(buf.data(), (int) buf.size());
    UnserializeState(chunk, 0);

    // Which preset name (if any) this just-restored state actually came from -
    // see GetAutoStatePresetNamePath()'s own comment. A missing/unreadable
    // sidecar just leaves mCurrentPresetName blank, same as any other instance
    // that never had a named preset behind it - never treated as fatal.
    WDL_String namePath;
    GetAutoStatePresetNamePath(namePath);
    FILE* nameFp = fopen(namePath.Get(), "rb");
    if (nameFp)
    {
      char nameBuf[256] = {0};
      size_t n = fread(nameBuf, 1, sizeof(nameBuf) - 1, nameFp);
      nameBuf[n] = '\0';
      mCurrentPresetName.Set(nameBuf);
      fclose(nameFp);
    }
  }

  fclose(fp);
}

void FirstSynth::SaveAutoState()
{
  WDL_String path;
  GetAutoStatePath(path);

  IByteChunk chunk;
  if (SerializeState(chunk))
  {
    FILE* fp = fopen(path.Get(), "wb");
    if (fp)
    {
      fwrite(chunk.GetData(), 1, (size_t) chunk.Size(), fp);
      fclose(fp);
    }
  }

  WDL_String namePath;
  GetAutoStatePresetNamePath(namePath);
  FILE* nameFp = fopen(namePath.Get(), "wb");
  if (nameFp)
  {
    fwrite(mCurrentPresetName.Get(), 1, (size_t) mCurrentPresetName.GetLength(), nameFp);
    fclose(nameFp);
  }
}
#endif

void FirstSynth::OnIdle()
{
  // Subscription licence gate (eni_auth, 2026-08-24) - same dirty-flag-drain idiom as
  // mLooperStateDirty below: the RunDeviceFlow worker thread (see OnMessage()) never
  // calls SendArbitraryMsgFromDelegate directly, only sets these flags.
  if (mLicenceDeviceCodeDirty.exchange(false, std::memory_order_relaxed))
  {
    std::string text;
    { std::lock_guard<std::mutex> lock(mLicenceTextMutex); text = mLicenceDeviceCodeText; }
    SendArbitraryMsgFromDelegate(kMsgTagLicenceDeviceCode, (int) text.size(), text.data());
  }

  if (mLicenceResultDirty.exchange(false, std::memory_order_relaxed))
  {
    int outcome = mLicenceResultOutcome.load(std::memory_order_relaxed);
    std::string msg;
    { std::lock_guard<std::mutex> lock(mLicenceTextMutex); msg = mLicenceResultMessage; }

    std::vector<uint8_t> buf;
    buf.reserve(1 + msg.size());
    buf.push_back((uint8_t) outcome);
    buf.insert(buf.end(), msg.begin(), msg.end());
    SendArbitraryMsgFromDelegate(kMsgTagLicenceLoginResult, (int) buf.size(), buf.data());

    if (outcome == 0) // success - re-check (message thread, same as the constructor) and unlock
    {
      mLicence = eni::CheckLicence();
      mLicenceValid.store(mLicence.valid, std::memory_order_relaxed);
      uint8_t stateByte = mLicence.valid ? 1 : 0;
      SendArbitraryMsgFromDelegate(kMsgTagLicenceState, 1, &stateByte);
    }
  }

  if (mLooperStateDirty.exchange(false, std::memory_order_relaxed))
  {
    uint8_t stateByte = (uint8_t) mLooper.GetState();
    SendArbitraryMsgFromDelegate(kMsgTagLooperState, 1, &stateByte);
  }

  if (mLooperWaveformDirty.exchange(false, std::memory_order_relaxed))
  {
    float peaks[LooperEffect<sample>::kWaveformBuckets];
    mLooper.GetWaveformPeaks(peaks);
    SendArbitraryMsgFromDelegate(kMsgTagLooperWaveform, sizeof(peaks), peaks);
  }

  // A queued cut can only land on the audio thread (see FirstSynth_Looper.h's
  // Process()/ApplyPendingCut()), so - same idiom as the two blocks above - it just
  // sets a flag there and this drains it on the main thread. Tell the UI the pending
  // cut is gone (stop the blink) and push a fresh waveform, since the loop's content/
  // length just changed.
  if (mLooper.ConsumeCutJustApplied())
  {
    uint8_t cutPendingBuf[5] = {0, 0, 0, 0, 0}; // 0 = no pending cut
    SendArbitraryMsgFromDelegate(kMsgTagLooperCutPending, sizeof(cutPendingBuf), cutPendingBuf);

    // a cut just landed, so a fresh undo snapshot exists (see
    // FirstSynth_Looper.h's ApplyPendingCut()/SaveUndoSnapshot()) - tell the UI
    // the Undo button is now usable.
    uint8_t undoAvailByte = 1;
    SendArbitraryMsgFromDelegate(kMsgTagLooperUndoAvailable, 1, &undoAvailByte);

    // the cut shifted/truncated the buffer, invalidating GetWaveformPeaks()'s
    // incremental scan cache (built for the pre-cut layout) - discard it so the
    // call below rescans the post-cut content from scratch, from the main thread
    // (see ResetWaveformScan()'s own comment for why this can't happen inside
    // ApplyPendingCut() itself, on the audio thread).
    mLooper.ResetWaveformScan();

    float peaks[LooperEffect<sample>::kWaveformBuckets];
    mLooper.GetWaveformPeaks(peaks);
    SendArbitraryMsgFromDelegate(kMsgTagLooperWaveform, sizeof(peaks), peaks);
  }

  // Same idiom again: RequestUndo() (message thread) just sets a flag,
  // PerformUndo() (audio thread, see Process()) does the actual restore, and this
  // drains the "it happened" notification on the main thread - tell the UI the
  // Undo button is used up (one-level undo, see PerformUndo()'s comment), clear
  // any pending-cut blink (PerformUndo() also cancels a still-queued cut, so it
  // can't immediately re-cut the just-restored audio), and refresh the waveform
  // to show the restored content.
  if (mLooper.ConsumeUndoJustApplied())
  {
    uint8_t cutPendingBuf[5] = {0, 0, 0, 0, 0};
    SendArbitraryMsgFromDelegate(kMsgTagLooperCutPending, sizeof(cutPendingBuf), cutPendingBuf);

    uint8_t undoAvailByte = 0;
    SendArbitraryMsgFromDelegate(kMsgTagLooperUndoAvailable, 1, &undoAvailByte);

    mLooper.ResetWaveformScan();
    float peaks[LooperEffect<sample>::kWaveformBuckets];
    mLooper.GetWaveformPeaks(peaks);
    SendArbitraryMsgFromDelegate(kMsgTagLooperWaveform, sizeof(peaks), peaks);
  }

  // While actively Recording or Overdubbing, buffer content is changing every
  // sample - push the waveform every tick here too (not just on transport press/
  // cut-apply/auto-stop), so it visibly keeps pace with what's actually being
  // recorded/overdubbed instead of sitting frozen on stale content. Bounded cost:
  // Recording auto-stops at the 40s cap, and Overdubbing's loop length is fixed
  // once established (GetWaveformPeaks() forces a full-but-bounded rescan for that
  // state specifically - see its own comment) - neither is an unconditional-forever
  // cost like the meter/progress pushes above.
  ELooperState looperState = mLooper.GetState();
  if (looperState == ELooperState::kRecording || looperState == ELooperState::kOverdubbing)
  {
    float peaks[LooperEffect<sample>::kWaveformBuckets];
    mLooper.GetWaveformPeaks(peaks);
    SendArbitraryMsgFromDelegate(kMsgTagLooperWaveform, sizeof(peaks), peaks);
  }

  // pushes CC7-driven Gain changes to the WebView UI - see ProcessMidiMsg's own
  // comment for why this can't happen directly from there (audio thread, and
  // SendParameterValueFromDelegate must run on the main thread).
  if (mGainUIDirty.exchange(false, std::memory_order_relaxed))
  {
    SendParameterValueFromDelegate(kParamGain, GetParam(kParamGain)->GetNormalized(), true);
  }

  // Push this tick's peak to the UI meter and reset for the next tick - sent
  // unconditionally every IDLE_TIMER_RATE (50ms), same as a real hardware meter's
  // continuous readout rather than only-on-change.
  float peak = (float) mMeterPeak.exchange(0., std::memory_order_relaxed);
  SendArbitraryMsgFromDelegate(kMsgTagMeterLevel, sizeof(float), &peak);

  // Looper recording/playback gauge - also pushed unconditionally every tick, same
  // reasoning as the meter above (a continuous readout, not just on state change).
  float looperProgress[2] = { (float) mLooper.GetBarFraction(), (float) mLooper.GetCursorFraction() };
  SendArbitraryMsgFromDelegate(kMsgTagLooperProgress, sizeof(looperProgress), looperProgress);

#ifdef APP_API
  // batched, at most once per idle tick (50ms) rather than once per param change -
  // see mAutoStateDirty's own comment in OnParamChange
  if (mAutoStateDirty.exchange(false, std::memory_order_relaxed))
    SaveAutoState();
#endif
}

void FirstSynth::ProcessMidiMsg(const IMidiMsg& msg)
{
  TRACE;

  int status = msg.StatusMsg();

  switch (status)
  {
    case IMidiMsg::kNoteOn:
    case IMidiMsg::kNoteOff:
    case IMidiMsg::kPolyAftertouch:
    case IMidiMsg::kControlChange:
    case IMidiMsg::kProgramChange:
    case IMidiMsg::kChannelAftertouch:
    case IMidiMsg::kPitchWheel:
    {
      goto handle;
    }
    default:
      return;
  }

handle:
  // MIDI CC7 (Channel Volume) now drives the Gain param directly, rather than a
  // separate hidden multiplier (mCCVolume, removed) applied after it - user found
  // having two overlapping "volume" controls (one invisible) confusing, and wanted
  // a MIDI keyboard's physical volume slider to move the same visible Gain knob
  // instead. ControlChange() already returns [0,1], which for Gain (a plain linear
  // 0-100% param, no curve) is exactly its own normalized value - no conversion
  // needed.
  if (status == IMidiMsg::kControlChange && msg.ControlChangeIdx() == IMidiMsg::kChannelVolume)
  {
    // SetParameterValue() updates the param's real value (audibly correct
    // immediately) and calls the DSP-side OnParamChange(int) - but for the
    // Standalone app, InformHostOfParamChange() is a no-op (no host to inform,
    // IPlugAPP.h) and OnParamChange(idx, kUI)'s default path never reaches the
    // WebView UI push (that needs SendParameterValueFromDelegate specifically,
    // which touches WebView2's COM object and must run on the main thread - this
    // handler runs on the audio thread) - so the Gain *param* was already correct
    // but the on-screen knob never moved. mGainUIDirty + OnIdle() below does that
    // push safely, same reasoning/pattern as mLooperStateDirty.
    SetParameterValue(kParamGain, msg.ControlChange(IMidiMsg::kChannelVolume));
    mGainUIDirty.store(true, std::memory_order_relaxed);
  }

  mDSP.ProcessMidiMsg(msg);
  SendMidiMsg(msg);
}
#endif

bool FirstSynth::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  if (msgTag == kMsgTagSetUIScale)
  {
    const int scalePercent = ctrlTag;
    // 2026-08-18: was PLUG_WIDTH/PLUG_HEIGHT (raw, un-DPI-scaled) - fixed
    // alongside the "editor opens cropped in real VST3 hosts" bug (see
    // WebViewEditorDelegate::SetScreenScale()'s own comment for the full
    // story). Resize()/EditorResizeFromUI() asks the HOST to resize the
    // parent window to exactly the width/height given here - on a >100%
    // scaled display, the un-scaled PLUG_WIDTH/HEIGHT value is smaller than
    // the real physical size the content needs, so the window would resize
    // but still not be big enough to show everything (confirmed: "拡大しま
    // すが、GUI全部は見えません" after this Zoom feature ran at 100%, right
    // after the getSize()-side fix alone had already made the *initial* open
    // size correct - proving this is a second, separate call path needing
    // the same DPI-awareness). GetScreenScale() is 1.0 until the host's
    // first setContentScaleFactor() call lands, so this is a no-op change
    // for hosts/situations that never report a scale.
    const double dpiScale = (double) GetScreenScale();
    const int newW = static_cast<int>(std::round(PLUG_WIDTH * dpiScale * scalePercent / 100.0));
    const int newH = static_cast<int>(std::round(PLUG_HEIGHT * dpiScale * scalePercent / 100.0));
    Resize(newW, newH);
    return true;
  }

#if IPLUG_DSP
  // Subscription licence gate (eni_auth, 2026-08-24). RunDeviceFlow() blocks (browser
  // + polling) and is not self-threading (unlike eni::RefreshInBackground(), called
  // from the constructor), so this spins its own thread - same .detach()-not-.join()
  // idiom as RefreshInBackground() itself, the only other threading precedent in this
  // codebase. Nothing here calls SendArbitraryMsgFromDelegate directly from the worker
  // thread - results are staged into the dirty-flag members and drained by OnIdle().
  if (msgTag == kMsgTagLicenceLoginRequest)
  {
    if (!mLicenceLoginInFlight.exchange(true, std::memory_order_relaxed))
    {
      std::thread([this]()
      {
        eni::Error err;
        const bool ok = eni::RunDeviceFlow(
          [this](const eni::DeviceCode& dc)
          {
            std::lock_guard<std::mutex> lock(mLicenceTextMutex);
            mLicenceDeviceCodeText = dc.userCode + "\n" + dc.verificationUri + "\n" + dc.verificationUriComplete;
            mLicenceDeviceCodeDirty.store(true, std::memory_order_relaxed);
          },
          err);

        {
          std::lock_guard<std::mutex> lock(mLicenceTextMutex);
          mLicenceResultMessage = ok ? std::string() : err.message;
        }
        mLicenceResultOutcome.store(ok ? 0 : (err.code == "no_subscription" ? 2 : 1), std::memory_order_relaxed);
        mLicenceResultDirty.store(true, std::memory_order_relaxed);
        mLicenceLoginInFlight.store(false, std::memory_order_relaxed);
      }).detach();
    }
    return true;
  }

  if (msgTag == kMsgTagLooperTransport || msgTag == kMsgTagLooperStop || msgTag == kMsgTagLooperClear)
  {
    ELooperState newState;
    if (msgTag == kMsgTagLooperTransport)    newState = mLooper.CycleTransport();
    else if (msgTag == kMsgTagLooperStop)    newState = mLooper.Stop();
    else /* kMsgTagLooperClear */            newState = mLooper.Clear();

    uint8_t stateByte = (uint8_t) newState;
    SendArbitraryMsgFromDelegate(kMsgTagLooperState, 1, &stateByte);

    // Refresh the waveform display on every transport/stop/clear press, not just
    // the moment a recording finishes - Overdub actively rewrites buffer content
    // as it plays, and Clear needs to blank the display back out (GetWaveformPeaks
    // correctly returns all-zero once mLoopLengthSamples is reset to 0).
    float peaks[LooperEffect<sample>::kWaveformBuckets];
    mLooper.GetWaveformPeaks(peaks);
    SendArbitraryMsgFromDelegate(kMsgTagLooperWaveform, sizeof(peaks), peaks);

    // Stop/Clear both cancel any pending cut internally (see their own comments in
    // FirstSynth_Looper.h) - tell the UI so the blink overlay clears immediately,
    // rather than waiting for a wrap that Stopped will never reach, or that no
    // longer means anything once Clear wiped the loop.
    if (msgTag == kMsgTagLooperStop || msgTag == kMsgTagLooperClear)
    {
      uint8_t cutPendingBuf[5] = {0, 0, 0, 0, 0};
      SendArbitraryMsgFromDelegate(kMsgTagLooperCutPending, sizeof(cutPendingBuf), cutPendingBuf);
    }

    // Clear() also wipes the undo snapshot (see its own comment) - Stop doesn't.
    if (msgTag == kMsgTagLooperClear)
    {
      uint8_t undoAvailByte = 0;
      SendArbitraryMsgFromDelegate(kMsgTagLooperUndoAvailable, 1, &undoAvailByte);
    }

    return true;
  }

  if (msgTag == kMsgTagLooperCut && dataSize >= 5)
  {
    const uint8_t* bytes = (const uint8_t*) pData;
    bool isLeft = bytes[0] != 0;
    float posFrac;
    memcpy(&posFrac, bytes + 1, 4);

    if (mLooper.QueueCut(isLeft, (double) posFrac))
    {
      uint8_t cutPendingBuf[5];
      cutPendingBuf[0] = isLeft ? 1 : 2;
      float sendFrac = (float) mLooper.GetPendingCutFraction();
      memcpy(cutPendingBuf + 1, &sendFrac, 4);
      SendArbitraryMsgFromDelegate(kMsgTagLooperCutPending, sizeof(cutPendingBuf), cutPendingBuf);
    }
    return true;
  }

  if (msgTag == kMsgTagLooperUndoCut)
  {
    // just requests it - the actual restore happens on the audio thread and is
    // reported back via ConsumeUndoJustApplied() in OnIdle() (see its comment),
    // same async pattern as kMsgTagLooperCut/QueueCut() above.
    mLooper.RequestUndo();
    return true;
  }

  // Cross-format preset browser - not gated on APP_API, see kMsgTagPresetList's
  // comment in FirstSynth.h. pData is the raw UTF8 preset name (not
  // null-terminated by the sender), so it's copied into a std::string first to get
  // a real null-terminated buffer for SavePresetAs()/LoadPresetByName().
  if (msgTag == kMsgTagPresetSave && dataSize > 0)
  {
    std::string name((const char*) pData, (size_t) dataSize);
    SavePresetAs(name.c_str());
    return true;
  }

  if (msgTag == kMsgTagPresetLoad && dataSize > 0)
  {
    std::string name((const char*) pData, (size_t) dataSize);
    LoadPresetByName(name.c_str());
    return true;
  }

  if (msgTag == kMsgTagPresetDelete && dataSize > 0)
  {
    std::string name((const char*) pData, (size_t) dataSize);
    DeletePresetByName(name.c_str());
    return true;
  }

  if (msgTag == kMsgTagPresetToggleFactory && dataSize > 0)
  {
    std::string name((const char*) pData, (size_t) dataSize);
    TogglePresetFactoryMark(name.c_str());
    return true;
  }

#ifdef APP_API
  if (msgTag == kMsgTagSetStandaloneTempo && dataSize >= 4)
  {
    float bpm;
    memcpy(&bpm, pData, 4);
    if (bpm > 0.f) // guard against a stray 0/negative value turning tempo-synced LFOs silent-freq (see LFO::ProcessBlock's own 0.0 guard)
      mStandaloneTempo.store((double) bpm, std::memory_order_relaxed);
    return true;
  }
#endif
#endif

  return false;
}

void FirstSynth::OnParamChange(int paramIdx)
{
#if IPLUG_DSP
  double value = GetParam(paramIdx)->Value();

#ifdef APP_API
  // gated on mAutoStateLoaded (see its own comment in FirstSynth.h) - ignores the
  // framework's own pre-editor OnParamReset(kReset) startup pass (default values,
  // fires before LoadAutoState() ever runs) so it can't clobber the real saved file
  // before it's been read. Once genuinely loaded, any param (drag, host automation,
  // even the restore itself, or the manual Load Preset path) marks state dirty;
  // OnIdle() below does the actual write, batched to once per idle tick rather than
  // once per param-change (which could be many times a second while dragging).
  if (mAutoStateLoaded.load(std::memory_order_relaxed))
    mAutoStateDirty.store(true, std::memory_order_relaxed);
#endif

  switch (paramIdx)
  {
    case kParamChorusRate:     mChorus.SetRateHz((sample) value); break;
    case kParamChorusDepth:    mChorus.SetDepth((sample) value / 100.); break;
    case kParamChorusMix:      mChorus.SetMix((sample) value / 100.); break;
    case kParamDelayTime:      mDelay.SetTimeMs((sample) value); break;
    case kParamDelayFeedback:  mDelay.SetFeedback((sample) value / 100.); break;
    case kParamDelayMix:       mDelay.SetMix((sample) value / 100.); break;
    case kParamDelayPingPong:  mDelay.SetPingPong(value > 0.5); break;
    case kParamDelayTimeTempo: mDelay.SetDivision((int) value); break;
    case kParamDelayTimeMode:  mDelay.SetSyncMode(value > 0.5); break;
    case kParamReverbDecay:    mReverb.SetDecay((sample) value / 100.); break;
    case kParamReverbDamping:  mReverb.SetDamping((sample) value / 100.); break;
    case kParamReverbMix:      mReverb.SetMix((sample) value / 100.); break;
    case kParamLooperReverse:  mLooper.SetReverse(value > 0.5); break;
    case kParamLooperFeedback: mLooper.SetFeedback((sample) value / 100.); break;
    case kParamLooperMix:      mLooper.SetMix((sample) value / 100.); break;
    case kParamBassBoost:      mBassBoost.SetAmount((sample) value / 100.); break;
    case kParamEQLowFreq:      mEQ.SetFreq(0, (sample) value); break;
    case kParamEQLowGain:      mEQ.SetGainDb(0, (sample) value); break;
    case kParamEQBand2Freq:    mEQ.SetFreq(1, (sample) value); break;
    case kParamEQBand2Gain:    mEQ.SetGainDb(1, (sample) value); break;
    case kParamEQBand2Q:       mEQ.SetQ(1, (sample) value); break;
    case kParamEQBand3Freq:    mEQ.SetFreq(2, (sample) value); break;
    case kParamEQBand3Gain:    mEQ.SetGainDb(2, (sample) value); break;
    case kParamEQBand3Q:       mEQ.SetQ(2, (sample) value); break;
    case kParamEQBand4Freq:    mEQ.SetFreq(3, (sample) value); break;
    case kParamEQBand4Gain:    mEQ.SetGainDb(3, (sample) value); break;
    case kParamEQBand4Q:       mEQ.SetQ(3, (sample) value); break;
    case kParamEQHighFreq:     mEQ.SetFreq(4, (sample) value); break;
    case kParamEQHighGain:     mEQ.SetGainDb(4, (sample) value); break;
    case kParamEQBypass:       mEQBypassed = value > 0.5; break;
    default:                   mDSP.SetParam(paramIdx, value); break;
  }
#endif
}

#ifdef WEBVIEW_EDITOR_DELEGATE
bool FirstSynth::CanNavigateToURL(const char* url)
{
  DBGMSG("Navigating to URL %s\n", url);

  return true;
}

bool FirstSynth::OnCanDownloadMIMEType(const char* mimeType)
{
  return std::string_view(mimeType) != "text/html";
}

void FirstSynth::OnDownloadedFile(const char* path)
{
  WDL_String str;
  str.SetFormatted(64, "Downloaded file to %s\n", path);
  LoadHTML(str.Get());
}

void FirstSynth::OnFailedToDownloadFile(const char* path)
{
  WDL_String str;
  str.SetFormatted(64, "Faild to download file to %s\n", path);
  LoadHTML(str.Get());
}

void FirstSynth::OnGetLocalDownloadPathForFile(const char* fileName, WDL_String& localPath)
{
  DesktopPath(localPath);
  localPath.AppendFormatted(MAX_WIN32_PATH_LEN, "/%s", fileName);
}

void FirstSynth::OnWebContentLoaded()
{
  // must call the base implementation - it sends the "params" JSON describing every
  // parameter to the UI and triggers OnUIOpen() (which syncs current values); without
  // this, overriding the method here would silently break the entire UI sync
  EDITOR_DELEGATE_CLASS::OnWebContentLoaded();

  // shows the build version in the page header (index.html's #versionLabel) so it's
  // visible without opening an installer/about box - PLUG_VERSION_STR (config.h)
  // stays the single source of truth, this just displays whatever that's set to.
  // Not gated on APP_API - relevant when hosted as CLAP too, unlike the
  // keyboard-input feature right below.
  EvaluateJavaScript("if (typeof SetVersionLabel === 'function') { SetVersionLabel('" PLUG_VERSION_STR "'); }");

  // cross-format preset browser (index.html) - not gated on APP_API, works the same
  // in every build, see kMsgTagPresetList's comment in FirstSynth.h
  SendPresetList();
  // Reflects whatever this instance's mCurrentPresetName already is right now -
  // correctly empty for a genuinely fresh instance (e.g. just inserted in a DAW
  // project), or the real last-loaded/-saved name if the GUI is merely being
  // reopened on an already-running instance (mCurrentPresetName isn't reset by a
  // WebView reload, only by an actual Load/Save/Delete - see its own comment).
  // Sent again below, after LoadAutoState(), for the Standalone case where that
  // call can update mCurrentPresetName from the on-disk sidecar file.
  SendCurrentPresetName();
  // "Factory" marks (see kMsgTagFactoryPresetList's own comment) - not tied to
  // this instance's own state at all (just a shared sidecar file next to the
  // presets themselves), so one send here is enough; no APP_API-only re-send
  // needed like SendCurrentPresetName() above.
  SendFactoryPresetList();

  // The Factory toggle button itself (index.html) is a developer-only tool for
  // marking presets, not something a real end user's copy should show - see
  // that button's own HTML comment. It starts hidden in the markup; this is
  // the only thing that ever reveals it, and only in a Debug build.
#ifdef _DEBUG
  EvaluateJavaScript("if (typeof SetDevBuild === 'function') { SetDevBuild(true); }");
#else
  EvaluateJavaScript("if (typeof SetDevBuild === 'function') { SetDevBuild(false); }");
#endif

  // Subscription licence gate (eni_auth, 2026-08-24) - not gated on APP_API either,
  // the lock screen must appear in every plugin format. mLicence was already computed
  // in the constructor, before any WebView existed; this is the first point it's safe
  // to call EvaluateJavaScript (same reasoning as LoadAutoState()'s own deferral below).
  {
    uint8_t stateByte = mLicence.valid ? 1 : 0;
    SendArbitraryMsgFromDelegate(kMsgTagLicenceState, 1, &stateByte);
  }

  // the computer-keyboard-as-MIDI-keyboard feature (index.html) is a dev convenience
  // for the standalone app only - a CLAP instance embedded in a host must not hijack
  // keys like A-L that the host itself may use for shortcuts
#ifdef APP_API
  EvaluateJavaScript("if (typeof EnableComputerKeyboardInput === 'function') { EnableComputerKeyboardInput(); }");

  // the manual Tempo control (index.html) only makes sense in Standalone - a CLAP/
  // VST/etc. instance already gets a real tempo from its host, see ProcessBlock()'s
  // own comment on mStandaloneTempo above.
  EvaluateJavaScript("if (typeof EnableStandaloneTempoControl === 'function') { EnableStandaloneTempoControl(); }");

  // "remember every param across launches" (user request) - restore right here,
  // now that the WebView has actually finished loading (EDITOR_DELEGATE_CLASS's call
  // above already sent one round of param values reflecting the compiled-in defaults;
  // this overwrites the UI with the real restored ones a moment later, exact same
  // UnserializeState()+OnRestoreState() combo as the manual Load Preset menu command
  // already uses successfully - see LoadAutoState()'s own comment for why this timing
  // was chosen over restoring earlier, directly in the constructor).
  LoadAutoState();
  OnRestoreState();
  // LoadAutoState() may have just updated mCurrentPresetName from the sidecar
  // file (Standalone-only) - re-send now that it reflects the real restored
  // state, superseding the empty one sent unconditionally above.
  SendCurrentPresetName();
  // only from this point on does a param change get treated as "real" and worth
  // saving - see mAutoStateLoaded's own comment in FirstSynth.h for the startup race
  // this prevents.
  mAutoStateLoaded.store(true, std::memory_order_relaxed);
#endif
}
#endif

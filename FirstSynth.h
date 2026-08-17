#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include <atomic>

using namespace iplug;

const int kNumPresets = 1;

enum EMsgTags
{
  kMsgTagSetUIScale = 0, // ctrlTag = scale percent (e.g. 100/90/80/70), from a WebView-only zoom preference - see index.html's ApplyUIScale()
  kMsgTagLooperTransport, // UI -> C++: cycles Empty->Recording->Playing<->Overdubbing, see LooperEffect::CycleTransport()
  kMsgTagLooperStop, // UI -> C++: -> ELooperState::kStopped (no-op if Empty)
  kMsgTagLooperState, // C++ -> UI: 1-byte ELooperState push, sent after every transport/stop/clear message and from OnIdle() when the DSP auto-stops recording at the 60s cap
  kMsgTagLooperClear, // UI -> C++: discards the recorded loop, -> ELooperState::kEmpty, see LooperEffect::Clear()
  kMsgTagMeterLevel, // C++ -> UI: 4-byte float, the linear peak (can exceed 1.0) seen across both output channels since the last OnIdle tick - drives the Master panel's level/clip meter
  kMsgTagLooperProgress, // C++ -> UI: 2 floats (barFraction, cursorFraction), pushed every OnIdle tick - drives the Looper page's recording/playback gauge, see LooperEffect::GetBarFraction()/GetCursorFraction()
  kMsgTagLooperWaveform, // C++ -> UI: LooperEffect<sample>::kWaveformBuckets floats (0..1 peak per bucket), pushed whenever a recording finishes - drives the faint waveform strip under the gauge, see LooperEffect::GetWaveformPeaks()
  kMsgTagLooperCut, // UI -> C++: 1 byte (0=left scissors, 1=right scissors) + 4-byte float position (0..1, same 60s-cap-relative scale as GetBarFraction()) - queues a cut, see LooperEffect::QueueCut()
  kMsgTagLooperCutPending, // C++ -> UI: 1 byte (0=none, 1=left pending, 2=right pending) + 4-byte float position - drives the blinking pending-cut overlay on the gauge/waveform
  kMsgTagLooperUndoCut, // UI -> C++: no data - undoes the last cut that actually landed, see LooperEffect::RequestUndo() (repurposed 2026-07-27 from "cancel a pending cut" - same tag number 10, new meaning, see progress.md)
  kMsgTagLooperUndoAvailable, // C++ -> UI: 1 byte (0/1) - whether an undo is currently available, drives the Undo button's enabled look
  kMsgTagSetStandaloneTempo, // UI -> C++: 4-byte float BPM. Standalone-only (see FirstSynth.cpp's ProcessBlock()/OnMessage()) - a real DAW host reports its own tempo via mTimeInfo.mTempo, but the Standalone app has no host transport to report one, so this lets the user set a base BPM for tempo-synced LFOs manually. Harmless if ever sent from a non-Standalone build (OnMessage() just stores it), but the UI control that sends it is only ever shown in Standalone.
  // Cross-format preset browser (2026-07-28) - unlike the Standalone-only manual
  // Save/Load Preset file dialog (IPlugAPP_dialog.cpp) or any given host's own
  // native VST3/CLAP preset management, these work identically in every build:
  // presets are just raw SerializeState() chunks, dumped to *.preset files in one
  // fixed shared folder (GetPresetsDir(), AppData\Local\FirstSynth\Presets\) so a
  // preset saved in one format/host is immediately visible from any other. Not
  // gated by APP_API - deliberately the one preset mechanism meant to work
  // everywhere.
  kMsgTagPresetList, // C++ -> UI: preset names (no extension), '\n'-joined, UTF8. Sent once from OnWebContentLoaded() and again after every save/delete, so the dropdown/prev-next browser always reflects what's on disk.
  kMsgTagPresetSave, // UI -> C++: UTF8 preset name to save the current full state as (see SavePresetAs()) - sanitized before touching the filesystem, see SanitizePresetName(). Overwrites in place if that name already exists.
  kMsgTagPresetLoad, // UI -> C++: UTF8 preset name to load (see LoadPresetByName())
  // added 2026-07-30 (ported from SuiKinKutsu, same day - user request: delete +
  // overwrite-in-place from the GUI) - appended per this project's "never
  // renumber" convention, not inserted among the 3 above.
  kMsgTagPresetDelete // UI -> C++: UTF8 preset name to delete (see DeletePresetByName())
};

enum EParams
{
  kParamGain = 0,
  kParamNoteGlideTime,
  kParamAttack,
  kParamDecay,
  kParamSustain,
  kParamRelease,
  kParamLFOShape,
  kParamLFORateHz,
  kParamLFORateTempo,
  kParamLFORateMode,
  kParamLFODepth,
  kParamOsc1Wave,
  kParamOsc1Octave,
  kParamOsc1Semi,
  kParamOsc1Fine,
  kParamOsc2Wave,
  kParamOsc2Octave,
  kParamOsc2Semi,
  kParamOsc2Fine,
  kParamMixOsc1,
  kParamMixOsc2,
  kParamMixNoise,
  kParamFilterCutoff,
  kParamFilterResonance,
  kParamFilterType,
  kParamFilterSlope,
  kParamFilterEnvAmount,
  kParamFilterAttack,
  kParamFilterDecay,
  kParamFilterSustain,
  kParamFilterRelease,
  kParamFilterLFOShape,
  kParamFilterLFORateHz,
  kParamFilterLFORateTempo,
  kParamFilterLFORateMode,
  kParamFilterLFODepth,
  kParamAmpLFOShape,
  kParamAmpLFORateHz,
  kParamAmpLFORateTempo,
  kParamAmpLFORateMode,
  kParamAmpLFODepth,
  kParamChorusRate,
  kParamChorusDepth,
  kParamChorusMix,
  kParamDelayTime,
  kParamDelayFeedback,
  kParamDelayMix,
  kParamDelayPingPong,
  kParamReverbDecay,
  kParamReverbDamping,
  kParamReverbMix,
  kParamLooperReverse,
  // kParamLooperSpeed removed 2026-07-22 - see FirstSynth_Looper.h's class comment for
  // why (variable-speed overdub-recording is fundamentally hard to do without either
  // aliasing or losing information; added and removed same session, never in a real
  // saved preset, so renumbering here doesn't break anything, unlike this project's
  // usual "never renumber" convention for params that could be in an existing preset)
  kParamLooperFeedback,
  kParamLooperMix,
  kParamBassBoost, // Master panel - see FirstSynth_Effects.h's BassBoostEffect
  kParamFilterKeyFollow, // FILTER panel - scales cutoff with note pitch, see FirstSynth_DSP.h's Voice::mFilterKeyFollow
  // 5-band parametric EQ, last in the master effects chain (after Reverb) - see
  // FirstSynth_Effects.h's ParametricEQEffect. Band 0 = Low Shelf, bands 1-3 =
  // Peaking/Bell (each with its own Q), band 4 = High Shelf - shelf bands have no Q
  // (fixed S=1 shelf slope, see that class's comment).
  kParamEQLowFreq,
  kParamEQLowGain,
  kParamEQBand2Freq,
  kParamEQBand2Gain,
  kParamEQBand2Q,
  kParamEQBand3Freq,
  kParamEQBand3Gain,
  kParamEQBand3Q,
  kParamEQBand4Freq,
  kParamEQBand4Gain,
  kParamEQBand4Q,
  kParamEQHighFreq,
  kParamEQHighGain,
  kParamYuragi, // Master panel - per-note random pitch offset + random L/R pan, see FirstSynth_DSP.h's Voice::mYuragiRate

  // Modulation Matrix (2026-07-28) - 2 free LFOs + 2 free ADSR envelopes, dedicated
  // to the matrix (not hard-wired to any single destination like Pitch/Filter/Amp
  // LFO are), plus 4 matrix slots each routing one of EMatrixSource's fixed
  // sources (these 2 LFOs/2 envelopes, or Velocity/Key Follow/Mod Wheel) to one of
  // EMatrixDest's fixed destinations at a bipolar amount. See FirstSynth_DSP.h for
  // EMatrixSource/EMatrixDest and the actual routing logic.
  kParamModLFO1Shape,
  kParamModLFO1RateHz,
  kParamModLFO1RateTempo,
  kParamModLFO1RateMode,
  kParamModLFO2Shape,
  kParamModLFO2RateHz,
  kParamModLFO2RateTempo,
  kParamModLFO2RateMode,
  kParamModEnv1Attack,
  kParamModEnv1Decay,
  kParamModEnv1Sustain,
  kParamModEnv1Release,
  kParamModEnv2Attack,
  kParamModEnv2Decay,
  kParamModEnv2Sustain,
  kParamModEnv2Release,
  kParamMatrix1Source,
  kParamMatrix1Dest,
  kParamMatrix1Amount,
  kParamMatrix2Source,
  kParamMatrix2Dest,
  kParamMatrix2Amount,
  kParamMatrix3Source,
  kParamMatrix3Dest,
  kParamMatrix3Amount,
  kParamMatrix4Source,
  kParamMatrix4Dest,
  kParamMatrix4Amount,
  // 4 -> 8 slots (2026-07-28 user request) - appended, not inserted among slots
  // 1-4 above, per this project's usual "never renumber" convention.
  kParamMatrix5Source,
  kParamMatrix5Dest,
  kParamMatrix5Amount,
  kParamMatrix6Source,
  kParamMatrix6Dest,
  kParamMatrix6Amount,
  kParamMatrix7Source,
  kParamMatrix7Dest,
  kParamMatrix7Amount,
  kParamMatrix8Source,
  kParamMatrix8Dest,
  kParamMatrix8Amount,
  // added 2026-08-01, appended per this project's "never renumber" convention -
  // pitch bend range in semitones, up to a full octave. The underlying iPlug2
  // MidiSynth already supports this (SetPitchBendRange(), default 12 semitones,
  // see MidiSynth.h) but this project never exposed it as a real param before now.
  kParamPitchBendRange,
  // added 2026-08-01, appended per this project's "never renumber" convention -
  // continuously morphs the Modulation Matrix's Velocity source's response curve
  // from exponential (soft notes disproportionately quiet) through linear/
  // proportional (0, the default) to logarithmic (soft notes disproportionately
  // loud). See FirstSynth_DSP.h's Voice::Trigger() for the actual shaping.
  kParamVelocityCurve,
  kNumParams
};

#if IPLUG_DSP
#include "FirstSynth_DSP.h"
#include "FirstSynth_Effects.h"
#include "FirstSynth_Looper.h"
#endif

class FirstSynth final : public Plugin
{
public:
  FirstSynth(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
  void OnIdle() override; // pushes a kMsgTagLooperState update to the UI if the looper auto-stopped mid-block (see FirstSynth_Looper.h's Process())
#endif
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
  void OnParamChange(int paramIdx) override;
#ifdef WEBVIEW_EDITOR_DELEGATE
  bool CanNavigateToURL(const char* url);
  bool OnCanDownloadMIMEType(const char* mimeType) override;
  void OnFailedToDownloadFile(const char* path) override;
  void OnDownloadedFile(const char* path) override;
  void OnGetLocalDownloadPathForFile(const char* fileName, WDL_String& localPath) override;
  void OnWebContentLoaded() override;
#endif

private:
#if IPLUG_DSP
  IPlugInstrumentDSP<sample> mDSP {16};
  BassBoostEffect<sample> mBassBoost;
  ChorusEffect<sample> mChorus;
  DelayEffect<sample> mDelay;
  ReverbEffect<sample> mReverb;
  ParametricEQEffect<sample> mEQ;
  LooperEffect<sample> mLooper;
  std::atomic<bool> mLooperStateDirty {false}; // set from ProcessBlock (audio thread) when the looper auto-stops recording, consumed by OnIdle() (main thread)
  std::atomic<bool> mLooperWaveformDirty {false}; // same idiom, also set when the looper auto-stops recording (the only recording-finish case that happens off the main thread - UI-triggered Stop/CycleTransport push the waveform directly, see OnMessage)
  std::atomic<double> mMeterPeak {0.}; // running max(|L|,|R|) since the last OnIdle read - see ProcessBlock's CAS loop and OnIdle()'s exchange-and-send
  // set from ProcessMidiMsg (audio thread) when CC7 changes Gain via SetParameterValue
  // - that call alone never reaches the WebView (SendParameterValueFromDelegate,
  // which the UI push needs, must run on the main thread - WebView2's COM object
  // isn't safe to touch from the audio thread), so OnIdle() does the actual push,
  // same atomic-flag-plus-OnIdle-drain idiom as mLooperStateDirty above.
  std::atomic<bool> mGainUIDirty {false};

  // Cross-format preset browser - see kMsgTagPresetList's comment in the EMsgTags
  // enum above for why this isn't APP_API-gated like the other preset/state
  // mechanisms in this file. All file I/O happens on the main thread (from
  // OnMessage()/OnWebContentLoaded(), the same threading context the existing
  // Standalone-only LoadAutoState()/SaveAutoState() already do their own
  // SerializeState()/UnserializeState() calls from), so no extra synchronization
  // beyond what iPlug2's own IPlugParameter storage already provides.
  void GetPresetsDir(WDL_String& path); // creates the directory if it doesn't exist yet
  void SendPresetList(); // enumerates *.preset files in GetPresetsDir(), pushes kMsgTagPresetList
  void SavePresetAs(const char* rawName); // sanitizes rawName, writes <dir>/<name>.preset, refreshes the list. Overwrites in place if that file already exists (no separate "overwrite" path needed).
  void LoadPresetByName(const char* rawName); // sanitizes rawName, reads <dir>/<name>.preset, restores it
  void DeletePresetByName(const char* rawName); // sanitizes rawName, removes <dir>/<name>.preset if present, refreshes the list
  // Preset names double as filenames, and rawName arrives from free-text WebView
  // input - only alnum/space/hyphen/underscore/parens survive, everything else
  // (including any path separator or "..") is stripped, and the result is length-
  // capped. This is the actual trust boundary between "arbitrary UI text" and "a
  // real filesystem path", so it lives in C++, not just as JS-side validation.
  static void SanitizePresetName(const char* rawName, WDL_String& outSafeName);
#ifdef APP_API
  // Standalone-only "remember every param across launches" - see FirstSynth.cpp's
  // LoadAutoState()/SaveAutoState() for the full explanation. A CLAP instance's state
  // is already persisted by the host's own project save/reload (SerializeState/
  // UnserializeState), so this file-based mechanism only makes sense for Standalone.
  void GetAutoStatePath(WDL_String& path);
  void LoadAutoState();
  void SaveAutoState();
  std::atomic<bool> mAutoStateDirty {false}; // set by OnParamChange (any param), consumed by OnIdle()
  // guards mAutoStateDirty against the framework's own early OnParamReset(kReset)
  // pass (IPlugAPP_host.cpp's Init(), called before OpenWindow()/OnWebContentLoaded())
  // - without this, that startup pass's default-valued OnParamChange calls looked
  // exactly like a real change to OnIdle(), and an idle tick landing before
  // LoadAutoState() ever ran (in OnWebContentLoaded(), later) would overwrite the
  // real saved file with fresh compiled-in defaults before it was ever read. Only
  // starts tracking real dirtiness once LoadAutoState()+OnRestoreState() have run.
  std::atomic<bool> mAutoStateLoaded {false};
  // BPM used for tempo-synced LFOs in Standalone only - see kMsgTagSetStandaloneTempo's
  // comment above for why this exists (no host transport to report a real tempo here).
  // Read from the audio thread (ProcessBlock), written from the main thread (OnMessage),
  // hence atomic - same idiom as mMeterPeak above.
  std::atomic<double> mStandaloneTempo {120.};
#endif
#endif
};

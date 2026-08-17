#pragma once

#include <vector>
#include <cstddef>
#include <atomic>
#include <cmath>
#include <algorithm>

// A simple ENSO-style looper, the last stage in the effects chain (after Reverb) in
// FirstSynth::ProcessBlock - it records "what you hear" (the fully-processed signal)
// and plays it back, blended additively with the live dry signal. Deliberately simple,
// matching the rest of this project's DSP style (see FirstSynth_Effects.h).
//
// Mix here is NOT a dry/wet crossfade like Chorus/Delay/Reverb's Mix - it's an additive
// send level for the loop's own output on top of the always-present dry signal
// (l = dryL + wetL * mMix). A crossfade would be wrong for a looper specifically: the
// loop's wet signal is silent whenever nothing has been recorded yet (Empty/Stopped/
// during the first Recording pass), so a crossfade at Mix=100% would silence the entire
// live synth before anything had even been looped - a real hardware loop pedal sums live
// playing with loop playback, it doesn't fade the live signal out as loop level goes up.
//
// **Feedback is a real-time-based decay** (reworked 2026-07-22), not a once-per-loop-
// revolution multiply. The old design only touched the buffer while Overdubbing,
// multiplying whatever was stored by Feedback in one lump each time the playhead
// passed a given index - two problems the user found by ear: (1) Feedback had no
// effect at all during plain Play (nothing overdubbing = nothing decaying), only
// while actively re-recording, and (2) each index's decay was a discrete jump applied
// once per full loop revolution, so - especially on short loops or low Feedback - the
// jump right at the loop's seam was audible as a sudden volume drop rather than a
// gradual fade. Fixed by tracking, per loop-buffer index, `mWriteTime` (when that
// sample was last actually written) against a continuously-advancing `mCurrentTime`
// (incremented every Process() call, in every state, so decay keeps progressing even
// while Stopped) - decay is now `pow(Feedback, elapsedSeconds/kDecayTimeScale)`,
// computed fresh on every *read*, not baked into the stored value via periodic
// rewrites. This means: pure Play, with no new writes at all, still audibly fades
// (elapsed keeps growing every time you read the same index again as the loop
// repeats) - solves problem (1). And since elapsed time varies *continuously* sample-
// to-sample (including right across the loop seam, where adjacent samples differ by
// only 1/sampleRate in elapsed time), the computed decay is smooth with no discrete
// jump anywhere - solves problem (2). Feedback=100% still means "never decays"
// exactly (pow(1, anything)=1), matching the pre-existing default/convention.
//
// **No variable playback speed** (removed 2026-07-22, was a `Speed` param/knob) -
// changing loop playback rate is easy (just a resampled read), but *recording new
// overdub content while playback runs at a non-1x rate* is not: at Speed<1x, several
// real-time input samples land on the same loop-buffer index before playback moves on,
// so something has to either discard most of them (audible aliasing - reported as a
// ring-modulation/bitcrush-like distortion while overdubbing) or average them (removes
// the harsh aliasing but bakes in a permanent bandwidth/resolution loss into the
// recorded loop content itself - reported as the played-back loop sounding "bit-crushed"
// even after the averaging fix). Neither is acceptable with this simple architecture,
// and doing it properly would need real anti-aliasing filtering or a fundamentally
// different design (e.g. separate per-layer tracks recorded at their own native rate).
// User chose to just remove the feature rather than pursue either. If pitch/rate-shifted
// playback is wanted again later, restrict it to non-overdubbing Play only (never let
// mSpeed apply while ELooperState::kOverdubbing is active) rather than reintroducing
// this same problem.
enum class ELooperState
{
  kEmpty = 0,
  kRecording,
  kPlaying,
  kOverdubbing,
  kStopped
};

template<typename T>
class LooperEffect
{
public:
  void SetSampleRate(double sampleRate)
  {
    mSampleRate = sampleRate;
    mMaxSamples = (size_t) (sampleRate * kMaxLoopSeconds);
    mBufferL.assign(mMaxSamples, (T) 0.);
    mBufferR.assign(mMaxSamples, (T) 0.);
    mWriteTime.assign(mMaxSamples, 0.);
    ResetLoop();
  }

  void SetReverse(bool reverse) { mReverse = reverse; }
  void SetFeedback(T feedback01) { mFeedback = feedback01; }
  void SetMix(T mix) { mMix = mix; }

  ELooperState GetState() const { return mState.load(std::memory_order_relaxed); }

  // Gauge readout for the WebView UI (pushed periodically from FirstSynth::OnIdle,
  // not audio-thread-safety-critical - reads mWritePos/mPlayPos/mLoopLengthSamples
  // across threads the same way the class comment above already documents as an
  // accepted benign race for display purposes, at worst a glitched frame).
  // Both fractions are relative to the full 40s cap (mMaxSamples), not the actual
  // recorded loop length, so the gauge's right edge always represents 40s and a
  // shorter loop visibly only occupies part of it - matches what was asked for.
  double GetBarFraction() const
  {
    if (mMaxSamples == 0) return 0.;
    size_t numer = (mState.load(std::memory_order_relaxed) == ELooperState::kRecording) ? mWritePos : mLoopLengthSamples;
    return (double) numer / (double) mMaxSamples;
  }

  double GetCursorFraction() const
  {
    if (mMaxSamples == 0) return 0.;
    bool recording = mState.load(std::memory_order_relaxed) == ELooperState::kRecording;
    double pos = recording ? (double) mWritePos : (double) mPlayPos;
    return pos / (double) mMaxSamples;
  }

  // Number of buckets GetWaveformPeaks() below fills in - fixed/small enough to push
  // over the same SendArbitraryMsgFromDelegate mechanism as the rest of this class's
  // WebView pushes without it being a meaningful amount of data.
  static constexpr int kWaveformBuckets = 300;

  // Rough per-bucket peak amplitude (0..1) of the recorded loop, for the WebView's
  // waveform-under-the-gauge display. Buckets span the *full* mMaxSamples (40s) range,
  // the same denominator GetBarFraction()/GetCursorFraction() use - not just
  // mLoopLengthSamples - so bucket i lines up with the same x position as fraction
  // i/kWaveformBuckets of the gauge bar's own width, with no separate scale to keep in
  // sync. Buckets past the recorded range are left at 0 (silent/empty), matching the
  // gauge's own unlit remainder past its color fill. Called on every transport/stop/
  // clear press, on cut-apply, and every OnIdle tick *while actively Recording* (see
  // FirstSynth.cpp).
  //
  // Incremental, not a full rescan every call (2026-07-26 fix) - it used to redo the
  // *entire* 0..validLen scan from scratch on every call, which is fine when called
  // once right after a recording finishes, but was too expensive to also call every
  // OnIdle tick *during* Recording (added so the waveform visibly keeps pace with the
  // gauge while recording): the scan cost grows with elapsed recording time, so each
  // tick took a little longer than the last, and the displayed waveform fell
  // increasingly further behind the gauge/cursor the longer a take went on ("だんだん
  // ずれていく" bug report). mWaveformScanPos/mWaveformCache remember how far it's
  // already scanned, so each call only processes the *new* samples since the last
  // call - a small, roughly constant amount of work per tick (about one tick's worth
  // of audio) regardless of how long the take has been running. Not const anymore for
  // this reason (mutates the cache) - only ever called from the main thread (see
  // FirstSynth.cpp), same as before.
  void GetWaveformPeaks(float* outPeaks)
  {
    if (mMaxSamples == 0)
    {
      for (int i = 0; i < kWaveformBuckets; i++) outPeaks[i] = 0.f;
      return;
    }

    if (mWaveformCache.empty()) mWaveformCache.assign(kWaveformBuckets, 0.f);

    ELooperState state = mState.load(std::memory_order_relaxed);

    // same "which length is actually valid right now" split GetBarFraction() uses -
    // while Recording, mLoopLengthSamples is still whatever it was from the
    // *previous* finalized take (stale), and the currently-growing take's real
    // extent is mWritePos instead.
    bool recording = state == ELooperState::kRecording;
    size_t validLen = recording ? mWritePos : mLoopLengthSamples;

    // Overdubbing continuously *rewrites existing* buffer indices as the playhead
    // cycles through the already-finalized loop (Process()'s kOverdubbing case) -
    // unlike Recording, which only ever writes forward once. The incremental "only
    // scan what's new since last time" trick below tracks *new* indices only, so it
    // would never notice overdubbed changes to indices it already scanned earlier.
    // Force a full, fresh rescan every call while Overdubbing instead - still cheap
    // enough to do every tick (unlike this class's old *unconditional* full rescan)
    // because mLoopLengthSamples is fixed once Overdubbing starts, so the per-tick
    // cost doesn't keep growing the way Recording's did (2026-07-26 bug report:
    // "オーバーダブ時に波形が更新されていかない").
    if (state == ELooperState::kOverdubbing)
    {
      mWaveformScanPos = 0;
      std::fill(mWaveformCache.begin(), mWaveformCache.end(), 0.f);
    }

    for (size_t s = mWaveformScanPos; s < validLen; s++)
    {
      int bucket = (int) ((double) s / (double) mMaxSamples * (double) kWaveformBuckets);
      if (bucket >= kWaveformBuckets) bucket = kWaveformBuckets - 1;
      float v = std::max((float) std::fabs((double) mBufferL[s]), (float) std::fabs((double) mBufferR[s]));
      if (v > mWaveformCache[bucket]) mWaveformCache[bucket] = v;
    }
    mWaveformScanPos = validLen;

    for (int i = 0; i < kWaveformBuckets; i++) outPeaks[i] = mWaveformCache[i];
  }

  // Discards the incremental scan cache GetWaveformPeaks() above builds up, so the
  // next call rescans from scratch instead of (wrongly) picking up from wherever the
  // *previous* take/loop-shape left off. Must be called whenever the buffer's actual
  // content/length changes discontinuously: a new recording starting (CycleTransport()
  // below), Clear()/ResetLoop(), and after a cut applies (ApplyPendingCut() runs on
  // the audio thread and can't safely touch mWaveformCache itself - see its own
  // comment - so FirstSynth.cpp calls this from the main thread instead, right before
  // its own post-cut GetWaveformPeaks() call).
  void ResetWaveformScan()
  {
    mWaveformScanPos = 0;
    mWaveformCache.assign(kWaveformBuckets, 0.f);
  }

  // Queues a cut - applied by Process() the moment it's *safe* (2026-07-27,
  // replaced "always wait for the next loop-wrap"), which for most requests is
  // effectively instant rather than a wait: isLeft=true deletes everything
  // *before* positionFraction01 (the loop starts there instead), and is safe the
  // instant the playhead is already at/past that point (mPlayPos >= cut pos) -
  // the retained tail just gets remapped under a still-advancing playhead, no
  // jump. If the playhead hasn't reached the cut point yet, it has to wait until
  // it does (not a full wrap - just passing through). isLeft=false cuts everything
  // *after* positionFraction01 (the loop ends there instead), safe the instant the
  // playhead is still *before* that point (mPlayPos < cut pos) - the retained head
  // doesn't move, so an unaffected playhead just continues normally. If the
  // playhead is already inside the region being removed, it has to wait for a full
  // wrap this time (the "safe" condition can't become true again until mPlayPos
  // resets near 0). See Process()'s kPlaying/kOverdubbing case for where this
  // condition is actually checked (every sample, not just at wraps) and
  // ApplyPendingCut() for the position remapping. positionFraction01 is on the
  // same 40s-cap-relative scale as GetBarFraction()/GetCursorFraction(), i.e.
  // straight from a click/touch position on the gauge. No-op (returns false) if
  // nothing's recorded, or the looper isn't actively Playing/Overdubbing - there's
  // no playhead to check safety against otherwise (Stop()/Clear() also cancel
  // whatever's already queued, for the same reason - see their own comments).
  bool QueueCut(bool isLeft, double positionFraction01)
  {
    ELooperState state = mState.load(std::memory_order_relaxed);
    if (mLoopLengthSamples == 0) return false;
    if (state != ELooperState::kPlaying && state != ELooperState::kOverdubbing) return false;

    size_t pos = (size_t) (positionFraction01 * (double) mMaxSamples);
    pos = std::min(pos, mLoopLengthSamples - 1);

    mPendingCutIsLeft = isLeft;
    mPendingCutPos = pos;
    mPendingCut.store(true, std::memory_order_relaxed);
    return true;
  }

  bool HasPendingCut() const { return mPendingCut.load(std::memory_order_relaxed); }
  bool GetPendingCutIsLeft() const { return mPendingCutIsLeft; }

  // Requests undoing the most recent cut that actually landed (2026-07-27,
  // replaced the old "cancel a still-pending cut" button - see progress.md for why:
  // most cuts now land near-instantly, per QueueCut()'s comment, so a "cancel
  // before it lands" window barely exists anymore; "undo what just happened" is
  // more useful). Just sets a flag - like QueueCut(), the actual buffer
  // restoration only happens on the audio thread (see Process()), never here
  // directly, since it mutates the same buffers Process() concurrently reads. Safe
  // to call even if nothing's undoable (no-op, checked again in Process()).
  void RequestUndo() { mUndoRequested.store(true, std::memory_order_relaxed); }

  bool HasUndo() const { return mHasUndo.load(std::memory_order_relaxed); }

  // One-shot flag: true the first time this is called after a requested undo
  // actually lands - consumed (cleared) by the call itself. Same
  // poll-from-OnIdle pattern as ConsumeCutJustApplied() below.
  bool ConsumeUndoJustApplied() { return mUndoJustApplied.exchange(false, std::memory_order_relaxed); }

  // same 40s-cap-relative scale as GetBarFraction() - directly usable as the blink
  // overlay's left/width percentages on the WebView side
  double GetPendingCutFraction() const
  {
    return mMaxSamples ? (double) mPendingCutPos / (double) mMaxSamples : 0.;
  }

  // One-shot flag: true the first time this is called after a queued cut actually
  // lands (see ApplyPendingCut() for when that is) - consumed (cleared) by the
  // call itself. Only ever meant to be polled from OnIdle (main thread), same as
  // the rest of this class's other dirty-flag-plus-drain fields - see FirstSynth.cpp.
  bool ConsumeCutJustApplied() { return mCutJustApplied.exchange(false, std::memory_order_relaxed); }

  // Advances the looper by one sample, recording/playing/overdubbing as appropriate,
  // and adds the loop's contribution into l/r (see class comment - additive, not a
  // crossfade). Returns true if the state changed on its own this sample (currently
  // only the auto-stop-at-40s case) so the caller knows to push a state update to the
  // WebView UI - a button press already updates the UI itself, this is only for DSP-
  // initiated transitions the UI wouldn't otherwise find out about.
  //
  // mState is atomic because CycleTransport()/Stop() are called from the WebView
  // message thread while Process() runs on the audio thread - the other position/
  // length bookkeeping below is plain (not atomic), since it only changes at the
  // instant of an infrequent user button press; a benign race there could at worst
  // glitch a few samples right at that moment, not corrupt anything or crash, and a
  // full lock-free redesign isn't warranted for that - matches the "deliberately
  // simple" DSP style used throughout this project.
  bool Process(T& l, T& r)
  {
    bool stateChanged = false;
    T wetL = 0., wetR = 0.;
    ELooperState state = mState.load(std::memory_order_relaxed);

    // advances regardless of state (including Empty/Stopped) - this is the "wall
    // clock" the Feedback decay is measured against, see class comment
    mCurrentTime += 1. / mSampleRate;

    // Checked unconditionally, regardless of state (unlike cuts, which need an
    // active playhead to judge "safe" timing against) - an undo just restores a
    // known-good prior buffer snapshot, so it's meaningful even from
    // Stopped/Empty (e.g. right after Clear()'s own snapshot-wipe... though at
    // that point HasUndo() is already false, see Clear()'s comment). Runs here,
    // on the audio thread, for the same reason ApplyPendingCut() does - it
    // mutates mBufferL/R/mWriteTime, which Process() itself concurrently reads.
    if (mUndoRequested.exchange(false, std::memory_order_relaxed) && mHasUndo.load(std::memory_order_relaxed))
    {
      PerformUndo();
    }

    switch (state)
    {
      case ELooperState::kEmpty:
      case ELooperState::kStopped:
        break;

      case ELooperState::kRecording:
        mBufferL[mWritePos] = l;
        mBufferR[mWritePos] = r;
        mWritePos++;
        if (mWritePos >= mMaxSamples)
        {
          mLoopLengthSamples = mMaxSamples;
          StampRecordedRange();
          mWritePos = 0;
          mPlayPos = 0.;
          mState.store(ELooperState::kPlaying, std::memory_order_relaxed);
          stateChanged = true;
        }
        break;

      case ELooperState::kPlaying:
      case ELooperState::kOverdubbing:
        if (mLoopLengthSamples > 0)
        {
          size_t pos = (size_t) mPlayPos;

          // decay computed fresh from elapsed real time since this index was last
          // written - not baked into the stored value via periodic rewrites, so
          // pure Play (no writes at all) still fades continuously as elapsed grows
          // on each repeat, and there's no discrete per-revolution jump anywhere
          // (see class comment for the full reasoning)
          double elapsed = mCurrentTime - mWriteTime[pos];
          T decay = DecayFactor(elapsed);
          wetL = mBufferL[pos] * decay;
          wetR = mBufferR[pos] * decay;

          if (state == ELooperState::kOverdubbing)
          {
            mBufferL[pos] = wetL + l;
            mBufferR[pos] = wetR + r;
            mWriteTime[pos] = mCurrentTime;
          }

          T dir = mReverse ? (T) -1. : (T) 1.;
          mPlayPos += dir;
          T lenT = (T) mLoopLengthSamples;
          while (mPlayPos < 0) mPlayPos += lenT;
          while (mPlayPos >= lenT) mPlayPos -= lenT;

          // Apply a queued cut the instant it's *safe* (2026-07-27, replaced
          // "only ever at a loop-wrap") - checked fresh every sample rather than
          // only right after a wrap, so a cut that's already safe when queued
          // lands on the very next sample instead of waiting up to a full loop
          // revolution. "Safe" means the playhead's current position won't be
          // discontinuous once the buffer is reshaped - see QueueCut()'s comment
          // for the reasoning behind each condition, and ApplyPendingCut() for how
          // mPlayPos itself gets remapped (not reset) when this fires.
          if (mPendingCut.load(std::memory_order_relaxed))
          {
            bool safe = mPendingCutIsLeft ? (mPlayPos >= (T) mPendingCutPos)
                                           : (mPlayPos < (T) mPendingCutPos);
            if (safe) ApplyPendingCut();
          }
        }
        break;
    }

    l = l + wetL * mMix;
    r = r + wetR * mMix;

    return stateChanged;
  }

  // Called when the single cycling transport button is pressed. C++ owns the state
  // machine - the WebView button doesn't decide the next state itself, it just asks to
  // cycle and gets told what the new state is (see progress.md "Looper page" section
  // for why: keeps DSP-initiated transitions like the 40s auto-stop consistent with
  // what the UI shows, with a single source of truth).
  //
  // Cycle order (2026-07-27, changed from Record->Play->Overdub->Play->...):
  // Record -> Overdub -> Play -> Overdub -> Play -> ... - a freshly-finished
  // recording now goes straight into Overdub rather than plain Play, matching the
  // user's own workflow (layering immediately after the first pass, rather than
  // needing an extra press to get into Overdub the first time). Play/Overdub still
  // simply toggle each other on every subsequent press either way.
  ELooperState CycleTransport()
  {
    ELooperState newState = mState.load(std::memory_order_relaxed);
    switch (newState)
    {
      case ELooperState::kEmpty:
        mWritePos = 0;
        ResetWaveformScan(); // a fresh take is starting - don't keep scanning from wherever any previous take left off
        newState = ELooperState::kRecording;
        break;

      case ELooperState::kRecording:
        mLoopLengthSamples = mWritePos;
        StampRecordedRange();
        mWritePos = 0;
        mPlayPos = 0.;
        newState = (mLoopLengthSamples > 0) ? ELooperState::kOverdubbing : ELooperState::kEmpty;
        break;

      case ELooperState::kPlaying:
        newState = ELooperState::kOverdubbing;
        break;

      case ELooperState::kOverdubbing:
        newState = ELooperState::kPlaying;
        break;

      case ELooperState::kStopped:
        newState = (mLoopLengthSamples > 0) ? ELooperState::kPlaying : ELooperState::kEmpty;
        break;
    }
    mState.store(newState, std::memory_order_relaxed);
    return newState;
  }

  // Stop always returns to a non-playing/non-recording state, keeping whatever's
  // already been recorded so Play can resume it later. From Empty, there's nothing to
  // stop - stays Empty.
  ELooperState Stop()
  {
    ELooperState newState = mState.load(std::memory_order_relaxed);
    if (newState != ELooperState::kEmpty)
    {
      // Stopping directly out of Recording (rather than cycling the transport
      // through to Playing first) used to silently discard the take - this branch
      // never used to run, so mLoopLengthSamples stayed at its old value (0 on a
      // first recording) and the just-recorded audio in mBufferL/R was never made
      // reachable by Process()'s Playing/Overdubbing case. Finalize it the same way
      // CycleTransport()'s own Recording branch does, so a first-time user hitting
      // Stop right after Record still keeps what they played (2026-07-26 bug report).
      if (newState == ELooperState::kRecording)
      {
        mLoopLengthSamples = mWritePos;
        StampRecordedRange();
        mWritePos = 0;
        mPlayPos = 0.;
      }

      newState = ELooperState::kStopped;
      mState.store(newState, std::memory_order_relaxed);
    }

    // A pending cut's "safe" condition (see QueueCut()'s comment) needs an
    // advancing playhead to ever become true, which stops happening the instant
    // we're Stopped - cancel it rather than leave it queued forever with no way
    // to actually apply, and no visual sense either (the UI's blink overlay would
    // otherwise just sit there blinking indefinitely).
    mPendingCut.store(false, std::memory_order_relaxed);

    return newState;
  }

  // Discards whatever's been recorded and goes back to Empty, regardless of current
  // state (recording included - clearing mid-record just abandons that pass too).
  // Doesn't need to zero the buffer contents themselves, only the position/length
  // bookkeeping - mLoopLengthSamples=0 already makes Process() ignore whatever old
  // samples are still sitting in mBufferL/R, and the next Recording pass overwrites
  // from position 0 anyway.
  ELooperState Clear()
  {
    ResetLoop();
    mPendingCut.store(false, std::memory_order_relaxed); // nothing left to cut
    mHasUndo.store(false, std::memory_order_relaxed); // ...or to undo, once the loop itself is gone
    return mState.load(std::memory_order_relaxed);
  }

private:
  // History: 60s originally -> 30s (2026-07-27, after the user tried the looper
  // for a while and felt 60s was more than needed - see the memory-footprint
  // reasoning below, still accurate) -> 40s (same day, right after - in practice
  // the user mostly records ~15s takes, well under either cap, but said the 30s
  // ceiling itself created a felt time-pressure while playing, regardless of
  // actually needing that much room; 40s was chosen as more headroom without
  // going back to the original 60s). Real trade-off, not just a cosmetic number:
  // mBufferL/mBufferR/mWriteTime below are all sized off this (sampleRate *
  // kMaxLoopSeconds), unconditionally allocated in SetSampleRate() whether or not
  // the looper ever actually gets used - at this project's `sample` type (double,
  // 8 bytes - confirmed via FirstSynth-win.props, unlike the iOS config) and the
  // user's own 96kHz setting, each extra second here costs ~2.2MB per plugin
  // instance (3 buffers x 8 bytes x 96000Hz). Purely a memory footprint trade-off,
  // *not* a CPU/realtime-processing one - Process() itself is O(1) per sample
  // regardless of this constant, and the length-scaling operations
  // (StampRecordedRange/GetWaveformPeaks/cut+undo snapshots) already scale with the
  // *actual* recorded length, not this cap, so they're no cheaper at a lower cap
  // unless a recording would otherwise have run longer. Easy to retune again if 40s
  // turns out too short/long in practice.
  static constexpr double kMaxLoopSeconds = 40.;

  void ResetLoop()
  {
    mState.store(ELooperState::kEmpty, std::memory_order_relaxed);
    mWritePos = 0;
    mPlayPos = 0.;
    mLoopLengthSamples = 0;
    ResetWaveformScan();
  }

  // Called once, right when a Recording pass finishes (either by hitting the 40s cap
  // in Process(), or the user manually stopping via CycleTransport()). Stamps every
  // index in the just-recorded range to the *same* "now" timestamp, uniformly - without
  // this, each sample's mWriteTime would reflect the real moment it was captured
  // *during* the recording pass itself, which takes real time to complete, so material
  // recorded near the start of the take would already read as "older" (and partially
  // decayed) than material recorded near the end, the very first time the loop plays
  // back - before any actual repeat-based aging was ever intended to happen. Called
  // from both the audio thread (Process()'s auto-stop) and the message thread
  // (CycleTransport()'s manual stop) - same class of accepted benign cross-thread
  // write as this class's other plain (non-atomic) position bookkeeping.
  void StampRecordedRange()
  {
    for (size_t i = 0; i < mLoopLengthSamples; i++)
      mWriteTime[i] = mCurrentTime;
  }

  // Called from Process()'s kPlaying/kOverdubbing case the instant a pending cut's
  // "safe" condition is met (see QueueCut()'s comment - not necessarily a
  // loop-wrap anymore, could be the very next sample after it was queued).
  // mWriteTime entries move/stay together with their corresponding samples so
  // Feedback decay timing for the retained audio is unaffected by the cut itself
  // (deliberately NOT re-stamped via StampRecordedRange() - a cut isn't a new
  // recording, so already-decaying content shouldn't suddenly read as fresh again).
  void ApplyPendingCut()
  {
    SaveUndoSnapshot(); // preserve pre-cut state before mutating anything below

    T oldPlayPos = mPlayPos;
    if (mPendingCutIsLeft)
    {
      // delete everything before mPendingCutPos: shift the retained tail down so it
      // starts at index 0
      size_t newLen = mLoopLengthSamples - mPendingCutPos;
      for (size_t i = 0; i < newLen; i++)
      {
        mBufferL[i] = mBufferL[mPendingCutPos + i];
        mBufferR[i] = mBufferR[mPendingCutPos + i];
        mWriteTime[i] = mWriteTime[mPendingCutPos + i];
      }
      mLoopLengthSamples = newLen;
      // the "safe" check in Process() guarantees oldPlayPos >= mPendingCutPos here,
      // so this remap always lands >= 0 - same underlying audio sample, just at
      // its new (shifted) index, so playback continues with zero discontinuity
      // instead of jumping back to the start (the old unconditional mPlayPos=0
      // behavior - only sounded seamless when it happened to coincide with an
      // actual loop-wrap, which isn't guaranteed to be when this runs anymore).
      mPlayPos = oldPlayPos - (T) mPendingCutPos;
    }
    else
    {
      // cut everything after mPendingCutPos: samples before it don't move at all,
      // and neither does the playhead - the "safe" check guarantees oldPlayPos is
      // already < mPendingCutPos (the new length), so it's already valid as-is.
      mLoopLengthSamples = mPendingCutPos;
      mPlayPos = oldPlayPos;
    }

    if (mLoopLengthSamples == 0) mLoopLengthSamples = 1; // never a zero-length loop - would break lenT math in Process()
    // defensive clamp only - the remapping above should never actually produce an
    // out-of-range value given the "safe" precondition, but a stray edge case
    // landing out of bounds would otherwise corrupt Process()'s buffer indexing
    if (mPlayPos < 0 || mPlayPos >= (T) mLoopLengthSamples) mPlayPos = 0.;

    mPendingCut.store(false, std::memory_order_relaxed);
    mCutJustApplied.store(true, std::memory_order_relaxed);
  }

  // Snapshots the currently-valid loop content/length so UndoLastCut() (well,
  // RequestUndo()+PerformUndo() - see those) can restore it later. Only ever
  // called right before ApplyPendingCut() mutates anything, so this always
  // captures the pre-cut state. Bounded cost (copies mLoopLengthSamples elements,
  // not the full 40s-cap mMaxSamples buffer) and infrequent (once per user-
  // triggered cut) - safe to do on the audio thread same as the shift/truncate
  // ApplyPendingCut() itself already does.
  void SaveUndoSnapshot()
  {
    mUndoLoopLengthSamples = mLoopLengthSamples;
    mUndoPlayPos = mPlayPos;
    mUndoBufferL.assign(mBufferL.begin(), mBufferL.begin() + mLoopLengthSamples);
    mUndoBufferR.assign(mBufferR.begin(), mBufferR.begin() + mLoopLengthSamples);
    mUndoWriteTime.assign(mWriteTime.begin(), mWriteTime.begin() + mLoopLengthSamples);
    mHasUndo.store(true, std::memory_order_relaxed);
  }

  // Restores the snapshot SaveUndoSnapshot() took right before the last cut.
  // Unlike ApplyPendingCut(), doesn't try to remap mPlayPos continuously - the
  // pre-cut buffer shape doesn't correspond to any single "same underlying
  // sample" position in the post-cut one in general, so this just restarts
  // playback from 0, a simple and predictable landing point (same as this
  // project's other deliberately-simple DSP choices - see class comment).
  // One-level undo only: calling this consumes the snapshot (mHasUndo->false),
  // it doesn't itself push another undo state onto some deeper stack.
  void PerformUndo()
  {
    mLoopLengthSamples = mUndoLoopLengthSamples;
    std::copy(mUndoBufferL.begin(), mUndoBufferL.begin() + mUndoLoopLengthSamples, mBufferL.begin());
    std::copy(mUndoBufferR.begin(), mUndoBufferR.begin() + mUndoLoopLengthSamples, mBufferR.begin());
    std::copy(mUndoWriteTime.begin(), mUndoWriteTime.begin() + mUndoLoopLengthSamples, mWriteTime.begin());
    mPlayPos = 0.;
    mPendingCut.store(false, std::memory_order_relaxed); // don't let a still-queued cut immediately re-cut the just-restored audio
    mHasUndo.store(false, std::memory_order_relaxed);
    mUndoJustApplied.store(true, std::memory_order_relaxed);
  }

  // pow(Feedback, elapsedSeconds/kDecayTimeScale) - Feedback=1 gives exactly 1
  // (pow(1,x)=1, never decays) regardless of elapsed; Feedback=0 gives exactly 0 for
  // any elapsed>0 (erases almost immediately). kDecayTimeScale=8s is just a chosen
  // reference pace (at Feedback=50%, content fades to half amplitude every ~8s of
  // real time, to a quarter after ~16s, etc.) - the single easy-to-retune constant if
  // this ever feels too fast/slow in practice.
  T DecayFactor(double elapsedSeconds) const
  {
    if (elapsedSeconds <= 0.) return (T) 1.;
    constexpr double kDecayTimeScale = 8.;
    return (T) std::pow((double) mFeedback, elapsedSeconds / kDecayTimeScale);
  }

  double mSampleRate = 44100.;
  size_t mMaxSamples = 0;
  std::vector<T> mBufferL, mBufferR;
  std::vector<double> mWriteTime; // per-index "last written" timestamp, see DecayFactor()
  double mCurrentTime = 0.; // continuously-advancing wall clock, see Process()

  std::atomic<ELooperState> mState {ELooperState::kEmpty};
  size_t mWritePos = 0;
  T mPlayPos = 0.;
  size_t mLoopLengthSamples = 0;

  // Pending-cut state - see QueueCut()/ApplyPendingCut()/Process()'s safety check.
  // mPendingCutIsLeft/mPendingCutPos are plain (not atomic): only ever written from
  // QueueCut() (message thread, infrequent user action) and read from
  // ApplyPendingCut() (audio thread, only when mPendingCut is true) - same accepted
  // benign-race class as this class's other plain position bookkeeping (see the
  // Process() doc comment above). mPendingCut itself IS atomic since it's the actual
  // cross-thread signal both sides check.
  std::atomic<bool> mPendingCut {false};
  bool mPendingCutIsLeft = false;
  size_t mPendingCutPos = 0;
  std::atomic<bool> mCutJustApplied {false};

  // One-level undo state - see RequestUndo()/SaveUndoSnapshot()/PerformUndo().
  // mUndoRequested is the message-thread->audio-thread signal (same pattern as
  // mPendingCut above); mUndoBufferL/R/mUndoWriteTime/mUndoLoopLengthSamples/
  // mUndoPlayPos are the actual snapshot, only ever written from
  // SaveUndoSnapshot() and read from PerformUndo() - both audio-thread-only (see
  // ApplyPendingCut()'s and Process()'s own comments), so no cross-thread race
  // concern for these specifically, unlike the atomics.
  std::atomic<bool> mUndoRequested {false};
  std::atomic<bool> mHasUndo {false};
  std::atomic<bool> mUndoJustApplied {false};
  std::vector<T> mUndoBufferL, mUndoBufferR;
  std::vector<double> mUndoWriteTime;
  size_t mUndoLoopLengthSamples = 0;
  T mUndoPlayPos = 0.;

  // GetWaveformPeaks()'s incremental scan state - main-thread-only (see that
  // method's own comment), no atomics needed unlike the fields above.
  size_t mWaveformScanPos = 0;
  std::vector<float> mWaveformCache;

  bool mReverse = false;
  T mFeedback = 0.5;
  T mMix = 1.;
};

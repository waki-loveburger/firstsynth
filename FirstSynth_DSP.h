#pragma once

#include "MidiSynth.h"
#include "Oscillator.h"
#include "ADSREnvelope.h"
#include "Smoothers.h"
#include "LFO.h"
#include <cmath>

using namespace iplug;

enum EModulations
{
  kModGainSmoother = 0,
  kModSustainSmoother,
  kModFilterSustainSmoother,
  kModPitchLFO,
  kModFilterLFO,
  kModAmpLFO,
  kModModLFO1, // Modulation Matrix - free LFO 1, see EMatrixSource/EMatrixDest below
  kModModLFO2, // Modulation Matrix - free LFO 2
  // same anti-zipper smoothing as kModSustainSmoother/kModFilterSustainSmoother -
  // without this, twisting a Mod Env's Sustain knob while a held note is actively
  // using it as a matrix source would click/jump at whatever destination it's routed to
  kModModEnv1SustainSmoother,
  kModModEnv2SustainSmoother,
  kNumModulations,
};

// Modulation Matrix (2026-07-28) - fixed source/destination lists for the 4
// matrix slots (kParamMatrix1..4 Source/Dest/Amount, see FirstSynth.h). Ordering
// here must exactly match both the InitEnum() name lists in FirstSynth.cpp's
// constructor and the <select> <option> order in index.html - all three are
// independently hand-maintained, nothing enforces the match automatically.
enum EMatrixSource
{
  kMatSrcNone = 0,
  kMatSrcModLFO1,
  kMatSrcModLFO2,
  kMatSrcModEnv1,
  kMatSrcModEnv2,
  kMatSrcVelocity,
  kMatSrcKeyFollow,
  kMatSrcModWheel,
  kNumMatrixSources,
};

enum EMatrixDest
{
  kMatDstNone = 0,
  kMatDstFilterCutoff,
  kMatDstFilterResonance,
  kMatDstOsc1Pitch,
  kMatDstOsc2Pitch,
  kMatDstAmpLevel,
  kMatDstPan,
  kMatDstWaveShape1,
  kMatDstWaveShape2,
  kMatDstOsc1Level,
  kMatDstOsc2Level,
  kMatDstNoiseLevel,
  // Appended (not inserted earlier in this list) 2026-07-28 per user request - a
  // much narrower-range alternative to kMatDstOsc1Pitch/kMatDstOsc2Pitch (those
  // are +-2 octaves at 100% amount, which the user found "わりと強い" even at
  // modest Amount settings for subtle vibrato-style use). These two are +-1
  // semitone at 100% amount instead - appended at the end, not inserted next to
  // Osc1Pitch/Osc2Pitch above, so any already-saved Matrix preset's Dest enum
  // values keep meaning the same thing (this project's usual "never renumber"
  // param convention, applied here since a Dest param's *value* is this enum).
  kMatDstOsc1PitchFine,
  kMatDstOsc2PitchFine,
  // Appended 2026-08-01 - scales an envelope's Attack/Decay/Release together
  // (a single "speed" factor, not each stage independently - Sustain is a
  // level, not a time, so it's untouched). See Voice::Trigger() for the actual
  // application (ADSREnvelope::Start()/Retrigger()'s existing timeScalar
  // argument, already designed for exactly this - "for key-follow scaling").
  kMatDstAmpEnvTime,
  kMatDstFilterEnvTime,
  kMatDstModEnv1Time,
  kMatDstModEnv2Time,
  kNumMatrixDests,
};

// Continuous morph across Sine -> Triangle -> Saw -> Square -> narrow Pulse.
// mWaveShape is a single continuous 0-4 control; each unit step crossfades
// between one pair of adjacent reference waveforms.
namespace FirstSynthOsc
{
  template<typename T>
  inline T Sine(T phase) { return std::sin(2. * 3.14159265358979323846 * phase); }

  // Phase-aligned to Sine: zero at phase=0 rising to +1 at 0.25 (peak), back
  // through zero at 0.5, to -1 at 0.75 (trough) - a plain Triangle(phase) (peak
  // at 0.5, not 0.25) is a quarter-period out of phase with Sine, which made the
  // Sine<->Triangle crossfade in Morph() below partially cancel (peaks landing on
  // the other wave's zero-crossings) instead of adding, weakening the amplitude
  // partway through the transition. Shifting by +0.25 here fixes that.
  template<typename T>
  inline T Triangle(T phase)
  {
    T t = phase + (T) 0.25;
    t -= std::floor(t);
    return t < 0.5 ? (4. * t - 1.) : (3. - 4. * t);
  }

  // Phase-aligned to Sine the same way as Triangle above: zero (rising) at
  // phase=0, so the Triangle<->Saw crossfade in Morph() doesn't suffer the same
  // cancellation - a plain Saw(phase) crosses zero at 0.5, a quarter... actually
  // half-period off from Sine/the aligned Triangle above.
  template<typename T>
  inline T Saw(T phase)
  {
    T t = phase + (T) 0.5;
    t -= std::floor(t);
    return 2. * t - 1.;
  }

  // duty < 0.5 means "high" (+1) for less of the cycle (a narrow pulse), "low"
  // (-1) otherwise. This polarity (high first, matching the now phase-aligned
  // Saw's hard-clip in Morph()'s segment 2, which is positive on phase (0,0.5)
  // and negative on (0.5,1) once Saw is phase-aligned - see Saw() above) is what
  // makes segment 2's hard-clip endpoint and segment 3's starting duty=0.5 line
  // up continuously without a polarity-flip click at the boundary.
  template<typename T>
  inline T Pulse(T phase, T duty)
  {
    T t = phase - std::floor(phase);
    return t < duty ? (T) 1. : (T) -1.;
  }

  // Triangle and Saw are really the same 2-segment "rise then fall" shape, just
  // with different rise/fall duration split: a symmetric 50/50 split is a
  // triangle, and a 100/0 split (rise takes the whole cycle, fall is instant) is
  // a sawtooth. AsymTriangle(phase, r) generalizes this with r = the fraction of
  // the cycle spent rising (0.5 = triangle, 1.0 = saw), continuously shearing one
  // into the other - this is what makes segment 1 of Morph() below a genuine
  // progressive "tilt" instead of an amplitude crossfade of two differently-
  // shaped, differently-phased waves (which used to leave a peak dip mid-
  // transition, the same kind of bug fixed for Sine<->Triangle above, just
  // smaller here since Triangle/Saw already shared a fundamental phase).
  // phi0 shifts the rise/fall boundary so the *rising* zero-crossing stays
  // pinned at phase=0 for every r (not just r=0.5 and r=1.0) - matching Sine/
  // Triangle/Saw's shared convention throughout the whole sweep, not just at
  // its endpoints, which is what keeps the peak at exactly +-1 for every r
  // (verify: at r=0.5 this reduces exactly to Triangle() above; at r=1.0 it
  // reduces exactly to Saw() above).
  template<typename T>
  inline T AsymTriangle(T phase, T r)
  {
    T phi0 = (T) 1. - r * (T) 0.5;
    T u = phase - phi0;
    u -= std::floor(u);
    if (u < r)
      return (T) -1. + (T) 2. * u / r;
    else
      return (T) 1. - (T) 2. * (u - r) / ((T) 1. - r);
  }

  // Segment boundaries for Morph() below - deliberately NOT evenly spaced
  // (user asked for a dwell/plateau at pure Saw around 2.0, easy to land on
  // exactly while turning the knob, plus more of the knob's range devoted to
  // the pulse-width sweep at the end): [0,1) Sine->Triangle, [1,kSawStart)
  // Triangle->Saw (tilt), [kSawStart,kSawEnd] held at pure Saw, (kSawEnd,
  // kSquareEnd) Saw->Square (hard-clip), [kSquareEnd,4] Square->narrow Pulse
  // (duty sweep).
  constexpr double kMorphSawStart = 1.95;
  constexpr double kMorphSawEnd = 2.05;
  constexpr double kMorphSquareEnd = 2.59;
  // 1/sqrt(3): the RMS of Saw/Triangle (a plain linear ramp). Used below to
  // compensate for the loudness increase hard-clipping/pulse-narrowing would
  // otherwise introduce - see the Saw->Square segment's comment.
  constexpr double kMorphTargetRMS = 0.5773502691896258;

  // ---------------------------------------------------------------------------
  // Band-limiting (2026-09-01). Every Morph() waveform except pure Sine is
  // piecewise-linear, so its naive form radiates energy above Nyquist that
  // folds back as inharmonic aliasing - the "hard / brittle / metallic top
  // end" the user reported, worst toward the Saw/Square/Pulse end of the knob
  // and worst at high notes. Rather than oversample the whole voice, each
  // value step (Saw wrap, Pulse edges, clip step) gets a PolyBLEP correction
  // and each slope corner (Triangle/AsymTriangle vertices, hard-clip
  // shoulders) gets a PolyBLAMP correction, both 2-sample. `dt` = phase
  // increment per sample (osc freq / sample rate); dt <= 0 disables all
  // corrections (the naive shape, e.g. for the JS waveform-display mirror,
  // which deliberately draws the ideal shape without BLEP ripple).
  template<typename T>
  inline T Wrap01(T x) { x -= std::floor(x); return x < (T) 0. ? x + (T) 1. : x; }

  // Residual for a unit-amplitude step. t = wrapped phase distance past the
  // discontinuity (0..1); add (h * PolyBlep) for an upward jump of size h,
  // subtract for a downward jump.
  template<typename T>
  inline T PolyBlep(T t, T dt)
  {
    if (t < dt)             { t /= dt;               return t + t - t * t - (T) 1.; }
    if (t > (T) 1. - dt)    { t = (t - (T) 1.) / dt; return t * t + t + t + (T) 1.; }
    return (T) 0.;
  }

  // Residual for a slope discontinuity (integral of PolyBlep). Add
  // (deltaSlope * dt * PolyBlamp), deltaSlope = change in the waveform's slope
  // in value-per-cycle units; a small non-negative hump, so a negative
  // deltaSlope (concave-down vertex, e.g. a triangle peak) rounds it off.
  template<typename T>
  inline T PolyBlamp(T t, T dt)
  {
    if (t < dt)          { t = t / dt - (T) 1.;        return (T) (-1. / 3.) * t * t * t; }
    if (t > (T) 1. - dt) { t = (t - (T) 1.) / dt + (T) 1.; return (T) (1. / 3.) * t * t * t; }
    return (T) 0.;
  }
  // ---------------------------------------------------------------------------

  // dt = phase increment per sample (osc freq / sample rate), for the
  // band-limiting corrections above. Pass 0 for the naive (pre-2026-09-01) shape.
  template<typename T>
  inline T Morph(T phase, T waveShape, T dt = (T) 0.)
  {
    waveShape = std::max((T) 0., std::min((T) 4., waveShape));
    dt = std::min(std::max(dt, (T) 0.), (T) 0.49); // keep the 1-dt BLEP window sane near Nyquist

    if (waveShape < (T) 1.)
    {
      T frac = waveShape;
      T a = Sine(phase), b = Triangle(phase);
      T out = a * (1. - frac) + b * frac;
      // Only the Triangle part aliases (Sine is already band-limited): round
      // its two vertices, scaled by the blend amount. Triangle() slope is +-4
      // val/cycle; peak at phase 0.25 (slope +4 -> -4), trough at 0.75 (-4 -> +4).
      if (dt > (T) 0. && frac > (T) 0.)
      {
        out += frac * (T) -8. * dt * PolyBlamp(Wrap01(phase - (T) 0.25), dt);
        out += frac * (T)  8. * dt * PolyBlamp(Wrap01(phase - (T) 0.75), dt);
      }
      return out;
    }
    else if (waveShape < (T) kMorphSawStart)
    {
      T frac = (waveShape - (T) 1.) / ((T) kMorphSawStart - (T) 1.);
      T r = (T) 0.5 + (T) 0.5 * frac;
      T out = AsymTriangle(phase, r);
      // AsymTriangle: rising slope +2/r, falling slope -2/(1-r) (val/cycle);
      // vertices at phase = phi0 and phi0 + r. Clamp r for the slope magnitude
      // only (position stays exact) so the near-saw end (r -> 1) doesn't blow
      // up - that sliver hands straight over to the pure-Saw BLEP below.
      if (dt > (T) 0.)
      {
        T phi0 = (T) 1. - r * (T) 0.5;
        T rs = std::min(r, (T) 0.98);
        T dSlope = (T) 2. / rs + (T) 2. / ((T) 1. - rs);
        out += dSlope * dt * PolyBlamp(Wrap01(phase - phi0), dt);       // -2/(1-r) -> +2/r
        out -= dSlope * dt * PolyBlamp(Wrap01(phase - phi0 - r), dt);   // +2/r -> -2/(1-r)
      }
      return out;
    }
    else if (waveShape <= (T) kMorphSawEnd)
    {
      // Saw() is phase-shifted +0.5, so its downward step of size 2 is at phase 0.5.
      T out = Saw(phase);
      if (dt > (T) 0.)
        out -= PolyBlep(Wrap01(phase - (T) 0.5), dt);
      return out;
    }
    else if (waveShape < (T) kMorphSquareEnd)
    {
      // saw -> square: progressively hard-clip the sawtooth itself instead of
      // crossfading two differently-shaped waves (which caused a perceived
      // octave-up artifact partway through the transition). The clipped/flat
      // fraction of the waveform is 1 - 1/k, not k itself - so a plain linear
      // k = 1 + frac*50 makes that flat fraction rocket up almost immediately
      // (e.g. already ~83% flat at only 10% into this segment), which felt
      // like a sudden jump to square rather than a gradual one. Solving
      // 1 - 1/k = frac * (1 - 1/kMax) instead makes the flat fraction itself
      // grow linearly with frac (still reaching the same kMax=51 endpoint the
      // Pulse segment's duty=0.5 starting point was tuned to match).
      T frac = (waveShape - (T) kMorphSawEnd) / ((T) kMorphSquareEnd - (T) kMorphSawEnd);
      const T kMax = (T) 51.;
      T k = (T) 1. / ((T) 1. - frac * (kMax - (T) 1.) / kMax);
      T raw = std::max((T) -1., std::min((T) 1., Saw(phase) * k));
      // Loudness compensation: hard-clipping raises the waveform's RMS from a
      // saw's 1/sqrt(3) toward a square's 1.0 (a genuine ~+4.8dB, not just a
      // perceptual illusion) even though the peak amplitude never exceeds +-1 -
      // this read as the sound abruptly getting louder on its own. RMS(k) for
      // this exact clip shape works out to sqrt(1 - 2/(3k)) (integrating the
      // clipped ramp over one period) - dividing by it and re-multiplying by
      // the target RMS keeps perceived loudness level with Saw's own, all the
      // way through this segment and the Pulse segment below (same gain
      // reused there, since Pulse's RMS is always exactly 1 regardless of duty).
      T rmsK = std::sqrt((T) 1. - (T) 2. / ((T) 3. * k));
      // Band-limit before the loudness-comp gain: the Saw's own step (size 2,
      // downward, phase 0.5) plus the two clip shoulders - it enters the upper
      // clip at phase 0.5/k (slope +2k -> 0) and leaves the lower clip at phase
      // 1 - 0.5/k (slope 0 -> +2k). At k -> 1 the two BLAMPs collapse onto phase
      // 0.5 and cancel, leaving just the Saw BLEP - continuous with the pure-Saw
      // branch above.
      if (dt > (T) 0.)
      {
        T corr = -PolyBlep(Wrap01(phase - (T) 0.5), dt);
        corr += (T) -2. * k * dt * PolyBlamp(Wrap01(phase - (T) 0.5 / k), dt);
        corr += (T)  2. * k * dt * PolyBlamp(Wrap01(phase - ((T) 1. - (T) 0.5 / k)), dt);
        raw += corr;
      }
      return raw * ((T) kMorphTargetRMS / rmsK);
    }
    else
    {
      // square -> rectangular: sweep the duty cycle of a single pulse. Pulse's
      // output is always exactly +-1 regardless of duty, so its RMS is always
      // exactly 1 - apply the same flat loudness-compensation gain the
      // hard-clip segment above approaches at its own endpoint, so there's no
      // audible level jump at the segment boundary (see that segment's comment).
      T frac = (waveShape - (T) kMorphSquareEnd) / ((T) 4. - (T) kMorphSquareEnd);
      T duty = (T) 0.5 - frac * (T) 0.4; // 0.5 -> 0.1 duty
      T raw = Pulse(phase, duty);
      // Band-limit both edges: rising +2 at phase 0, falling -2 at phase = duty.
      if (dt > (T) 0.)
      {
        raw += PolyBlep(Wrap01(phase), dt);
        raw -= PolyBlep(Wrap01(phase - duty), dt);
      }
      return raw * (T) kMorphTargetRMS;
    }
  }
}

// Zavalishin/"TPT" state-variable filter (trapezoidal integration, not the naive
// forward-Euler Chamberlin form). Produces low/band/high simultaneously each
// sample so the "filter type" knob can blend continuously between them.
// Two stages are cascaded for the 24dB/oct mode.
//
// The forward-Euler version this replaced went numerically unstable (blew up to
// inf/nan, heard as a click then silence) whenever cutoff got pushed high with
// resonance low - the instability condition was g*damp > ~2, which is easy to
// hit at default (0%) resonance and max cutoff. TPT integration is unconditionally
// stable for any cutoff below Nyquist regardless of damp/Q, so no such clamp is needed.
template<typename T>
struct SVFStage
{
  T mIC1eq = 0., mIC2eq = 0.;

  void Reset() { mIC1eq = 0.; mIC2eq = 0.; }

  // g = tan(pi * cutoffHz / sampleRate) (prewarped cutoff), damp = 1/Q
  void Process(T input, T g, T damp, T& outLow, T& outBand, T& outHigh)
  {
    T a1 = (T) 1. / ((T) 1. + g * (g + damp));
    T a2 = g * a1;
    T a3 = g * a2;

    T v3 = input - mIC2eq;
    T v1 = a1 * mIC1eq + a2 * v3;
    T v2 = mIC2eq + a2 * mIC1eq + a3 * v3;

    mIC1eq = (T) 2. * v1 - mIC1eq;
    mIC2eq = (T) 2. * v2 - mIC2eq;

    outBand = v1;
    outLow = v2;
    outHigh = input - damp * v1 - v2;
  }
};

template<typename T>
class IPlugInstrumentDSP
{
public:
#pragma mark - Voice
  class Voice : public SynthVoice
  {
  public:
    Voice()
    : mAMPEnv("gain", [&](){ mPhase1 = mPhase2 = 0.; }) // capture ok on RT thread?
    , mRandSeed((uint32_t) (uintptr_t) this) // unique-ish per-voice seed so pooled voices don't draw identical Yuragi/noise sequences
    {
//      DBGMSG("new Voice: %i control inputs.\n", static_cast<int>(mInputs.size()));
      mAMPEnv.SetAttackShape(2.); // ease-in attack (was a linear ramp) - softens the onset transient
      mFilterEnv.SetAttackShape(2.); // same treatment, on trial - softens the cutoff sweep's onset too
      // decorrelate each pooled voice's (and each oscillator's) Osc Drift phase
      mDriftPhA1 = Rand() * 0.5 + 0.5; mDriftPhB1 = Rand() * 0.5 + 0.5;
      mDriftPhA2 = Rand() * 0.5 + 0.5; mDriftPhB2 = Rand() * 0.5 + 0.5;
    }

    bool GetBusy() const override
    {
      return mAMPEnv.GetBusy();
    }

    void Trigger(double level, bool isRetrigger) override
    {
      mPhase1 = mPhase2 = 0.; // oscillator phase always restarts at 0 on note-on

      // Key velocity removed for now (user request) - every note triggers at full
      // envelope depth (1.) regardless of the incoming MIDI velocity's `level`
      // (0-1), rather than scaling the amp/filter envelope peak by it. `level` is
      // still captured below for the Modulation Matrix's Velocity source though -
      // that's a new, independent use of it, not a re-enabling of the old behavior.
      //
      // Velocity Curve (2026-08-01, kParamVelocityCurve): a single continuous knob
      // morphing the shape from exponential through linear/proportional to
      // logarithmic. Implemented as a power curve, exponent = 4^-mVelocityCurve:
      // at -1 (fully exponential) exponent=4 (soft notes disproportionately
      // quiet), at 0 (linear, the default) exponent=1 (level unchanged), at +1
      // (fully logarithmic) exponent=0.25 (soft notes disproportionately loud).
      T curveExponent = std::pow((T) 4., -mVelocityCurve);
      mVelocity = std::pow((T) level, curveExponent);

      // Matrix Env Time destinations (2026-08-01, kMatDstAmpEnvTime/
      // kMatDstFilterEnvTime/kMatDstModEnv1Time/kMatDstModEnv2Time): scale that
      // envelope's Attack/Decay/Release together via ADSREnvelope::Start()/
      // Retrigger()'s existing `timeScalar` argument (already there for exactly
      // this - "for key-follow scaling", see ADSREnvelope.h). Only meaningful
      // once per note (the scalar is baked in at Start/Retrigger, not re-read
      // per-sample), so this is evaluated here from whatever source values are
      // available at the instant of triggering - NOT the same as the smooth
      // per-sample matrix evaluation every other destination gets in
      // ProcessSamplesAccumulating. Mod LFO1/2 are DSP-level (shared across
      // voices), not reachable from this per-voice Trigger() without extra
      // plumbing this project doesn't have yet, so they contribute 0 here (a
      // documented gap, not a silent bug) if routed to one of these
      // destinations - every other source (Velocity, Key Follow, Mod Wheel, Mod
      // Env 1/2's own GetPrevOutput()) works normally.
      T matrixSrcTrigger[kNumMatrixSources];
      matrixSrcTrigger[kMatSrcNone] = (T) 0.;
      matrixSrcTrigger[kMatSrcModLFO1] = (T) 0.; // not available at Trigger() time, see comment above
      matrixSrcTrigger[kMatSrcModLFO2] = (T) 0.;
      matrixSrcTrigger[kMatSrcModEnv1] = mModEnv1.GetPrevOutput();
      matrixSrcTrigger[kMatSrcModEnv2] = mModEnv2.GetPrevOutput();
      matrixSrcTrigger[kMatSrcVelocity] = mVelocity;
      // NOT mInputs[kVoiceControlPitch] here (2026-08-01 bug fix) - VoiceAllocator
      // (VoiceAllocator.cpp's StartVoice()) only sets a *glide target* for the
      // pitch control before calling Trigger(); the actual mInputs[] value isn't
      // written until the glide is advanced during the next ProcessBlock, so at
      // this exact moment mInputs[kVoiceControlPitch] still holds whatever this
      // pooled voice's PREVIOUS note left behind - reading it here produced a
      // seemingly random Decay/Release time on repeated presses of the same key
      // (whichever stale value happened to be sitting in the reused voice slot),
      // not this note's own pitch. `mKey` (base SynthVoice member) is set
      // correctly right before Trigger() is called every time, so compute pitch
      // from it directly using the same default key->pitch mapping VoiceAllocator
      // itself uses (VoiceAllocator.cpp: `(key - 69) / 12`, A4=440Hz=MIDI 69) -
      // would need revisiting if this project ever calls SetKeyToPitchFn() to
      // override that default (it doesn't, as of this fix).
      T triggerPitch = ((T) mKey - (T) 69.) / (T) 12.;
      matrixSrcTrigger[kMatSrcKeyFollow] = triggerPitch / (T) 4.;
      matrixSrcTrigger[kMatSrcModWheel] = mModWheel;

      T ampEnvTimeMod = (T) 0., filterEnvTimeMod = (T) 0., modEnv1TimeMod = (T) 0., modEnv2TimeMod = (T) 0.;
      for (int s = 0; s < kNumMatrixSlots; s++)
      {
        T contribution = matrixSrcTrigger[mMatrixSource[s]] * (mMatrixAmount[s] / (T) 100.);
        if (mMatrixDest[s] == kMatDstAmpEnvTime) ampEnvTimeMod += contribution;
        else if (mMatrixDest[s] == kMatDstFilterEnvTime) filterEnvTimeMod += contribution;
        else if (mMatrixDest[s] == kMatDstModEnv1Time) modEnv1TimeMod += contribution;
        else if (mMatrixDest[s] == kMatDstModEnv2Time) modEnv2TimeMod += contribution;
      }
      // +-100% amount = 16x slower/faster (4 octaves) - positive Amount means a
      // LONGER (slower) envelope, matching the "Time" naming (more Time = slower).
      // Was 2 octaves (4x) - user found Key Follow -> Env Time too subtle across
      // the keyboard (wanted piano-like low/high decay-time contrast) - widened
      // here rather than touching kMatSrcKeyFollow's own shared scaling, so every
      // other Key-Follow-routed destination (Filter Cutoff, Pitch, etc.) is
      // unaffected.
      const T kEnvTimeRangeOctaves = (T) 4.;
      T ampEnvTimeScalar = std::pow((T) 2., ampEnvTimeMod * kEnvTimeRangeOctaves);
      T filterEnvTimeScalar = std::pow((T) 2., filterEnvTimeMod * kEnvTimeRangeOctaves);
      T modEnv1TimeScalar = std::pow((T) 2., modEnv1TimeMod * kEnvTimeRangeOctaves);
      T modEnv2TimeScalar = std::pow((T) 2., modEnv2TimeMod * kEnvTimeRangeOctaves);

      if(isRetrigger)
      {
        mAMPEnv.Retrigger(1., ampEnvTimeScalar);
        mFilterEnv.Retrigger(1., filterEnvTimeScalar);
        mModEnv1.Retrigger(1., modEnv1TimeScalar);
        mModEnv2.Retrigger(1., modEnv2TimeScalar);
      }
      else
      {
        mAMPEnv.Start(1., ampEnvTimeScalar);
        mFilterEnv.Start(1., filterEnvTimeScalar);
        mModEnv1.Start(1., modEnv1TimeScalar);
        mModEnv2.Start(1., modEnv2TimeScalar);
      }

      if (!isRetrigger)
      {
        mHPFStage.Reset();
        mMoogOut1 = mMoogOut2 = mMoogOut3 = mMoogOut4 = (T) 0.;
        mMoogIn1 = mMoogIn2 = mMoogIn3 = mMoogIn4 = (T) 0.;
      }

      // Yuragi: width (not speed) of a fresh, independent random draw each trigger -
      // same "width, not speed" concept as SuiKinKutsu's Yuragi knob. At 0 every note
      // is identical (offset 0, dead-center pan); the wider the knob, the further each
      // note's pitch/pan can land from center. Rand() already returns [-1, 1].
      T yuragiPitch = Rand();
      T yuragiPan = Rand();

      mYuragiPitchOffsetOctaves = yuragiPitch * mYuragiRate * (T) (0.6 / 12.); // +-0.6 semitones at 100% (was +-1.2, halved per user request)

      mBasePanNorm = yuragiPan * mYuragiRate * (T) 0.5 + (T) 0.5; // 0..1, centered at 0.5 when width/draw is 0 - cached (Modulation Matrix's Pan destination recombines with this per-sample, see ProcessSamplesAccumulating)
      T angle = mBasePanNorm * (T) (3.14159265358979323846 / 2.);
      mGainL = std::cos(angle);
      mGainR = std::sin(angle);
    }

    void Release() override
    {
      mAMPEnv.Release();
      mFilterEnv.Release();
      mModEnv1.Release();
      mModEnv2.Release();
    }

    void ProcessSamplesAccumulating(T** inputs, T** outputs, int nInputs, int nOutputs, int startIdx, int nFrames) override
    {
      // inputs to the synthesizer can just fetch a value every block, like this:
//      double gate = mInputs[kVoiceControlGate].endValue;
      double pitch = mInputs[kVoiceControlPitch].endValue;
      double pitchBend = mInputs[kVoiceControlPitchBend].endValue;

      // Modulation Matrix - Osc1/Osc2 Pitch destinations, evaluated once per block
      // (matching this file's existing convention: osc1Freq/osc2Freq/phaseInc1/
      // phaseInc2 below are themselves only computed once per block, using
      // inputs[kModPitchLFO][0] - the block-*start* LFO value, not a per-sample
      // one - so pitch modulation generally isn't per-sample-accurate here even
      // for the pre-existing Pitch LFO; these two new destinations follow that
      // same simplification rather than introducing an inconsistency). For the
      // two envelope sources, GetPrevOutput() (not Process(), which advances
      // time) reads the value already reached by the end of the *previous* block -
      // equivalent to "this block's start" without desyncing the real per-sample
      // Process() calls used for every other destination inside the loop below.
      T matrixSrcBlockStart[kNumMatrixSources];
      matrixSrcBlockStart[kMatSrcNone] = (T) 0.;
      matrixSrcBlockStart[kMatSrcModLFO1] = inputs[kModModLFO1][0];
      matrixSrcBlockStart[kMatSrcModLFO2] = inputs[kModModLFO2][0];
      matrixSrcBlockStart[kMatSrcModEnv1] = mModEnv1.GetPrevOutput();
      matrixSrcBlockStart[kMatSrcModEnv2] = mModEnv2.GetPrevOutput();
      matrixSrcBlockStart[kMatSrcVelocity] = mVelocity;
      matrixSrcBlockStart[kMatSrcKeyFollow] = (T) pitch / (T) 4.; // ~-1..1 across a typical ~4-octave playable range
      matrixSrcBlockStart[kMatSrcModWheel] = mModWheel;

      T matOsc1PitchOct = (T) 0., matOsc2PitchOct = (T) 0.; // additive octaves, +-4 at +-100% amount (was +-2, per user request)
      // Pitch Fine (2026-07-28) - +-1 semitone at +-100% amount, a much narrower
      // alternative for subtle vibrato-style use (see kMatDstOsc1PitchFine's own
      // comment near EMatrixDest for why this exists alongside the coarser pair above).
      const T kSemitoneOctaves = (T) (1. / 12.);
      T matOsc1PitchFineOct = (T) 0., matOsc2PitchFineOct = (T) 0.;
      for (int s = 0; s < kNumMatrixSlots; s++)
      {
        T contribution = matrixSrcBlockStart[mMatrixSource[s]] * (mMatrixAmount[s] / (T) 100.);
        if (mMatrixDest[s] == kMatDstOsc1Pitch) matOsc1PitchOct += contribution * (T) 4.;
        else if (mMatrixDest[s] == kMatDstOsc2Pitch) matOsc2PitchOct += contribution * (T) 4.;
        else if (mMatrixDest[s] == kMatDstOsc1PitchFine) matOsc1PitchFineOct += contribution * kSemitoneOctaves;
        else if (mMatrixDest[s] == kMatDstOsc2PitchFine) matOsc2PitchFineOct += contribution * kSemitoneOctaves;
      }

      // Osc Drift (2026-09-01, kParamOscDrift): a slow, bounded, per-oscillator
      // pitch wander for analog-style "movement" - two incommensurate slow sines
      // summed per oscillator (smooth, bounded, and block-size/sample-rate
      // independent, unlike a per-block random walk). Osc1 and Osc2 use
      // different rates, so with both oscillators up the patch slowly beats and
      // thickens even though this synth has no unison. Evaluated once per block
      // (the wander is far slower than a block, so per-sample would be wasted
      // work); phases free-run and are seeded per-voice in the ctor.
      double drift1Oct = 0., drift2Oct = 0.;
      if (mOscDrift > (T) 0.)
      {
        const double kTwoPi = 6.283185307179586;
        const double kDriftMaxOct = 12.0 / 1200.0; // peak +-12 cents at 100%
        double adv = (double) nFrames / mSampleRate;
        mDriftPhA1 += 0.11 * adv; mDriftPhA1 -= std::floor(mDriftPhA1);
        mDriftPhB1 += 0.17 * adv; mDriftPhB1 -= std::floor(mDriftPhB1);
        mDriftPhA2 += 0.13 * adv; mDriftPhA2 -= std::floor(mDriftPhA2);
        mDriftPhB2 += 0.19 * adv; mDriftPhB2 -= std::floor(mDriftPhB2);
        double d = (double) mOscDrift * kDriftMaxOct * 0.5;
        drift1Oct = d * (std::sin(kTwoPi * mDriftPhA1) + std::sin(kTwoPi * mDriftPhB1));
        drift2Oct = d * (std::sin(kTwoPi * mDriftPhA2) + std::sin(kTwoPi * mDriftPhB2));
      }

      // convert from "1v/oct" pitch space to frequency in Hertz
      double osc1Freq = 440. * pow(2., pitch + pitchBend + inputs[kModPitchLFO][0] + mTuneOctaves1 + mYuragiPitchOffsetOctaves + matOsc1PitchOct + matOsc1PitchFineOct + drift1Oct);
      double osc2Freq = 440. * pow(2., pitch + pitchBend + inputs[kModPitchLFO][0] + mTuneOctaves2 + mYuragiPitchOffsetOctaves + matOsc2PitchOct + matOsc2PitchFineOct + drift2Oct);
      double phaseInc1 = osc1Freq / mSampleRate;
      double phaseInc2 = osc2Freq / mSampleRate;

      const T envAmountOctaves = (mFilterEnvAmount / (T) 100.) * (T) 8.; // +-8 octaves at +-100% (was +-6, then +-4 before that - user found the effect too weak each time)

      // make sound output for each output channel
      for(auto i = startIdx; i < startIdx + nFrames; i++)
      {
        // Modulation Matrix - every other destination is already evaluated inside
        // this per-sample loop (unlike the two Pitch destinations above), so these
        // are gathered/accumulated fresh every sample. mModEnv1/mModEnv2's real
        // Process() calls (the only ones that actually advance their envelope
        // state - GetPrevOutput() above never does) happen here.
        T matrixSrc[kNumMatrixSources];
        matrixSrc[kMatSrcNone] = (T) 0.;
        matrixSrc[kMatSrcModLFO1] = inputs[kModModLFO1][i];
        matrixSrc[kMatSrcModLFO2] = inputs[kModModLFO2][i];
        matrixSrc[kMatSrcModEnv1] = mModEnv1.Process(inputs[kModModEnv1SustainSmoother][i]);
        matrixSrc[kMatSrcModEnv2] = mModEnv2.Process(inputs[kModModEnv2SustainSmoother][i]);
        matrixSrc[kMatSrcVelocity] = mVelocity;
        matrixSrc[kMatSrcKeyFollow] = (T) pitch / (T) 4.;
        matrixSrc[kMatSrcModWheel] = mModWheel;

        T matDest[kNumMatrixDests] = { (T) 0. };
        for (int s = 0; s < kNumMatrixSlots; s++)
        {
          T amt = mMatrixAmount[s] / (T) 100.;
          T contribution;
          // 2026-08-01: Velocity->Amp Level felt "too weak" - the generic formula
          // below (src * amount) treats every source as bipolar/zero-centered, which
          // is right for the LFOs/envelopes/Key Follow/Mod Wheel but wrong for
          // Velocity (0..1, unipolar): at 100% amount it only ever added up to +1
          // on top of the already-full baseline, so soft notes never got quieter and
          // hard notes only reached ~2x (+6dB). User asked specifically for this
          // pairing to behave like ordinary velocity sensitivity instead: at 100%
          // amount, velocity 0 -> silence, velocity 1 -> unchanged (matches the
          // ampLFOMod baseline of 1 below), linear in between. Scoped to this exact
          // source/destination pair only - Velocity's contribution to every other
          // destination (Pitch, Filter Cutoff, etc.) is deliberately left as-is,
          // that's a separate question the user didn't ask to revisit yet.
          if (mMatrixSource[s] == kMatSrcVelocity && mMatrixDest[s] == kMatDstAmpLevel)
            contribution = (matrixSrc[kMatSrcVelocity] - (T) 1.) * amt;
          else
            contribution = matrixSrc[mMatrixSource[s]] * amt;
          matDest[mMatrixDest[s]] += contribution;
        }
        // kMatDstNone's accumulator (matDest[0]) intentionally collects any
        // None-destination slots' contributions and is simply never read below.

        float noise = Rand();

        // Osc1/Osc2 Level, Noise Level: additive on the existing 0-1 mix values,
        // +-1 range at +-100% amount, reclamped 0-1 - matches this synth's other
        // 0-1-scale mix/level params.
        T mixOsc1 = std::max((T) 0., std::min((T) 1., mMixOsc1 + matDest[kMatDstOsc1Level]));
        T mixOsc2 = std::max((T) 0., std::min((T) 1., mMixOsc2 + matDest[kMatDstOsc2Level]));
        T mixNoise = std::max((T) 0., std::min((T) 1., mMixNoise + matDest[kMatDstNoiseLevel]));

        // Wave Shape 1/2: additive on the existing 0-4 morph value, +-4 range (full
        // sweep) at +-100% amount - Morph() itself already clamps to [0,4]. The
        // trailing arg is the per-sample phase increment (freq/sampleRate), used
        // for the PolyBLEP/PolyBLAMP band-limiting inside Morph().
        T osc1Out = FirstSynthOsc::Morph<T>(mPhase1, mWaveShape1 + matDest[kMatDstWaveShape1] * (T) 4., (T) phaseInc1);
        mPhase1 += phaseInc1;
        mPhase1 -= std::floor(mPhase1);

        T osc2Out = FirstSynthOsc::Morph<T>(mPhase2, mWaveShape2 + matDest[kMatDstWaveShape2] * (T) 4., (T) phaseInc2);
        mPhase2 += phaseInc2;
        mPhase2 -= std::floor(mPhase2);

        T dry = osc1Out * mixOsc1 + osc2Out * mixOsc2 + noise * mixNoise;

        T envVal = mAMPEnv.Process(inputs[kModSustainSmoother][i]);
        T filterEnvVal = mFilterEnv.Process(inputs[kModFilterSustainSmoother][i]);

        // pitch is in octaves relative to A4 (see osc1Freq/osc2Freq above), so at 100%
        // Key Follow (mFilterKeyFollow == 1) this term exactly matches osc pitch's own
        // octave shift - same additive-octave convention as Env Amount/Filter LFO here.
        // Matrix Filter Cutoff destination: additive octaves, same +-4-octave-at-100%
        // scale as the dedicated Filter LFO's own Depth knob (kModFilterLFO term
        // above) - so an LFO patched via the matrix sweeps the same range as one
        // patched via the dedicated Filter LFO section, at matching 100% settings.
        T cutoffHz = mFilterCutoff * std::pow((T) 2., envAmountOctaves * filterEnvVal + inputs[kModFilterLFO][i] + mFilterKeyFollow * (T) pitch + matDest[kMatDstFilterCutoff] * (T) 4.);
        cutoffHz = std::max((T) 20., std::min((T) (mSampleRate * 0.49), cutoffHz));
        // Matrix Filter Resonance destination: additive on the 0-100 normalized
        // resonance scale (matching the knob's own %), +-100 range at +-100% amount,
        // reclamped before ProcessMoogLadder's own 0-1 resonance scale below.
        T qNorm = std::max((T) 0., std::min((T) 100., ((mFilterQ - (T) 0.5) / (T) 19.5) * (T) 100. + matDest[kMatDstFilterResonance] * (T) 100.));

        // Smooths out instantaneous cutoff jumps (Random/Square LFO shapes,
        // stepped Matrix modulation, etc.) before they reach the filter - see
        // mSmoothedCutoffHz's own comment for why.
        mSmoothedCutoffHz += (cutoffHz - mSmoothedCutoffHz) * mCutoffSmoothCoeff;

        // Filter is LP-only now (Moog ladder) - BP/HP retired 2026-08-16, see
        // mFilterType's own comment.
        T filtered = ProcessMoogLadder(dry, mSmoothedCutoffHz, qNorm / (T) 100.);

        // Fixed highpass-in-series stage (2026-08-16, replaced selectable BP/HP) -
        // only a cutoff knob, no resonance control (kHPFDamp is a fixed constant,
        // not modulatable/user-adjustable, matching the "cutoff knob only" design).
        T hpfCutoffHz = std::max((T) 20., std::min((T) (mSampleRate * 0.49), mHPFCutoff));
        T hpfG = std::tan((T) 3.14159265358979323846 * hpfCutoffHz / (T) mSampleRate);
        T hpfLow, hpfBand, hpfHigh;
        mHPFStage.Process(filtered, hpfG, kHPFDamp, hpfLow, hpfBand, hpfHigh);
        filtered = hpfHigh;

        // tremolo: bipolar LFO/matrix centered on 1x gain. Matrix Amp Level
        // destination shares the same +-1-at-100%-amount multiplicative convention
        // as the existing Amp LFO term it's added alongside.
        T ampLFOMod = (T) 1. + inputs[kModAmpLFO][i] + matDest[kMatDstAmpLevel];

        // Matrix Pan destination: only recomputed (cos/sin) when actually in use -
        // matDest[kMatDstPan] is 0 whenever no slot targets Pan, which is the
        // common case, so this preserves mGainL/mGainR's existing Yuragi-only
        // per-note-constant behavior exactly when the matrix isn't used for pan.
        T gainL = mGainL, gainR = mGainR;
        if (matDest[kMatDstPan] != (T) 0.)
        {
          T panNorm = std::max((T) 0., std::min((T) 1., mBasePanNorm + matDest[kMatDstPan] * (T) 0.5));
          T angle = panNorm * (T) (3.14159265358979323846 / 2.);
          gainL = std::cos(angle);
          gainR = std::sin(angle);
        }

        // an MPE synth can use pressure here in addition to gain
        T voiceOut = filtered * envVal * mGain * ampLFOMod;
        outputs[0][i] += voiceOut * gainL;
        outputs[1][i] += voiceOut * gainR;
      }
    }

    void SetSampleRateAndBlockSize(double sampleRate, int blockSize) override
    {
      mSampleRate = sampleRate;
      mAMPEnv.SetSampleRate(sampleRate);
      mFilterEnv.SetSampleRate(sampleRate);
      mModEnv1.SetSampleRate(sampleRate);
      mModEnv2.SetSampleRate(sampleRate);
      // See mSmoothedCutoffHz's own comment - fixed ~3ms one-pole smoothing
      // time constant, cached here (once per sample-rate change) rather than
      // recomputed every sample.
      mCutoffSmoothCoeff = (T) (1. - std::exp(-1. / (0.003 * sampleRate)));
    }

    void SetProgramNumber(int pgm) override
    {
      //TODO:
    }

    // this is called by the VoiceAllocator to set generic control values - CC
    // messages not special-cased elsewhere (VoiceAllocator.cpp/MidiSynth.cpp)
    // reach here with controlNumber = the real MIDI CC number and value already
    // normalized [0,1] (same convention FirstSynth.cpp's existing CC7->Gain
    // handling relies on). Only CC1 (Mod Wheel) is used, for the Modulation Matrix.
    void SetControl(int controlNumber, float value) override
    {
      if (controlNumber == 1)
        mModWheel = (T) value;
    }

  public:
    ADSREnvelope<T> mAMPEnv;
    ADSREnvelope<T> mFilterEnv { "filterEnv" }; // independent from mAMPEnv - no oscillator-phase resetFunc needed, mAMPEnv's already handles that
    // Modulation Matrix (2026-07-28) - 2 free envelopes, not hard-wired to any
    // destination (see EMatrixSource/EMatrixDest near the top of this file).
    ADSREnvelope<T> mModEnv1 { "modEnv1" };
    ADSREnvelope<T> mModEnv2 { "modEnv2" };
    T mVelocity = 1.; // 0-1, captured in Trigger() - Matrix source only, doesn't scale mAMPEnv/mFilterEnv (see Trigger()'s own comment)
    T mModWheel = 0.; // 0-1, CC1 - see SetControl() above
    // per-slot routing for the 8 Modulation Matrix slots (4 -> 8, 2026-07-28 user
    // request) - EMatrixSource/EMatrixDest indices, set from FirstSynth_DSP.h's
    // SetParam() via the usual ForEachVoice pattern. mMatrixAmount is a percent, +-100.
    static const int kNumMatrixSlots = 8;
    int mMatrixSource[kNumMatrixSlots] = { kMatSrcNone, kMatSrcNone, kMatSrcNone, kMatSrcNone, kMatSrcNone, kMatSrcNone, kMatSrcNone, kMatSrcNone };
    int mMatrixDest[kNumMatrixSlots] = { kMatDstNone, kMatDstNone, kMatDstNone, kMatDstNone, kMatDstNone, kMatDstNone, kMatDstNone, kMatDstNone };
    T mMatrixAmount[kNumMatrixSlots] = { 0., 0., 0., 0., 0., 0., 0., 0. };
    T mWaveShape1 = 0.;    // 0-4, continuous Sine->Triangle->Saw->Square->Pulse morph
    T mTuneOctaves1 = 0.;  // combined octave/semitone/fine offset, in octaves
    T mWaveShape2 = 0.;
    T mTuneOctaves2 = 0.;
    T mMixOsc1 = 1.;
    T mMixOsc2 = 0.;
    T mMixNoise = 0.;
    T mFilterCutoff = 10000.;
    T mFilterQ = 0.7;
    // Vestigial (2026-08-16) - BP/HP retired entirely, the main Filter is
    // LP-only now (always the Moog ladder below). Kept storing this value
    // (still a valid param, see FirstSynth.cpp's kParamFilterType comment) but
    // nothing reads it anymore - see mHPFStage/mHPFCutoff below for the new
    // fixed highpass-in-series stage that replaced selectable BP/HP.
    T mFilterType = 0.;
    T mFilterEnvAmount = 0.; // percent, +-100
    T mFilterKeyFollow = 0.; // 0-1 (0-100%), see kParamFilterKeyFollow
    // Fixed second filter stage in series after the main Filter (2026-08-16,
    // replaced selectable BP/HP) - just a highpass, only a cutoff knob, no
    // resonance control (mHPFStage's damp is a fixed constant, see its own
    // Process() call in ProcessSamplesAccumulating).
    T mHPFCutoff = 20.;
    T mYuragiRate = 0.; // 0-1 (0-100%), see kParamYuragi - width of per-note random pitch/pan
    T mVelocityCurve = 0.; // -1..1, see kParamVelocityCurve - shapes mVelocity in Trigger()
    // Osc Drift (2026-09-01, kParamOscDrift) - depth 0-1 of a slow, bounded,
    // per-oscillator pitch wander. mDriftPh* are free-running phase accumulators
    // for two incommensurate slow sines per oscillator (osc1 uses A1/B1, osc2
    // uses A2/B2 at different rates), seeded per-voice in the ctor so pooled
    // voices don't drift in lockstep. Applied per block in
    // ProcessSamplesAccumulating; never reset on Trigger.
    T mOscDrift = 0.;
    double mDriftPhA1 = 0., mDriftPhB1 = 0., mDriftPhA2 = 0., mDriftPhB2 = 0.;

  private:
    T mPhase1 = 0.;
    T mPhase2 = 0.;
    double mSampleRate = 44100.;
    // 2026-08-16: user reported big momentary volume spikes when the Filter
    // LFO's Random or Square shape modulates Cutoff - those shapes jump
    // instantly between values (no continuous ramp the way Sine/Triangle do),
    // and feeding a truly instantaneous cutoff jump into ProcessMoogLadder's
    // strongly nonlinear gain-compensation term (`0.35013*fSq*fSq`, a 4th-power
    // function of cutoff) can momentarily spike the output before the ladder's
    // internal state (mMoogOut1-4, tuned to the *old* cutoff) catches up to the
    // new one - a known characteristic of time-varying resonant filters, not
    // specific to Random/Square, just most audible with them since every other
    // LFO shape (and envelopes, Key Follow, Matrix modulation) already changes
    // cutoff continuously. Fixed with a fast (~3ms) one-pole smoother on the
    // *final* cutoffHz value (after all modulation sources are summed) right
    // before it reaches ProcessMoogLadder - short enough to preserve the
    // audible character of a fast LFO sweep, long enough to turn a true
    // instant jump into a fast ramp the filter's internal state can track
    // without a transient. See SetSampleRateAndBlockSize() for
    // mCutoffSmoothCoeff.
    T mSmoothedCutoffHz = 10000.;
    T mCutoffSmoothCoeff = (T) 1.;
    // Fixed highpass-in-series stage (2026-08-16, replaced selectable BP/HP -
    // see mFilterType/mHPFCutoff's own comments) - single SVFStage, only its
    // `high` output is used, with a fixed (not user-controllable) Butterworth
    // damp for a clean, non-resonant rolloff.
    SVFStage<T> mHPFStage;
    static constexpr T kHPFDamp = (T) 1.4142135623730951; // 1/Q, Q=0.7071 (Butterworth)

    // Moog ladder - the only Filter type now (BP/HP retired 2026-08-16), ported from Chaoscape's
    // ChaoscapeEngine::ProcessMoogLadder (same classic Stilson/Smith-style
    // 4-stage cascade + tanh-saturated resonant feedback), per-voice instead
    // of per-engine since this synth is polyphonic. kMoogResonanceScale
    // matches Chaoscape's own "boosted" scale (its Mode 2) rather than
    // Chaoscape's milder default (Mode 1's 4.2) - user asked for the
    // resonance raised, not just a straight port.
    static constexpr T kMoogResonanceScale = (T) 6.0;
    T mMoogOut1 = (T) 0., mMoogOut2 = (T) 0., mMoogOut3 = (T) 0., mMoogOut4 = (T) 0.;
    T mMoogIn1 = (T) 0., mMoogIn2 = (T) 0., mMoogIn3 = (T) 0., mMoogIn4 = (T) 0.;

    T ProcessMoogLadder(T input, T cutoffHz, T resonance01)
    {
      // Normalized-cutoff mapping - **normalized against Nyquist (sampleRate*0.5),
      // not the full sample rate** (2026-08-16, superseding the previous 0.45->0.49
      // clamp adjustment above, which only masked a bigger issue). Verified by the
      // user with a spectrum analyzer (white noise, Q=0, Cutoff at max): even at
      // "20000Hz" the signal was already down -12.6dB by 5000Hz and -58dB by
      // 20000Hz - the filter was nowhere near "open" at its labeled max. Confirmed
      // numerically (a standalone Python simulation of this exact formula) that
      // normalizing against the *full* sample rate meant `fc` never got anywhere
      // near this ladder topology's intended range even at the old clamp's ceiling
      // - Chaoscape's own port (`ChaoscapeEngine.h`) has the same
      // sample-rate-normalized convention, so this wasn't a copy-paste slip, just
      // an inherited miscalibration. Switching to Nyquist normalization (clamp
      // raised to 0.98, i.e. stopping just short of Nyquist itself for stability)
      // measured dramatically closer to "actually open at max": -0.9dB by 5000Hz,
      // -17dB by 20000Hz at the "20000Hz" label, instead of -58dB. Still not a
      // perfectly accurate Hz label across the whole knob (e.g. "1000Hz" still
      // measures a real -3dB point closer to ~250-300Hz) - an inherent limit of
      // this classic Stilson/Smith-style approximation's fixed coefficients
      // (`1.16`/`0.35013`/`0.3` below), not something a normalization fix alone
      // can fully correct - but this is a large, verified improvement over the
      // previous behavior. If instability/self-oscillation is reported at extreme
      // Cutoff+Resonance settings, 0.98 is the first thing to back off.
      T fc = std::max((T) 0., std::min((T) 0.98, cutoffHz / ((T) mSampleRate * (T) 0.5)));
      T f = fc * (T) 1.16;
      T fSq = f * f;
      T fb = (resonance01 * kMoogResonanceScale) * ((T) 1. - (T) 0.15 * fSq);

      T x = std::tanh(input - mMoogOut4 * fb);
      x *= (T) 0.35013 * fSq * fSq;

      mMoogOut1 = x + (T) 0.3 * mMoogIn1 + ((T) 1. - f) * mMoogOut1; mMoogIn1 = x;
      mMoogOut2 = mMoogOut1 + (T) 0.3 * mMoogIn2 + ((T) 1. - f) * mMoogOut2; mMoogIn2 = mMoogOut1;
      mMoogOut3 = mMoogOut2 + (T) 0.3 * mMoogIn3 + ((T) 1. - f) * mMoogOut3; mMoogIn3 = mMoogOut2;
      mMoogOut4 = mMoogOut3 + (T) 0.3 * mMoogIn4 + ((T) 1. - f) * mMoogOut4; mMoogIn4 = mMoogOut3;

      return mMoogOut4;
    }
    T mYuragiPitchOffsetOctaves = 0.; // drawn fresh in Trigger(), applied every sample until the next trigger
    T mGainL = (T) 0.70710678; // constant-power center pan until the first Trigger()
    T mGainR = (T) 0.70710678;
    T mBasePanNorm = 0.5; // 0..1, the Yuragi-only pan Trigger() computed mGainL/mGainR from - Modulation Matrix's Pan destination recombines with this per-sample rather than overwriting mGainL/mGainR outright

  private:
    // noise generator for test
    uint32_t mRandSeed;
    
    // return single-precision floating point number on [-1, 1]
    float Rand()
    {
      mRandSeed = mRandSeed * 0x0019660D + 0x3C6EF35F;
      uint32_t temp = ((mRandSeed >> 9) & 0x007FFFFF) | 0x3F800000;
      return (*reinterpret_cast<float*>(&temp))*2.f - 3.f;
    }

  };

public:
#pragma mark -
  IPlugInstrumentDSP(int nVoices)
  {
    for (auto i = 0; i < nVoices; i++)
    {
      // add a voice to Zone 0.
      mSynth.AddVoice(new Voice(), 0);
    }

    mPitchLFO.SetPolarity(true); // bipolar
    mFilterLFO.SetPolarity(true);
    mAmpLFO.SetPolarity(true);
    mModLFO1.SetPolarity(true);
    mModLFO2.SetPolarity(true);
    // no Depth param for these two (unlike Pitch/Filter/Amp LFO) - each Matrix
    // slot's own Amount is the only scale control, so a separate Depth would just
    // be redundant. Fixed at unity so the modulation buffer carries the raw +-1
    // oscillation directly.
    mModLFO1.SetScalar(1.);
    mModLFO2.SetScalar(1.);

    // some MidiSynth API examples:
    // mSynth.SetKeyToPitchFn([](int k){return (k - 69.)/24.;}); // quarter-tone scale
    // mSynth.SetNoteGlideTime(0.5); // portamento
  }

  void ProcessBlock(T** inputs, T** outputs, int nOutputs, int nFrames, double qnPos = 0., bool transportIsRunning = false, double tempo = 120.)
  {
    // clear outputs
    for(auto i = 0; i < nOutputs; i++)
    {
      memset(outputs[i], 0, nFrames * sizeof(T));
    }
    
    mParamSmoother.ProcessBlock(mParamsToSmooth, mModulations.GetList(), nFrames);
    mPitchLFO.ProcessBlock(mModulations.GetList()[kModPitchLFO], nFrames, qnPos, transportIsRunning, tempo);
    mFilterLFO.ProcessBlock(mModulations.GetList()[kModFilterLFO], nFrames, qnPos, transportIsRunning, tempo);
    mAmpLFO.ProcessBlock(mModulations.GetList()[kModAmpLFO], nFrames, qnPos, transportIsRunning, tempo);
    mModLFO1.ProcessBlock(mModulations.GetList()[kModModLFO1], nFrames, qnPos, transportIsRunning, tempo);
    mModLFO2.ProcessBlock(mModulations.GetList()[kModModLFO2], nFrames, qnPos, transportIsRunning, tempo);
    mSynth.ProcessBlock(mModulations.GetList(), outputs, 0, nOutputs, nFrames);
    
    for(int s=0; s < nFrames;s++)
    {
      T smoothedGain = mModulations.GetList()[kModGainSmoother][s];
      outputs[0][s] *= smoothedGain;
      outputs[1][s] *= smoothedGain;
    }
  }

  void Reset(double sampleRate, int blockSize)
  {
    mSynth.SetSampleRateAndBlockSize(sampleRate, blockSize);
    mSynth.Reset();
    mPitchLFO.SetSampleRate(sampleRate);
    mFilterLFO.SetSampleRate(sampleRate);
    mAmpLFO.SetSampleRate(sampleRate);
    mModLFO1.SetSampleRate(sampleRate);
    mModLFO2.SetSampleRate(sampleRate);
    mModulationsData.Resize(blockSize * kNumModulations);
    mModulations.Empty();
    
    for(int i = 0; i < kNumModulations; i++)
    {
      mModulations.Add(mModulationsData.Get() + (blockSize * i));
    }
  }

  void ProcessMidiMsg(const IMidiMsg& msg)
  {
    mSynth.AddMidiMsgToQueue(msg);
  }

  void SetParam(int paramIdx, double value)
  {
    using EEnvStage = ADSREnvelope<sample>::EStage;
    
    switch (paramIdx) {
      case kParamNoteGlideTime:
        mSynth.SetNoteGlideTime(value / 1000.);
        break;
      case kParamPitchBendRange:
        mSynth.SetPitchBendRange(static_cast<int>(value));
        break;
      case kParamGain:
        mParamsToSmooth[kModGainSmoother] = (T) value / 100.;
        break;
      case kParamSustain:
        mParamsToSmooth[kModSustainSmoother] = (T) value / 100.;
        break;
      case kParamAttack:
      case kParamDecay:
      case kParamRelease:
      {
        EEnvStage stage = static_cast<EEnvStage>(EEnvStage::kAttack + (paramIdx - kParamAttack));
        mSynth.ForEachVoice([stage, value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mAMPEnv.SetStageTime(stage, value);
        });
        break;
      }
      case kParamLFODepth:
        mPitchLFO.SetScalar(value / 100.);
        break;
      case kParamLFORateTempo:
        mPitchLFO.SetQNScalarFromDivision(static_cast<int>(value));
        break;
      case kParamLFORateHz:
        mPitchLFO.SetFreqCPS(value);
        break;
      case kParamLFORateMode:
        mPitchLFO.SetRateMode(value > 0.5);
        break;
      case kParamLFOShape:
        mPitchLFO.SetShape(static_cast<int>(value));
        break;
      case kParamFilterLFODepth:
        mFilterLFO.SetScalar((value / 100.) * 4.); // +-4 octaves at 100%, matching Filter Env Amount's range
        break;
      case kParamFilterLFORateTempo:
        mFilterLFO.SetQNScalarFromDivision(static_cast<int>(value));
        break;
      case kParamFilterLFORateHz:
        mFilterLFO.SetFreqCPS(value);
        break;
      case kParamFilterLFORateMode:
        mFilterLFO.SetRateMode(value > 0.5);
        break;
      case kParamFilterLFOShape:
        mFilterLFO.SetShape(static_cast<int>(value));
        break;
      case kParamAmpLFODepth:
        mAmpLFO.SetScalar(value / 100.); // tremolo depth, 0-1
        break;
      case kParamAmpLFORateTempo:
        mAmpLFO.SetQNScalarFromDivision(static_cast<int>(value));
        break;
      case kParamAmpLFORateHz:
        mAmpLFO.SetFreqCPS(value);
        break;
      case kParamAmpLFORateMode:
        mAmpLFO.SetRateMode(value > 0.5);
        break;
      case kParamAmpLFOShape:
        mAmpLFO.SetShape(static_cast<int>(value));
        break;
      case kParamOsc1Wave:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mWaveShape1 = (T) value;
        });
        break;
      case kParamOsc1Octave: mOsc1Octave = value; UpdateTuneOffset1(); break;
      case kParamOsc1Semi:   mOsc1Semi = value;   UpdateTuneOffset1(); break;
      case kParamOsc1Fine:   mOsc1Fine = value;   UpdateTuneOffset1(); break;
      case kParamOsc2Wave:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mWaveShape2 = (T) value;
        });
        break;
      case kParamOsc2Octave: mOsc2Octave = value; UpdateTuneOffset2(); break;
      case kParamOsc2Semi:   mOsc2Semi = value;   UpdateTuneOffset2(); break;
      case kParamOsc2Fine:   mOsc2Fine = value;   UpdateTuneOffset2(); break;
      case kParamMixOsc1:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mMixOsc1 = (T) value / 100.;
        });
        break;
      case kParamMixOsc2:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mMixOsc2 = (T) value / 100.;
        });
        break;
      case kParamMixNoise:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mMixNoise = (T) value / 100.;
        });
        break;
      case kParamFilterCutoff:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mFilterCutoff = (T) value;
        });
        break;
      case kParamFilterResonance:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          T q = (T) 0.5 + ((T) value / 100.) * (T) 19.5;
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mFilterQ = q;
        });
        break;
      case kParamFilterType:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mFilterType = (T) value;
        });
        break;
      // kParamFilterSlope: removed - BP/HP is now unconditionally 24dB (see
      // ProcessSamplesAccumulating). Left the enum slot itself alone (not
      // deleted/renumbered) to avoid shifting every later param's index and
      // corrupting saved presets/host automation - same "never renumber"
      // convention as kParamFilterType above.
      case kParamFilterEnvAmount:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mFilterEnvAmount = (T) value;
        });
        break;
      case kParamFilterKeyFollow:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mFilterKeyFollow = (T) value / 100.;
        });
        break;
      case kParamHPFCutoff:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mHPFCutoff = (T) value;
        });
        break;
      case kParamYuragi:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mYuragiRate = (T) value / 100.;
        });
        break;
      case kParamVelocityCurve:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mVelocityCurve = (T) value / 100.;
        });
        break;
      case kParamOscDrift:
        mSynth.ForEachVoice([value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mOscDrift = (T) value / 100.;
        });
        break;
      case kParamFilterSustain:
        mParamsToSmooth[kModFilterSustainSmoother] = (T) value / 100.;
        break;
      case kParamFilterAttack:
      case kParamFilterDecay:
      case kParamFilterRelease:
      {
        EEnvStage stage = static_cast<EEnvStage>(EEnvStage::kAttack + (paramIdx - kParamFilterAttack));
        mSynth.ForEachVoice([stage, value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mFilterEnv.SetStageTime(stage, value);
        });
        break;
      }

      // Modulation Matrix (2026-07-28) - see EMatrixSource/EMatrixDest and
      // ProcessSamplesAccumulating's routing logic near the top of this file.
      // No Depth param exists for these two LFOs (see the constructor's own
      // comment) - only Shape/Rate/Sync, unlike Pitch/Filter/Amp LFO.
      case kParamModLFO1RateTempo:
        mModLFO1.SetQNScalarFromDivision(static_cast<int>(value));
        break;
      case kParamModLFO1RateHz:
        mModLFO1.SetFreqCPS(value);
        break;
      case kParamModLFO1RateMode:
        mModLFO1.SetRateMode(value > 0.5);
        break;
      case kParamModLFO1Shape:
        mModLFO1.SetShape(static_cast<int>(value));
        break;
      case kParamModLFO2RateTempo:
        mModLFO2.SetQNScalarFromDivision(static_cast<int>(value));
        break;
      case kParamModLFO2RateHz:
        mModLFO2.SetFreqCPS(value);
        break;
      case kParamModLFO2RateMode:
        mModLFO2.SetRateMode(value > 0.5);
        break;
      case kParamModLFO2Shape:
        mModLFO2.SetShape(static_cast<int>(value));
        break;
      case kParamModEnv1Sustain:
        mParamsToSmooth[kModModEnv1SustainSmoother] = (T) value / 100.;
        break;
      case kParamModEnv1Attack:
      case kParamModEnv1Decay:
      case kParamModEnv1Release:
      {
        EEnvStage stage = static_cast<EEnvStage>(EEnvStage::kAttack + (paramIdx - kParamModEnv1Attack));
        mSynth.ForEachVoice([stage, value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mModEnv1.SetStageTime(stage, value);
        });
        break;
      }
      case kParamModEnv2Sustain:
        mParamsToSmooth[kModModEnv2SustainSmoother] = (T) value / 100.;
        break;
      case kParamModEnv2Attack:
      case kParamModEnv2Decay:
      case kParamModEnv2Release:
      {
        EEnvStage stage = static_cast<EEnvStage>(EEnvStage::kAttack + (paramIdx - kParamModEnv2Attack));
        mSynth.ForEachVoice([stage, value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mModEnv2.SetStageTime(stage, value);
        });
        break;
      }
      // Source/Dest are InitEnum params - SetParam() always receives the real
      // (denormalized) enum index here, same convention kParamLFOShape/
      // kParamFilterType etc. above already rely on.
      case kParamMatrix1Source:
      case kParamMatrix2Source:
      case kParamMatrix3Source:
      case kParamMatrix4Source:
      case kParamMatrix5Source:
      case kParamMatrix6Source:
      case kParamMatrix7Source:
      case kParamMatrix8Source:
      {
        int slot = (paramIdx - kParamMatrix1Source) / 3;
        int v = static_cast<int>(value);
        mSynth.ForEachVoice([slot, v](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mMatrixSource[slot] = v;
        });
        break;
      }
      case kParamMatrix1Dest:
      case kParamMatrix2Dest:
      case kParamMatrix3Dest:
      case kParamMatrix4Dest:
      case kParamMatrix5Dest:
      case kParamMatrix6Dest:
      case kParamMatrix7Dest:
      case kParamMatrix8Dest:
      {
        int slot = (paramIdx - kParamMatrix1Dest) / 3;
        int v = static_cast<int>(value);
        mSynth.ForEachVoice([slot, v](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mMatrixDest[slot] = v;
        });
        break;
      }
      case kParamMatrix1Amount:
      case kParamMatrix2Amount:
      case kParamMatrix3Amount:
      case kParamMatrix4Amount:
      case kParamMatrix5Amount:
      case kParamMatrix6Amount:
      case kParamMatrix7Amount:
      case kParamMatrix8Amount:
      {
        int slot = (paramIdx - kParamMatrix1Amount) / 3;
        mSynth.ForEachVoice([slot, value](SynthVoice& voice) {
          dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mMatrixAmount[slot] = (T) value;
        });
        break;
      }
      default:
        break;
    }
  }

public:
  MidiSynth mSynth { VoiceAllocator::kPolyModePoly, MidiSynth::kDefaultBlockSize };
  WDL_TypedBuf<T> mModulationsData; // Sample data for global modulations (e.g. smoothed sustain)
  WDL_PtrList<T> mModulations; // Ptrlist for global modulations
  LogParamSmooth<T, kNumModulations> mParamSmoother;
  sample mParamsToSmooth[kNumModulations];
  LFO<T> mPitchLFO;
  LFO<T> mFilterLFO;
  LFO<T> mAmpLFO;
  LFO<T> mModLFO1; // Modulation Matrix - free LFO 1, see EMatrixSource/EMatrixDest
  LFO<T> mModLFO2; // Modulation Matrix - free LFO 2

private:
  double mOsc1Octave = 0., mOsc1Semi = 0., mOsc1Fine = 0.;
  double mOsc2Octave = 0., mOsc2Semi = 0., mOsc2Fine = 0.;

  void UpdateTuneOffset1()
  {
    T offset = (T) (mOsc1Octave + (mOsc1Semi / 12.) + (mOsc1Fine / 1200.));
    mSynth.ForEachVoice([offset](SynthVoice& voice) {
      dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mTuneOctaves1 = offset;
    });
  }

  void UpdateTuneOffset2()
  {
    T offset = (T) (mOsc2Octave + (mOsc2Semi / 12.) + (mOsc2Fine / 1200.));
    mSynth.ForEachVoice([offset](SynthVoice& voice) {
      dynamic_cast<IPlugInstrumentDSP::Voice&>(voice).mTuneOctaves2 = offset;
    });
  }
};

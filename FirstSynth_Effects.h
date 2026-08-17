#pragma once

#include <cmath>
#include <vector>
#include <algorithm>

// Master-bus effects chain (Bass Boost -> Chorus -> Delay -> Reverb -> 5-band EQ),
// applied to the final stereo mix in FirstSynth::ProcessBlock, after the synth voices
// and CC7 volume. Deliberately simple implementations (linear-interpolated modulated
// delay for chorus, a feedback delay line for delay, a small Schroeder-style
// comb+allpass network for reverb, standard RBJ cookbook biquads for the EQ) rather
// than anything fancier - matches the rest of the DSP in this project.

// A single biquad stage, transposed Direct Form II (2 state vars, numerically nicer
// than Direct Form I) - just holds coefficients + state; coefficient math for each
// filter type lives in the Calc*Coeffs functions below, kept separate so the same
// filter shell is reused for shelf and peaking bands (and BassBoostEffect below)
// alike. Moved above BassBoostEffect (was originally only above ParametricEQEffect,
// added later) since it's now used by both.
template<typename T>
class BiquadFilter
{
public:
  void SetCoeffs(T b0, T b1, T b2, T a1, T a2)
  {
    mB0 = b0; mB1 = b1; mB2 = b2; mA1 = a1; mA2 = a2;
  }

  T Process(T x)
  {
    T y = mB0 * x + mZ1;
    mZ1 = mB1 * x - mA1 * y + mZ2;
    mZ2 = mB2 * x - mA2 * y;
    return y;
  }

private:
  T mB0 = 1., mB1 = 0., mB2 = 0., mA1 = 0., mA2 = 0.;
  T mZ1 = 0., mZ2 = 0.;
};

// Standard RBJ Audio Cookbook biquad coefficient formulas (normalized so a0 == 1).
// Shelf bands use a fixed S=1 shelf slope (a single reasonable "musical" slope,
// matching this project's "deliberately simple" DSP style rather than exposing a
// 6th per-band param) - at S=1, the cookbook's alpha reduces to a constant
// sin(w0)/2*sqrt(2), independent of gain.
template<typename T>
void CalcPeakingCoeffs(T freq, T gainDb, T q, T sampleRate, T& b0, T& b1, T& b2, T& a1, T& a2)
{
  T A = std::pow((T) 10., gainDb / (T) 40.);
  T w0 = (T) 2. * (T) 3.14159265358979323846 * freq / sampleRate;
  T cosw0 = std::cos(w0);
  T alpha = std::sin(w0) / ((T) 2. * q);

  T rb0 = (T) 1. + alpha * A;
  T rb1 = (T) -2. * cosw0;
  T rb2 = (T) 1. - alpha * A;
  T ra0 = (T) 1. + alpha / A;
  T ra1 = (T) -2. * cosw0;
  T ra2 = (T) 1. - alpha / A;

  b0 = rb0 / ra0; b1 = rb1 / ra0; b2 = rb2 / ra0;
  a1 = ra1 / ra0; a2 = ra2 / ra0;
}

template<typename T>
void CalcLowShelfCoeffs(T freq, T gainDb, T sampleRate, T& b0, T& b1, T& b2, T& a1, T& a2)
{
  T A = std::pow((T) 10., gainDb / (T) 40.);
  T w0 = (T) 2. * (T) 3.14159265358979323846 * freq / sampleRate;
  T cosw0 = std::cos(w0);
  T sinw0 = std::sin(w0);
  T sqrtA = std::sqrt(A);
  T alpha = sinw0 * (T) 0.70710678118654752440; // sin(w0)/2 * sqrt(2), S=1

  T rb0 =        A * ((A + (T) 1.) - (A - (T) 1.) * cosw0 + (T) 2. * sqrtA * alpha);
  T rb1 = (T) 2. * A * ((A - (T) 1.) - (A + (T) 1.) * cosw0);
  T rb2 =        A * ((A + (T) 1.) - (A - (T) 1.) * cosw0 - (T) 2. * sqrtA * alpha);
  T ra0 =             (A + (T) 1.) + (A - (T) 1.) * cosw0 + (T) 2. * sqrtA * alpha;
  T ra1 = (T) -2. * ((A - (T) 1.) + (A + (T) 1.) * cosw0);
  T ra2 =             (A + (T) 1.) + (A - (T) 1.) * cosw0 - (T) 2. * sqrtA * alpha;

  b0 = rb0 / ra0; b1 = rb1 / ra0; b2 = rb2 / ra0;
  a1 = ra1 / ra0; a2 = ra2 / ra0;
}

template<typename T>
void CalcHighShelfCoeffs(T freq, T gainDb, T sampleRate, T& b0, T& b1, T& b2, T& a1, T& a2)
{
  T A = std::pow((T) 10., gainDb / (T) 40.);
  T w0 = (T) 2. * (T) 3.14159265358979323846 * freq / sampleRate;
  T cosw0 = std::cos(w0);
  T sinw0 = std::sin(w0);
  T sqrtA = std::sqrt(A);
  T alpha = sinw0 * (T) 0.70710678118654752440; // sin(w0)/2 * sqrt(2), S=1

  T rb0 =        A * ((A + (T) 1.) + (A - (T) 1.) * cosw0 + (T) 2. * sqrtA * alpha);
  T rb1 = (T) -2. * A * ((A - (T) 1.) + (A + (T) 1.) * cosw0);
  T rb2 =        A * ((A + (T) 1.) + (A - (T) 1.) * cosw0 - (T) 2. * sqrtA * alpha);
  T ra0 =             (A + (T) 1.) - (A - (T) 1.) * cosw0 + (T) 2. * sqrtA * alpha;
  T ra1 =  (T) 2. * ((A - (T) 1.) - (A + (T) 1.) * cosw0);
  T ra2 =             (A + (T) 1.) - (A - (T) 1.) * cosw0 - (T) 2. * sqrtA * alpha;

  b0 = rb0 / ra0; b1 = rb1 / ra0; b2 = rb2 / ra0;
  a1 = ra1 / ra0; a2 = ra2 / ra0;
}

// Bass Boost v2 (2026-07-27, replaced the original one-pole "add a lowpassed copy
// back on top" trick - see git history/progress.md for that version) - user found the
// old trick's effect too weak (~+6dB max at DC) and asked, after mentioning a
// (unconfirmed, hearsay - no source) recollection that some vintage synths' fuller
// low end came from an always-on bass boost, for it to be made stronger via two
// combined techniques: (1) a real RBJ low-shelf filter (reusing BiquadFilter/
// CalcLowShelfCoeffs above, same math the 5-band EQ's own Low Shelf band uses) for a
// properly-defined, much stronger boost (0-15dB, matching the EQ's own gain range,
// vs. the old trick's soft ~6dB ceiling), and (2) light saturation applied *only* to
// the shelf's own boosted difference (input - shelfOutput isolates just what the
// shelf added, which is concentrated at low frequencies since a shelf leaves highs
// essentially unchanged) - mirrors how real analog "bass boost" circuits often aren't
// a clean gain bump alone, the circuit driving into mild saturation adds some of the
// perceived low-end "thickness" too. At Amount=0 the shelf gain is 0dB, so the
// isolated difference is ~0 and saturation has nothing to act on - exact bypass,
// same as the old version. Frequency stays fixed at 150Hz (user confirmed no need to
// make it adjustable).
template<typename T>
class BassBoostEffect
{
public:
  void SetSampleRate(double sampleRate)
  {
    mSampleRate = sampleRate;
    RecalcCoeffs();
  }

  void SetAmount(T amount01)
  {
    mAmount = amount01;
    RecalcCoeffs();
  }

  void Process(T& l, T& r)
  {
    T shelfL = mFilterL.Process(l);
    T shelfR = mFilterR.Process(r);

    // isolate just what the shelf added (concentrated at low frequencies) and
    // saturate only that part, not the whole signal
    T boostL = shelfL - l;
    T boostR = shelfR - r;
    T driven = (T) 1. + mAmount * (T) 3.; // more Amount = harder into saturation
    boostL = std::tanh(boostL * driven) / driven;
    boostR = std::tanh(boostR * driven) / driven;

    l += boostL;
    r += boostR;
  }

private:
  void RecalcCoeffs()
  {
    T b0, b1, b2, a1, a2;
    constexpr T kCutoffHz = (T) 150.;
    T gainDb = mAmount * (T) 15.; // 0-100% -> 0-15dB, matches the 5-band EQ's own range
    CalcLowShelfCoeffs<T>(kCutoffHz, gainDb, (T) mSampleRate, b0, b1, b2, a1, a2);
    mFilterL.SetCoeffs(b0, b1, b2, a1, a2);
    mFilterR.SetCoeffs(b0, b1, b2, a1, a2);
  }

  double mSampleRate = 44100.;
  T mAmount = 0.;
  BiquadFilter<T> mFilterL, mFilterR;
};

template<typename T>
class DelayEffect
{
public:
  void SetSampleRate(double sampleRate)
  {
    mSampleRate = sampleRate;
    mBufferL.assign((size_t) (sampleRate * 2.1) + 1, (T) 0.); // max 2s delay
    mBufferR.assign(mBufferL.size(), (T) 0.);
    mWritePos = 0;
  }

  void SetTimeMs(T ms) { mTimeMs = ms; }
  void SetFeedback(T feedback) { mFeedback = feedback; }
  void SetMix(T mix) { mMix = mix; }
  void SetPingPong(bool pingPong) { mPingPong = pingPong; }

  void Process(T& l, T& r)
  {
    int size = (int) mBufferL.size();
    int delaySamples = std::min((int) (mTimeMs * (T) 0.001 * mSampleRate), size - 1);
    int readPos = (mWritePos - delaySamples + size) % size;

    T delayedL = mBufferL[readPos];
    T delayedR = mBufferR[readPos];

    if (mPingPong)
    {
      // true ping-pong needs an asymmetric feed: the dry signal enters *only* the left
      // line, and the right line only ever receives the left line's feedback (cross-fed).
      // With the synth's mono (L==R) source, feeding both lines identically - even with
      // feedback swapped - just makes both lines stay identical forever (swapping two
      // equal values is a no-op), which is why it wasn't audibly bouncing. Feeding dry
      // into one side only breaks that symmetry: verified by impulse response that this
      // alternates L, R, L, R each repeat with feedback-scaled amplitude.
      T dryMono = (l + r) * (T) 0.5;
      mBufferL[mWritePos] = dryMono + delayedR * mFeedback;
      mBufferR[mWritePos] = delayedL * mFeedback;
    }
    else
    {
      mBufferL[mWritePos] = l + delayedL * mFeedback;
      mBufferR[mWritePos] = r + delayedR * mFeedback;
    }

    l = l * ((T) 1. - mMix) + delayedL * mMix;
    r = r * ((T) 1. - mMix) + delayedR * mMix;

    mWritePos = (mWritePos + 1) % size;
  }

private:
  double mSampleRate = 44100.;
  T mTimeMs = 300.;
  T mFeedback = 0.3;
  T mMix = 0.3;
  bool mPingPong = false;
  std::vector<T> mBufferL, mBufferR;
  int mWritePos = 0;
};

template<typename T>
class ChorusEffect
{
public:
  void SetSampleRate(double sampleRate)
  {
    mSampleRate = sampleRate;
    mBufferL.assign((size_t) (sampleRate * 0.06) + 1, (T) 0.); // 60ms max, modulated delay stays well under this
    mBufferR.assign(mBufferL.size(), (T) 0.);
    mWritePos = 0;
  }

  void SetRateHz(T hz) { mRateHz = hz; }
  void SetDepth(T depth01) { mDepth = depth01; }
  void SetMix(T mix) { mMix = mix; }

  void Process(T& l, T& r)
  {
    T lfo = (T) 0.5 + (T) 0.5 * std::sin(mPhase * (T) 6.283185307179586);
    mPhase += mRateHz / mSampleRate;
    if (mPhase >= (T) 1.) mPhase -= (T) 1.;

    T modMs = (T) 2. + mDepth * (T) 18.; // 2ms base .. up to 20ms swing at full depth
    T delaySamplesL = modMs * (T) 0.001 * (T) mSampleRate * lfo;
    T delaySamplesR = delaySamplesL + (T) 2.; // small offset between channels for stereo width

    T delayedL = ReadInterp(mBufferL, delaySamplesL);
    T delayedR = ReadInterp(mBufferR, delaySamplesR);

    mBufferL[mWritePos] = l;
    mBufferR[mWritePos] = r;
    mWritePos = (mWritePos + 1) % (int) mBufferL.size();

    l = l * ((T) 1. - mMix) + delayedL * mMix;
    r = r * ((T) 1. - mMix) + delayedR * mMix;
  }

private:
  // 2026-08-16: found via a live crash dump (Studio One, "vector subscript out of
  // range" debug assertion) that this function could index mBufferL/mBufferR out
  // of bounds - root-caused to a real gap in the wrap-around math, not just a
  // theoretical worry: only the *lower* bound was ever re-wrapped
  // (`while (readPos < 0) readPos += size`), with no matching upper-bound wrap,
  // and no clamp on the final `(int)readPos` cast. Under the normal Depth/Rate
  // param ranges `delaySamples` should always stay comfortably under `size` (the
  // buffer is sized for up to 60ms, delay only ever modulates up to ~20ms+2
  // samples), so this was never expected to actually be reached - but DAWs call
  // OnReset() (and therefore SetSampleRate(), which reallocates mBufferL/mBufferR)
  // far more often than Standalone does (every transport start/stop, unrelated
  // project changes, etc. - see this project's own window-resize investigation
  // notes), and this Chorus runs unconditionally on the master bus regardless of
  // notes - exactly matching the reported "crashes even while completely idle"
  // symptom. Added a proper symmetric upper-bound wrap (matching the pattern
  // already used correctly elsewhere in this codebase, e.g. the Looper's own
  // mPlayPos wrap in FirstSynth_Looper.h) plus a hard final clamp as a backstop -
  // this makes the function correct regardless of the exact underlying trigger
  // (parameter edge case, floating-point rounding at the boundary, or a possible
  // race between SetSampleRate() reallocating the buffer and Process() reading it
  // - not fully ruled out, but out of scope for this fix).
  T ReadInterp(const std::vector<T>& buf, T delaySamples)
  {
    int size = (int) buf.size();
    if (size <= 0) return (T) 0.;
    T readPos = (T) mWritePos - delaySamples;
    while (readPos < 0) readPos += (T) size;
    while (readPos >= (T) size) readPos -= (T) size;
    int i0 = (int) readPos;
    if (i0 >= size) i0 = size - 1;
    int i1 = (i0 + 1) % size;
    T frac = readPos - (T) i0;
    return buf[i0] * ((T) 1. - frac) + buf[i1] * frac;
  }

  double mSampleRate = 44100.;
  T mRateHz = 0.5;
  T mDepth = 0.5;
  T mMix = 0.3;
  std::vector<T> mBufferL, mBufferR;
  int mWritePos = 0;
  T mPhase = 0.;
};

// Single feedback comb filter with a one-pole damping filter in the feedback path
// (the classic Freeverb-style "damping" control - absorbs high frequencies each pass).
template<typename T>
class DampedCombFilter
{
public:
  void SetSampleRate(double sampleRate, T delayMs)
  {
    mBuffer.assign((size_t) (sampleRate * delayMs * (T) 0.001) + 1, (T) 0.);
    mIdx = 0;
    mFilterStore = 0.;
  }

  void SetFeedback(T feedback) { mFeedback = feedback; }
  void SetDamping(T damping) { mDamping = damping; }

  T Process(T input)
  {
    T output = mBuffer[mIdx];
    mFilterStore = output * ((T) 1. - mDamping) + mFilterStore * mDamping;
    mBuffer[mIdx] = input + mFilterStore * mFeedback;
    mIdx = (mIdx + 1) % mBuffer.size();
    return output;
  }

private:
  std::vector<T> mBuffer;
  size_t mIdx = 0;
  T mFeedback = 0.5;
  T mDamping = 0.5;
  T mFilterStore = 0.;
};

template<typename T>
class AllpassFilter
{
public:
  void SetSampleRate(double sampleRate, T delayMs)
  {
    mBuffer.assign((size_t) (sampleRate * delayMs * (T) 0.001) + 1, (T) 0.);
    mIdx = 0;
  }

  T Process(T input)
  {
    T bufOut = mBuffer[mIdx];
    T output = -input + bufOut;
    mBuffer[mIdx] = input + bufOut * (T) 0.5;
    mIdx = (mIdx + 1) % mBuffer.size();
    return output;
  }

private:
  std::vector<T> mBuffer;
  size_t mIdx = 0;
};

// Small Schroeder-style reverb: 4 parallel damped combs + 2 series allpass filters per
// channel, with slightly offset delay times between L/R for stereo width.
template<typename T>
class ReverbEffect
{
public:
  void SetSampleRate(double sampleRate)
  {
    mSampleRate = sampleRate;
    static const T kCombMsL[4]     = { 29.7, 37.1, 41.1, 43.7 };
    static const T kCombMsR[4]     = { 30.3, 37.8, 41.9, 44.5 };
    static const T kAllpassMsL[2]  = { 5.0, 1.7 };
    static const T kAllpassMsR[2]  = { 5.1, 1.8 };

    for (int i = 0; i < 4; i++)
    {
      mCombL[i].SetSampleRate(sampleRate, kCombMsL[i]);
      mCombR[i].SetSampleRate(sampleRate, kCombMsR[i]);
    }
    for (int i = 0; i < 2; i++)
    {
      mAllpassL[i].SetSampleRate(sampleRate, kAllpassMsL[i]);
      mAllpassR[i].SetSampleRate(sampleRate, kAllpassMsR[i]);
    }
  }

  void SetDecay(T decay01)
  {
    T feedback = (T) 0.7 + decay01 * (T) 0.28; // stays comfortably < 1 for stability
    for (auto& c : mCombL) c.SetFeedback(feedback);
    for (auto& c : mCombR) c.SetFeedback(feedback);
  }

  void SetDamping(T damping01)
  {
    for (auto& c : mCombL) c.SetDamping(damping01);
    for (auto& c : mCombR) c.SetDamping(damping01);
  }

  void SetMix(T mix) { mMix = mix; }

  void Process(T& l, T& r)
  {
    T inL = l, inR = r;
    T wetL = 0., wetR = 0.;

    for (auto& c : mCombL) wetL += c.Process(inL);
    for (auto& c : mCombR) wetR += c.Process(inR);
    wetL *= (T) 0.25;
    wetR *= (T) 0.25;

    for (auto& a : mAllpassL) wetL = a.Process(wetL);
    for (auto& a : mAllpassR) wetR = a.Process(wetR);

    l = inL * ((T) 1. - mMix) + wetL * mMix;
    r = inR * ((T) 1. - mMix) + wetR * mMix;
  }

private:
  double mSampleRate = 44100.;
  T mMix = 0.3;
  DampedCombFilter<T> mCombL[4], mCombR[4];
  AllpassFilter<T> mAllpassL[2], mAllpassR[2];
};

// 5-band parametric EQ - Low Shelf, 3x Peaking/Bell, High Shelf, in series. Placed
// *last* in the master effects chain (after Reverb) per the user's request to add it
// "at the end of the effects connection" - shapes the final tonal balance including
// the reverb tail, not just the dry synth signal. Each band recalculates its own
// coefficients independently whenever one of its params changes (freq/gain/Q are
// cheap to renormalize; nothing here runs per-sample except Process() itself).
template<typename T>
class ParametricEQEffect
{
public:
  static constexpr int kNumBands = 5;

  void SetSampleRate(double sampleRate)
  {
    mSampleRate = sampleRate;
    for (int i = 0; i < kNumBands; i++) RecalcBand(i);
  }

  void SetFreq(int band, T freqHz) { mFreq[band] = freqHz; RecalcBand(band); }
  void SetGainDb(int band, T gainDb) { mGainDb[band] = gainDb; RecalcBand(band); }
  void SetQ(int band, T q) { mQ[band] = q; RecalcBand(band); }

  void Process(T& l, T& r)
  {
    for (int i = 0; i < kNumBands; i++)
    {
      l = mFilterL[i].Process(l);
      r = mFilterR[i].Process(r);
    }
  }

private:
  void RecalcBand(int band)
  {
    T b0, b1, b2, a1, a2;
    T sr = (T) mSampleRate;
    // same Nyquist safety margin convention as the per-voice filter in FirstSynth_DSP.h
    T freq = std::min(mFreq[band], sr * (T) 0.49);

    if (band == 0)
      CalcLowShelfCoeffs<T>(freq, mGainDb[band], sr, b0, b1, b2, a1, a2);
    else if (band == kNumBands - 1)
      CalcHighShelfCoeffs<T>(freq, mGainDb[band], sr, b0, b1, b2, a1, a2);
    else
      CalcPeakingCoeffs<T>(freq, mGainDb[band], mQ[band], sr, b0, b1, b2, a1, a2);

    mFilterL[band].SetCoeffs(b0, b1, b2, a1, a2);
    mFilterR[band].SetCoeffs(b0, b1, b2, a1, a2);
  }

  double mSampleRate = 44100.;
  T mFreq[kNumBands]   = { (T) 100., (T) 300., (T) 1000., (T) 3000., (T) 8000. };
  T mGainDb[kNumBands] = { (T) 0., (T) 0., (T) 0., (T) 0., (T) 0. };
  T mQ[kNumBands]      = { (T) 0.7, (T) 0.7, (T) 0.7, (T) 0.7, (T) 0.7 }; // unused for shelf bands (0, 4)
  BiquadFilter<T> mFilterL[kNumBands], mFilterR[kNumBands];
};

// Mirrors FirstSynth_Effects.h's ParametricEQEffect coefficient math (RBJ Audio
// Cookbook biquads) purely for visualization - draws the combined magnitude response
// of all 5 bands (Low Shelf, 3x Peaking, High Shelf) across 20Hz-20kHz. Kept in sync
// manually with the C++ side, same as waveform-display.js does for oscillator shapes -
// any change to the DSP's coefficient formulas must be hand-mirrored here too.
//
// Uses a fixed reference sample rate rather than the real host rate: this is a
// visualization approximation only (the actual DSP always uses the real sample rate),
// and the error is negligible except very close to Nyquist, where shelf/peak bands
// aren't typically placed anyway - matches this project's "deliberately simple" style.
function calcPeakingCoeffs(freq, gainDb, q, sampleRate) {
  const A = Math.pow(10, gainDb / 40);
  const w0 = 2 * Math.PI * freq / sampleRate;
  const cosw0 = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * q);

  const rb0 = 1 + alpha * A;
  const rb1 = -2 * cosw0;
  const rb2 = 1 - alpha * A;
  const ra0 = 1 + alpha / A;
  const ra1 = -2 * cosw0;
  const ra2 = 1 - alpha / A;

  return { b0: rb0 / ra0, b1: rb1 / ra0, b2: rb2 / ra0, a1: ra1 / ra0, a2: ra2 / ra0 };
}

function calcLowShelfCoeffs(freq, gainDb, sampleRate) {
  const A = Math.pow(10, gainDb / 40);
  const w0 = 2 * Math.PI * freq / sampleRate;
  const cosw0 = Math.cos(w0);
  const sinw0 = Math.sin(w0);
  const sqrtA = Math.sqrt(A);
  const alpha = sinw0 * 0.70710678118654752440; // sin(w0)/2 * sqrt(2), S=1

  const rb0 =     A * ((A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alpha);
  const rb1 = 2 * A * ((A - 1) - (A + 1) * cosw0);
  const rb2 =     A * ((A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alpha);
  const ra0 =         (A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alpha;
  const ra1 =    -2 * ((A - 1) + (A + 1) * cosw0);
  const ra2 =         (A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alpha;

  return { b0: rb0 / ra0, b1: rb1 / ra0, b2: rb2 / ra0, a1: ra1 / ra0, a2: ra2 / ra0 };
}

function calcHighShelfCoeffs(freq, gainDb, sampleRate) {
  const A = Math.pow(10, gainDb / 40);
  const w0 = 2 * Math.PI * freq / sampleRate;
  const cosw0 = Math.cos(w0);
  const sinw0 = Math.sin(w0);
  const sqrtA = Math.sqrt(A);
  const alpha = sinw0 * 0.70710678118654752440; // sin(w0)/2 * sqrt(2), S=1

  const rb0 =      A * ((A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alpha);
  const rb1 = -2 * A * ((A - 1) + (A + 1) * cosw0);
  const rb2 =      A * ((A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alpha);
  const ra0 =          (A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alpha;
  const ra1 =      2 * ((A - 1) - (A + 1) * cosw0);
  const ra2 =          (A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alpha;

  return { b0: rb0 / ra0, b1: rb1 / ra0, b2: rb2 / ra0, a1: ra1 / ra0, a2: ra2 / ra0 };
}

// |H(e^jw)| in dB, evaluated directly from the complex transfer function (z^-1 =
// cos(w) - j*sin(w), z^-2 = cos(2w) - j*sin(2w)) rather than a closed-form magnitude
// formula - easier to verify against the coefficient math above by inspection.
function biquadMagDb(c, w) {
  const cw = Math.cos(w), sw = Math.sin(w);
  const c2w = Math.cos(2 * w), s2w = Math.sin(2 * w);
  const numRe = c.b0 + c.b1 * cw + c.b2 * c2w;
  const numIm = -c.b1 * sw - c.b2 * s2w;
  const denRe = 1 + c.a1 * cw + c.a2 * c2w;
  const denIm = -c.a1 * sw - c.a2 * s2w;
  const numMag2 = numRe * numRe + numIm * numIm;
  const denMag2 = Math.max(denRe * denRe + denIm * denIm, 1e-12);
  const mag = Math.sqrt(numMag2 / denMag2);
  return 20 * Math.log10(Math.max(mag, 1e-6));
}

// Custom UI widget, no interaction of its own - purely a readout, same pattern as
// waveform-display.js (see that file's own comment). Band index 0 = Low Shelf,
// 1-3 = Peaking, 4 = High Shelf, matching ParametricEQEffect's band layout exactly.
class EQCurveDisplay extends HTMLElement {
  static REF_SAMPLE_RATE = 48000;
  static MIN_DB = -18;
  static MAX_DB = 18;
  static MIN_FREQ = 20;
  static MAX_FREQ = 20000;

  constructor() {
    super();

    this.freq = [100, 300, 1000, 3000, 8000];
    this.gainDb = [0, 0, 0, 0, 0];
    this.q = [0.7, 0.7, 0.7, 0.7, 0.7]; // unused for bands 0/4 (shelf)

    this.attachShadow({ mode: 'open' });
    this.shadowRoot.innerHTML = `
    <style>
      :host {
        display: block;
      }
      canvas {
        display: block;
        width: 100%;
        height: 100%;
        background-color: var(--surface-subtle, #eef1f5);
        border-radius: 8px;
      }
    </style>
    <canvas width="600" height="150"></canvas>
    `;

    this.canvas = this.shadowRoot.querySelector('canvas');
    this.ctx = this.canvas.getContext('2d');
    this.draw();
  }

  setFreq(band, hz) { this.freq[band] = hz; this.draw(); }
  setGain(band, db) { this.gainDb[band] = db; this.draw(); }
  setQ(band, q) { this.q[band] = q; this.draw(); }

  bandCoeffs(band, sampleRate) {
    if (band === 0) return calcLowShelfCoeffs(this.freq[0], this.gainDb[0], sampleRate);
    if (band === 4) return calcHighShelfCoeffs(this.freq[4], this.gainDb[4], sampleRate);
    return calcPeakingCoeffs(this.freq[band], this.gainDb[band], this.q[band], sampleRate);
  }

  // Summing each band's dB magnitude is exact (not an approximation) for cascaded
  // biquads: |H1*H2| = |H1|*|H2|, so dB(H1*H2) = dB(H1) + dB(H2).
  totalDbAt(freqHz) {
    const sr = EQCurveDisplay.REF_SAMPLE_RATE;
    const w = 2 * Math.PI * freqHz / sr;
    let total = 0;
    for (let band = 0; band < 5; band++) {
      total += biquadMagDb(this.bandCoeffs(band, sr), w);
    }
    return total;
  }

  draw() {
    const { ctx, canvas } = this;
    const w = canvas.width, h = canvas.height;
    const { MIN_DB, MAX_DB, MIN_FREQ, MAX_FREQ } = EQCurveDisplay;
    const logRange = Math.log10(MAX_FREQ / MIN_FREQ);

    const dbToY = (db) => h - ((db - MIN_DB) / (MAX_DB - MIN_DB)) * h;
    const freqToX = (f) => (Math.log10(f / MIN_FREQ) / logRange) * w;

    ctx.clearRect(0, 0, w, h);

    // dB gridlines (0dB emphasized as the "flat" reference line)
    ctx.lineWidth = 1;
    [-12, -6, 0, 6, 12].forEach((db) => {
      const y = dbToY(db);
      ctx.strokeStyle = db === 0
        ? (getComputedStyle(this).getPropertyValue('--muted-text') || '#8a94a3')
        : (getComputedStyle(this).getPropertyValue('--border') || '#c9ced6');
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
    });

    // combined response curve
    ctx.strokeStyle = getComputedStyle(this).getPropertyValue('--accent') || '#2563eb';
    ctx.lineWidth = 2;
    ctx.beginPath();
    const steps = 150;
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      const f = MIN_FREQ * Math.pow(MAX_FREQ / MIN_FREQ, t);
      const db = Math.max(MIN_DB, Math.min(MAX_DB, this.totalDbAt(f)));
      const x = freqToX(f);
      const y = dbToY(db);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();

    // per-band freq/gain markers - a quick visual anchor for which dot is which knob
    ctx.fillStyle = getComputedStyle(this).getPropertyValue('--accent') || '#2563eb';
    for (let band = 0; band < 5; band++) {
      const x = freqToX(Math.max(MIN_FREQ, Math.min(MAX_FREQ, this.freq[band])));
      const y = dbToY(Math.max(MIN_DB, Math.min(MAX_DB, this.gainDb[band])));
      ctx.beginPath();
      ctx.arc(x, y, 3, 0, 2 * Math.PI);
      ctx.fill();
    }
  }
}

customElements.define('eq-curve-display', EQCurveDisplay);

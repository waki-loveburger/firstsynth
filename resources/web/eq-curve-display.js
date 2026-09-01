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

// Custom UI widget - was a pure readout, same pattern as waveform-display.js,
// until 2026-08-25 added mouse/touch dragging of the per-band markers (user
// request). Band index 0 = Low Shelf, 1-3 = Peaking, 4 = High Shelf, matching
// ParametricEQEffect's band layout exactly.
class EQCurveDisplay extends HTMLElement {
  static REF_SAMPLE_RATE = 48000;
  // Axis range - deliberately wider than the real Gain param range below, so
  // a band sitting at its max/min doesn't render right at the very edge of
  // the chart.
  static MIN_DB = -18;
  static MAX_DB = 18;
  static MIN_FREQ = 20;
  static MAX_FREQ = 20000;
  // The *real* kParamEQ*Gain range (FirstSynth.cpp's InitDouble calls) -
  // dragging must clamp to this, not the wider axis range above, or it could
  // ask for a dB value the actual param can't represent.
  static PARAM_MIN_DB = -15;
  static PARAM_MAX_DB = 15;
  // 2026-08-25 user request: "ポイントをもう少し大きく表示してください" (make
  // the points a bit bigger) - also doubles as the pointer hit-test radius
  // (in the same canvas-internal 600x150 coordinate space the points are
  // drawn in), so bigger dots are also easier to actually grab.
  static POINT_RADIUS = 6;
  static POINT_HIT_RADIUS = 11;

  constructor() {
    super();

    this.freq = [100, 300, 1000, 3000, 8000];
    this.gainDb = [0, 0, 0, 0, 0];
    this.q = [0.7, 0.7, 0.7, 0.7, 0.7]; // unused for bands 0/4 (shelf)
    this.draggingBand = null;

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
        cursor: pointer;
        touch-action: none;
      }
    </style>
    <canvas width="600" height="150"></canvas>
    `;

    this.canvas = this.shadowRoot.querySelector('canvas');
    this.ctx = this.canvas.getContext('2d');
    this.canvas.addEventListener('pointerdown', (e) => this.onPointerDown(e));
    this.draw();
  }

  setFreq(band, hz) { this.freq[band] = hz; this.draw(); }
  setGain(band, db) { this.gainDb[band] = db; this.draw(); }
  setQ(band, q) { this.q[band] = q; this.draw(); }

  // Converts a pointer event's page-space position into this.canvas's own
  // internal 600x150 coordinate space - the canvas's *displayed* CSS size
  // (width:100%/height:100%, see the <style> above) is very likely a
  // different physical pixel size than that internal buffer, so client
  // coordinates need rescaling before they mean anything here.
  getCanvasPoint(e) {
    const rect = this.canvas.getBoundingClientRect();
    const scaleX = this.canvas.width / rect.width;
    const scaleY = this.canvas.height / rect.height;
    return { x: (e.clientX - rect.left) * scaleX, y: (e.clientY - rect.top) * scaleY };
  }

  // Same freqToX/dbToY math as draw() below, duplicated rather than shared -
  // draw() computes them as closures over its own local w/h, and threading
  // those through as parameters everywhere read worse than the small
  // duplication.
  findBandAt(cx, cy) {
    const { MIN_DB, MAX_DB, MIN_FREQ, MAX_FREQ, POINT_HIT_RADIUS } = EQCurveDisplay;
    const w = this.canvas.width, h = this.canvas.height;
    const logRange = Math.log10(MAX_FREQ / MIN_FREQ);
    const dbToY = (db) => h - ((db - MIN_DB) / (MAX_DB - MIN_DB)) * h;
    const freqToX = (f) => (Math.log10(f / MIN_FREQ) / logRange) * w;

    let closestBand = null;
    let closestDist = POINT_HIT_RADIUS;
    for (let band = 0; band < 5; band++) {
      const x = freqToX(Math.max(MIN_FREQ, Math.min(MAX_FREQ, this.freq[band])));
      const y = dbToY(Math.max(MIN_DB, Math.min(MAX_DB, this.gainDb[band])));
      const dist = Math.hypot(cx - x, cy - y);
      if (dist < closestDist) {
        closestDist = dist;
        closestBand = band;
      }
    }
    return closestBand;
  }

  onPointerDown(e) {
    const { x, y } = this.getCanvasPoint(e);
    const band = this.findBandAt(x, y);
    if (band === null) return; // missed every point - not a drag, leave the click alone

    e.preventDefault();
    this.draggingBand = band;
    this.canvas.style.cursor = 'grabbing';
    // fired once per gesture (not per pointermove, unlike 'point-drag' below) -
    // lets index.html pair BPCFUI/EPCFUI around the whole drag, same "begin/end
    // gesture" contract knob-control.js's own startDrag()/onEnd() already
    // follow, so DAW automation-lane recording sees one gesture, not a storm of
    // disconnected single-value writes.
    this.dispatchEvent(new CustomEvent('point-drag-start', { detail: { band }, bubbles: true }));
    this.updateFromPointer(x, y, band);

    const onMove = (ev) => {
      const p = this.getCanvasPoint(ev);
      this.updateFromPointer(p.x, p.y, band);
    };
    const onUp = () => {
      document.removeEventListener('pointermove', onMove);
      document.removeEventListener('pointerup', onUp);
      document.removeEventListener('pointercancel', onUp);
      this.draggingBand = null;
      this.canvas.style.cursor = 'pointer';
      this.draw();
      this.dispatchEvent(new CustomEvent('point-drag-end', { detail: { band }, bubbles: true }));
    };
    // listens on document, not the canvas, same reasoning as knob-control.js's
    // own startDrag() - the pointer can move faster than the mouse stays over
    // this (fairly small) canvas while dragging
    document.addEventListener('pointermove', onMove);
    document.addEventListener('pointerup', onUp);
    document.addEventListener('pointercancel', onUp);
  }

  // cx/cy are in canvas-internal coordinates (see getCanvasPoint()) - clamped
  // to the chart's own bounds first (so dragging past an edge pins the point
  // there instead of extrapolating nonsense), then converted to real Hz/dB
  // and clamped again to the real param range (PARAM_MIN_DB/MAX_DB - the
  // chart's own axis is intentionally wider, see its own comment).
  updateFromPointer(cx, cy, band) {
    const { MIN_DB, MAX_DB, MIN_FREQ, MAX_FREQ, PARAM_MIN_DB, PARAM_MAX_DB } = EQCurveDisplay;
    const w = this.canvas.width, h = this.canvas.height;
    const logRange = Math.log10(MAX_FREQ / MIN_FREQ);

    const xClamped = Math.max(0, Math.min(w, cx));
    const yClamped = Math.max(0, Math.min(h, cy));

    const freq = MIN_FREQ * Math.pow(MAX_FREQ / MIN_FREQ, xClamped / w);
    const dbRaw = MAX_DB - (yClamped / h) * (MAX_DB - MIN_DB); // inverse of draw()'s dbToY

    this.freq[band] = Math.max(MIN_FREQ, Math.min(MAX_FREQ, freq));
    this.gainDb[band] = Math.max(PARAM_MIN_DB, Math.min(PARAM_MAX_DB, dbRaw));
    this.draw();

    // 'this', not an inner shadow-DOM node, so this already reaches listeners
    // outside the shadow root without needing composed:true - same pattern
    // knob-control.js's own 'user-change' event uses.
    this.dispatchEvent(new CustomEvent('point-drag', {
      detail: { band, freq: this.freq[band], gainDb: this.gainDb[band] },
      bubbles: true,
    }));
  }

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

    // per-band freq/gain markers - a quick visual anchor for which dot is which
    // knob, and draggable (see onPointerDown()/updateFromPointer() above). The
    // currently-dragged one (if any) gets a lighter fill + outline ring, so
    // there's clear feedback for which point is actually under the pointer.
    const accentColor = getComputedStyle(this).getPropertyValue('--accent') || '#2563eb';
    for (let band = 0; band < 5; band++) {
      const x = freqToX(Math.max(MIN_FREQ, Math.min(MAX_FREQ, this.freq[band])));
      const y = dbToY(Math.max(MIN_DB, Math.min(MAX_DB, this.gainDb[band])));
      const isDragging = this.draggingBand === band;

      ctx.beginPath();
      ctx.arc(x, y, EQCurveDisplay.POINT_RADIUS, 0, 2 * Math.PI);
      ctx.fillStyle = accentColor;
      ctx.fill();

      if (isDragging) {
        ctx.beginPath();
        ctx.arc(x, y, EQCurveDisplay.POINT_RADIUS + 3, 0, 2 * Math.PI);
        ctx.lineWidth = 2;
        ctx.strokeStyle = getComputedStyle(this).getPropertyValue('--surface') || '#ffffff';
        ctx.stroke();
      }
    }
  }
}

customElements.define('eq-curve-display', EQCurveDisplay);

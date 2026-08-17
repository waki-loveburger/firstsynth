// Mirrors FirstSynthOsc::Morph on the C++ side (FirstSynth_DSP.h) purely for
// visualization - draws ~2 cycles of the currently selected morphed waveform.
class WaveformDisplay extends HTMLElement {
  constructor() {
    super();

    this.value = 0; // 0-4, continuous Sine->Triangle->Saw->Square->Pulse

    this.attachShadow({ mode: 'open' });
    this.shadowRoot.innerHTML = `
    <style>
      :host {
        display: inline-block;
      }
      canvas {
        display: block;
        background-color: var(--surface-subtle, #eef1f5);
        border-radius: 6px;
      }
    </style>
    <canvas width="160" height="70"></canvas>
    `;

    this.canvas = this.shadowRoot.querySelector('canvas');
    this.ctx = this.canvas.getContext('2d');
    this.draw();
  }

  sine(phase) {
    return Math.sin(2 * Math.PI * phase);
  }

  // Phase-aligned to sine (zero at phase=0 rising) - see FirstSynth_DSP.h's
  // Triangle() comment for why (fixes a Sine<->Triangle crossfade amplitude dip).
  triangle(phase) {
    let t = phase + 0.25;
    t -= Math.floor(t);
    return t < 0.5 ? (4 * t - 1) : (3 - 4 * t);
  }

  // Phase-aligned to sine the same way - see FirstSynth_DSP.h's Saw() comment.
  saw(phase) {
    let t = phase + 0.5;
    t -= Math.floor(t);
    return 2 * t - 1;
  }

  // duty < 0.5 means "high" (+1) for less of the cycle (a narrow pulse) - this
  // polarity matches the now phase-aligned saw's hard-clip in morph()'s segment
  // 2, see FirstSynth_DSP.h's Pulse() comment for the full continuity reasoning.
  pulse(phase, duty) {
    const t = phase - Math.floor(phase);
    return t < duty ? 1 : -1;
  }

  // See FirstSynth_DSP.h's AsymTriangle() comment - generalizes Triangle/Saw as
  // a single 2-segment "rise then fall" shape with r = fraction of the cycle
  // spent rising (0.5 = triangle, 1.0 = saw), shearing continuously between
  // them with the rising zero-crossing pinned at phase=0 throughout.
  asymTriangle(phase, r) {
    const phi0 = 1 - r * 0.5;
    let u = phase - phi0;
    u -= Math.floor(u);
    if (u < r) return -1 + 2 * u / r;
    return 1 - 2 * (u - r) / (1 - r);
  }

  // Segment boundaries - see FirstSynth_DSP.h's matching constants comment
  // (non-uniform on purpose: a dwell/plateau at pure Saw around 2.0, more of
  // the knob's range devoted to the pulse-width sweep at the end).
  static SAW_START = 1.95;
  static SAW_END = 2.05;
  static SQUARE_END = 2.59;
  // 1/sqrt(3), the RMS of Saw/Triangle - see FirstSynth_DSP.h's
  // kMorphTargetRMS comment for why this is used as a loudness-compensation
  // target for the Saw->Square and Square->Pulse segments.
  static TARGET_RMS = 0.5773502691896258;

  morph(phase, waveShape) {
    waveShape = Math.max(0, Math.min(4, waveShape));
    const { SAW_START, SAW_END, SQUARE_END, TARGET_RMS } = WaveformDisplay;

    if (waveShape < 1) {
      const frac = waveShape;
      const a = this.sine(phase), b = this.triangle(phase);
      return a * (1 - frac) + b * frac;
    } else if (waveShape < SAW_START) {
      const frac = (waveShape - 1) / (SAW_START - 1);
      return this.asymTriangle(phase, 0.5 + 0.5 * frac);
    } else if (waveShape <= SAW_END) {
      return this.saw(phase);
    } else if (waveShape < SQUARE_END) {
      // saw -> square: progressively hard-clip the sawtooth itself - see
      // FirstSynth_DSP.h's matching comment for why k is derived from the
      // clipped/flat fraction (1 - 1/k) growing linearly with frac, instead of
      // k itself growing linearly (which saturated almost immediately).
      const frac = (waveShape - SAW_END) / (SQUARE_END - SAW_END);
      const kMax = 51;
      const k = 1 / (1 - frac * (kMax - 1) / kMax);
      const raw = Math.max(-1, Math.min(1, this.saw(phase) * k));
      // Loudness compensation - see FirstSynth_DSP.h's matching comment:
      // hard-clipping raises RMS from a saw's 1/sqrt(3) toward a square's 1.0,
      // a genuine ~+4.8dB even though peak amplitude never exceeds +-1.
      const rmsK = Math.sqrt(1 - 2 / (3 * k));
      return raw * (TARGET_RMS / rmsK);
    } else {
      // square -> rectangular: sweep the duty cycle of a single pulse. Pulse's
      // RMS is always exactly 1 regardless of duty, so apply the same flat
      // compensation gain the hard-clip segment above approaches at its own
      // endpoint (see that segment's comment) to avoid a level jump at the boundary.
      const frac = (waveShape - SQUARE_END) / (4 - SQUARE_END);
      const raw = this.pulse(phase, 0.5 - frac * 0.4); // 0.5 -> 0.1 duty
      return raw * TARGET_RMS;
    }
  }

  draw() {
    const { ctx, canvas } = this;
    const w = canvas.width;
    const h = canvas.height;
    const midY = h / 2;
    const amp = h * 0.4;
    const cycles = 2;

    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = 'var(--border, #c9ced6)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, midY);
    ctx.lineTo(w, midY);
    ctx.stroke();

    ctx.strokeStyle = getComputedStyle(this).getPropertyValue('--accent') || '#2563eb';
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let x = 0; x <= w; x++) {
      const phase = (x / w) * cycles;
      const y = midY - this.morph(phase, this.value) * amp;
      if (x === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }

  connectedCallback() {
    this.paramId = this.getAttribute('param-id') || -1;
  }

  updateValueFromHost(normalizedValue) {
    const min = parseFloat(this.getAttribute('min')) || 0;
    const max = parseFloat(this.getAttribute('max')) || 4;
    this.value = min + normalizedValue * (max - min);
    this.draw();
  }
}

customElements.define('waveform-display', WaveformDisplay);

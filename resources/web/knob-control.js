// Temporary diagnostic (2026-07-28) - user reports knobs sometimes don't respond
// to the very first grab attempt (no visible reaction at all), with no clear
// pattern, and the second attempt always works. The 2026-07-21 fix for a related
// "needed a second touch" bug (switching mousedown/touchstart -> pointerdown,
// see startDrag()'s own comment below) is still in place and doesn't fully
// explain this - checked the installed WebView2 Runtime version too (150.x, well
// past the one documented WebView2 focus-routing regression found for this class
// of symptom, which was fixed in 134.0.3124.68), so that specific known bug isn't
// the cause here either. This capturing-phase, document-level listener logs every
// pointerdown anywhere on the page (target tag/class, not just ones that reach a
// knob) exactly once - added here, at module scope, not inside the class/inside
// connectedCallback(), specifically so it fires once total rather than once per
// knob instance (this file backs ~70 knob elements on one page - a per-instance
// listener would print ~70 duplicate lines per click). Compare this line against
// whether startDrag() below's own log line fires for the SAME click, next time a
// grab silently fails - if this line fires but startDrag()'s doesn't, the
// pointerdown never reached the knob's own circle element at all (a hit-testing/
// focus-routing problem upstream of this file); if neither fires, the input never
// reached the page/WebView; if both fire but the knob still didn't visibly
// respond, the bug is somewhere in the drag-update logic itself, not event
// delivery. Remove once this is root-caused - search "Temporary diagnostic" in
// this file.
document.addEventListener('pointerdown', (e) => {
  console.log(`[knob-diag] document pointerdown: target=<${e.target.tagName}${e.target.className ? '.' + e.target.className : ''}> pointerType=${e.pointerType} t=${performance.now().toFixed(1)}`);
}, true /* capturing phase - fires before any other listener, even if something else stops propagation */);

class KnobControl extends HTMLElement {
  constructor() {
    super();
    
    this.paramId = 0;
    this.controlTag = '';
    this.defaultValue = 0.0;
    this.value = 0.0;

    this.label = this.getAttribute('label') || '';
    this.minValue = parseFloat(this.getAttribute('min')) || 0;
    this.maxValue = parseFloat(this.getAttribute('max')) || 100;
    this.shapeType = this.getAttribute('shape') || 'linear'; // 'linear' | 'pow' | 'log'
    this.shapeExponent = parseFloat(this.getAttribute('shape-exponent')) || 1;
    this.valueLabels = this.getAttribute('value-labels') ? this.getAttribute('value-labels').split('|') : null;
    this.isInteger = this.hasAttribute('integer-display');
    // total digit budget (integer digits + decimals combined) - decimals shrink as the
    // integer part grows, so e.g. with max-digits=4: "5.320" (1+3) at low values down to
    // "4000" (4+0) once the value itself needs all 4 digits. Unlike integer-display,
    // this only drops precision where the value's own magnitude demands it, not always.
    this.maxDigits = parseInt(this.getAttribute('max-digits')) || 0;
    // prepends "+" for zero/positive values (negative values already show their own
    // "-") - for bipolar params where the sign itself is the meaningful part, e.g. a
    // +/-% speed offset from "normal"
    this.signedDisplay = this.hasAttribute('signed-display');
    this.stepSize = parseFloat(this.getAttribute('step')) || 0;
    const units = this.getAttribute('units') || '';
    const minAngle = parseFloat(this.getAttribute('min-angle')) || -135;
    const maxAngle = parseFloat(this.getAttribute('max-angle')) || 135;
    // defaults are CSS var() expressions (not raw hex) so the knob's own dial colors
    // follow the host page's theme (e.g. dark mode, see index.html's ApplyDarkMode())
    // automatically via custom-property inheritance through the shadow boundary - an
    // explicit attribute on the tag still overrides this per-knob if ever needed.
    const circleStrokeColor = this.getAttribute('circle-stroke-color') || 'var(--knob-circle-stroke, #eef1f5)';
    const circleStrokeWidth = parseFloat(this.getAttribute('circle-stroke-width')) || 2;
    const circleFillColor = this.getAttribute('circle-fill-color') || 'var(--knob-circle-fill, #18202d)';
    const pointerColor = this.getAttribute('pointer-color') || 'var(--knob-pointer, #2563eb)';
    const pointerWidth = parseFloat(this.getAttribute('pointer-width')) || 4;
    const valueArcColor = this.getAttribute('value-arc-color') || 'var(--knob-value-arc, #2563eb)';
    const valueArcWidth = parseFloat(this.getAttribute('value-arc-width')) || 3;
    const trackBgColor = this.getAttribute('track-bg-color') || 'var(--knob-track-bg, #c9ced6)';
    
    this.attachShadow({ mode: 'open' });
    this.shadowRoot.innerHTML = `
    <style>
    :host {
      display: inline-block;
      color: var(--text, #18202d);
    }
    .container {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      width: 100%;
      height: 100%;
    }
    .label {
      margin-bottom: var(--knob-label-gap, 8px);
      color: var(--text, #18202d);
      font-size: 14px;
      font-weight: 600;
      pointer-events: none;
    }
    .value {
      margin-top: var(--knob-value-gap, 8px);
      color: var(--muted-text, #5d6675);
      font-size: 12px;
      min-height: 17px;
      text-align: center;
      white-space: nowrap;
      pointer-events: none;
    }
    svg {
      width: var(--knob-size, 80px);
      height: var(--knob-size, 80px);
    }
    .hidden-cursor {
      cursor: none;
    }
    </style>
    <div class="container">
    <div class="label">${this.label}</div>
    <svg viewBox="0 0 100 100" width="80" height="80">
    <circle cx="50" cy="50" r="42" fill="${circleFillColor}" stroke="${circleStrokeColor}" stroke-width=${circleStrokeWidth} @mousedown="startDrag"></circle>
    <path class="track-bg" fill="none" stroke="${trackBgColor}" stroke-width=${valueArcWidth} d=""></path>
    <path class="value-arc" fill="none" stroke="${valueArcColor}" stroke-width=${valueArcWidth} d=""></path>
    <line class="pointer" x1="50" y1="10" x2="50" y2="50" stroke="${pointerColor}" stroke-width=${pointerWidth} transform="rotate(0, 50, 50)"></line>
    </svg>
    <div class="value">0 ${units}</div>
    </div>
    `;
    
    const pointer = this.shadowRoot.querySelector('.pointer');
    const valueElement = this.shadowRoot.querySelector('.value');
    let currentNorm = 0; // normalized [0,1] - the single source of truth for drag/wheel feel

    const valueArc = this.shadowRoot.querySelector('.value-arc');
    const trackBg = this.shadowRoot.querySelector('.track-bg');

    const polarToCartesian = (centerX, centerY, radius, angleInDegrees) => {
      const angleInRadians = (angleInDegrees - 90) * Math.PI / 180.0;
      return {
        x: centerX + (radius * Math.cos(angleInRadians)),
        y: centerY + (radius * Math.sin(angleInRadians))
      };
    };
    
    const createTrackBg = () => {
      const arcRadius = 48;
      const start = polarToCartesian(50, 50, arcRadius, minAngle);
      const end = polarToCartesian(50, 50, arcRadius, maxAngle);
      
      const largeArcFlag = maxAngle - minAngle <= 180 ? 0 : 1;
      
      const d = [
        'M', start.x, start.y,
        'A', arcRadius, arcRadius, 0, largeArcFlag, 1, end.x, end.y
      ].join(' ');
      
      trackBg.setAttribute('d', d);
    };
    
    createTrackBg();
    
    // For a bipolar param (range genuinely straddles zero, e.g. -100..100) the arc
    // starts from the *zero angle*, not minAngle - so at the default/center value the
    // arc is empty (reads as "off"/neutral), and turning either direction fills an
    // arc growing outward from center. A knob whose range doesn't actually include 0
    // (plain unipolar like 0..100, or a range that's negative-only) keeps the
    // original always-from-minAngle behavior, since "zero-centered" doesn't apply -
    // realToNormalized(0) would be outside [0,1] (even NaN for some pow curves) for
    // those, so this is guarded, not just always-on.
    const updateValueArc = (angle) => {
      const isBipolar = this.minValue < 0 && this.maxValue > 0;
      const startAngle = isBipolar ? (minAngle + realToNormalized(0) * (maxAngle - minAngle)) : minAngle;
      const endAngle = angle;
      const arcRadius = 48;

      const start = polarToCartesian(50, 50, arcRadius, startAngle);
      const end = polarToCartesian(50, 50, arcRadius, endAngle);

      // sweep-flag must flip when the arc runs "backwards" from a centered start
      // (e.g. turning a bipolar knob below center) - a fixed sweep=1 only happened to
      // work before because start was always the lowest possible angle
      const sweepFlag = endAngle >= startAngle ? 1 : 0;
      const largeArcFlag = Math.abs(endAngle - startAngle) <= 180 ? 0 : 1;

      const d = [
        'M', start.x, start.y,
        'A', arcRadius, arcRadius, 0, largeArcFlag, sweepFlag, end.x, end.y
      ].join(' ');

      valueArc.setAttribute('d', d);
    };
    
    // mirrors IParam::ShapePowCurve / ShapeExp on the C++ side, so normalized <-> real
    // conversions stay consistent with how the host interprets this param.
    // logMin is recomputed on every call (not cached) since minValue can change after
    // construction, once the host's real min/max arrives via the "params" message.
    const getLogMin = () => (this.minValue <= 0 ? 0.00000001 : this.minValue);
    const normalizedToReal = (norm) => {
      if (this.shapeType === 'log') {
        const logMin = getLogMin();
        return logMin * Math.pow(this.maxValue / logMin, norm);
      }
      return this.minValue + Math.pow(norm, this.shapeExponent) * (this.maxValue - this.minValue);
    };
    const realToNormalized = (real) => {
      if (this.shapeType === 'log') {
        const logMin = getLogMin();
        return Math.log(real / logMin) / Math.log(this.maxValue / logMin);
      }
      return Math.pow((real - this.minValue) / (this.maxValue - this.minValue), 1 / this.shapeExponent);
    };

    const signPrefix = (v) => (this.signedDisplay && v >= 0) ? '+' : '';

    // formats a real value exactly the way updateValue() below does, so it can be used
    // to measure the widest string this knob could ever display (at minValue/maxValue)
    const formatDisplayValue = (v) => {
      if (this.isInteger) return `${signPrefix(v)}${Math.round(v)} ${units}`;
      const absValue = Math.abs(v);
      if (this.maxDigits > 0) {
        const integerDigits = Math.max(1, Math.floor(absValue).toString().length);
        const decimals = Math.max(0, this.maxDigits - integerDigits);
        return `${signPrefix(v)}${v.toFixed(decimals)} ${units}`;
      }
      const decimals = absValue < 1 ? 3 : (absValue < 10 ? 2 : 1);
      return `${signPrefix(v)}${v.toFixed(decimals)} ${units}`;
    };

    // the value text's digit count changes as it's dragged (e.g. "5 %" -> "100 %"),
    // which without a fixed-width box reflows the whole knob-container and everything
    // after it in the row/grid. Size the box once, from this knob's own min/max, to the
    // widest string it could ever show - narrower knobs stay narrow, wide-range knobs
    // (e.g. 20-20000 Hz) get more room, but no single knob's box ever changes width
    // while dragging.
    const sizeValueBox = () => {
      const widest = this.valueLabels
        ? Math.max(...this.valueLabels.map(l => l.length))
        : Math.max(formatDisplayValue(this.minValue).length, formatDisplayValue(this.maxValue).length);
      valueElement.style.minWidth = `${widest}ch`;
    };

    // normValue is always [0,1] here - dragging/scrolling moves the knob by an even
    // amount regardless of the param's shape, so the curve only affects the real-world
    // value (and its displayed number), never how fast the knob spins under the mouse.
    const updateValue = (normValue, fromHost = false) => {
      sizeValueBox();
      normValue = Math.min(1, Math.max(0, normValue));
      let finalValue = normalizedToReal(normValue);

      if (this.stepSize > 0) {
        // snap to discrete detents (e.g. Octave/Semi) - both the displayed value
        // and the knob's own rotation snap, rather than only rounding the label
        // while the pointer keeps turning smoothly (that's what integer-display alone does)
        finalValue = Math.round(finalValue / this.stepSize) * this.stepSize;
        normValue = realToNormalized(finalValue);
      } else if (this.minValue < 0 && this.maxValue > 0) {
        // bipolar, continuous (no stepSize - stepped bipolar params like Octave
        // already land exactly on 0 as one of their integer detents automatically) -
        // a small center detent so dragging "roughly to the middle" reliably lands
        // on exactly 0 rather than some barely-off value the user can't see/hit by
        // eye. Window is in normalized [0,1] units so it's a consistent fraction of
        // the knob's rotation regardless of the param's real-world range/curve.
        const zeroNorm = realToNormalized(0);
        const kCenterSnapWindow = 0.02;
        if (Math.abs(normValue - zeroNorm) < kCenterSnapWindow) {
          normValue = zeroNorm;
          finalValue = 0;
        }
      }

      currentNorm = normValue;

      if (this.valueLabels) {
        const idx = Math.min(this.valueLabels.length - 1, Math.max(0, Math.round(finalValue)));
        valueElement.textContent = this.valueLabels[idx];
      } else {
        valueElement.textContent = formatDisplayValue(finalValue);
      }
      const angle = minAngle + normValue * (maxAngle - minAngle);
      pointer.setAttribute('transform', `rotate(${angle}, 50, 50)`);
      updateValueArc(angle);

      if (!fromHost) {
        if (typeof window['SPVFUI'] === 'function') {
          window['SPVFUI'](this.paramId, normValue);
        }
        // let other UI elements (e.g. a waveform-display) react immediately -
        // the host does not echo self-originated changes back via OnParamChange
        // while a drag is in progress, so they can't rely on that round-trip
        this.dispatchEvent(new CustomEvent('user-change', { detail: { normValue, finalValue }, bubbles: true }));
      }
    };

    // Pointer Events (not mousedown/touchstart) deliberately - on WebView2, the very
    // first click that also activates the host window is sometimes consumed as pure
    // window-activation and never reaches a 'mousedown' listener at all, so the knob
    // needed a second touch to respond. 'pointerdown' goes through a different
    // internal Chromium path that doesn't have this gap, and it already unifies
    // mouse/touch/pen so the old touches[0]/clientY branching isn't needed either.
    const startDrag = (e) => {
      // Temporary diagnostic (2026-07-28) - see the module-level pointerdown
      // listener's comment near the top of this file for what to compare this
      // against next time a grab silently fails.
      console.log(`[knob-diag] startDrag fired: label="${this.label}" button=${e.button} pointerType=${e.pointerType} hasFocus=${document.hasFocus()} t=${performance.now().toFixed(1)}`);

      if (e.button == 2) return; // right click (context menu

      if (typeof window['BPCFUI'] === 'function') {
        window['BPCFUI'](this.paramId);
      }

      e.preventDefault();

      let initialY = e.clientY;
      let initialNorm = currentNorm;

      const onMove = (e) => {
        const deltaY = initialY - e.clientY;
        const normChange = deltaY / 100;
        updateValue(initialNorm + normChange);
      };

      const onEnd = () => {
        document.removeEventListener('pointermove', onMove);
        document.removeEventListener('pointerup', onEnd);
        document.removeEventListener('pointercancel', onEnd);
        document.body.classList.remove('hidden-cursor');
        document.body.style.cursor = '';

        if (typeof window['EPCFUI'] === 'function') {
          window['EPCFUI'](this.paramId);
        }
      };

      document.addEventListener('pointermove', onMove);
      document.addEventListener('pointerup', onEnd);
      document.addEventListener('pointercancel', onEnd);
      document.body.classList.add('hidden-cursor');
      document.body.style.cursor = 'none';
    };

    const onWheel = (e) => {
      e.preventDefault();
      const delta = e.deltaY < 0 ? 1 : -1;
      updateValue(currentNorm + (delta / 100));
    };

    const onDblClick = (e) => {
      e.preventDefault();

      if (typeof window['BPCFUI'] === 'function') {
        window['BPCFUI'](this.paramId);
      }

      updateValue(realToNormalized(this.defaultValue));

      if (typeof window['EPCFUI'] === 'function') {
        window['EPCFUI'](this.paramId);
      }
    };

    this.shadowRoot.querySelector('circle').addEventListener('pointerdown', startDrag);
    this.shadowRoot.querySelector('circle').addEventListener('wheel', onWheel);
    this.shadowRoot.querySelector('circle').addEventListener('dblclick', onDblClick);
    
//    updateValue(currentValue);
    
    this.shadowRoot.querySelector('.container').__updateValue = updateValue;
    
    // Expose updateValue method with a different name to avoid conflicts
    this.updateValueFromHost = (normalizedValue) => updateValue(normalizedValue, true);

    // 2026-08-26: re-renders the pointer/arc/label from whatever normalized
    // value this knob already has, without changing it - currentNorm itself
    // has no public getter, so callers that mutate this knob's min/max/
    // shapeExponent *after* construction (e.g. a dev-only alternate-curve
    // A/B test) have no other way to force the displayed number to reflect
    // that change until the next real drag/host update happens to occur.
    this.refreshDisplay = () => updateValue(currentNorm, true);
  }
  
  connectedCallback() {
    // Set the parameter ID, control tag, and initial value
    this.paramId = this.getAttribute('param-id') || -1;
    this.controlTag = this.getAttribute('control-tag') || '';
    this.defaultValue = parseFloat(this.getAttribute('default-value')) || 0.0;
  }

  attributeChangedCallback(attrName, oldVal, newVal) {
    if (attrName === 'param-id') {
      this.paramId = parseInt(newVal);
    } else if (attrName === 'control-tag') {
      this.controlTag = parseInt(newVal);
    } else if (attrName === 'min') {
      this.minValue = parseFloat(newVal);
    } else if (attrName === 'max') {
      this.maxValue = parseFloat(newVal);
    } else if (attrName === 'label') {
      this.label = newVal;
    } else if (attrName === 'default-value') {
      this.defaultValue = parseFloat(newVal);
    }
  }

  static get observedAttributes() {
    return ['param-id', 'default-value', 'control-tag', 'label', 'min', 'max'/*, 'value'*/];
  }
  
  // Getter and setter for value
  get value() {
    return this.getAttribute('value');
  }

  set value(newValue) {
    this.setAttribute('value', newValue);
  }
}

customElements.define('knob-control', KnobControl);

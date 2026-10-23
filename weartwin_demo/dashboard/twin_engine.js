/**
 * WearTwin — Streaming Digital Twin & Feature Extraction Engine (JavaScript)
 * 100% Parity with schema.py, features.py, and engine.py
 */

const WearTwinEngine = (function() {
  const INPUT_COLS = ["hr", "rmssd", "spo2", "rr", "skin_t", "amb_t", "hum", "imu"];
  const BASE_COLS = ["hr", "rmssd", "spo2", "rr", "skin_t"];
  const STATES = ["Normal", "Fatigue", "Thermal strain", "Cardio-resp stress"];
  const CALIBRATION_WINDOWS = 20; // 10 min (20 * 30s)
  const DT_MIN = 0.5; // 30s in minutes
  const READY_GREEN = 75.0;
  const READY_AMBER = 50.0;
  const ACTIVE_IMU = 0.12;

  // Apparent temperature (Steadman formula)
  function apparentTemp(tC, rh) {
    if (tC === null || isNaN(tC) || rh === null || isNaN(rh)) return null;
    const e = (rh / 100.0) * 6.105 * Math.exp((17.27 * tC) / (237.7 + tC));
    return tC + 0.33 * e - 4.0;
  }

  // Piecewise-linear scale function
  function scale(x, lo, thr, hi) {
    if (x === null || isNaN(x)) return 0.0;
    let xp = [lo, thr, hi];
    let fp = [0.0, 0.5, 1.0];
    if (lo > hi) {
      xp = [hi, thr, lo];
      fp = [1.0, 0.5, 0.0];
    }
    if (x <= xp[0]) return fp[0];
    if (x >= xp[2]) return fp[2];
    if (x <= xp[1]) {
      return fp[0] + ((fp[1] - fp[0]) * (x - xp[0])) / (xp[1] - xp[0]);
    }
    return fp[1] + ((fp[2] - fp[1]) * (x - xp[1])) / (xp[2] - xp[1]);
  }

  // Severity indices
  function severityIndices(coreTemp, fatigue, hrr, spo2, rr) {
    const thermal = scale(coreTemp, 37.5, 38.3, 39.2);
    const fat = scale(fatigue, 0.15, 0.50, 0.85);
    const c1 = scale(hrr, 0.60, 0.80, 0.95);
    const c2 = scale(spo2, 96.0, 90.0, 84.0);
    const c3 = scale(rr, 25.0, 35.0, 45.0);
    const cardio = Math.max(c1, c2, c3);
    return { thermal, fatigue: fat, cardio };
  }

  // State label logic
  function labelState(thermal, fatigue, cardio, alertThreshold = 0.5) {
    const idx = [fatigue, thermal, cardio]; // order matching STATES[1:]
    const maxVal = Math.max(...idx);
    if (maxVal >= alertThreshold) {
      return idx.indexOf(maxVal) + 1; // 1: Fatigue, 2: Thermal, 3: Cardio
    }
    return 0; // Normal
  }

  // Readiness score (0-100)
  function readinessScore(thermal, fatigue, cardio) {
    const idx = [fatigue, thermal, cardio];
    const maxVal = Math.max(...idx);
    const meanVal = (fatigue + thermal + cardio) / 3.0;
    const strain = 0.7 * maxVal + 0.3 * meanVal;
    return Math.max(0, Math.min(100, 100.0 * (1.0 - strain)));
  }

  // Readiness band string
  function readinessBand(score) {
    if (score === null || isNaN(score)) return "Calibrating";
    if (score >= READY_GREEN) return "Ready";
    if (score >= READY_AMBER) return "Caution";
    return "Rest/rotate";
  }

  // Median calculation
  function median(arr) {
    const valid = arr.filter(v => v !== null && !isNaN(v)).sort((a, b) => a - b);
    if (valid.length === 0) return null;
    const mid = Math.floor(valid.length / 2);
    return valid.length % 2 !== 0 ? valid[mid] : (valid[mid - 1] + valid[mid]) / 2.0;
  }

  // Mean calculation
  function mean(arr) {
    const valid = arr.filter(v => v !== null && !isNaN(v));
    if (valid.length === 0) return null;
    return valid.reduce((a, b) => a + b, 0) / valid.length;
  }

  /**
   * Soldier Twin Class (mimics engine.SoldierTwin)
   */
  class SoldierTwin {
    constructor(soldierId, options = {}) {
      this.soldierId = soldierId;
      this.confirmK = options.confirmK || 3;
      this.ema = options.ema !== undefined ? options.ema : 0.35;
      
      // Personal resting baseline estimates
      this.baseline = {
        hr: options.hr_rest || 62.0,
        rmssd: options.rmssd_base || 50.0,
        spo2: options.spo2_base || 98.0,
        rr: options.rr_rest || 14.0,
        skin_t: options.skin_t_base || 33.5,
        hr_max: options.hr_max || 195.0,
        tc0: 37.0,
        mass: 75.0,
        fitness: 1.0,
        tau_tc: 18.0
      };

      this.rows = [];
      this.history = [];
      this.stateId = 0;
      this._cand = 0;
      this._candN = 0;
      this._ready = null;
      
      // Internal latent physiology tracker for simulation mode
      this.latent = {
        tc: this.baseline.tc0,
        fatigue: 0.0,
        dehydration: 0.0,
        effortSmooth: 0.0,
        activeMin: 0.0,
        heatExposureMin: 0.0
      };
    }

    reset() {
      this.rows = [];
      this.history = [];
      this.stateId = 0;
      this._cand = 0;
      this._candN = 0;
      this._ready = null;
      this.latent = {
        tc: this.baseline.tc0,
        fatigue: 0.0,
        dehydration: 0.0,
        effortSmooth: 0.0,
        activeMin: 0.0,
        heatExposureMin: 0.0
      };
    }

    /**
     * Ingest one 30-second window
     * @param {Object} window - {hr, rmssd, spo2, rr, skin_t, amb_t, hum, imu}
     */
    update(window) {
      const cleanWin = {};
      INPUT_COLS.forEach(c => {
        cleanWin[c] = (window[c] === undefined || window[c] === null || isNaN(window[c])) ? null : Number(window[c]);
      });
      this.rows.push(cleanWin);
      
      const n = this.rows.length;
      const tMin = (n - 1) * DT_MIN;

      // During resting calibration (first 20 windows)
      if (n <= CALIBRATION_WINDOWS) {
        // Dynamically compute baseline medians from calibration windows
        BASE_COLS.forEach(c => {
          const vals = this.rows.map(r => r[c]).filter(v => v !== null);
          if (vals.length > 0) this.baseline[c] = median(vals);
        });

        const out = {
          soldier_id: this.soldierId,
          t_min: tMin,
          status: "Calibrating",
          state: "Calibrating",
          raw_state: "Calibrating",
          readiness: 100.0,
          readiness_raw: 100.0,
          band: "Ready",
          alert: false,
          thermal_idx: 0.0,
          fatigue_idx: 0.0,
          cardio_idx: 0.0,
          progress: n / CALIBRATION_WINDOWS,
          p_Normal: 1.0,
          p_Fatigue: 0.0,
          p_Thermal: 0.0,
          p_Cardio: 0.0,
          features: {
            hr_dev: 0.0,
            rmssd_lr: 0.0,
            spo2_dev: 0.0,
            rr_dev: 0.0,
            skin_dev: 0.0,
            active_min: 0.0,
            apparent_temp: apparentTemp(cleanWin.amb_t || 24, cleanWin.hum || 50)
          },
          latent: { ...this.latent }
        };
        this.history.push(out);
        return out;
      }

      // Feature extraction (causal past windows)
      const feats = this._computeFeatures();

      // Latent physiology estimation
      this._updateLatentPhysiology(cleanWin, feats);

      // Model estimation: severity indices
      const indices = severityIndices(
        this.latent.tc,
        this.latent.fatigue,
        this.latent.hrr,
        cleanWin.spo2 !== null ? cleanWin.spo2 : this.baseline.spo2,
        cleanWin.rr !== null ? cleanWin.rr : this.baseline.rr
      );

      // Raw state prediction
      const rawState = labelState(indices.thermal, indices.fatigue, indices.cardio);
      const readyRaw = readinessScore(indices.thermal, indices.fatigue, indices.cardio);

      // Readiness EMA smoothing
      this._ready = this._ready === null 
        ? readyRaw 
        : this.ema * readyRaw + (1.0 - this.ema) * this._ready;
      this._ready = Math.max(0, Math.min(100, this._ready));

      // Debounce: confirmed state only updates after confirm_k consecutive raw predictions
      if (rawState === this._cand) {
        this._candN++;
      } else {
        this._cand = rawState;
        this._candN = 1;
      }
      if (this._candN >= this.confirmK) {
        this.stateId = rawState;
      }

      // Simulated state probabilities
      const probas = this._estimateProbabilities(indices, rawState);

      const out = {
        soldier_id: this.soldierId,
        t_min: tMin,
        status: "Live",
        state: STATES[this.stateId],
        raw_state: STATES[rawState],
        readiness: Math.round(this._ready * 10) / 10,
        readiness_raw: Math.round(readyRaw * 10) / 10,
        band: readinessBand(this._ready),
        alert: this.stateId !== 0,
        thermal_idx: Math.round(indices.thermal * 1000) / 1000,
        fatigue_idx: Math.round(indices.fatigue * 1000) / 1000,
        cardio_idx: Math.round(indices.cardio * 1000) / 1000,
        progress: 1.0,
        p_Normal: probas.Normal,
        p_Fatigue: probas.Fatigue,
        p_Thermal: probas.Thermal,
        p_Cardio: probas.Cardio,
        features: feats,
        latent: {
          core_temp: Math.round(this.latent.tc * 100) / 100,
          fatigue: Math.round(this.latent.fatigue * 100) / 100,
          dehydration_pct: Math.round(this.latent.dehydration * 100) / 100,
          hrr: Math.round(this.latent.hrr * 100) / 100,
          active_min: Math.round(this.latent.activeMin * 10) / 10
        }
      };

      this.history.push(out);
      return out;
    }

    _computeFeatures() {
      const curr = this.rows[this.rows.length - 1];
      const ap = apparentTemp(curr.amb_t, curr.hum);

      // Baseline deviations
      const hrDev = curr.hr !== null ? curr.hr - this.baseline.hr : 0;
      const rmssdRatio = curr.rmssd !== null && this.baseline.rmssd > 0 
        ? Math.log(Math.max(1.0, curr.rmssd) / Math.max(1.0, this.baseline.rmssd)) 
        : 0;
      const spo2Dev = curr.spo2 !== null ? curr.spo2 - this.baseline.spo2 : 0;
      const rrDev = curr.rr !== null ? curr.rr - this.baseline.rr : 0;
      const skinDev = curr.skin_t !== null ? curr.skin_t - this.baseline.skin_t : 0;

      // Trailing 3m (6 windows) & 10m (20 windows)
      const last6 = this.rows.slice(-6);
      const last20 = this.rows.slice(-20);

      const hrDevMed3m = median(last6.map(r => r.hr !== null ? r.hr - this.baseline.hr : null)) || hrDev;
      const hrDevMed10m = median(last20.map(r => r.hr !== null ? r.hr - this.baseline.hr : null)) || hrDev;
      const imuMean10m = mean(last20.map(r => r.imu)) || (curr.imu || 0.02);

      const hrPerImu = hrDevMed10m / (imuMean10m + 0.1);

      return {
        hr: curr.hr,
        rmssd: curr.rmssd,
        spo2: curr.spo2,
        rr: curr.rr,
        skin_t: curr.skin_t,
        amb_t: curr.amb_t,
        hum: curr.hum,
        imu: curr.imu,
        apparent_temp: ap,
        hr_dev: hrDev,
        rmssd_lr: rmssdRatio,
        spo2_dev: spo2Dev,
        rr_dev: rrDev,
        skin_dev: skinDev,
        hr_dev_med_3m: hrDevMed3m,
        hr_dev_med_10m: hrDevMed10m,
        imu_mean_10m: imuMean10m,
        hr_per_imu: hrPerImu,
        active_min: this.latent.activeMin,
        heat_exposure_min: this.latent.heatExposureMin
      };
    }

    _updateLatentPhysiology(curr, feats) {
      const imu = curr.imu !== null ? curr.imu : 0.05;
      const hr = curr.hr !== null ? curr.hr : this.baseline.hr;
      const skinT = curr.skin_t !== null ? curr.skin_t : this.baseline.skin_t;
      const ap = feats.apparent_temp !== null ? feats.apparent_temp : 24.0;

      // Active accumulation
      if (imu > ACTIVE_IMU) {
        this.latent.activeMin += DT_MIN;
        if (ap > 30.0) {
          this.latent.heatExposureMin += DT_MIN;
        }
      }

      // Effort estimation
      const demand = Math.min(imu / 0.55, 1.2);
      const effort = Math.min(demand / this.baseline.fitness, 1.2);
      this.latent.effortSmooth += (effort - this.latent.effortSmooth) * (DT_MIN / 1.0);

      // Thermal dynamics
      const hf = Math.max(0, Math.min((ap - 22.0) / 20.0, 1.2));
      const skinRise = Math.max(0, skinT - this.baseline.skin_t);
      const tcEq = this.baseline.tc0 + 1.8 * effort * (1 + 0.6 * hf) + 0.25 * this.latent.dehydration + 0.5 * skinRise;
      this.latent.tc += (tcEq - this.latent.tc) * (DT_MIN / this.baseline.tau_tc);

      // Sweat & Dehydration
      const sweatLph = Math.max(0.1, Math.min(0.1 + 1.6 * effort * (0.5 + hf), 2.2));
      this.latent.dehydration += (sweatLph * (DT_MIN / 60.0) * 0.7) / this.baseline.mass * 100.0;

      // Fatigue accumulation
      const acc = 0.006 * Math.pow(Math.max(effort - 0.15, 0.0) / 0.25, 1.5) * (1 + 0.5 * Math.max(this.latent.tc - 38.0, 0.0));
      const timeFatigue = Math.min(this.latent.activeMin / 150.0, 0.85);
      this.latent.fatigue = Math.max(this.latent.fatigue, timeFatigue * 0.8 + acc * 0.2);
      this.latent.fatigue = Math.max(0.0, Math.min(1.0, this.latent.fatigue));

      // Heart rate reserve
      const hrMax = this.baseline.hr_max;
      const hrRest = this.baseline.hr_rest;
      this.latent.hrr = Math.max(0.0, Math.min(1.0, (hr - hrRest) / Math.max(1.0, hrMax - hrRest)));
    }

    _estimateProbabilities(indices, stateId) {
      const f = indices.fatigue;
      const t = indices.thermal;
      const c = indices.cardio;
      const maxI = Math.max(f, t, c);

      let pNorm = Math.max(0.05, 1.0 - maxI * 1.5);
      let pFat = Math.max(0.01, f);
      let pTherm = Math.max(0.01, t);
      let pCard = Math.max(0.01, c);

      if (stateId === 0) pNorm += 0.4;
      else if (stateId === 1) pFat += 0.4;
      else if (stateId === 2) pTherm += 0.4;
      else if (stateId === 3) pCard += 0.4;

      const sum = pNorm + pFat + pTherm + pCard;
      return {
        Normal: Math.round((pNorm / sum) * 1000) / 1000,
        Fatigue: Math.round((pFat / sum) * 1000) / 1000,
        Thermal: Math.round((pTherm / sum) * 1000) / 1000,
        Cardio: Math.round((pCard / sum) * 1000) / 1000
      };
    }
  }

  return {
    INPUT_COLS,
    STATES,
    CALIBRATION_WINDOWS,
    DT_MIN,
    READY_GREEN,
    READY_AMBER,
    apparentTemp,
    scale,
    severityIndices,
    labelState,
    readinessScore,
    readinessBand,
    SoldierTwin
  };
})();

if (typeof module !== "undefined" && module.exports) {
  module.exports = WearTwinEngine;
}

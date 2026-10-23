/**
 * WearTwin Tactical Web Audio Synthesizer
 * Procedural real-time sound design via Web Audio API (Zero external MP3 dependencies)
 */

const WearTwinAudio = (function() {
  let ctx = null;
  let isMuted = false;
  let heartbeatInterval = null;
  let currentBpm = 62;

  function init() {
    if (!ctx) {
      const AudioContext = window.AudioContext || window.webkitAudioContext;
      if (AudioContext) {
        ctx = new AudioContext();
      }
    }
    if (ctx && ctx.state === 'suspended') {
      ctx.resume().catch(() => {});
    }
  }

  function toggleMute() {
    isMuted = !isMuted;
    if (isMuted) {
      stopHeartbeat();
    } else if (currentBpm > 0) {
      startHeartbeat(currentBpm);
    }
    return isMuted;
  }

  function getMuted() {
    return isMuted;
  }

  // Tactical click / blip
  function playClick(freq = 880, dur = 0.04) {
    if (isMuted) return;
    init();
    if (!ctx) return;

    try {
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(freq, ctx.currentTime);
      osc.frequency.exponentialRampToValueAtTime(freq * 0.5, ctx.currentTime + dur);

      gain.gain.setValueAtTime(0.04, ctx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + dur);

      osc.connect(gain);
      gain.connect(ctx.destination);

      osc.start();
      osc.stop(ctx.currentTime + dur);
    } catch (e) {}
  }

  // Heartbeat sound (lub-dub)
  function playHeartbeatThump() {
    if (isMuted) return;
    init();
    if (!ctx) return;

    try {
      const t = ctx.currentTime;
      // "Lub" (lower, deeper)
      const osc1 = ctx.createOscillator();
      const gain1 = ctx.createGain();
      osc1.type = 'sine';
      osc1.frequency.setValueAtTime(65, t);
      osc1.frequency.exponentialRampToValueAtTime(35, t + 0.12);
      gain1.gain.setValueAtTime(0.08, t);
      gain1.gain.exponentialRampToValueAtTime(0.001, t + 0.12);
      osc1.connect(gain1);
      gain1.connect(ctx.destination);
      osc1.start(t);
      osc1.stop(t + 0.12);

      // "Dub" (slightly higher, 0.14s later)
      const osc2 = ctx.createOscillator();
      const gain2 = ctx.createGain();
      osc2.type = 'sine';
      osc2.frequency.setValueAtTime(80, t + 0.14);
      osc2.frequency.exponentialRampToValueAtTime(45, t + 0.24);
      gain2.gain.setValueAtTime(0.05, t + 0.14);
      gain2.gain.exponentialRampToValueAtTime(0.001, t + 0.24);
      osc2.connect(gain2);
      gain2.connect(ctx.destination);
      osc2.start(t + 0.14);
      osc2.stop(t + 0.24);
    } catch (e) {}
  }

  function startHeartbeat(bpm = 62) {
    currentBpm = bpm;
    stopHeartbeat();
    if (isMuted || bpm <= 0) return;
    
    // Interval in ms
    const intervalMs = (60.0 / Math.max(40, Math.min(200, bpm))) * 1000;
    playHeartbeatThump();
    heartbeatInterval = setInterval(playHeartbeatThump, intervalMs);
  }

  function updateHeartbeatBpm(bpm) {
    if (bpm === currentBpm) return;
    currentBpm = bpm;
    if (heartbeatInterval) {
      startHeartbeat(bpm);
    }
  }

  function stopHeartbeat() {
    if (heartbeatInterval) {
      clearInterval(heartbeatInterval);
      heartbeatInterval = null;
    }
  }

  // Alert alarm chime
  function playAlertAlarm(state) {
    if (isMuted) return;
    init();
    if (!ctx) return;

    try {
      const t = ctx.currentTime;
      let f1 = 523.25; // C5
      let f2 = 659.25; // E5
      let f3 = 783.99; // G5

      if (state === "Thermal strain") {
        f1 = 440; f2 = 554.37; f3 = 659.25; // Warm chord
      } else if (state === "Cardio-resp stress") {
        f1 = 698.46; f2 = 880; f3 = 1046.5; // High urgency
      } else if (state === "Fatigue") {
        f1 = 392; f2 = 493.88; f3 = 587.33; // Muted tone
      }

      [f1, f2, f3].forEach((f, idx) => {
        const osc = ctx.createOscillator();
        const gain = ctx.createGain();
        osc.type = 'triangle';
        osc.frequency.setValueAtTime(f, t + idx * 0.08);

        gain.gain.setValueAtTime(0.06, t + idx * 0.08);
        gain.gain.exponentialRampToValueAtTime(0.001, t + idx * 0.08 + 0.35);

        osc.connect(gain);
        gain.connect(ctx.destination);
        osc.start(t + idx * 0.08);
        osc.stop(t + idx * 0.08 + 0.35);
      });
    } catch (e) {}
  }

  // Liquid energy surge sound
  function playWaterSurge() {
    if (isMuted) return;
    init();
    if (!ctx) return;

    try {
      const t = ctx.currentTime;
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(140, t);
      osc.frequency.exponentialRampToValueAtTime(320, t + 0.2);
      osc.frequency.exponentialRampToValueAtTime(180, t + 0.45);

      gain.gain.setValueAtTime(0.03, t);
      gain.gain.exponentialRampToValueAtTime(0.001, t + 0.45);

      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start(t);
      osc.stop(t + 0.45);
    } catch (e) {}
  }

  return {
    init,
    toggleMute,
    getMuted,
    playClick,
    playHeartbeatThump,
    startHeartbeat,
    updateHeartbeatBpm,
    stopHeartbeat,
    playAlertAlarm,
    playWaterSurge
  };
})();

if (typeof module !== "undefined" && module.exports) {
  module.exports = WearTwinAudio;
}

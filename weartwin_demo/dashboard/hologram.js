/**
 * WearTwin Holographic Soldier & Fluid Readiness Canvas Engine
 * Procedural Hologram matching the 3D glowing digital twin mesh aesthetic
 * with real-time fluid dynamics water-fill simulation
 */

class HologramViewer {
  constructor(canvasId) {
    this.canvas = document.getElementById(canvasId);
    if (!this.canvas) return;
    this.ctx = this.canvas.getContext('2d');
    
    // Virtual resolution
    this.width = 400;
    this.height = 560;

    // Simulation state
    this.readiness = 100.0;
    this.targetReadiness = 100.0;
    this.state = "Normal";
    this.bpm = 62;
    this.rr = 14;
    this.skinT = 33.5;
    this.coreT = 37.0;
    this.activeMin = 0.0;
    this.spo2 = 98.0;
    this.imu = 0.02;

    // Time and animation
    this.time = 0;
    this.wavePhase = 0;
    this.sloshVelocity = 0;
    this.waveAmplitude = 3.5;

    // Particles
    this.bubbles = [];
    this.ambientParticles = [];
    this.initParticles();

    // Resize handling
    this.setupCanvas();
    window.addEventListener('resize', () => this.setupCanvas());

    // Mouse hover detection
    this.mouse = { x: -1, y: -1, active: false };
    this.hoverZone = null;
    this.canvas.addEventListener('mousemove', (e) => this.onMouseMove(e));
    this.canvas.addEventListener('mouseleave', () => { this.mouse.active = false; this.hoverZone = null; });

    // Build anatomical model vertices and connections
    this.buildAnatomy();

    // Start render loop
    this.animate = this.animate.bind(this);
    requestAnimationFrame(this.animate);
  }

  setupCanvas() {
    const rect = this.canvas.parentElement.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    this.canvas.width = rect.width * dpr;
    this.canvas.height = rect.height * dpr;
    this.scale = Math.min((rect.width * dpr) / this.width, (rect.height * dpr) / this.height) * 0.96;
    this.offsetX = (this.canvas.width - this.width * this.scale) / 2;
    this.offsetY = (this.canvas.height - this.height * this.scale) / 2;
  }

  initParticles() {
    // Ambient cyberspace particles floating up
    for (let i = 0; i < 40; i++) {
      this.ambientParticles.push({
        x: Math.random() * this.width,
        y: Math.random() * this.height,
        vx: (Math.random() - 0.5) * 0.4,
        vy: -0.3 - Math.random() * 0.6,
        size: 0.8 + Math.random() * 1.8,
        alpha: 0.1 + Math.random() * 0.4
      });
    }

    // Effervescent micro-bubbles inside fluid
    for (let i = 0; i < 25; i++) {
      this.bubbles.push({
        x: 120 + Math.random() * 160,
        y: 200 + Math.random() * 320,
        vy: -0.6 - Math.random() * 1.2,
        wobbleSpeed: 2 + Math.random() * 3,
        size: 1.0 + Math.random() * 2.2,
        alpha: 0.2 + Math.random() * 0.6
      });
    }
  }

  buildAnatomy() {
    // Exact humanoid coordinates calibrated to anatomical scale (width: 400, height: 560, center X: 200)
    const CX = 200;

    this.nodes = [
      // Skull & Head
      { id: 'crown',      x: CX,       y: 42,  zone: 'brain',  r: 3.5 },
      { id: 'forehead',   x: CX,       y: 56,  zone: 'brain',  r: 3.0 },
      { id: 'temple_l',   x: CX - 18,  y: 60,  zone: 'brain',  r: 2.5 },
      { id: 'temple_r',   x: CX + 18,  y: 60,  zone: 'brain',  r: 2.5 },
      { id: 'ear_l',      x: CX - 22,  y: 72,  zone: 'brain',  r: 2.2 },
      { id: 'ear_r',      x: CX + 22,  y: 72,  zone: 'brain',  r: 2.2 },
      { id: 'chin',       x: CX,       y: 86,  zone: 'head',   r: 3.0 },

      // Neck & Spine
      { id: 'c3',         x: CX,       y: 98,  zone: 'spine',  r: 2.5 },
      { id: 'c7',         x: CX,       y: 112, zone: 'spine',  r: 3.0 },
      { id: 't4',         x: CX,       y: 146, zone: 'spine',  r: 3.0 },
      { id: 't8',         x: CX,       y: 182, zone: 'spine',  r: 3.0 },
      { id: 'l2',         x: CX,       y: 222, zone: 'spine',  r: 3.0 },
      { id: 'l5',         x: CX,       y: 260, zone: 'spine',  r: 3.2 },

      // Clavicles & Shoulders
      { id: 'clav_l',     x: CX - 28,  y: 116, zone: 'torso',  r: 2.5 },
      { id: 'clav_r',     x: CX + 28,  y: 116, zone: 'torso',  r: 2.5 },
      { id: 'shldr_l',    x: CX - 64,  y: 122, zone: 'arm_l',  r: 4.0 },
      { id: 'shldr_r',    x: CX + 64,  y: 122, zone: 'arm_r',  r: 4.0 },

      // Thorax & Organs
      { id: 'sternum_top',x: CX,       y: 124, zone: 'lungs',  r: 3.0 },
      { id: 'heart',      x: CX - 12,  y: 152, zone: 'heart',  r: 6.0 }, // Heart organ
      { id: 'lung_l',     x: CX - 32,  y: 148, zone: 'lungs',  r: 4.5 },
      { id: 'lung_r',     x: CX + 32,  y: 148, zone: 'lungs',  r: 4.5 },
      { id: 'xiphoid',    x: CX,       y: 178, zone: 'torso',  r: 3.0 },
      { id: 'rib_l1',     x: CX - 44,  y: 150, zone: 'torso',  r: 2.2 },
      { id: 'rib_r1',     x: CX + 44,  y: 150, zone: 'torso',  r: 2.2 },
      { id: 'rib_l2',     x: CX - 46,  y: 176, zone: 'torso',  r: 2.2 },
      { id: 'rib_r2',     x: CX + 46,  y: 176, zone: 'torso',  r: 2.2 },
      { id: 'navel',      x: CX,       y: 218, zone: 'core',   r: 2.8 },
      { id: 'flank_l',    x: CX - 40,  y: 216, zone: 'core',   r: 2.5 },
      { id: 'flank_r',    x: CX + 40,  y: 216, zone: 'core',   r: 2.5 },

      // Pelvis
      { id: 'pelvis_l',   x: CX - 36,  y: 258, zone: 'pelvis', r: 3.5 },
      { id: 'pelvis_r',   x: CX + 36,  y: 258, zone: 'pelvis', r: 3.5 },
      { id: 'groin',      x: CX,       y: 284, zone: 'pelvis', r: 3.0 },

      // Left Arm
      { id: 'bicep_l',    x: CX - 72,  y: 158, zone: 'arm_l',  r: 2.8 },
      { id: 'elbow_l',    x: CX - 84,  y: 196, zone: 'arm_l',  r: 3.8 },
      { id: 'forearm_l',  x: CX - 94,  y: 236, zone: 'arm_l',  r: 2.8 },
      { id: 'wrist_l',    x: CX - 104, y: 278, zone: 'arm_l',  r: 3.2 },
      { id: 'hand_l',     x: CX - 110, y: 298, zone: 'arm_l',  r: 2.8 },
      { id: 'fingers_l',  x: CX - 114, y: 320, zone: 'arm_l',  r: 2.0 },

      // Right Arm
      { id: 'bicep_r',    x: CX + 72,  y: 158, zone: 'arm_r',  r: 2.8 },
      { id: 'elbow_r',    x: CX + 84,  y: 196, zone: 'arm_r',  r: 3.8 },
      { id: 'forearm_r',  x: CX + 94,  y: 236, zone: 'arm_r',  r: 2.8 },
      { id: 'wrist_r',    x: CX + 104, y: 278, zone: 'arm_r',  r: 3.2 },
      { id: 'hand_r',     x: CX + 110, y: 298, zone: 'arm_r',  r: 2.8 },
      { id: 'fingers_r',  x: CX + 114, y: 320, zone: 'arm_r',  r: 2.0 },

      // Left Leg
      { id: 'hip_l',      x: CX - 32,  y: 294, zone: 'leg_l',  r: 4.0 },
      { id: 'thigh_l1',   x: CX - 36,  y: 334, zone: 'leg_l',  r: 3.0 },
      { id: 'thigh_l2',   x: CX - 40,  y: 374, zone: 'leg_l',  r: 3.0 },
      { id: 'knee_l',     x: CX - 42,  y: 416, zone: 'leg_l',  r: 4.2 },
      { id: 'calf_l1',    x: CX - 42,  y: 456, zone: 'leg_l',  r: 3.0 },
      { id: 'calf_l2',    x: CX - 40,  y: 492, zone: 'leg_l',  r: 2.8 },
      { id: 'ankle_l',    x: CX - 38,  y: 520, zone: 'leg_l',  r: 3.5 },
      { id: 'heel_l',     x: CX - 42,  y: 532, zone: 'leg_l',  r: 2.5 },
      { id: 'toe_l',      x: CX - 32,  y: 536, zone: 'leg_l',  r: 2.2 },

      // Right Leg
      { id: 'hip_r',      x: CX + 32,  y: 294, zone: 'leg_r',  r: 4.0 },
      { id: 'thigh_r1',   x: CX + 36,  y: 334, zone: 'leg_r',  r: 3.0 },
      { id: 'thigh_r2',   x: CX + 40,  y: 374, zone: 'leg_r',  r: 3.0 },
      { id: 'knee_r',     x: CX + 42,  y: 416, zone: 'leg_r',  r: 4.2 },
      { id: 'calf_r1',    x: CX + 42,  y: 456, zone: 'leg_r',  r: 3.0 },
      { id: 'calf_r2',    x: CX + 40,  y: 492, zone: 'leg_r',  r: 2.8 },
      { id: 'ankle_r',    x: CX + 38,  y: 520, zone: 'leg_r',  r: 3.5 },
      { id: 'heel_r',     x: CX + 42,  y: 532, zone: 'leg_r',  r: 2.5 },
      { id: 'toe_r',      x: CX + 32,  y: 536, zone: 'leg_r',  r: 2.2 }
    ];

    // Constellation wireframe connections
    this.edges = [
      // Skull
      ['crown', 'forehead'], ['forehead', 'temple_l'], ['forehead', 'temple_r'],
      ['temple_l', 'ear_l'], ['temple_r', 'ear_r'], ['ear_l', 'chin'], ['ear_r', 'chin'],
      ['crown', 'temple_l'], ['crown', 'temple_r'], ['forehead', 'chin'],

      // Spine & Neck
      ['chin', 'c3'], ['c3', 'c7'], ['c7', 't4'], ['t4', 't8'], ['t8', 'l2'], ['l2', 'l5'], ['l5', 'groin'],

      // Shoulder Girdle
      ['c7', 'clav_l'], ['c7', 'clav_r'], ['clav_l', 'shldr_l'], ['clav_r', 'shldr_r'],
      ['clav_l', 'sternum_top'], ['clav_r', 'sternum_top'], ['c7', 'sternum_top'],

      // Thorax & Ribs
      ['sternum_top', 'lung_l'], ['sternum_top', 'lung_r'], ['sternum_top', 'heart'],
      ['heart', 'xiphoid'], ['t4', 'lung_l'], ['t4', 'lung_r'], ['t4', 'heart'],
      ['lung_l', 'rib_l1'], ['lung_r', 'rib_r1'], ['xiphoid', 'rib_l2'], ['xiphoid', 'rib_r2'],
      ['shldr_l', 'rib_l1'], ['shldr_r', 'rib_r1'], ['rib_l1', 'rib_l2'], ['rib_r1', 'rib_r2'],
      ['xiphoid', 'navel'], ['t8', 'navel'], ['rib_l2', 'flank_l'], ['rib_r2', 'flank_r'],
      ['navel', 'flank_l'], ['navel', 'flank_r'],

      // Pelvis
      ['flank_l', 'pelvis_l'], ['flank_r', 'pelvis_r'], ['navel', 'l5'],
      ['l5', 'pelvis_l'], ['l5', 'pelvis_r'], ['pelvis_l', 'groin'], ['pelvis_r', 'groin'],
      ['pelvis_l', 'hip_l'], ['pelvis_r', 'hip_r'], ['groin', 'hip_l'], ['groin', 'hip_r'],

      // Left Arm
      ['shldr_l', 'bicep_l'], ['bicep_l', 'elbow_l'], ['elbow_l', 'forearm_l'],
      ['forearm_l', 'wrist_l'], ['wrist_l', 'hand_l'], ['hand_l', 'fingers_l'],

      // Right Arm
      ['shldr_r', 'bicep_r'], ['bicep_r', 'elbow_r'], ['elbow_r', 'forearm_r'],
      ['forearm_r', 'wrist_r'], ['wrist_r', 'hand_r'], ['hand_r', 'fingers_r'],

      // Left Leg
      ['hip_l', 'thigh_l1'], ['thigh_l1', 'thigh_l2'], ['thigh_l2', 'knee_l'],
      ['knee_l', 'calf_l1'], ['calf_l1', 'calf_l2'], ['calf_l2', 'ankle_l'],
      ['ankle_l', 'heel_l'], ['ankle_l', 'toe_l'], ['heel_l', 'toe_l'],

      // Right Leg
      ['hip_r', 'thigh_r1'], ['thigh_r1', 'thigh_r2'], ['thigh_r2', 'knee_r'],
      ['knee_r', 'calf_r1'], ['calf_r1', 'calf_r2'], ['calf_r2', 'ankle_r'],
      ['ankle_r', 'heel_r'], ['ankle_r', 'toe_r'], ['heel_r', 'toe_r']
    ];

    // Build fast lookup table for nodes
    this.nodeMap = {};
    this.nodes.forEach(n => { this.nodeMap[n.id] = n; });
  }

  setTelemetry(data) {
    if (data.readiness !== undefined) {
      const prev = this.targetReadiness;
      this.targetReadiness = Math.max(0, Math.min(100, data.readiness));
      if (Math.abs(prev - this.targetReadiness) > 10) {
        this.sloshVelocity = (prev - this.targetReadiness) * 0.15;
      }
    }
    if (data.state) this.state = data.state;
    if (data.hr) this.bpm = data.hr;
    if (data.rr) this.rr = data.rr;
    if (data.skin_t) this.skinT = data.skin_t;
    if (data.core_temp) this.coreT = data.core_temp;
    if (data.active_min !== undefined) this.activeMin = data.active_min;
    if (data.spo2) this.spo2 = data.spo2;
    if (data.imu !== undefined) this.imu = data.imu;
  }

  onMouseMove(e) {
    const rect = this.canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    const clientX = (e.clientX - rect.left) * dpr;
    const clientY = (e.clientY - rect.top) * dpr;

    // Transform to virtual coordinates
    this.mouse.x = (clientX - this.offsetX) / this.scale;
    this.mouse.y = (clientY - this.offsetY) / this.scale;
    this.mouse.active = true;

    // Detect closest node
    let closest = null;
    let minDist = 28;
    this.nodes.forEach(n => {
      const d = Math.hypot(n.x - this.mouse.x, n.y - this.mouse.y);
      if (d < minDist) {
        minDist = d;
        closest = n;
      }
    });
    this.hoverZone = closest;
  }

  getColorTheme() {
    // Dynamic color gradient based on readiness
    // >= 75: Bio-Cyan / Emerald
    // 50-74: Radiant Amber / Golden Orange
    // < 50: Crimson Danger
    if (this.readiness >= 75) {
      return {
        name: 'Ready',
        primary: '#00f0ff',
        secondary: '#10b981',
        glow: 'rgba(0, 240, 255, 0.45)',
        fillTop: 'rgba(0, 240, 255, 0.75)',
        fillBottom: 'rgba(16, 185, 129, 0.85)',
        accent: '#22c55e',
        lightColor: 'rgba(0, 240, 255, 0.15)'
      };
    } else if (this.readiness >= 50) {
      return {
        name: 'Caution',
        primary: '#f59e0b',
        secondary: '#ea580c',
        glow: 'rgba(245, 158, 11, 0.45)',
        fillTop: 'rgba(245, 158, 11, 0.75)',
        fillBottom: 'rgba(234, 88, 12, 0.85)',
        accent: '#f59e0b',
        lightColor: 'rgba(245, 158, 11, 0.15)'
      };
    } else {
      return {
        name: 'Critical',
        primary: '#ef4444',
        secondary: '#b91c1c',
        glow: 'rgba(239, 68, 68, 0.55)',
        fillTop: 'rgba(239, 68, 68, 0.80)',
        fillBottom: 'rgba(185, 28, 28, 0.90)',
        accent: '#ef4444',
        lightColor: 'rgba(239, 68, 68, 0.20)'
      };
    }
  }

  updatePhysics(dt) {
    this.time += dt;
    this.wavePhase += dt * 3.5;

    // Smooth readiness interpolation
    this.readiness += (this.targetReadiness - this.readiness) * 0.08;

    // Sloshing physics spring
    this.sloshVelocity *= 0.92;
    this.waveAmplitude = 3.5 + Math.abs(this.sloshVelocity);

    // Update ambient particles
    this.ambientParticles.forEach(p => {
      p.y += p.vy;
      p.x += p.vx + Math.sin(this.time * 2 + p.y * 0.02) * 0.2;
      if (p.y < 0) {
        p.y = this.height + 10;
        p.x = Math.random() * this.width;
      }
    });

    // Update bubbles inside fluid
    const fluidY = this.getFluidHeight();
    this.bubbles.forEach(b => {
      b.y += b.vy;
      b.x += Math.sin(this.time * b.wobbleSpeed) * 0.35;
      if (b.y < fluidY) {
        b.y = this.height - 20 - Math.random() * 40;
        b.x = 140 + Math.random() * 120;
      }
    });
  }

  getFluidHeight() {
    // 100% -> head top (y = 35)
    // 0% -> feet (y = 540)
    const topY = 32;
    const bottomY = 542;
    return bottomY - (this.readiness / 100.0) * (bottomY - topY);
  }

  animate() {
    this.updatePhysics(0.016);
    this.render();
    requestAnimationFrame(this.animate);
  }

  render() {
    const ctx = this.ctx;
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

    ctx.save();
    ctx.translate(this.offsetX, this.offsetY);
    ctx.scale(this.scale, this.scale);

    const theme = this.getColorTheme();

    // 1. Draw glowing holographic pedestal & floor ring
    this.drawPedestal(theme);

    // 2. Draw ambient particles & cyberspace grid lines
    this.drawAmbient(theme);

    // 3. Draw Holographic Silhouette & Fluid Fill
    this.drawFluidAndBody(theme);

    // 4. Draw anatomical wireframe mesh & skeleton nodes
    this.drawSkeletonMesh(theme);

    // 5. Draw pulsating vital organs (Heart, Lungs, Brain)
    this.drawVitalOrgans(theme);

    // 6. Draw interactive hover HUD
    if (this.mouse.active && this.hoverZone) {
      this.drawHoverHUD(theme);
    }

    ctx.restore();
  }

  drawPedestal(theme) {
    const ctx = this.ctx;
    const CX = 200;
    const baseY = 540;

    // Glowing elliptical base
    ctx.save();
    ctx.beginPath();
    ctx.ellipse(CX, baseY, 105, 24, 0, 0, Math.PI * 2);
    const grad = ctx.createRadialGradient(CX, baseY, 10, CX, baseY, 105);
    grad.addColorStop(0, theme.glow);
    grad.addColorStop(0.6, theme.lightColor);
    grad.addColorStop(1, 'transparent');
    ctx.fillStyle = grad;
    ctx.fill();

    // Concentric scan rings
    ctx.strokeStyle = theme.primary;
    ctx.lineWidth = 1.0;
    ctx.globalAlpha = 0.45;
    ctx.beginPath();
    ctx.ellipse(CX, baseY, 90, 20, 0, 0, Math.PI * 2);
    ctx.stroke();

    ctx.globalAlpha = 0.25;
    ctx.beginPath();
    ctx.ellipse(CX, baseY, 60, 14, 0, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();
  }

  drawAmbient(theme) {
    const ctx = this.ctx;
    ctx.save();

    // Ambient floating cyberspace dust
    this.ambientParticles.forEach(p => {
      ctx.beginPath();
      ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
      ctx.fillStyle = theme.primary;
      ctx.globalAlpha = p.alpha;
      ctx.fill();
    });

    // Subtle holographic scanlines
    const scanY = (this.time * 60) % this.height;
    ctx.strokeStyle = theme.primary;
    ctx.lineWidth = 1.2;
    ctx.globalAlpha = 0.15;
    ctx.beginPath();
    ctx.moveTo(40, scanY);
    ctx.lineTo(this.width - 40, scanY);
    ctx.stroke();

    ctx.restore();
  }

  /**
   * Defines the anatomical outer body contour for fluid clipping
   */
  createBodyContourPath() {
    const path = new Path2D();
    const CX = 200;

    // Head
    path.moveTo(CX, 30);
    path.bezierCurveTo(CX + 24, 30, CX + 30, 48, CX + 28, 70); // Right head
    path.bezierCurveTo(CX + 26, 85, CX + 18, 92, CX + 14, 98); // Right jaw
    path.lineTo(CX + 15, 110); // Right neck

    // Right Shoulder & Arm
    path.bezierCurveTo(CX + 45, 114, CX + 72, 118, CX + 82, 126); // Right shoulder
    path.bezierCurveTo(CX + 94, 155, CX + 104, 195, CX + 102, 215); // Right bicep
    path.bezierCurveTo(CX + 112, 245, CX + 120, 280, CX + 122, 305); // Right forearm/hand
    path.bezierCurveTo(CX + 124, 328, CX + 116, 335, CX + 106, 330); // Right fingertips
    path.bezierCurveTo(CX + 96, 310, CX + 88, 270, CX + 78, 230); // Inner arm
    path.lineTo(CX + 54, 185); // Right armpit

    // Right Torso & Flank
    path.bezierCurveTo(CX + 52, 215, CX + 48, 245, CX + 44, 275); // Right flank
    path.bezierCurveTo(CX + 48, 290, CX + 50, 310, CX + 50, 335); // Right outer thigh
    path.bezierCurveTo(CX + 50, 375, CX + 52, 415, CX + 50, 435); // Right outer knee
    path.bezierCurveTo(CX + 48, 470, CX + 46, 505, CX + 44, 528); // Right calf
    path.bezierCurveTo(CX + 50, 538, CX + 44, 542, CX + 26, 542); // Right foot sole
    path.bezierCurveTo(CX + 22, 532, CX + 24, 505, CX + 24, 470); // Inner right calf
    path.bezierCurveTo(CX + 24, 420, CX + 22, 360, CX + 20, 320); // Inner right thigh
    path.lineTo(CX, 290); // Crotch

    // Left Leg
    path.lineTo(CX - 20, 320);
    path.bezierCurveTo(CX - 22, 360, CX - 24, 420, CX - 24, 470);
    path.bezierCurveTo(CX - 24, 505, CX - 22, 532, CX - 26, 542);
    path.bezierCurveTo(CX - 44, 542, CX - 50, 538, CX - 44, 528);
    path.bezierCurveTo(CX - 46, 505, CX - 48, 470, CX - 50, 435);
    path.bezierCurveTo(CX - 52, 415, CX - 50, 375, CX - 50, 335);
    path.bezierCurveTo(CX - 48, 310, CX - 48, 290, CX - 44, 275);

    // Left Torso & Arm
    path.bezierCurveTo(CX - 48, 245, CX - 52, 215, CX - 54, 185);
    path.lineTo(CX - 78, 230);
    path.bezierCurveTo(CX - 88, 270, CX - 96, 310, CX - 106, 330);
    path.bezierCurveTo(CX - 116, 335, CX - 124, 328, CX - 122, 305);
    path.bezierCurveTo(CX - 120, 280, CX - 112, 245, CX - 102, 215);
    path.bezierCurveTo(CX - 104, 195, CX - 94, 155, CX - 82, 126);
    path.bezierCurveTo(CX - 72, 118, CX - 45, 114, CX - 15, 110);
    path.lineTo(CX - 14, 98);
    path.bezierCurveTo(CX - 18, 92, CX - 26, 85, CX - 28, 70);
    path.bezierCurveTo(CX - 30, 48, CX - 24, 30, CX, 30);
    path.closePath();

    return path;
  }

  drawFluidAndBody(theme) {
    const ctx = this.ctx;
    const bodyContour = this.createBodyContourPath();
    const fluidY = this.getFluidHeight();

    ctx.save();

    // 1. Draw outer body aura & wireframe silhouette
    ctx.strokeStyle = theme.primary;
    ctx.lineWidth = 1.6;
    ctx.shadowColor = theme.glow;
    ctx.shadowBlur = 18;
    ctx.globalAlpha = 0.35;
    ctx.stroke(bodyContour);

    // 2. Draw subtle inner body shadow
    ctx.fillStyle = 'rgba(4, 12, 28, 0.45)';
    ctx.fill(bodyContour);

    // 3. CLIP TO BODY CONTOUR FOR FLUID SIMULATION
    ctx.save();
    ctx.clip(bodyContour);

    // 3A. Draw fluid background gradient (from fluidY down to bottom)
    if (this.readiness > 0.5) {
      const fluidGrad = ctx.createLinearGradient(0, fluidY, 0, 545);
      fluidGrad.addColorStop(0, theme.fillTop);
      fluidGrad.addColorStop(0.5, theme.fillBottom);
      fluidGrad.addColorStop(1, 'rgba(4, 16, 40, 0.95)');

      ctx.beginPath();
      ctx.moveTo(0, 550);
      ctx.lineTo(0, fluidY);

      // Liquid surface sinusoidal wave
      const wAmp = this.waveAmplitude;
      const wPhase = this.wavePhase;
      for (let x = 0; x <= this.width; x += 10) {
        const yWave = fluidY + Math.sin(x * 0.04 + wPhase) * wAmp + Math.cos(x * 0.08 - wPhase * 0.6) * (wAmp * 0.5);
        ctx.lineTo(x, yWave);
      }
      ctx.lineTo(this.width, 550);
      ctx.closePath();

      ctx.fillStyle = fluidGrad;
      ctx.globalAlpha = 0.78;
      ctx.fill();

      // 3B. Specular Liquid Meniscus Highlight (bright glowing water surface line)
      ctx.beginPath();
      for (let x = 0; x <= this.width; x += 6) {
        const yWave = fluidY + Math.sin(x * 0.04 + wPhase) * wAmp + Math.cos(x * 0.08 - wPhase * 0.6) * (wAmp * 0.5);
        if (x === 0) ctx.moveTo(x, yWave);
        else ctx.lineTo(x, yWave);
      }
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 2.5;
      ctx.shadowColor = theme.primary;
      ctx.shadowBlur = 14;
      ctx.globalAlpha = 0.92;
      ctx.stroke();

      // 3C. Secondary refractive wave
      ctx.beginPath();
      for (let x = 0; x <= this.width; x += 8) {
        const yWave = fluidY + 4 + Math.sin(x * 0.05 + wPhase + 1.2) * (wAmp * 0.7);
        if (x === 0) ctx.moveTo(x, yWave);
        else ctx.lineTo(x, yWave);
      }
      ctx.strokeStyle = theme.primary;
      ctx.lineWidth = 1.5;
      ctx.globalAlpha = 0.6;
      ctx.stroke();

      // 3D. Effervescent fluid bubbles rising inside the liquid
      this.bubbles.forEach(b => {
        if (b.y >= fluidY + 2) {
          ctx.beginPath();
          ctx.arc(b.x, b.y, b.size, 0, Math.PI * 2);
          ctx.fillStyle = '#ffffff';
          ctx.globalAlpha = b.alpha * 0.85;
          ctx.fill();
        }
      });
    }

    ctx.restore(); // End clipping

    ctx.restore();
  }

  drawSkeletonMesh(theme) {
    const ctx = this.ctx;
    const fluidY = this.getFluidHeight();

    ctx.save();

    // 1. Draw interconnected wireframe constellation lines
    this.edges.forEach(([id1, id2]) => {
      const n1 = this.nodeMap[id1];
      const n2 = this.nodeMap[id2];
      if (!n1 || !n2) return;

      const midY = (n1.y + n2.y) / 2;
      const isSubmerged = midY >= fluidY;

      ctx.beginPath();
      ctx.moveTo(n1.x, n1.y);
      ctx.lineTo(n2.x, n2.y);

      if (isSubmerged) {
        // Bright glowing bio-luminescence when bathed in fluid
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 1.6;
        ctx.globalAlpha = 0.55;
        ctx.shadowColor = theme.primary;
        ctx.shadowBlur = 8;
      } else {
        // Exposed digital wireframe skeleton (dry, flickering holographic energy)
        ctx.strokeStyle = theme.primary;
        ctx.lineWidth = 1.0;
        ctx.globalAlpha = 0.28 + Math.sin(this.time * 4 + n1.y) * 0.08;
        ctx.shadowBlur = 0;
      }
      ctx.stroke();
    });

    // 2. Draw triangulation webbing (faint lattice)
    ctx.lineWidth = 0.5;
    ctx.strokeStyle = theme.primary;
    ctx.globalAlpha = 0.12;
    [
      ['crown', 'c3'], ['temple_l', 'c3'], ['temple_r', 'c3'],
      ['c7', 'lung_l'], ['c7', 'lung_r'], ['sternum_top', 't4'],
      ['rib_l1', 'navel'], ['rib_r1', 'navel'], ['t8', 'pelvis_l'], ['t8', 'pelvis_r'],
      ['bicep_l', 'rib_l1'], ['bicep_r', 'rib_r1'],
      ['groin', 'thigh_l1'], ['groin', 'thigh_r1'],
      ['knee_l', 'knee_r']
    ].forEach(([id1, id2]) => {
      const n1 = this.nodeMap[id1];
      const n2 = this.nodeMap[id2];
      if (n1 && n2) {
        ctx.beginPath();
        ctx.moveTo(n1.x, n1.y);
        ctx.lineTo(n2.x, n2.y);
        ctx.stroke();
      }
    });

    // 3. Draw glowing node vertices (constellation points)
    this.nodes.forEach(n => {
      const isSubmerged = n.y >= fluidY;
      const isHovered = this.hoverZone && this.hoverZone.id === n.id;

      ctx.beginPath();
      ctx.arc(n.x, n.y, isHovered ? n.r * 1.8 : n.r, 0, Math.PI * 2);

      if (isSubmerged) {
        ctx.fillStyle = '#ffffff';
        ctx.shadowColor = theme.secondary;
        ctx.shadowBlur = 10;
        ctx.globalAlpha = 0.85;
      } else {
        ctx.fillStyle = theme.primary;
        ctx.shadowColor = theme.glow;
        ctx.shadowBlur = 5;
        ctx.globalAlpha = 0.45;
      }

      ctx.fill();

      // Node ring pulse on key joints
      if (['crown', 'heart', 'shldr_l', 'shldr_r', 'knee_l', 'knee_r', 'ankle_l', 'ankle_r'].includes(n.id)) {
        ctx.beginPath();
        ctx.arc(n.x, n.y, n.r + 2.5 + Math.sin(this.time * 3 + n.x) * 1.5, 0, Math.PI * 2);
        ctx.strokeStyle = isSubmerged ? '#ffffff' : theme.primary;
        ctx.lineWidth = 0.8;
        ctx.globalAlpha = isSubmerged ? 0.6 : 0.25;
        ctx.stroke();
      }
    });

    ctx.restore();
  }

  drawVitalOrgans(theme) {
    const ctx = this.ctx;
    ctx.save();

    // 1. HEART ORGAN PULSE (Matches current BPM)
    const heartNode = this.nodeMap['heart'];
    if (heartNode) {
      const beatFreq = (this.bpm / 60.0) * Math.PI * 2;
      const beatCycle = (this.time * beatFreq) % (Math.PI * 2);
      // Double thump (lub-dub) wave
      const thump = Math.pow(Math.max(0, Math.sin(beatCycle)), 8) + 
                    0.5 * Math.pow(Math.max(0, Math.sin(beatCycle - 0.5)), 8);
      const heartRadius = 6.0 + thump * 5.0;

      // Glow aura around heart
      ctx.beginPath();
      ctx.arc(heartNode.x, heartNode.y, heartRadius * 2.5, 0, Math.PI * 2);
      const heartGrad = ctx.createRadialGradient(heartNode.x, heartNode.y, 2, heartNode.x, heartNode.y, heartRadius * 2.5);
      heartGrad.addColorStop(0, this.bpm > 140 ? 'rgba(239, 68, 68, 0.7)' : 'rgba(0, 240, 255, 0.7)');
      heartGrad.addColorStop(1, 'transparent');
      ctx.fillStyle = heartGrad;
      ctx.globalAlpha = 0.85;
      ctx.fill();

      // Heart core dot
      ctx.beginPath();
      ctx.arc(heartNode.x, heartNode.y, heartRadius, 0, Math.PI * 2);
      ctx.fillStyle = '#ffffff';
      ctx.shadowColor = this.bpm > 140 ? '#ef4444' : '#00f0ff';
      ctx.shadowBlur = 15;
      ctx.globalAlpha = 1.0;
      ctx.fill();
    }

    // 2. LUNGS EXPANSION (Matches Respiration Rate)
    const lungL = this.nodeMap['lung_l'];
    const lungR = this.nodeMap['lung_r'];
    if (lungL && lungR) {
      const respFreq = (this.rr / 60.0) * Math.PI * 2;
      const breath = 0.5 + 0.5 * Math.sin(this.time * respFreq);
      const lungSize = 5.0 + breath * 3.5;

      [lungL, lungR].forEach(n => {
        ctx.beginPath();
        ctx.arc(n.x, n.y, lungSize * 2.0, 0, Math.PI * 2);
        const lungGrad = ctx.createRadialGradient(n.x, n.y, 2, n.x, n.y, lungSize * 2.0);
        lungGrad.addColorStop(0, 'rgba(16, 185, 129, 0.5)');
        lungGrad.addColorStop(1, 'transparent');
        ctx.fillStyle = lungGrad;
        ctx.fill();
      });
    }

    // 3. BRAIN SYNAPTIC FLASHES (Crown node)
    const crown = this.nodeMap['crown'];
    if (crown) {
      const flash = Math.sin(this.time * 6) > 0.6;
      if (flash) {
        ctx.beginPath();
        ctx.arc(crown.x, crown.y + 12, 14, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(0, 240, 255, 0.25)';
        ctx.shadowColor = '#00f0ff';
        ctx.shadowBlur = 12;
        ctx.fill();
      }
    }

    ctx.restore();
  }

  drawHoverHUD(theme) {
    const ctx = this.ctx;
    const n = this.hoverZone;
    if (!n) return;

    ctx.save();

    // Determine telemetry text based on anatomical zone
    let title = "ANATOMICAL NODE";
    let detail = `${n.id} [${n.x.toFixed(0)}, ${n.y.toFixed(0)}]`;

    if (n.zone === 'heart') {
      title = "CARDIAC MONITOR";
      detail = `HR: ${this.bpm.toFixed(0)} bpm | RMSSD: ${(50 * Math.exp(-0.01 * (this.bpm - 62))).toFixed(0)} ms`;
    } else if (n.zone === 'lungs') {
      title = "RESPIRATORY & PULMONARY";
      detail = `RR: ${this.rr.toFixed(0)} brpm | SpO2: ${this.spo2.toFixed(1)}%`;
    } else if (n.zone === 'brain') {
      title = "CEREBRAL & THERMAL";
      detail = `Core Tc: ${this.coreT.toFixed(2)} °C | Skin: ${this.skinT.toFixed(1)} °C`;
    } else if (n.zone === 'core') {
      title = "PHYSIOLOGICAL CORE";
      detail = `Active Time: ${this.activeMin.toFixed(0)} min | Exertion Index`;
    } else if (n.zone.startsWith('arm') || n.zone.startsWith('leg')) {
      title = "BIOMECHANICAL / IMU";
      detail = `Acceleration: ${this.imu.toFixed(2)} g | Cadence Active`;
    }

    // Floating callout box
    const boxW = 190;
    const boxH = 44;
    let boxX = n.x + 16;
    let boxY = n.y - 22;

    if (boxX + boxW > this.width) boxX = n.x - boxW - 16;
    if (boxY + boxH > this.height) boxY = this.height - boxH - 10;
    if (boxY < 10) boxY = 10;

    // Leader line
    ctx.strokeStyle = theme.primary;
    ctx.lineWidth = 1.0;
    ctx.beginPath();
    ctx.moveTo(n.x, n.y);
    ctx.lineTo(boxX + (boxX > n.x ? 0 : boxW), boxY + boxH / 2);
    ctx.stroke();

    // Callout container
    ctx.fillStyle = 'rgba(6, 16, 36, 0.92)';
    ctx.strokeStyle = theme.primary;
    ctx.lineWidth = 1.2;
    ctx.shadowColor = theme.glow;
    ctx.shadowBlur = 10;
    ctx.beginPath();
    ctx.roundRect(boxX, boxY, boxW, boxH, 6);
    ctx.fill();
    ctx.stroke();

    // Callout text
    ctx.fillStyle = theme.primary;
    ctx.font = '600 10px JetBrains Mono, monospace';
    ctx.fillText(title, boxX + 10, boxY + 16);

    ctx.fillStyle = '#ffffff';
    ctx.font = '400 10px Inter, sans-serif';
    ctx.fillText(detail, boxX + 10, boxY + 32);

    ctx.restore();
  }
}

if (typeof module !== "undefined" && module.exports) {
  module.exports = HologramViewer;
}

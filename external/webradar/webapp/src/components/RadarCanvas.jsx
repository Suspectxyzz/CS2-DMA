import { useRef, useEffect } from "react";
import { getRadarPosition, teamEnum } from "../utilities/utilities";

// ============================================================================
//  Constants
// ============================================================================
const SNAPSHOT_BUFFER_LIMIT = 10;
const EXTRAPOLATION_MAX_MS = 28;

const IS_MOBILE =
  (typeof window !== "undefined" && window.matchMedia &&
    window.matchMedia("(pointer: coarse)").matches) ||
  (typeof window !== "undefined" && "ontouchstart" in window);

const IS_LOCALHOST =
  typeof location !== "undefined" &&
  (location.hostname === "localhost" ||
    location.hostname === "127.0.0.1" ||
    location.hostname.startsWith("192.168."));

const RENDER_DELAY_MIN_MS = IS_MOBILE ? 72 : IS_LOCALHOST ? 18 : 44;
const RENDER_DELAY_MAX_MS = IS_MOBILE ? 132 : IS_LOCALHOST ? 58 : 106;
const INITIAL_RENDER_DELAY_MS = IS_MOBILE ? 90 : IS_LOCALHOST ? 24 : 64;
const DPR_LIMIT = IS_MOBILE ? 2 : 2.5;

// Label layout constants (Task 13)
const LABEL_MAX_WIDTH = 80; // px
const LABEL_LINE_HEIGHT = 12;
const LABEL_PADDING_X = 4;
const LABEL_PADDING_Y = 3;
const LABEL_GAP = 3;

// Autozoom constants (Task 14, reference: BoltObserv loopSlow.js)
const AUTOZOOM_SMOOTHING = 32;
const AUTOZOOM_PADDING = 0.3;
const AUTOZOOM_MIN_ZOOM = 1.3;

// Vertical indicator constants (Task 15, reference: BoltObserv loopFast.js:40-92)
const DEFAULT_Z_RANGE = { min: -300, max: 300 };
const VERT_SCALE_DELTA = 0.3;
const VERT_COLOR_RANGE = [
  [50, 120, 255],  // bottom (blue)
  [80, 220, 100],  // middle (green)
  [240, 70, 70],   // top (red)
];

// ============================================================================
//  Math helpers
// ============================================================================
const clamp = (v, min, max) => Math.min(Math.max(v, min), max);

const vw = (v) => (v * window.innerWidth) / 100;
const vmin = (v) => (v * Math.min(window.innerWidth, window.innerHeight)) / 100;

const shortestAngleDelta = (from, to) => {
  let diff = ((to - from) % 360 + 540) % 360 - 180;
  return diff;
};

const hasFiniteVec3 = (v) =>
  v && Number.isFinite(v.x) && Number.isFinite(v.y) && Number.isFinite(v.z);

const cloneVec3 = (v) => ({ x: v.x, y: v.y, z: v.z });

// Rectangle intersection test (Task 13.2, reference: viewer.js:1714-1719)
const rectsIntersect = (a, b, padding = 0) =>
  a.x < b.x + b.width + padding &&
  a.x + a.width > b.x - padding &&
  a.y < b.y + b.height + padding &&
  a.y + a.height > b.y - padding;

// Truncate text with ellipsis if exceeding maxWidth (Task 13.3, reference: viewer.js:1772-1786)
const truncateText = (ctx, text, maxWidth) => {
  if (!text) return "";
  if (ctx.measureText(text).width <= maxWidth) return text;
  const chars = Array.from(text);
  while (chars.length > 1) {
    chars.pop();
    const truncated = chars.join("") + "...";
    if (ctx.measureText(truncated).width <= maxWidth) {
      return truncated;
    }
  }
  return text.charAt(0);
};

// Blend two RGB colors (Task 15.1, reference: BoltObserv loopFast.js:57-78)
const blendColor = (c1, c2, t) => ({
  r: Math.round(c1[0] + (c2[0] - c1[0]) * t),
  g: Math.round(c1[1] + (c2[1] - c1[1]) * t),
  b: Math.round(c1[2] + (c2[2] - c1[2]) * t),
});

// Get vertical indicator color based on Z (Task 15.1)
const getVerticalColor = (z, zRange) => {
  const range = zRange.max - zRange.min;
  if (range <= 0) return null;
  let perc = (z - zRange.min) / range;
  perc = clamp(perc, 0, 1);

  const [bottom, middle, top] = VERT_COLOR_RANGE;
  let color;
  if (perc < 0.5) {
    color = blendColor(bottom, middle, perc * 2);
  } else {
    color = blendColor(middle, top, (perc - 0.5) * 2);
  }
  return `rgb(${color.r}, ${color.g}, ${color.b})`;
};

// Get vertical indicator scale based on Z (Task 15.2, reference: BoltObserv loopFast.js:82-86)
const getVerticalScale = (z, zRange, scaleDelta) => {
  const range = zRange.max - zRange.min;
  if (range <= 0) return 1;
  let perc = (z - zRange.min) / range;
  perc = clamp(perc, 0, 1);
  return 1 + (perc - 0.5) * 2 * scaleDelta;
};

// Get zRange from mapData with fallback (Task 15)
const getZRange = (mapData) => mapData?.zRange ?? DEFAULT_Z_RANGE;

// Compute autozoom scale and translation (Task 14, reference: BoltObserv loopSlow.js:20-78)
// Returns { scale, x, y } where x/y are percentages relative to container size.
const computeAutozoom = (players, mapData, settings, calibration) => {
  if (!settings.autozoom) return { scale: 1, x: 0, y: 0 };

  let minX = 1, maxX = 0, minY = 1, maxY = 0;
  let count = 0;
  for (const p of players) {
    if (p.m_is_dead) continue;
    const pos = getRadarPosition(mapData, p.m_position);
    if (!pos || (pos.x <= 0 && pos.y <= 0)) continue;
    const cal = applyCalibration(pos, calibration);
    if (cal.x < minX) minX = cal.x;
    if (cal.x > maxX) maxX = cal.x;
    if (cal.y < minY) minY = cal.y;
    if (cal.y > maxY) maxY = cal.y;
    count++;
  }
  if (count === 0) return { scale: 1, x: 0, y: 0 };

  const boundsSize = Math.max(maxX - minX, maxY - minY);
  let scale = 1 + (1 - boundsSize);
  scale = Math.max(1, scale - AUTOZOOM_PADDING);

  const centerX = (minX + maxX) / 2;
  const centerY = (minY + maxY) / 2;
  let x = (0.5 - centerX) * 100;
  let y = (0.5 - centerY) * 100;

  if (scale < AUTOZOOM_MIN_ZOOM) {
    scale = 1;
    x = 0;
    y = 0;
  }
  return { scale, x, y };
};

// Hermite cubic interpolation (reference: KevqDMA viewer.js:1363-1379)
// p0/p1 = positions, v0/v1 = velocities, t = [0,1], dtSec = interval seconds
const hermiteInterpolate = (p0, p1, v0, v1, t, dtSec) => {
  const tt = clamp(t, 0, 1);
  const t2 = tt * tt;
  const t3 = t2 * tt;
  const h00 = 2 * t3 - 3 * t2 + 1;
  const h10 = t3 - 2 * t2 + tt;
  const h01 = -2 * t3 + 3 * t2;
  const h11 = t3 - t2;
  return {
    x: h00 * p0.x + h10 * v0.x * dtSec + h01 * p1.x + h11 * v1.x * dtSec,
    y: h00 * p0.y + h10 * v0.y * dtSec + h01 * p1.y + h11 * v1.y * dtSec,
    z: (p0.z + p1.z) * 0.5,
  };
};

const deriveVelocity = (from, to, dtSec) => {
  if (!hasFiniteVec3(from) || !hasFiniteVec3(to) || dtSec <= 0) {
  return { x: 0, y: 0, z: 0 };
  }
  return {
    x: (to.x - from.x) / dtSec,
    y: (to.y - from.y) / dtSec,
    z: (to.z - from.z) / dtSec,
  };
};

// Apply per-map calibration (Task 19.5) to a normalized [0,1] radar position.
// Order: rotate around center (0.5, 0.5) → scale around center → translate.
// Returns the calibrated {x, y} in [0,1] space, or the input unchanged if no
// calibration is provided.
const applyCalibration = (pos, calibration) => {
  if (!pos || !calibration) return pos;
  const rot = Number(calibration.rotationDeg) || 0;
  const scl = Number(calibration.scale);
  const offX = Number(calibration.offsetX) || 0;
  const offY = Number(calibration.offsetY) || 0;
  if (!rot && (!Number.isFinite(scl) || scl === 1) && !offX && !offY) return pos;

  let x = pos.x - 0.5;
  let y = pos.y - 0.5;

  if (rot) {
    const rad = (rot * Math.PI) / 180;
    const cos = Math.cos(rad);
    const sin = Math.sin(rad);
    const nx = x * cos - y * sin;
    const ny = x * sin + y * cos;
    x = nx;
    y = ny;
  }

  if (Number.isFinite(scl) && scl !== 1) {
    x *= scl;
    y *= scl;
  }

  x += 0.5 + offX;
  y += 0.5 + offY;

  return { x, y };
};

// ============================================================================
//  Snapshot buffer + adaptive delay (reference: viewer.js:674-685, 895-931)
// ============================================================================
// Each snapshot: { payload, sampleTime, arrivalTime }
// payload: { playerArray, bombData, projectiles }
//
// The CS2-DMA server does not send m_capture_time / m_server_time / m_velocity,
// so we use the client-side arrival time (performance.now()) as the sample time
// and derive velocity from consecutive position samples.

const createNetState = () => ({
  frameGapMs: 8,
  arrivalJitterMs: 0,
  renderDelayMs: INITIAL_RENDER_DELAY_MS,
  lastArrivalAt: 0,
  // serverClockOffsetMs is 0 because sampleTime === arrivalTime (no server clock).
  serverClockOffsetMs: 0,
});

const pushSnapshot = (buffer, state, payload) => {
  const arrivalNow = performance.now();
  const sampleTime = arrivalNow;

  const prev = buffer.length > 0 ? buffer[buffer.length - 1] : null;
  if (prev) {
    const arrivalGap = clamp(arrivalNow - prev.arrivalTime, 1, 250);
    // Smoothed frame interval (EMA)
    state.frameGapMs = state.frameGapMs > 0
      ? state.frameGapMs * 0.65 + arrivalGap * 0.35
      : arrivalGap;
    // Jitter: deviation of arrival gap from smoothed frame gap
    const jitterSample = Math.abs(arrivalGap - state.frameGapMs);
    state.arrivalJitterMs = state.arrivalJitterMs > 0
      ? state.arrivalJitterMs * 0.82 + jitterSample * 0.18
      : jitterSample;
  }
  state.lastArrivalAt = arrivalNow;

  buffer.push({ payload, sampleTime, arrivalTime: arrivalNow });
  if (buffer.length > SNAPSHOT_BUFFER_LIMIT) {
    buffer.splice(0, buffer.length - SNAPSHOT_BUFFER_LIMIT);
  }

  updateAdaptiveDelay(state);
};

// Adaptive render delay (reference: viewer.js:674-685)
// renderDelay = max(minFloor, frameGap * 2.2, halfRtt + 10 + jitter * 1.55)
// Without server timestamps, halfRtt ≈ frameGap * 0.5.
const updateAdaptiveDelay = (state) => {
  const gap = Number.isFinite(state.frameGapMs) ? state.frameGapMs : 8;
  const jitter = Number.isFinite(state.arrivalJitterMs) ? state.arrivalJitterMs : 0;
  const halfRtt = gap * 0.5; // approximated (no server clock)
  const target = Math.max(
    RENDER_DELAY_MIN_MS,
    gap * 2.2,
    halfRtt + 10 + jitter * 1.55
  );
  if (!Number.isFinite(state.renderDelayMs) || state.renderDelayMs <= 0) {
    state.renderDelayMs = clamp(target, RENDER_DELAY_MIN_MS, RENDER_DELAY_MAX_MS);
  } else {
    state.renderDelayMs = clamp(
      state.renderDelayMs * 0.82 + target * 0.18,
      RENDER_DELAY_MIN_MS,
      RENDER_DELAY_MAX_MS
    );
  }
};

const estimateServerNow = (state, clientNow) =>
  clientNow - state.serverClockOffsetMs; // === clientNow (offset 0)

// ============================================================================
//  Sampling / interpolation (reference: viewer.js:1493-1558, 1390-1432)
// ============================================================================
const buildPlayerIndex = (players) => {
  const index = new Map();
  for (let i = 0; i < players.length; i++) {
    index.set(String(players[i].m_idx), players[i]);
  }
  return index;
};

const samplePlayerState = (prevPlayer, nextPlayer, alpha, dtMs, extrapolationMs) => {
  const latest = nextPlayer || prevPlayer;
  if (!latest) return null;

  const prevPos = hasFiniteVec3(prevPlayer?.m_position) ? cloneVec3(prevPlayer.m_position) : null;
  const nextPos = hasFiniteVec3(nextPlayer?.m_position) ? cloneVec3(nextPlayer.m_position) : prevPos ? cloneVec3(prevPos) : null;
  if (!nextPos && !prevPos) return null;

  const dtSec = Math.max(0.001, dtMs / 1000);
  let position = nextPos ? cloneVec3(nextPos) : cloneVec3(prevPos);
  let rotation = 270 - Number(latest.m_eye_angle || 0);

  if (prevPlayer && nextPlayer && prevPos && nextPos) {
    const fromVelocity = deriveVelocity(prevPos, nextPos, dtSec);
    const toVelocity = fromVelocity; // symmetric (no server velocity)
    position = hermiteInterpolate(prevPos, nextPos, fromVelocity, toVelocity, alpha, dtSec);

    const prevRotation = 270 - Number(prevPlayer.m_eye_angle || 0);
    const nextRotation = 270 - Number(nextPlayer.m_eye_angle || 0);
    rotation = prevRotation + shortestAngleDelta(prevRotation, nextRotation) * alpha;

    // Extrapolation (reference: viewer.js:1557, 1412-1416)
    if (extrapolationMs > 0 && !latest.m_is_dead) {
      const extSec = Math.min(extrapolationMs, EXTRAPOLATION_MAX_MS) / 1000;
      position.x += toVelocity.x * extSec;
      position.y += toVelocity.y * extSec;
      rotation += (shortestAngleDelta(prevRotation, nextRotation) / dtSec) * extSec;
    }
  }

  return { ...latest, m_position: position, _rotation: rotation };
};

// Build an interpolated payload for the given render time.
// Returns { players, bomb, projectiles } or null.
const sampleRenderPayload = (buffer, state, clientNow) => {
  if (!buffer.length) return null;

  const estimatedServerNow = estimateServerNow(state, clientNow);
  const renderTime = estimatedServerNow - state.renderDelayMs;

  if (buffer.length === 1) {
    const only = buffer[0];
    const extMs = clamp(renderTime - only.sampleTime, 0, EXTRAPOLATION_MAX_MS);
    return {
      players: only.payload.playerArray.map((p) =>
        samplePlayerState(p, p, 1, state.frameGapMs, extMs)
      ).filter(Boolean),
      bomb: only.payload.bombData,
      projectiles: only.payload.projectiles,
    };
  }

  // renderTime before oldest snapshot → clamp to oldest
  if (renderTime <= buffer[0].sampleTime) {
    const only = buffer[0];
    return {
      players: only.payload.playerArray.map((p) =>
        samplePlayerState(p, p, 0, state.frameGapMs, 0)
      ).filter(Boolean),
      bomb: only.payload.bombData,
      projectiles: only.payload.projectiles,
    };
  }

  // Find the bracketing pair
  for (let i = 1; i < buffer.length; i++) {
    const older = buffer[i - 1];
    const newer = buffer[i];
    if (renderTime <= newer.sampleTime) {
      const dtMs = Math.max(1, newer.sampleTime - older.sampleTime);
      const alpha = clamp((renderTime - older.sampleTime) / dtMs, 0, 1);
      return interpolatePayloads(older.payload, newer.payload, alpha, dtMs, 0);
    }
  }

  // renderTime after newest → extrapolate (reference: viewer.js:1554-1558)
  const older = buffer[buffer.length - 2];
  const newer = buffer[buffer.length - 1];
  const dtMs = Math.max(1, newer.sampleTime - older.sampleTime);
  const extrapolationMs = clamp(renderTime - newer.sampleTime, 0, EXTRAPOLATION_MAX_MS);
  return interpolatePayloads(older.payload, newer.payload, 1, dtMs, extrapolationMs);
};

const interpolatePayloads = (prevPayload, nextPayload, alpha, dtMs, extrapolationMs) => {
  const prevIndex = buildPlayerIndex(prevPayload.playerArray || []);
  const nextIndex = buildPlayerIndex(nextPayload.playerArray || []);
  const keys = new Set([...prevIndex.keys(), ...nextIndex.keys()]);
  const players = [];
  for (const key of keys) {
    const sampled = samplePlayerState(
      prevIndex.get(key),
      nextIndex.get(key),
      alpha,
      dtMs,
      extrapolationMs
    );
    if (sampled) players.push(sampled);
  }
  players.sort((a, b) => Number(a.m_idx) - Number(b.m_idx));
  return {
    players,
    bomb: nextPayload.bombData || prevPayload.bombData,
    projectiles: nextPayload.projectiles || prevPayload.projectiles || [],
  };
};

// ============================================================================
//  Canvas drawing helpers
// ============================================================================
const roundRect = (ctx, x, y, w, h, r) => {
  const rad = Math.min(r, w * 0.5, h * 0.5);
  ctx.beginPath();
  ctx.moveTo(x + rad, y);
  ctx.arcTo(x + w, y, x + w, y + h, rad);
  ctx.arcTo(x + w, y + h, x, y + h, rad);
  ctx.arcTo(x, y + h, x, y, rad);
  ctx.arcTo(x, y, x + w, y, rad);
  ctx.closePath();
};

const getPlayerColor = (player, localTeam) => {
  if (player.m_is_local) return "#ffffff";
  if (player.m_team !== localTeam) return "#ff2f2f";
  if (player.m_team === teamEnum.terrorist) return "#df7d29";
  return "#84c8ed";
};

// Draw a single player marker (reference: viewer.js:1654-1712)
// Labels are no longer drawn here; they are collected and laid out separately
// for anti-collision (Task 13). Vertical indicator applied here (Task 15).
const drawPlayer = (ctx, player, mapData, localTeam, settings, mapRotation, imgW, imgH, calibration) => {
  const radarPos = getRadarPosition(mapData, player.m_position);
  if (!radarPos || (radarPos.x <= 0 && radarPos.y <= 0)) return;
  const calibratedPos = applyCalibration(radarPos, calibration);

  const x = imgW * calibratedPos.x;
  const y = imgH * calibratedPos.y;
  let dotSize = vw(0.7 * (settings.dotSize ?? 1));
  const deadOpacity = settings.deadPlayerOpacity ?? 0.4;
  const coneScale = settings.viewConeSize ?? 1;
  const rotation = player._rotation != null ? player._rotation : 270 - (player.m_eye_angle || 0);

  // Hide dead players if setting is off
  if (player.m_is_dead && !settings.showDeadPlayers) return;

  // Vertical indicator (Task 15): color or scale mode
  const vertIndicator = settings.vertIndicator ?? "none";
  let vertColor = null;
  if (!player.m_is_dead && vertIndicator !== "none" && player.m_position) {
    const zRange = getZRange(mapData);
    if (vertIndicator === "color") {
      vertColor = getVerticalColor(player.m_position.z, zRange);
    } else if (vertIndicator === "scale") {
      dotSize *= getVerticalScale(player.m_position.z, zRange, VERT_SCALE_DELTA);
    }
  }

  const bodySize = dotSize * 0.85;
  const half = bodySize * 0.5;
  const radius = bodySize * 0.3;

  ctx.save();
  ctx.translate(x, y);

  if (player.m_is_dead) {
    // Dead: draw X cross
    ctx.globalAlpha = deadOpacity;
    const crossSize = dotSize * 0.35;
    ctx.lineWidth = 1.5;
    ctx.lineCap = "round";
    ctx.strokeStyle = "rgba(255, 255, 255, 0.9)";
    ctx.beginPath();
    ctx.moveTo(-crossSize, -crossSize);
    ctx.lineTo(crossSize, crossSize);
    ctx.moveTo(crossSize, -crossSize);
    ctx.lineTo(-crossSize, crossSize);
    ctx.stroke();
    ctx.restore();
    return;
  }

  // --- Alive player ---
  ctx.globalAlpha = 1;

  // View cone (drawn first, behind body)
  if (settings.showViewCones) {
    ctx.save();
    ctx.rotate((rotation * Math.PI) / 180);
    const coneWidth = vw(1.5 * coneScale);
    const coneLength = vw(3 * coneScale);
    ctx.globalAlpha = 0.3;
    ctx.fillStyle = "#ffffff";
    ctx.beginPath();
    // Cone opens forward: base at player (0), apex pointing backward (-coneLength)
    // After rotation by (270 - eye_angle), this matches the original DOM geometry.
    ctx.moveTo(-coneWidth * 0.5, 0);
    ctx.lineTo(coneWidth * 0.5, 0);
    ctx.lineTo(0, -coneLength);
    ctx.closePath();
    ctx.fill();
    ctx.restore();
  }

  // Bomb carrier dashed ring
  if (player.m_has_bomb) {
    ctx.save();
    ctx.setLineDash([3, 2]);
    ctx.lineWidth = 1.25;
    ctx.strokeStyle = "rgba(255, 176, 72, 0.95)";
    ctx.beginPath();
    ctx.arc(0, 0, dotSize * 0.65, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.restore();
  }

  // Player body: rounded rectangle, rotated to facing direction
  ctx.save();
  ctx.rotate((rotation * Math.PI) / 180);
  ctx.fillStyle = vertColor || getPlayerColor(player, localTeam);
  roundRect(ctx, -half, -half, bodySize, bodySize, radius);
  ctx.fill();
  ctx.lineWidth = 1.05;
  ctx.strokeStyle = "rgba(7, 18, 29, 0.95)";
  ctx.stroke();
  ctx.restore();

  ctx.restore();
};

// Collect label info for a player (Task 13). Returns null if no label needed.
// Text is truncated to LABEL_MAX_WIDTH (in unscaled coordinates).
const collectLabelInfo = (ctx, player, mapData, localTeam, settings, mapRotation, imgW, imgH, calibration) => {
  const radarPos = getRadarPosition(mapData, player.m_position);
  if (!radarPos || (radarPos.x <= 0 && radarPos.y <= 0)) return null;
  const calibratedPos = applyCalibration(radarPos, calibration);

  const showName =
    (settings.showAllNames && player.m_team === localTeam) ||
    (settings.showEnemyNames && player.m_team !== localTeam);
  const showWeapon = settings.showWeapon ?? true;
  const activeWeapon = showWeapon && player.m_weapons && player.m_weapons.m_active;

  if (!showName && !(settings.showHealth || (showWeapon && activeWeapon))) return null;
  if (player.m_is_dead && !showName) return null;

  const infoScale = settings.infoTextSize ?? 1;
  const maxTextWidth = LABEL_MAX_WIDTH / infoScale;

  const lines = [];
  if (showName) {
    ctx.font = "11px TASAOrbiter, sans-serif";
    const text = truncateText(ctx, player.m_name, maxTextWidth);
    lines.push({ text, color: "#ffffff", font: "11px TASAOrbiter, sans-serif" });
  }
  if (settings.showHealth && !player.m_is_dead) {
    lines.push({ text: `${player.m_health}hp`, color: "#4ade80", font: "10px TASAOrbiter, sans-serif" });
  }
  if (showWeapon && activeWeapon && !player.m_is_dead) {
    ctx.font = "10px TASAOrbiter, sans-serif";
    const text = truncateText(ctx, activeWeapon, maxTextWidth);
    lines.push({ text, color: "rgba(253, 224, 71, 0.8)", font: "10px TASAOrbiter, sans-serif" });
  }
  if (!lines.length) return null;

  const x = imgW * calibratedPos.x;
  const y = imgH * calibratedPos.y;
  let dotSize = vw(0.7 * (settings.dotSize ?? 1));

  // Match the scale-mode dotSize so label gap tracks the rendered marker size
  const vertIndicator = settings.vertIndicator ?? "none";
  if (!player.m_is_dead && vertIndicator === "scale" && player.m_position) {
    const zRange = getZRange(mapData);
    dotSize *= getVerticalScale(player.m_position.z, zRange, VERT_SCALE_DELTA);
  }

  return {
    key: String(player.m_idx),
    centerX: x,
    centerY: y,
    dotSize,
    lines,
    infoScale,
    mapRotation,
  };
};

// Layout labels with anti-collision (Task 13.1 + 13.2).
// 6 candidate positions per label: right-up, right-down, left-up, left-down, up, down.
// Picks first non-colliding candidate; falls back to least-colliding candidate.
const layoutLabels = (ctx, labelInfos, canvasW, canvasH) => {
  const placedRects = [];
  const results = [];

  for (const info of labelInfos) {
    const { centerX, centerY, dotSize, lines, infoScale } = info;

    // Measure box size from truncated lines
    let maxTextWidth = 0;
    for (const line of lines) {
      ctx.font = line.font;
      const w = ctx.measureText(line.text).width;
      if (w > maxTextWidth) maxTextWidth = w;
    }
    const boxWidth = (maxTextWidth + LABEL_PADDING_X * 2) * infoScale;
    const boxHeight = (lines.length * LABEL_LINE_HEIGHT + LABEL_PADDING_Y * 2) * infoScale;
    const gap = dotSize * 0.5 + LABEL_GAP;

    // 6 candidate positions (Task 13.1)
    const candidates = [
      { x: centerX + gap, y: centerY - boxHeight - gap },            // 1. right-up
      { x: centerX + gap, y: centerY + gap },                         // 2. right-down
      { x: centerX - boxWidth - gap, y: centerY - boxHeight - gap },  // 3. left-up
      { x: centerX - boxWidth - gap, y: centerY + gap },              // 4. left-down
      { x: centerX - boxWidth * 0.5, y: centerY - boxHeight - gap },  // 5. up
      { x: centerX - boxWidth * 0.5, y: centerY + gap },              // 6. down
    ];

    let chosenRect = null;
    let fallbackRect = null;
    let minCollisions = Infinity;

    for (const candidate of candidates) {
      const rect = {
        x: clamp(candidate.x, 2, canvasW - boxWidth - 2),
        y: clamp(candidate.y, 2, canvasH - boxHeight - 2),
        width: boxWidth,
        height: boxHeight,
      };
      if (!fallbackRect) fallbackRect = rect;

      let collisions = 0;
      for (const placed of placedRects) {
        if (rectsIntersect(rect, placed, 2)) collisions++;
      }

      if (collisions === 0) {
        chosenRect = rect;
        break;
      }
      if (collisions < minCollisions) {
        minCollisions = collisions;
        fallbackRect = rect;
      }
    }

    const rect = chosenRect || fallbackRect;
    if (!rect) continue;
    placedRects.push(rect);
    results.push({ ...info, rect });
  }

  return results;
};

// Draw laid-out labels (Task 13). Background pill + multi-line text, counter-rotated.
const drawLabels = (ctx, layouts) => {
  for (const layout of layouts) {
    const { rect, lines, infoScale, mapRotation } = layout;
    const cx = rect.x + rect.width / 2;
    const cy = rect.y + rect.height / 2;

    ctx.save();
    ctx.translate(cx, cy);
    if (mapRotation) ctx.rotate((-mapRotation * Math.PI) / 180);
    ctx.scale(infoScale, infoScale);

    const localW = rect.width / infoScale;
    const localH = rect.height / infoScale;

    // Background pill
    ctx.fillStyle = "rgba(6, 16, 24, 0.82)";
    roundRect(ctx, -localW / 2, -localH / 2, localW, localH, 3);
    ctx.fill();

    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    const startY = -((lines.length - 1) * LABEL_LINE_HEIGHT) / 2;
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      ctx.font = line.font;
      ctx.fillStyle = line.color;
      ctx.fillText(line.text, 0, startY + i * LABEL_LINE_HEIGHT);
    }

    ctx.restore();
  }
};

// Draw bomb marker
const drawBomb = (ctx, bombData, playerArray, mapData, localTeam, settings, imgW, imgH, time, calibration) => {
  if (!bombData) return;

  const isPlanted = bombData.m_blow_time > 0 && !bombData.m_is_defused;
  const hasPosition = bombData.x !== 0 || bombData.y !== 0;

  // Carried (no position, not planted) — player ring handles it
  if (!isPlanted && !hasPosition) return;

  const radarPos = getRadarPosition(mapData, bombData);
  if (!radarPos || (radarPos.x <= 0 && radarPos.y <= 0)) return;
  const calibratedPos = applyCalibration(radarPos, calibration);

  const x = imgW * calibratedPos.x;
  const y = imgH * calibratedPos.y;
  const size = vw(1.5 * (settings.bombSize ?? 0.5));

  ctx.save();
  ctx.translate(x, y);

  if (isPlanted) {
    // Planted: solid circle + glow + countdown
    const pulse = 0.85 + Math.sin(time * 0.008) * 0.15;
    // Glow
    ctx.globalAlpha = 0.25 * pulse;
    ctx.fillStyle = "#ff2f2f";
    ctx.beginPath();
    ctx.arc(0, 0, size * 1.8, 0, Math.PI * 2);
    ctx.fill();
    // Solid circle
    ctx.globalAlpha = 1;
    ctx.fillStyle = bombData.m_is_defusing ? "#ffcc00" : "#c90b0b";
    ctx.beginPath();
    ctx.arc(0, 0, size * 0.6, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = 1;
    ctx.strokeStyle = "rgba(7, 18, 29, 0.8)";
    ctx.stroke();
    // Countdown text
    if (settings.showBombTimer !== false) {
      ctx.fillStyle = "#ffffff";
      ctx.font = "bold 11px TASAOrbiter, sans-serif";
      ctx.textAlign = "center";
      ctx.textBaseline = "top";
      ctx.fillText(bombData.m_blow_time.toFixed(1), 0, size * 0.7);
    }
  } else {
    // Dropped: dashed circle
    ctx.setLineDash([3, 2]);
    ctx.lineWidth = 1.5;
    ctx.strokeStyle =
      bombData.m_is_defused
        ? "#50904c"
        : localTeam === teamEnum.counterTerrorist
        ? "#6492b4"
        : "#c90b0b";
    ctx.beginPath();
    ctx.arc(0, 0, size * 0.6, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setLineDash([]);
    // Inner dot
    ctx.fillStyle = ctx.strokeStyle;
    ctx.beginPath();
    ctx.arc(0, 0, size * 0.2, 0, Math.PI * 2);
    ctx.fill();
  }

  ctx.restore();
};

// Draw a single projectile (migrated from Projectiles.jsx DOM → Canvas)
const drawProjectile = (ctx, projectile, imgW, imgH, time) => {
  const pos = projectile.m_position;
  if (!pos || pos.x == null || pos.y == null) return;

  const x = imgW * pos.x;
  const y = imgH * pos.y;
  const type = projectile.m_type;
  const life = projectile.m_life_remaining;

  ctx.save();
  ctx.translate(x, y);

  if (type === "smoke") {
    const radius = vmin(7.5);
    let alpha = 0.6;
    if (life != null && life < 2) {
      alpha = clamp(life / 2, 0, 1) * 0.6;
    }
    ctx.globalAlpha = alpha;
    ctx.fillStyle = "rgba(159, 157, 157, 0.6)";
    ctx.beginPath();
    ctx.arc(0, 0, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = vmin(0.3);
    ctx.strokeStyle = "rgba(215, 215, 215, 0.8)";
    ctx.stroke();
  } else if (type === "inferno") {
    const pulse = 1 + Math.sin(time * 0.005) * 0.08;
    const radius = vmin(5) * pulse;
    ctx.globalAlpha = 0.5 + Math.sin(time * 0.005) * 0.15;
    ctx.fillStyle = "rgba(255, 72, 72, 0.5)";
    ctx.beginPath();
    ctx.arc(0, 0, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = vmin(0.15);
    ctx.strokeStyle = "#ce3f00";
    ctx.stroke();
  } else if (type === "flash") {
    const radius = vmin(1.5);
    ctx.globalAlpha = 0.9;
    ctx.fillStyle = "#ffffff";
    drawStar(ctx, 0, 0, 5, radius, radius * 0.5);
    ctx.fill();
  } else if (type === "explosive") {
    const isExploding = life != null && life < 0.5;
    const radius = vmin(1.5);
    if (isExploding) {
      const progress = 1 - clamp(life / 0.5, 0, 1);
      ctx.globalAlpha = 0.8 * (1 - progress);
      ctx.fillStyle = "rgba(255, 60, 60, 0.8)";
      ctx.beginPath();
      ctx.arc(0, 0, radius * (1 + progress * 2), 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 0.8;
    ctx.fillStyle = "rgba(255, 60, 60, 0.8)";
    ctx.beginPath();
    ctx.arc(0, 0, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = vmin(0.1);
    ctx.strokeStyle = "#ff0000";
    ctx.stroke();
  } else if (type === "decoy") {
    const radius = vmin(1.5);
    ctx.globalAlpha = 0.7;
    ctx.fillStyle = "rgba(255, 200, 0, 0.7)";
    ctx.beginPath();
    ctx.arc(0, 0, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = vmin(0.1);
    ctx.strokeStyle = "#ffcc00";
    ctx.stroke();
  }

  ctx.restore();
};

const drawStar = (ctx, cx, cy, spikes, outerRadius, innerRadius) => {
  let rot = (Math.PI / 2) * 3;
  const step = Math.PI / spikes;
  ctx.beginPath();
  ctx.moveTo(cx, cy - outerRadius);
  for (let i = 0; i < spikes; i++) {
    ctx.lineTo(cx + Math.cos(rot) * outerRadius, cy + Math.sin(rot) * outerRadius);
    rot += step;
    ctx.lineTo(cx + Math.cos(rot) * innerRadius, cy + Math.sin(rot) * innerRadius);
    rot += step;
  }
  ctx.lineTo(cx, cy - outerRadius);
  ctx.closePath();
};

// ============================================================================
//  RadarCanvas component
// ============================================================================
const RadarCanvas = ({
  playerArray,
  bombData,
  projectiles,
  mapData,
  localTeam,
  settings,
  mapRotation = 0,
  calibration,
  radarImageRef,
  autozoomRef,
}) => {
  const canvasRef = useRef();
  const bufferRef = useRef([]);
  const netStateRef = useRef(createNetState());
  const lastMapRef = useRef(null);
  // Autozoom smoothing queues (Task 14.3, reference: BoltObserv loopSlow.js:64-75)
  const autozoomStateRef = useRef({
    scaleQueue: [1],
    xQueue: [0],
    yQueue: [0],
  });
  // Keep latest props accessible inside the rAF loop without restarting it
  const propsRef = useRef({});
  propsRef.current = { playerArray, bombData, projectiles, mapData, localTeam, settings, mapRotation, calibration };

  // Push a snapshot whenever game data changes
  useEffect(() => {
    if (!playerArray || !playerArray.length) return;
    // Reset buffer on map change (positions are map-specific)
    if (mapData && mapData.name !== lastMapRef.current) {
      bufferRef.current = [];
      netStateRef.current = createNetState();
      lastMapRef.current = mapData.name;
    }
    pushSnapshot(bufferRef.current, netStateRef.current, {
      playerArray,
      bombData,
      projectiles: projectiles || [],
    });
  }, [playerArray, bombData, projectiles, mapData]);

  // Canvas sizing + DPR adaptation
  useEffect(() => {
    const canvas = canvasRef.current;
    const img = radarImageRef?.current;
    if (!canvas || !img) return;

    const resize = () => {
      const cssW = img.clientWidth;
      const cssH = img.clientHeight;
      if (!cssW || !cssH) return;
      const dpr = Math.min(window.devicePixelRatio || 1, DPR_LIMIT);
      const pxW = Math.max(1, Math.round(cssW * dpr));
      const pxH = Math.max(1, Math.round(cssH * dpr));
      if (canvas.width !== pxW || canvas.height !== pxH) {
        canvas.width = pxW;
        canvas.height = pxH;
        canvas.style.width = cssW + "px";
        canvas.style.height = cssH + "px";
      }
    };

    resize();
    const ro = new ResizeObserver(resize);
    ro.observe(img);
    return () => ro.disconnect();
  }, [radarImageRef, mapData]);

  // rAF render loop — runs once, reads everything from refs
  useEffect(() => {
    let raf;
    const loop = () => {
      const canvas = canvasRef.current;
      const img = radarImageRef?.current;
      if (canvas && img && img.clientWidth && img.clientHeight) {
        const ctx = canvas.getContext("2d", { alpha: true });
        const dpr = Math.min(window.devicePixelRatio || 1, DPR_LIMIT);
        const cssW = img.clientWidth;
        const cssH = img.clientHeight;
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.clearRect(0, 0, cssW, cssH);

        const {
          mapData: md,
          localTeam: lt,
          settings: st,
          mapRotation: mr,
          calibration: cal,
          playerArray: pa,
          bombData: bd,
          projectiles: pr,
        } = propsRef.current;
        const useInterp = st.smoothTransition !== false;

        let sampled = null;
        if (useInterp) {
          sampled = sampleRenderPayload(bufferRef.current, netStateRef.current, performance.now());
        }
        if (!sampled) {
          // Fallback: latest data directly
          sampled = {
            players: pa || [],
            bomb: bd,
            projectiles: pr || [],
          };
        }

        if (md && sampled.players.length) {
          // 1. Projectiles (bottom layer)
          for (const proj of sampled.projectiles) {
            drawProjectile(ctx, proj, cssW, cssH, performance.now());
          }
          // 2. Dead players
          for (const p of sampled.players) {
            if (p.m_is_dead) drawPlayer(ctx, p, md, lt, st, mr, cssW, cssH, cal);
          }
          // 3. Bomb
          drawBomb(ctx, sampled.bomb, sampled.players, md, lt, st, cssW, cssH, performance.now(), cal);
          // 4. Alive players (top layer)
          for (const p of sampled.players) {
            if (!p.m_is_dead) drawPlayer(ctx, p, md, lt, st, mr, cssW, cssH, cal);
          }
          // 5. Labels with anti-collision layout (Task 13)
          const labelInfos = [];
          for (const p of sampled.players) {
            const info = collectLabelInfo(ctx, p, md, lt, st, mr, cssW, cssH, cal);
            if (info) labelInfos.push(info);
          }
          if (labelInfos.length) {
            const layouts = layoutLabels(ctx, labelInfos, cssW, cssH);
            drawLabels(ctx, layouts);
          }
        }

        // Autozoom (Task 14): compute bounding box, smooth via 32-frame queue,
        // apply transform to the autozoom container.
        const az = computeAutozoom(sampled.players, md, st, cal);
        const azState = autozoomStateRef.current;
        azState.scaleQueue.unshift(az.scale);
        azState.scaleQueue = azState.scaleQueue.slice(0, AUTOZOOM_SMOOTHING);
        azState.xQueue.unshift(az.x);
        azState.xQueue = azState.xQueue.slice(0, AUTOZOOM_SMOOTHING);
        azState.yQueue.unshift(az.y);
        azState.yQueue = azState.yQueue.slice(0, AUTOZOOM_SMOOTHING);
        const avgScale = azState.scaleQueue.reduce((a, b) => a + b, 0) / azState.scaleQueue.length;
        const avgX = azState.xQueue.reduce((a, b) => a + b, 0) / azState.xQueue.length;
        const avgY = azState.yQueue.reduce((a, b) => a + b, 0) / azState.yQueue.length;
        if (autozoomRef?.current) {
          autozoomRef.current.style.transform =
            `scale(${avgScale}) translate(${avgX}%, ${avgY}%)`;
        }
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, [radarImageRef, autozoomRef]);

  return (
    <canvas
      ref={canvasRef}
      className="absolute left-0 top-0 pointer-events-none"
      style={{ zIndex: 2 }}
    />
  );
};

export default RadarCanvas;

import { useRef, useEffect, useState, useCallback } from 'react';
import './RobotTopDown.css';

const OFFSETS_STORAGE_KEY = "pacbot_sensor_offsets";
const SENSOR_LABELS = ["N", "S", "W", "E", "NE", "SE", "SW", "NW"];

function loadOffsets() {
  try {
    const raw = JSON.parse(localStorage.getItem(OFFSETS_STORAGE_KEY));
    if (Array.isArray(raw) && raw.length === 8) return raw.map(n => Number(n) || 0);
  } catch { /* ignore */ }
  return [0, 0, 0, 0, 0, 0, 0, 0];
}

// Sensor slot angles in degrees (0 = North, clockwise).
// Slots 0-3 from config.h, slots 4-7 unmounted placeholders.
const SENSOR_ANGLES = [
  0,    // slot 0 = North
  180,  // slot 1 = South
  270,  // slot 2 = West
  90,   // slot 3 = East
  45,   // slot 4 = NE (unmounted)
  135,  // slot 5 = SE (unmounted)
  225,  // slot 6 = SW (unmounted)
  315,  // slot 7 = NW (unmounted)
];

function draw(canvas, sensors, direction) {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  const ctx = canvas.getContext("2d");
  ctx.scale(dpr, dpr);
  const w = rect.width, h = rect.height;
  const cx = w / 2, cy = h / 2;
  const scale = Math.min(w, h) / 200;
  const robotRadius = 40 * scale;

  ctx.clearRect(0, 0, w, h);

  // Grid
  const gridStep = 40 * scale;
  ctx.strokeStyle = "#111";
  for (let x = 0; x < w; x += gridStep) {
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
  }
  for (let y = 0; y < h; y += gridStep) {
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
  }

  // Sensor lines
  const maxDist = 2000;
  const maxLineLen = 150 * scale;
  for (let i = 0; i < 8; i++) {
    const angleDeg = SENSOR_ANGLES[i] - 90; // -90 converts from N=0° to screen coords (right=0°)
    const angleRad = angleDeg * Math.PI / 180;
    const dist = sensors[i];
    if (dist < 0) continue;
    const lineLen = robotRadius + (dist / maxDist) * maxLineLen;

    ctx.strokeStyle = dist < 150 ? "#f00" : dist < 400 ? "#ff0" : "#0f0";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(cx + Math.cos(angleRad) * robotRadius, cy + Math.sin(angleRad) * robotRadius);
    ctx.lineTo(cx + Math.cos(angleRad) * lineLen, cy + Math.sin(angleRad) * lineLen);
    ctx.stroke();

    const labelOffset = lineLen + 14 * scale;
    ctx.fillStyle = "#888";
    ctx.font = `${Math.round(10 * scale)}px Courier New`;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(
      Math.round(dist) + "mm",
      cx + Math.cos(angleRad) * labelOffset,
      cy + Math.sin(angleRad) * labelOffset
    );
  }

  // Robot body
  ctx.strokeStyle = "#0f0";
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.arc(cx, cy, robotRadius, 0, Math.PI * 2);
  ctx.stroke();

  // Front indicator
  ctx.fillStyle = "#0f0";
  ctx.beginPath();
  ctx.arc(cx, cy - robotRadius + 8 * scale, 4 * scale, 0, Math.PI * 2);
  ctx.fill();

  // Direction arrow
  if (direction) {
    const dirs = { forward: -90, backward: 90, left: 180, right: 0 };
    const aRad = dirs[direction] * Math.PI / 180;
    const arrowLen = 25 * scale;
    const ax = cx + Math.cos(aRad) * arrowLen;
    const ay = cy + Math.sin(aRad) * arrowLen;
    ctx.strokeStyle = "#ff0";
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(ax, ay);
    ctx.stroke();
    const headLen = 8 * scale;
    ctx.beginPath();
    ctx.moveTo(ax, ay);
    ctx.lineTo(ax - headLen * Math.cos(aRad - 0.4), ay - headLen * Math.sin(aRad - 0.4));
    ctx.moveTo(ax, ay);
    ctx.lineTo(ax - headLen * Math.cos(aRad + 0.4), ay - headLen * Math.sin(aRad + 0.4));
    ctx.stroke();
  }

  // Wheels
  for (let i = 0; i < 4; i++) {
    const a = (45 + i * 90 - 90) * Math.PI / 180;
    const wx = cx + Math.cos(a) * robotRadius;
    const wy = cy + Math.sin(a) * robotRadius;
    ctx.fillStyle = "#666";
    ctx.fillRect(wx - 5 * scale, wy - 3 * scale, 10 * scale, 6 * scale);
  }
}

export default function RobotTopDown({ sensors, direction, send }) {
  const canvasRef = useRef(null);
  const [offsets, setOffsets] = useState(loadOffsets);
  const [showOffsets, setShowOffsets] = useState(false);

  useEffect(() => {
    if (canvasRef.current && sensors) {
      draw(canvasRef.current, sensors, direction);
    }
  }, [sensors, direction]);

  const updateOffset = useCallback((idx, val) => {
    setOffsets(prev => {
      const next = [...prev];
      next[idx] = val;
      return next;
    });
  }, []);

  const sendOffsets = useCallback(() => {
    const numeric = offsets.map(v => {
      const n = parseInt(v, 10);
      return Number.isFinite(n) ? n : 0;
    });
    localStorage.setItem(OFFSETS_STORAGE_KEY, JSON.stringify(numeric));
    send?.({ type: "set_sensor_offsets", offsets: numeric });
  }, [offsets, send]);

  return (
    <section className="panel robot-panel">
      <div className="robot-panel-header">
        <h2>TOP-DOWN VIEW</h2>
        <button
          className="offset-toggle"
          onClick={() => setShowOffsets(v => !v)}
          title={showOffsets ? "Hide sensor offsets" : "Show sensor offsets"}
        >
          {showOffsets ? "−" : "+"}
        </button>
      </div>
      <canvas ref={canvasRef} />
      {showOffsets && (
        <div className="sensor-offsets">
          <div className="offset-grid">
            {SENSOR_LABELS.map((label, i) => (
              <label key={i}>
                <span>{label}</span>
                <input
                  type="number"
                  step="1"
                  value={offsets[i]}
                  onChange={(e) => updateOffset(i, e.target.value)}
                />
              </label>
            ))}
          </div>
          <button className="cmd-btn offset-send" onClick={sendOffsets}>SEND OFFSETS</button>
        </div>
      )}
    </section>
  );
}

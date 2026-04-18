import { useRef, useEffect, useState } from 'react';
import './IMUPanel.css';

const HISTORY_LEN = 120;

function drawGraph(canvas, data, color, rangeMin, rangeMax) {
  const ctx = canvas.getContext("2d");
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  const zeroY = h * (1 - (0 - rangeMin) / (rangeMax - rangeMin));
  ctx.strokeStyle = "#222";
  ctx.beginPath();
  ctx.moveTo(0, zeroY);
  ctx.lineTo(w, zeroY);
  ctx.stroke();

  if (data.length < 2) return;
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  for (let i = 0; i < data.length; i++) {
    const x = (i / (HISTORY_LEN - 1)) * w;
    const y = h * (1 - (data[i] - rangeMin) / (rangeMax - rangeMin));
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

function IMUGraph({ label, value, color, rangeMin, rangeMax, historyRef, tick }) {
  const canvasRef = useRef(null);

  useEffect(() => {
    if (canvasRef.current && historyRef.current) {
      drawGraph(canvasRef.current, historyRef.current, color, rangeMin, rangeMax);
    }
  }, [tick, color, rangeMin, rangeMax, historyRef]);

  return (
    <div className="imu-row">
      <label>{label} <span className="imu-value">{value.toFixed(2)}&deg;</span></label>
      <canvas ref={canvasRef} width={300} height={80} />
    </div>
  );
}

export default function IMUPanel({ imu }) {
  const yawHistory = useRef([]);
  const pitchHistory = useRef([]);
  const rollHistory = useRef([]);
  const [tick, setTick] = useState(0);

  const yaw = imu?.yaw ?? 0;
  const pitch = imu?.pitch ?? 0;
  const roll = imu?.roll ?? 0;

  // imu is a new object reference on every state update (from JSON.parse),
  // so this fires every time, even when values are unchanged — giving scrolling flat lines
  useEffect(() => {
    if (!imu) return;
    function push(arr, val) {
      arr.push(val);
      if (arr.length > HISTORY_LEN) arr.shift();
    }
    push(yawHistory.current, imu.yaw ?? 0);
    push(pitchHistory.current, imu.pitch ?? 0);
    push(rollHistory.current, imu.roll ?? 0);
    setTick(t => t + 1);
  }, [imu]);

  return (
    <section className="panel imu-panel">
      <h2>IMU</h2>
      <IMUGraph label="YAW" value={yaw} color="#0f0" rangeMin={0} rangeMax={360} historyRef={yawHistory} tick={tick} />
      <IMUGraph label="PITCH" value={pitch} color="#0ff" rangeMin={-30} rangeMax={30} historyRef={pitchHistory} tick={tick} />
      <IMUGraph label="ROLL" value={roll} color="#f0f" rangeMin={-180} rangeMax={180} historyRef={rollHistory} tick={tick} />
    </section>
  );
}

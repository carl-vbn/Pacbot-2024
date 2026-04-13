import { useState, useCallback } from 'react';
import './RobotStatusPanel.css';

export default function RobotStatusPanel({ robotState, send }) {
  const [intervalMs, setIntervalMs] = useState(50);

  const sendStartLog = useCallback(() => send({ type: "start_log" }), [send]);
  const sendStopLog = useCallback(() => send({ type: "stop_log" }), [send]);
  const sendStatus = useCallback(() => send({ type: "status" }), [send]);
  const sendPing = useCallback(() => send({ type: "ping" }), [send]);
  const sendInterval = useCallback(() => {
    send({ type: "set_interval", interval_ms: intervalMs });
  }, [send, intervalMs]);

  const state = robotState?.robot_state ?? "disconnected";
  const uptime = robotState?.uptime_ms ?? 0;
  const sensorMask = robotState?.sensor_mask ?? 0;
  const imuPresent = robotState?.imu_present ?? false;
  const numMotors = robotState?.num_motors ?? 0;
  const driveMode = robotState?.drive_mode ?? 0;

  const sensorSlots = [];
  for (let i = 0; i < 8; i++) {
    if (sensorMask & (1 << i)) sensorSlots.push(i);
  }

  return (
    <section className="panel robot-status-panel">
      <h2>ROBOT STATUS</h2>
      <div className="status-info">
        <div className="status-row">
          <span className="status-label">STATE</span>
          <span className="status-value">{state.toUpperCase()}</span>
        </div>
        <div className="status-row">
          <span className="status-label">UPTIME</span>
          <span className="status-value">{(uptime / 1000).toFixed(1)}s</span>
        </div>
        <div className="status-row">
          <span className="status-label">SENSORS</span>
          <span className="status-value">{sensorSlots.length > 0 ? sensorSlots.join(', ') : 'N/A'}</span>
        </div>
        <div className="status-row">
          <span className="status-label">IMU</span>
          <span className="status-value">{imuPresent ? 'YES' : 'NO'}</span>
        </div>
        <div className="status-row">
          <span className="status-label">MOTORS</span>
          <span className="status-value">{numMotors}</span>
        </div>
        <div className="status-row">
          <span className="status-label">DRIVE MODE</span>
          <span className="status-value">{driveMode === 1 ? 'CARDINAL' : 'MANUAL'}</span>
        </div>
      </div>
      <div className="command-buttons">
        <button className="cmd-btn" onClick={sendStartLog}>START LOG</button>
        <button className="cmd-btn" onClick={sendStopLog}>STOP LOG</button>
      </div>
      <div className="command-buttons">
        <button className="cmd-btn" onClick={sendStatus}>STATUS</button>
        <button className="cmd-btn" onClick={sendPing}>PING</button>
      </div>
      <div className="interval-control">
        <label>
          INTERVAL
          <input
            type="number" min="10" step="10"
            value={intervalMs}
            onChange={(e) => setIntervalMs(parseInt(e.target.value) || 10)}
          />
          <span>ms</span>
        </label>
        <button className="cmd-btn" onClick={sendInterval}>SET</button>
      </div>
    </section>
  );
}

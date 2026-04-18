import { useState, useCallback } from 'react';
import './LogControls.css';

export default function LogControls({ send }) {
  const [intervalMs, setIntervalMs] = useState(50);

  const sendStartLog = useCallback(() => send({ type: "start_log" }), [send]);
  const sendStopLog = useCallback(() => send({ type: "stop_log" }), [send]);
  const sendInterval = useCallback(() => {
    send({ type: "set_interval", interval_ms: intervalMs });
  }, [send, intervalMs]);

  return (
    <div className="panel log-controls">
      <h2>LOGGING</h2>
      <div className="log-buttons">
        <button className="cmd-btn" onClick={sendStartLog}>START LOG</button>
        <button className="cmd-btn" onClick={sendStopLog}>STOP LOG</button>
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
    </div>
  );
}

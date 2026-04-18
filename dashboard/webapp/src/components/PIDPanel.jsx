import { useState, useCallback } from 'react';
import './PIDPanel.css';

const LOOPS = [
  { id: 0, name: "HEADING",   defaults: { kp: 2.0,  ki: 0.05, kd: 0.1  } },
  { id: 1, name: "CENTERING", defaults: { kp: 1.0,  ki: 0.0,  kd: 0.2  } },
  { id: 2, name: "FORWARD",   defaults: { kp: 1.0,  ki: 0.02, kd: 0.01 } },
];

const STORAGE_KEY = "pacbot_pid_presets";

function loadPresets() {
  try {
    return JSON.parse(localStorage.getItem(STORAGE_KEY)) || {};
  } catch { return {}; }
}

function savePresets(presets) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(presets));
}

function PIDRow({ loop, gains, onChange }) {
  return (
    <div className="pid-loop">
      <div className="pid-loop-header">
        <span className="pid-loop-name">{loop.name}</span>
      </div>
      <div className="pid-inputs">
        {["kp", "ki", "kd"].map(param => (
          <label key={param}>
            <span>{param.toUpperCase()}</span>
            <input
              type="number"
              step="any"
              value={gains[param]}
              onChange={(e) => onChange(param, e.target.value)}
            />
          </label>
        ))}
      </div>
    </div>
  );
}

export default function PIDPanel({ send }) {
  const [gains, setGains] = useState(() =>
    Object.fromEntries(LOOPS.map(l => [l.id, { ...l.defaults }]))
  );
  const [presets, setPresets] = useState(loadPresets);
  const [presetName, setPresetName] = useState("");
  const [selectedPreset, setSelectedPreset] = useState("");

  const updateGain = useCallback((loopId, param, value) => {
    setGains(prev => ({
      ...prev,
      [loopId]: { ...prev[loopId], [param]: value },
    }));
  }, []);

  const sendLoop = useCallback((loopId) => {
    const g = gains[loopId];
    send({ type: "set_pid", loop: loopId, kp: parseFloat(g.kp), ki: parseFloat(g.ki), kd: parseFloat(g.kd) });
  }, [send, gains]);

  const sendAll = useCallback(() => {
    LOOPS.forEach(l => sendLoop(l.id));
  }, [sendLoop]);

  const savePreset = useCallback(() => {
    const name = presetName.trim();
    if (!name) return;
    const data = {};
    LOOPS.forEach(l => {
      data[l.id] = {
        kp: parseFloat(gains[l.id].kp),
        ki: parseFloat(gains[l.id].ki),
        kd: parseFloat(gains[l.id].kd),
      };
    });
    const updated = { ...presets, [name]: data };
    setPresets(updated);
    savePresets(updated);
    setSelectedPreset(name);
  }, [presetName, gains, presets]);

  const loadPreset = useCallback(() => {
    const data = presets[selectedPreset];
    if (!data) return;
    const updated = {};
    LOOPS.forEach(l => {
      updated[l.id] = data[l.id] ? { ...data[l.id] } : { ...l.defaults };
    });
    setGains(updated);
  }, [selectedPreset, presets]);

  const deletePreset = useCallback(() => {
    if (!selectedPreset) return;
    const updated = { ...presets };
    delete updated[selectedPreset];
    setPresets(updated);
    savePresets(updated);
    setSelectedPreset("");
  }, [selectedPreset, presets]);

  const resetDefaults = useCallback(() => {
    setGains(Object.fromEntries(LOOPS.map(l => [l.id, { ...l.defaults }])));
  }, []);

  const presetNames = Object.keys(presets);

  return (
    <section className="panel pid-panel">
      <h2>PID TUNING</h2>
      <div className="pid-presets">
        <div className="preset-row">
          <select
            value={selectedPreset}
            onChange={(e) => setSelectedPreset(e.target.value)}
          >
            <option value="">-- preset --</option>
            {presetNames.map(n => <option key={n} value={n}>{n}</option>)}
          </select>
          <button className="cmd-btn" onClick={loadPreset} disabled={!selectedPreset}>LOAD</button>
          <button className="cmd-btn" onClick={deletePreset} disabled={!selectedPreset}>DEL</button>
        </div>
        <div className="preset-row">
          <input
            type="text"
            placeholder="preset name"
            value={presetName}
            onChange={(e) => setPresetName(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && savePreset()}
          />
          <button className="cmd-btn" onClick={savePreset} disabled={!presetName.trim()}>SAVE</button>
        </div>
      </div>
      {LOOPS.map(loop => (
        <PIDRow
          key={loop.id}
          loop={loop}
          gains={gains[loop.id]}
          onChange={(param, val) => updateGain(loop.id, param, val)}
        />
      ))}
      <div className="pid-actions">
        <button className="cmd-btn" onClick={sendAll}>SEND</button>
        <button className="cmd-btn" onClick={resetDefaults}>DEFAULTS</button>
      </div>
    </section>
  );
}

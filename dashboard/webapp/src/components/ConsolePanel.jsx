import { useRef, useEffect, useCallback } from 'react';
import './ConsolePanel.css';

function formatTime(date) {
  return date.toLocaleTimeString('en-US', { hour12: false }) +
    '.' + String(date.getMilliseconds()).padStart(3, '0');
}

export default function ConsolePanel({ logs, onClear, send }) {
  const bottomRef = useRef(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [logs]);

  const sendStatus = useCallback(() => send({ type: "status" }), [send]);
  const sendPing = useCallback(() => send({ type: "ping" }), [send]);

  return (
    <section className="panel console-panel">
      <div className="console-header">
        <h2>CONSOLE</h2>
        <div className="console-actions">
          <button className="cmd-btn console-cmd" onClick={sendStatus}>STATUS</button>
          <button className="cmd-btn console-cmd" onClick={sendPing}>PING</button>
          <button className="console-clear" onClick={onClear}>CLEAR</button>
        </div>
      </div>
      <div className="console-output">
        {logs.map((entry, i) => (
          <div key={i} className={`console-line ${entry.level}`}>
            <span className="console-time">{formatTime(entry.time)}</span>
            <span className="console-msg">{entry.message}</span>
          </div>
        ))}
        <div ref={bottomRef} />
      </div>
    </section>
  );
}

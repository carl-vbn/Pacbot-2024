import { useState, useEffect, useCallback } from 'react';
import './Controls.css';

const MOTOR_CONFIG = {
  forward:    [1,  0,  0, -1],
  backward:   [-1, 0,  0,  1],
  left:       [0,  1,  1,  0],
  right:      [0, -1, -1,  0],
  rotate_ccw: [1,  1, -1,  1],
  rotate_cw:  [-1, -1, 1, -1],
};

const DIR_CODES = { stop: 0, north: 1, east: 2, south: 3, west: 4 };

const ACTION_TO_CARDINAL = {
  forward: 'north', backward: 'south', left: 'west', right: 'east',
};

export default function Controls({ activeDirection, onDirectionChange, send }) {
  const [speed, setSpeed] = useState(128);
  const [cardinalMode, setCardinalMode] = useState(false);

  const sendManualMotors = useCallback((action, spd) => {
    if (action === 'stop') {
      send({ type: "set_motors", speeds: [0, 0, 0, 0] });
      return;
    }
    const mults = MOTOR_CONFIG[action];
    if (!mults) return;
    const speeds = mults.map(m => Math.max(-255, Math.min(255, Math.round(m * spd))));
    send({ type: "set_motors", speeds });
  }, [send]);

  const sendCardinalMove = useCallback((dir, spd) => {
    const code = DIR_CODES[dir] ?? 0;
    send({ type: "cardinal_move", direction: code, speed: code === 0 ? 0 : spd });
  }, [send]);

  const setDirection = useCallback((dir) => {
    onDirectionChange(dir);
    if (cardinalMode) {
      const cd = ACTION_TO_CARDINAL[dir];
      if (cd) sendCardinalMove(cd, speed);
    } else {
      sendManualMotors(dir, speed);
    }
  }, [onDirectionChange, cardinalMode, sendManualMotors, sendCardinalMove, speed]);

  const rotate = useCallback((action) => {
    if (cardinalMode) return;
    onDirectionChange(null);
    sendManualMotors(action, speed);
  }, [cardinalMode, onDirectionChange, sendManualMotors, speed]);

  const stop = useCallback(() => {
    onDirectionChange(null);
    if (cardinalMode) {
      sendCardinalMove('stop', 0);
    } else {
      sendManualMotors('stop', 0);
    }
  }, [onDirectionChange, cardinalMode, sendManualMotors, sendCardinalMove]);

  const toggleMode = useCallback(() => {
    const newMode = !cardinalMode;
    setCardinalMode(newMode);
    send({ type: "set_drive_mode", mode: newMode ? 1 : 0 });
    onDirectionChange(null);
    if (newMode) {
      sendCardinalMove('stop', 0);
    } else {
      sendManualMotors('stop', 0);
    }
  }, [cardinalMode, send, onDirectionChange, sendCardinalMove, sendManualMotors]);

  const calibrate = useCallback(() => {
    send({ type: "calibrate" });
  }, [send]);

  const changeSpeed = useCallback((newSpeed) => {
    setSpeed(newSpeed);
    if (activeDirection) {
      if (cardinalMode) {
        const cd = ACTION_TO_CARDINAL[activeDirection];
        if (cd) sendCardinalMove(cd, newSpeed);
      } else {
        sendManualMotors(activeDirection, newSpeed);
      }
    }
  }, [activeDirection, cardinalMode, sendManualMotors, sendCardinalMove]);

  useEffect(() => {
    function onKeyDown(e) {
      if (e.target.tagName === "INPUT") return;
      switch (e.key.toLowerCase()) {
        case "w": setDirection("forward"); break;
        case "a": setDirection("left"); break;
        case "x": stop(); break;
        case "d": setDirection("right"); break;
        case "s": setDirection("backward"); break;
        case "q": rotate("rotate_ccw"); break;
        case "e": rotate("rotate_cw"); break;
        case "m": toggleMode(); break;
        case "+":
        case "=":
          setSpeed(prev => {
            const next = Math.min(255, prev + 10);
            if (activeDirection) {
              if (cardinalMode) {
                const cd = ACTION_TO_CARDINAL[activeDirection];
                if (cd) sendCardinalMove(cd, next);
              } else {
                sendManualMotors(activeDirection, next);
              }
            }
            return next;
          });
          break;
        case "-":
          setSpeed(prev => {
            const next = Math.max(0, prev - 10);
            if (activeDirection) {
              if (cardinalMode) {
                const cd = ACTION_TO_CARDINAL[activeDirection];
                if (cd) sendCardinalMove(cd, next);
              } else {
                sendManualMotors(activeDirection, next);
              }
            }
            return next;
          });
          break;
      }
    }
    document.addEventListener("keydown", onKeyDown);
    return () => document.removeEventListener("keydown", onKeyDown);
  }, [setDirection, stop, rotate, toggleMode, activeDirection, cardinalMode, sendManualMotors, sendCardinalMove]);

  return (
    <div className="panel controls-panel">
      <h2>MOTOR CONTROLS</h2>
      <div className="mode-row">
        <button
          className={`mode-btn ${!cardinalMode ? 'active' : ''}`}
          onClick={cardinalMode ? toggleMode : undefined}
        >
          MANUAL
        </button>
        <button
          className={`mode-btn ${cardinalMode ? 'active' : ''}`}
          onClick={!cardinalMode ? toggleMode : undefined}
        >
          CARDINAL
        </button>
        <button className="cmd-btn calibrate-btn" onClick={calibrate}>
          CALIBRATE
        </button>
      </div>
      <div className="direction-controls">
        <div className="dpad">
          <div className="dir-row">
            {!cardinalMode && (
              <button className="rot-btn" onClick={() => rotate("rotate_ccw")}>
                Q &#8634;
              </button>
            )}
            <button
              className={activeDirection === "forward" ? "active" : ""}
              onClick={() => setDirection("forward")}
            >
              W &uarr;
            </button>
            {!cardinalMode && (
              <button className="rot-btn" onClick={() => rotate("rotate_cw")}>
                E &#8635;
              </button>
            )}
          </div>
          <div className="dir-row">
            <button
              className={activeDirection === "left" ? "active" : ""}
              onClick={() => setDirection("left")}
            >
              &larr; A
            </button>
            <button
              className={activeDirection === null ? "active" : ""}
              onClick={stop}
            >
              X STOP
            </button>
            <button
              className={activeDirection === "right" ? "active" : ""}
              onClick={() => setDirection("right")}
            >
              D &rarr;
            </button>
          </div>
          <button
            className={activeDirection === "backward" ? "active" : ""}
            onClick={() => setDirection("backward")}
          >
            S &darr;
          </button>
        </div>
      </div>
      <div className="sliders">
        <label>
          SPEED <span>{speed}</span>
          <input
            type="range" min="0" max="255" step="1"
            value={speed}
            onChange={(e) => changeSpeed(parseInt(e.target.value))}
          />
        </label>
      </div>
    </div>
  );
}

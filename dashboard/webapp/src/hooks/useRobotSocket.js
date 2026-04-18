import { useState, useEffect, useRef, useCallback } from 'react';

const WS_URL = `ws://${window.location.host}/ws`;

export default function useRobotSocket(onLog, onExternalDirection) {
  const [wsConnected, setWsConnected] = useState(false);
  const [robotState, setRobotState] = useState(null);
  const wsRef = useRef(null);
  const onLogRef = useRef(onLog);
  onLogRef.current = onLog;
  const onExternalDirectionRef = useRef(onExternalDirection);
  onExternalDirectionRef.current = onExternalDirection;

  const send = useCallback((obj) => {
    if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(obj));
    }
  }, []);

  useEffect(() => {
    let reconnectTimeout;
    let wasConnected = false;

    function connect() {
      onLogRef.current?.("info", "Connecting to server...");
      const ws = new WebSocket(WS_URL);
      wsRef.current = ws;

      ws.onopen = () => {
        setWsConnected(true);
        wasConnected = true;
        onLogRef.current?.("info", "Connected to server");
      };
      ws.onclose = () => {
        setWsConnected(false);
        setRobotState(prev => prev ? { ...prev, robot_connected: false } : null);
        if (wasConnected) {
          onLogRef.current?.("error", "Disconnected from server");
          wasConnected = false;
        }
        onLogRef.current?.("warn", "Reconnecting in 2s...");
        reconnectTimeout = setTimeout(connect, 2000);
      };
      ws.onerror = () => ws.close();
      ws.onmessage = (e) => {
        const msg = JSON.parse(e.data);
        switch (msg.type) {
          case "state":
            // Swap and negate pitch/roll to correct for IMU mounting orientation
            if (msg.imu) {
              const p = msg.imu.pitch;
              msg.imu.pitch = -msg.imu.roll;
              msg.imu.roll = p;

              if (msg.imu.pitch < 0) {
                msg.imu.pitch += 180;
              } else if (msg.imu.pitch > 0) {
                msg.imu.pitch -= 180;
              }
            }
            setRobotState(msg);
            break;
          case "log":
            onLogRef.current?.(msg.severity.toLowerCase(), `[ROBOT] ${msg.text}`);
            break;
          case "pong":
            onLogRef.current?.("info", `PONG uptime=${msg.uptime_ms}ms`);
            break;
          case "external_direction":
            onExternalDirectionRef.current?.(msg.direction ?? null);
            break;
          case "device_info": {
            const mask = msg.sensor_mask ?? 0;
            const slots = [];
            for (let i = 0; i < 8; i++) if (mask & (1 << i)) slots.push(i);
            onLogRef.current?.("info",
              `[STATUS] sensors=[${slots.join(',')}] imu=${msg.imu_present ? 'yes' : 'no'} motors=${msg.num_motors}`);
            break;
          }
        }
      };
    }

    connect();
    return () => {
      clearTimeout(reconnectTimeout);
      if (wsRef.current) wsRef.current.close();
    };
  }, []);

  return { wsConnected, robotState, send };
}

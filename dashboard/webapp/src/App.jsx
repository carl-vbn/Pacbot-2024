import { useState, useCallback } from 'react';
import useRobotSocket from './hooks/useRobotSocket';
import ConnectionStatus from './components/ConnectionStatus';
import IMUPanel from './components/IMUPanel';
import RobotTopDown from './components/RobotTopDown';
import Orientation3D from './components/Orientation3D';
import Controls from './components/Controls';
import LogControls from './components/LogControls';
import ConsolePanel from './components/ConsolePanel';
import PIDPanel from './components/PIDPanel';

export default function App() {
  const [logs, setLogs] = useState([]);
  const [activeDirection, setActiveDirection] = useState(null);

  const addLog = useCallback((level, message) => {
    setLogs(prev => [...prev, { time: new Date(), level, message }]);
  }, []);

  const clearLogs = useCallback(() => setLogs([]), []);

  const { wsConnected, robotState, send } = useRobotSocket(addLog);

  return (
    <>
      <header>
        <h1>PACBOT MISSION CONTROL</h1>
        <ConnectionStatus
          wsConnected={wsConnected}
          robotConnected={robotState?.robot_connected ?? false}
          robotState={robotState?.robot_state ?? "disconnected"}
        />
      </header>
      <main>
        <section className="left-column">
          <IMUPanel imu={robotState?.imu} />
          <PIDPanel send={send} />
        </section>
        <Orientation3D imu={robotState?.imu} sensors={robotState?.sensors} />
        <section className="right-panel">
          <RobotTopDown sensors={robotState?.sensors} direction={activeDirection} send={send} />
          <LogControls send={send} />
          <Controls activeDirection={activeDirection} onDirectionChange={setActiveDirection} send={send} />
        </section>
        <ConsolePanel logs={logs} onClear={clearLogs} send={send} />
      </main>
    </>
  );
}

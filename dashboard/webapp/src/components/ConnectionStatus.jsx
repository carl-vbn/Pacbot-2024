export default function ConnectionStatus({ wsConnected, robotConnected, robotState }) {
  return (
    <div className="connection-status">
      <span className={`status-indicator ${wsConnected ? 'connected' : 'disconnected'}`}>
        WEB ↔ SERVER
      </span>
      <span className={`status-indicator ${robotConnected ? 'connected' : 'disconnected'}`}>
        SERVER ↔ ROBOT
      </span>
      {robotConnected && (
        <span className="status-indicator connected" style={{ borderColor: '#ff0', color: '#ff0' }}>
          {robotState.toUpperCase()}
        </span>
      )}
    </div>
  );
}

package main

import (
	"bufio"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"math"
	"net"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// Protocol message types: Pi -> Server
const (
	MsgAlive      = 0x01
	MsgDeviceInfo = 0x02
	MsgSensorData = 0x03
	MsgLog        = 0x04
	MsgPong       = 0x05
)

// Protocol command types: Server -> Pi
const (
	CmdStartLog     = 0x11
	CmdSetMotor     = 0x12
	CmdSetInterval  = 0x13
	CmdPing         = 0x14
	CmdSetMotors    = 0x15
	CmdStatus = 0x16
	CmdSetDriveMode = 0x17
	CmdCardinalMove = 0x18
	CmdCalibrate    = 0x19
	CmdStopLog      = 0x1A
	CmdSetPid       = 0x1B
	CmdSetSensorOffsets = 0x1C
)

// Drive modes
const (
	DriveManual   = 0
	DriveCardinal = 1
)


var severityNames = map[byte]string{
	0: "DEBUG",
	1: "INFO",
	2: "WARN",
	3: "ERROR",
}

func round2(f float64) float64 {
	return math.Round(f*100) / 100
}

type robotState struct {
	mu          sync.RWMutex
	connected   bool
	state       string // "disconnected", "idle", "setup_done", "logging"
	uptimeMs    uint32
	sensorMask  byte
	imuPresent  bool
	numMotors   byte
	timestampMs uint32
	sensors     [8]int16
	yaw         float32
	pitch       float32
	roll        float32
	driveMode   byte
	addr        *net.UDPAddr
	lastSeen    time.Time
}

type wsClient struct {
	conn *websocket.Conn
	send chan []byte
}

type pidState struct {
	set        bool
	kp, ki, kd float32
}

type server struct {
	robot    robotState
	udpConn  *net.UDPConn
	mu       sync.RWMutex
	clients  map[*wsClient]bool
	upgrader websocket.Upgrader

	// Last-sent PID gains per loop (heading, centering, forward).
	// Re-sent to the robot on every reconnect so the firmware always
	// runs with the UI's currently selected coefficients.
	pidMu    sync.Mutex
	pidGains [3]pidState

	// Speed used by the IPC interface, which only carries direction.
	// Tracks the most recent non-zero speed seen on a cardinal_move from
	// the webapp so the UI slider still governs external commands.
	speedMu           sync.Mutex
	lastCardinalSpeed int
}

func newServer() *server {
	s := &server{
		clients: make(map[*wsClient]bool),
		upgrader: websocket.Upgrader{
			CheckOrigin: func(r *http.Request) bool { return true },
		},
	}
	s.robot.state = "disconnected"
	s.robot.sensorMask = 0x0F // default: 4 sensors (slots 0-3)
	s.robot.numMotors = 4
	s.lastCardinalSpeed = 128
	for i := range s.robot.sensors {
		s.robot.sensors[i] = -1
	}
	return s
}

func (s *server) buildStateJSON() []byte {
	s.robot.mu.RLock()
	defer s.robot.mu.RUnlock()

	sensors := make([]int16, 8)
	copy(sensors[:], s.robot.sensors[:])

	msg := map[string]any{
		"type":            "state",
		"robot_connected": s.robot.connected,
		"robot_state":     s.robot.state,
		"uptime_ms":       s.robot.uptimeMs,
		"sensor_mask":     s.robot.sensorMask,
		"imu_present":     s.robot.imuPresent,
		"num_motors":      s.robot.numMotors,
		"timestamp_ms":    s.robot.timestampMs,
		"sensors":         sensors,
		"drive_mode":      s.robot.driveMode,
		"imu": map[string]float64{
			"yaw":   round2(float64(s.robot.yaw)),
			"pitch": round2(float64(s.robot.pitch)),
			"roll":  round2(float64(s.robot.roll)),
		},
	}

	data, _ := json.Marshal(msg)
	return data
}

func (s *server) broadcastState() {
	data := s.buildStateJSON()
	s.mu.RLock()
	defer s.mu.RUnlock()
	for c := range s.clients {
		select {
		case c.send <- data:
		default:
		}
	}
}

func (s *server) broadcastJSON(msg map[string]any) {
	data, err := json.Marshal(msg)
	if err != nil {
		return
	}
	s.mu.RLock()
	defer s.mu.RUnlock()
	for c := range s.clients {
		select {
		case c.send <- data:
		default:
		}
	}
}

func (s *server) handleUDP(data []byte, addr *net.UDPAddr) {
	if len(data) < 1 {
		return
	}

	s.robot.mu.Lock()

	if s.robot.addr == nil || s.robot.addr.String() != addr.String() {
		if s.robot.addr == nil {
			log.Printf("Robot discovered at %s", addr)
		} else {
			log.Printf("Robot reconnected from %s", addr)
		}
		s.robot.addr = addr
		go s.resendPidGains()
	}
	s.robot.connected = true
	s.robot.lastSeen = time.Now()

	msgType := data[0]

	switch msgType {
	case MsgAlive:
		if len(data) < 5 {
			s.robot.mu.Unlock()
			return
		}
		s.robot.uptimeMs = binary.LittleEndian.Uint32(data[1:5])
		s.robot.state = "idle"
		s.robot.mu.Unlock()
		log.Printf("<- ALIVE uptime=%dms", s.robot.uptimeMs)
		s.broadcastState()

	case MsgDeviceInfo:
		if len(data) < 4 {
			s.robot.mu.Unlock()
			return
		}
		s.robot.sensorMask = data[1]
		s.robot.imuPresent = data[2] != 0
		s.robot.numMotors = data[3]
		s.robot.state = "setup_done"
		for i := 0; i < 8; i++ {
			if s.robot.sensorMask&(1<<i) != 0 {
				s.robot.sensors[i] = 0
			} else {
				s.robot.sensors[i] = -1
			}
		}
		mask := s.robot.sensorMask
		imu := s.robot.imuPresent
		motors := s.robot.numMotors
		s.robot.mu.Unlock()
		log.Printf("<- DEVICE_INFO mask=0x%02x imu=%v motors=%d", mask, imu, motors)
		s.broadcastJSON(map[string]any{
			"type":        "device_info",
			"sensor_mask": mask,
			"imu_present": imu,
			"num_motors":  motors,
		})
		s.broadcastState()

	case MsgSensorData:
		if len(data) < 6 {
			s.robot.mu.Unlock()
			return
		}
		s.robot.timestampMs = binary.LittleEndian.Uint32(data[1:5])
		count := int(data[5])
		s.robot.state = "logging"

		pos := 6
		idx := 0
		for i := 0; i < 8; i++ {
			if s.robot.sensorMask&(1<<i) != 0 {
				if idx < count && pos+2 <= len(data) {
					s.robot.sensors[i] = int16(binary.LittleEndian.Uint16(data[pos : pos+2]))
					pos += 2
					idx++
				}
			} else {
				s.robot.sensors[i] = -1
			}
		}

		if pos < len(data) && data[pos] != 0 {
			pos++
			if pos+12 <= len(data) {
				s.robot.yaw = math.Float32frombits(binary.LittleEndian.Uint32(data[pos : pos+4]))
				s.robot.pitch = math.Float32frombits(binary.LittleEndian.Uint32(data[pos+4 : pos+8]))
				s.robot.roll = math.Float32frombits(binary.LittleEndian.Uint32(data[pos+8 : pos+12]))

			}
		} else if pos < len(data) {
			pos++
		}
		s.robot.mu.Unlock()
		s.broadcastState()

	case MsgLog:
		if len(data) < 4 {
			s.robot.mu.Unlock()
			return
		}
		severity := data[1]
		textLen := binary.LittleEndian.Uint16(data[2:4])
		text := ""
		if int(4+textLen) <= len(data) {
			text = string(data[4 : 4+textLen])
		}
		s.robot.mu.Unlock()

		sevName, ok := severityNames[severity]
		if !ok {
			sevName = fmt.Sprintf("LVL%d", severity)
		}
		log.Printf("<- LOG/%s: %s", sevName, text)
		s.broadcastJSON(map[string]any{
			"type":     "log",
			"severity": sevName,
			"text":     text,
		})

	case MsgPong:
		if len(data) < 5 {
			s.robot.mu.Unlock()
			return
		}
		uptime := binary.LittleEndian.Uint32(data[1:5])
		s.robot.mu.Unlock()
		log.Printf("<- PONG uptime=%dms", uptime)
		s.broadcastJSON(map[string]any{
			"type":      "pong",
			"uptime_ms": uptime,
		})

	default:
		s.robot.mu.Unlock()
		log.Printf("<- unknown message type 0x%02x (%d bytes)", msgType, len(data))
	}
}

func (s *server) sendToRobot(data []byte) error {
	s.robot.mu.RLock()
	addr := s.robot.addr
	s.robot.mu.RUnlock()

	if addr == nil {
		return fmt.Errorf("no robot connected")
	}
	_, err := s.udpConn.WriteToUDP(data, addr)
	return err
}

// resendPidGains pushes every PID loop the user has touched to the robot.
// Called in a goroutine on reconnect so the firmware starts fresh with the
// UI's currently selected coefficients.
//
// A small inter-packet delay avoids a firmware race: the shared-buffer
// command queue only has one pending slot, so back-to-back packets inside
// the same comms poll would overwrite each other before core 0 picks them up.
func (s *server) resendPidGains() {
	s.pidMu.Lock()
	gains := s.pidGains
	s.pidMu.Unlock()

	loopNames := []string{"HEADING", "CENTERING", "FORWARD"}
	for i := 0; i < len(gains); i++ {
		if !gains[i].set {
			continue
		}
		payload := make([]byte, 14)
		payload[0] = CmdSetPid
		payload[1] = byte(i)
		binary.LittleEndian.PutUint32(payload[2:], math.Float32bits(gains[i].kp))
		binary.LittleEndian.PutUint32(payload[6:], math.Float32bits(gains[i].ki))
		binary.LittleEndian.PutUint32(payload[10:], math.Float32bits(gains[i].kd))
		if err := s.sendToRobot(payload); err != nil {
			log.Printf("PID resend %s failed: %v", loopNames[i], err)
			return
		}
		log.Printf("-> CMD_SET_PID %s (on reconnect) kp=%.3f ki=%.3f kd=%.3f",
			loopNames[i], gains[i].kp, gains[i].ki, gains[i].kd)
		time.Sleep(20 * time.Millisecond)
	}
}

func (s *server) handleWSCommand(msg map[string]any) {
	cmdType, _ := msg["type"].(string)

	switch cmdType {
	case "start_log":
		if err := s.sendToRobot([]byte{CmdStartLog}); err != nil {
			log.Printf("CMD_START_LOG failed: %v", err)
			return
		}
		s.robot.mu.Lock()
		s.robot.state = "logging"
		s.robot.mu.Unlock()
		log.Println("-> CMD_START_LOG")
		s.broadcastState()

	case "stop_log":
		if err := s.sendToRobot([]byte{CmdStopLog}); err != nil {
			log.Printf("CMD_STOP_LOG failed: %v", err)
			return
		}
		s.robot.mu.Lock()
		s.robot.state = "setup_done"
		s.robot.mu.Unlock()
		log.Println("-> CMD_STOP_LOG")
		s.broadcastState()

	case "set_motor":
		idx, ok1 := msg["index"].(float64)
		spd, ok2 := msg["speed"].(float64)
		if !ok1 || !ok2 {
			log.Println("set_motor: missing index or speed")
			return
		}
		index := int(idx)
		speed := int16(spd)
		if index < 0 || index > 3 {
			log.Println("set_motor: index must be 0-3")
			return
		}
		payload := make([]byte, 4)
		payload[0] = CmdSetMotor
		payload[1] = byte(index)
		binary.LittleEndian.PutUint16(payload[2:], uint16(speed))
		if err := s.sendToRobot(payload); err != nil {
			log.Printf("CMD_SET_MOTOR failed: %v", err)
			return
		}
		log.Printf("-> CMD_SET_MOTOR idx=%d speed=%d", index, speed)

	case "set_motors":
		speeds, ok := msg["speeds"].([]any)
		if !ok || len(speeds) != 4 {
			log.Println("set_motors: expected 4 speeds")
			return
		}
		payload := make([]byte, 9)
		payload[0] = CmdSetMotors
		for i, v := range speeds {
			sp, ok := v.(float64)
			if !ok {
				return
			}
			s16 := int16(sp)
			binary.LittleEndian.PutUint16(payload[1+i*2:], uint16(s16))
		}
		if err := s.sendToRobot(payload); err != nil {
			log.Printf("CMD_SET_MOTORS failed: %v", err)
			return
		}
		log.Printf("-> CMD_SET_MOTORS [%d %d %d %d]",
			int16(binary.LittleEndian.Uint16(payload[1:])),
			int16(binary.LittleEndian.Uint16(payload[3:])),
			int16(binary.LittleEndian.Uint16(payload[5:])),
			int16(binary.LittleEndian.Uint16(payload[7:])))

	case "set_interval":
		ms, ok := msg["interval_ms"].(float64)
		if !ok || ms < 10 {
			return
		}
		payload := make([]byte, 3)
		payload[0] = CmdSetInterval
		binary.LittleEndian.PutUint16(payload[1:], uint16(ms))
		if err := s.sendToRobot(payload); err != nil {
			log.Printf("CMD_SET_INTERVAL failed: %v", err)
			return
		}
		log.Printf("-> CMD_SET_INTERVAL %dms", int(ms))

	case "status":
		if err := s.sendToRobot([]byte{CmdStatus}); err != nil {
			log.Printf("CMD_STATUS failed: %v", err)
			return
		}
		log.Println("-> CMD_STATUS")

	case "set_drive_mode":
		mode, ok := msg["mode"].(float64)
		if !ok {
			return
		}
		m := byte(mode)
		payload := []byte{CmdSetDriveMode, m}
		if err := s.sendToRobot(payload); err != nil {
			log.Printf("CMD_SET_DRIVE_MODE failed: %v", err)
			return
		}
		s.robot.mu.Lock()
		s.robot.driveMode = m
		s.robot.mu.Unlock()
		modeName := "MANUAL"
		if m == DriveCardinal {
			modeName = "CARDINAL"
		}
		log.Printf("-> CMD_SET_DRIVE_MODE %s", modeName)
		s.broadcastState()

	case "cardinal_move":
		dir, ok1 := msg["direction"].(float64)
		spd, ok2 := msg["speed"].(float64)
		if !ok1 || !ok2 {
			return
		}
		payload := []byte{CmdCardinalMove, byte(dir), byte(spd)}
		if err := s.sendToRobot(payload); err != nil {
			log.Printf("CMD_CARDINAL_MOVE failed: %v", err)
			return
		}
		log.Printf("-> CMD_CARDINAL_MOVE dir=%d speed=%d", int(dir), int(spd))
		if byte(dir) != 0 && byte(spd) != 0 {
			s.speedMu.Lock()
			s.lastCardinalSpeed = int(byte(spd))
			s.speedMu.Unlock()
		}

	case "calibrate":
		if err := s.sendToRobot([]byte{CmdCalibrate}); err != nil {
			log.Printf("CMD_CALIBRATE failed: %v", err)
			return
		}
		log.Println("-> CMD_CALIBRATE")

	case "set_pid":
		loopId, ok1 := msg["loop"].(float64)
		kp, ok2 := msg["kp"].(float64)
		ki, ok3 := msg["ki"].(float64)
		kd, ok4 := msg["kd"].(float64)
		if !ok1 || !ok2 || !ok3 || !ok4 {
			log.Println("set_pid: missing loop, kp, ki, or kd")
			return
		}
		payload := make([]byte, 14)
		payload[0] = CmdSetPid
		payload[1] = byte(loopId)
		binary.LittleEndian.PutUint32(payload[2:], math.Float32bits(float32(kp)))
		binary.LittleEndian.PutUint32(payload[6:], math.Float32bits(float32(ki)))
		binary.LittleEndian.PutUint32(payload[10:], math.Float32bits(float32(kd)))
		if err := s.sendToRobot(payload); err != nil {
			log.Printf("CMD_SET_PID failed: %v", err)
			return
		}
		loopNames := []string{"HEADING", "CENTERING", "FORWARD"}
		name := "?"
		if int(loopId) < len(loopNames) {
			name = loopNames[int(loopId)]
		}
		log.Printf("-> CMD_SET_PID %s kp=%.3f ki=%.3f kd=%.3f", name, kp, ki, kd)

		// Remember these gains so we can resend them on reconnect
		if int(loopId) >= 0 && int(loopId) < len(s.pidGains) {
			s.pidMu.Lock()
			s.pidGains[int(loopId)] = pidState{true, float32(kp), float32(ki), float32(kd)}
			s.pidMu.Unlock()
		}

	case "set_sensor_offsets":
		offsets, ok := msg["offsets"].([]any)
		if !ok || len(offsets) != 8 {
			log.Println("set_sensor_offsets: expected 8 offsets")
			return
		}
		payload := make([]byte, 1+16)
		payload[0] = CmdSetSensorOffsets
		for i, v := range offsets {
			off, ok := v.(float64)
			if !ok {
				return
			}
			binary.LittleEndian.PutUint16(payload[1+i*2:], uint16(int16(off)))
		}
		if err := s.sendToRobot(payload); err != nil {
			log.Printf("CMD_SET_SENSOR_OFFSETS failed: %v", err)
			return
		}
		log.Printf("-> CMD_SET_SENSOR_OFFSETS")

	case "ping":
		if err := s.sendToRobot([]byte{CmdPing}); err != nil {
			log.Printf("CMD_PING failed: %v", err)
			return
		}
		log.Println("-> CMD_PING")

	default:
		log.Printf("Unknown WS command: %s", cmdType)
	}
}

func (s *server) udpListener() {
	buf := make([]byte, 512)
	for {
		n, addr, err := s.udpConn.ReadFromUDP(buf)
		if err != nil {
			log.Printf("UDP read error: %v", err)
			continue
		}
		data := make([]byte, n)
		copy(data, buf[:n])
		s.handleUDP(data, addr)
	}
}

// handleExternalDirection processes a single-character cardinal command
// arriving from the local IPC socket. Valid inputs are n/e/s/w (cardinal
// moves) and x (stop). Forwards the move to the robot and broadcasts the
// direction so the webapp can mirror the highlight.
func (s *server) handleExternalDirection(c byte) error {
	var dirCode byte
	var uiDir any
	switch c {
	case 'n':
		dirCode, uiDir = 1, "forward"
	case 'e':
		dirCode, uiDir = 2, "right"
	case 's':
		dirCode, uiDir = 3, "backward"
	case 'w':
		dirCode, uiDir = 4, "left"
	case 'x':
		dirCode, uiDir = 0, nil
	default:
		return fmt.Errorf("invalid direction %q (expected n/e/s/w/x)", string(c))
	}

	s.speedMu.Lock()
	speed := s.lastCardinalSpeed
	s.speedMu.Unlock()
	if dirCode == 0 {
		speed = 0
	}

	label := "stop"
	if uiDir != nil {
		label = uiDir.(string)
	}

	payload := []byte{CmdCardinalMove, dirCode, byte(speed)}
	sendErr := s.sendToRobot(payload)

	suffix := ""
	if sendErr != nil {
		suffix = fmt.Sprintf(" (not forwarded: %v)", sendErr)
	}
	log.Printf("-> (IPC) CMD_CARDINAL_MOVE dir=%s speed=%d%s", label, speed, suffix)
	s.broadcastJSON(map[string]any{
		"type":     "log",
		"severity": "INFO",
		"text":     fmt.Sprintf("[IPC] direction=%s speed=%d%s", label, speed, suffix),
	})
	s.broadcastJSON(map[string]any{
		"type":      "external_direction",
		"direction": uiDir,
	})
	return sendErr
}

func (s *server) handleIPCConn(conn net.Conn) {
	defer conn.Close()
	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		line := strings.TrimSpace(strings.ToLower(scanner.Text()))
		if line == "" {
			continue
		}
		if len(line) != 1 {
			log.Printf("IPC: ignoring %q (expected single char n/e/s/w/x)", line)
			continue
		}
		if err := s.handleExternalDirection(line[0]); err != nil {
			log.Printf("IPC: %v", err)
		}
	}
}

func (s *server) ipcListener(path string) {
	// Remove any stale socket file left by a previous crash so we can bind.
	if _, err := os.Stat(path); err == nil {
		if err := os.Remove(path); err != nil {
			log.Printf("Failed to remove stale IPC socket %s: %v", path, err)
			return
		}
	}
	listener, err := net.Listen("unix", path)
	if err != nil {
		log.Printf("Failed to listen on IPC socket %s: %v", path, err)
		return
	}
	defer listener.Close()
	if err := os.Chmod(path, 0666); err != nil {
		log.Printf("Failed to chmod IPC socket: %v", err)
	}
	log.Printf("IPC socket listening on %s", path)

	for {
		conn, err := listener.Accept()
		if err != nil {
			log.Printf("IPC accept error: %v", err)
			continue
		}
		go s.handleIPCConn(conn)
	}
}

func (s *server) disconnectWatcher() {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	for range ticker.C {
		s.robot.mu.Lock()
		if s.robot.connected && time.Since(s.robot.lastSeen) > 5*time.Second {
			s.robot.connected = false
			s.robot.state = "disconnected"
			s.robot.driveMode = DriveManual
			s.robot.addr = nil
			log.Println("Robot disconnected (timeout)")
			s.robot.mu.Unlock()
			s.broadcastState()
		} else {
			s.robot.mu.Unlock()
		}
	}
}

func (s *server) wsWriter(c *wsClient) {
	defer c.conn.Close()
	for msg := range c.send {
		c.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
		if err := c.conn.WriteMessage(websocket.TextMessage, msg); err != nil {
			return
		}
	}
}

func (s *server) handleWS(w http.ResponseWriter, r *http.Request) {
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("WS upgrade error: %v", err)
		return
	}

	client := &wsClient{
		conn: conn,
		send: make(chan []byte, 64),
	}

	s.mu.Lock()
	s.clients[client] = true
	n := len(s.clients)
	s.mu.Unlock()
	log.Printf("WS client connected (%d total)", n)

	client.send <- s.buildStateJSON()

	go s.wsWriter(client)

	defer func() {
		s.mu.Lock()
		delete(s.clients, client)
		n := len(s.clients)
		s.mu.Unlock()
		close(client.send)
		log.Printf("WS client disconnected (%d total)", n)
	}()

	for {
		_, raw, err := conn.ReadMessage()
		if err != nil {
			return
		}
		var msg map[string]any
		if err := json.Unmarshal(raw, &msg); err != nil {
			continue
		}
		s.handleWSCommand(msg)
	}
}

func main() {
	udpPort := flag.Int("udp-port", 9000, "UDP port for robot communication")
	httpPort := flag.Int("http-port", 8765, "HTTP/WebSocket port for dashboard")
	staticDir := flag.String("static", "../webapp/dist", "Static files directory")
	ipcSocket := flag.String("ipc-socket", "/tmp/pacbot.sock", "Unix domain socket for local IPC direction commands")
	flag.Parse()

	srv := newServer()

	udpAddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf(":%d", *udpPort))
	if err != nil {
		log.Fatalf("Failed to resolve UDP address: %v", err)
	}
	srv.udpConn, err = net.ListenUDP("udp", udpAddr)
	if err != nil {
		log.Fatalf("Failed to listen on UDP :%d: %v", *udpPort, err)
	}
	defer srv.udpConn.Close()

	go srv.udpListener()
	go srv.disconnectWatcher()
	go srv.ipcListener(*ipcSocket)

	http.HandleFunc("/ws", srv.handleWS)
	http.Handle("/", http.FileServer(http.Dir(*staticDir)))

	log.Printf("PacBot dashboard server")
	log.Printf("  Robot UDP port: %d", *udpPort)
	log.Printf("  Dashboard HTTP: http://localhost:%d", *httpPort)
	log.Printf("  Static files:   %s", *staticDir)
	log.Printf("  IPC socket:     %s", *ipcSocket)

	if err := http.ListenAndServe(fmt.Sprintf(":%d", *httpPort), nil); err != nil {
		log.Fatalf("HTTP server error: %v", err)
	}
}

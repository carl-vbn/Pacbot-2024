package game

import (
	"fmt"
)

/***************************** Interpret Commands *****************************/

// Convert byte commands from clients into actions involving the game state
func (gs *gameState) interpretCommand(msg []byte) {

	// Log the command if necessary
	if commandLogEnable {
		fmt.Printf("\033[2m\033[36m| Response: %s`\033[0m\n", string(msg))
	}

	// If there's no message, return
	if len(msg) < 1 {
		return
	}

	// Decide the command type based on the first byte
	switch msg[0] {
	case 'p':
		fmt.Println("pausing...")
		gs.pause()
	case 'P':
		fmt.Println("playing...")
		gs.play()
	}
}
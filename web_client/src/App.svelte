<style>

  :root {
    font-family: Inter, system-ui, Avenir, Helvetica, Arial, sans-serif;

    color-scheme: light dark;
    color: rgba(255, 255, 255, 0.87);
    background-color: #242424;

    font-synthesis: none;
    text-rendering: optimizeLegibility;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
    -webkit-text-size-adjust: 100%;
  }

  :global(body) {
    margin: 0;
    padding: 0;
    border: 0;
  }

  .maze-space {

    /* Positioning */
    position: absolute;
    transform: translate(50%, 50%);
    bottom: 50%;
    right: 50%;
  }

  :global(.path) {
      border: 1px solid cyan !important;
  }

</style>

<script>

  /* Config */
  import config from '../../config.json';

  /* Agents */
  import Ghost from './lib/agents/Ghost.svelte';
  import Pacman from './lib/agents/Pacman.svelte';

  /* Environment */
  import Fruit from './lib/environment/Fruit.svelte';
  import Maze from './lib/environment/Maze.svelte';
  import Pellets from './lib/environment/Pellets.svelte';

  /* Info Boxes */
  import Lives from './lib/info_boxes/Lives.svelte';
  import Mps from './lib/info_boxes/Mps.svelte';
  import Score from './lib/info_boxes/Score.svelte';
  import Ticker from './lib/info_boxes/Ticker.svelte';
  import Reset from './lib/info_boxes/reset.svelte';

  // Creating a websocket client
  var socket = new WebSocket(`ws://${config.ServerIP}:${config.WebSocketPort}`);
  var botSocket = new WebSocket(`ws://${config.BotIP}:${config.BotSocketPort}`);

  socket.binaryType = 'arraybuffer';
  let socketOpen = false;
  let botSocketOpen = false;

  /*
    This generates an empty array of pellet states
    (0 = none, 1 = pellet, 2 = super)
  */
  let pelletGrid = [];
  for (let row = 0; row < 31; row++) {
    pelletGrid[row] = [];
    for (let col = 0; col < 28; col++) {
      pelletGrid[row][col] = 0;
    }
  }

  /*
    This generates an array of intersection labels
  */
  const intersections = [
    0b0000_0000000000000000000000000000, // row 0
    0b0000_0100001000001001000001000010, // row 1
    0b0000_0000000000000000000000000000, // row 2
    0b0000_0000000000000000000000000000, // row 3
    0b0000_0000000000000000000000000000, // row 4
    0b0000_0100001001001001001001000010, // row 5
    0b0000_0000000000000000000000000000, // row 6
    0b0000_0000000000000000000000000000, // row 7
    0b0000_0100001001001001001001000010, // row 8
    0b0000_0000000000000000000000000000, // row 9
    0b0000_0000000000000000000000000000, // row 10
    0b0000_0000000001001001001000000000, // row 11
    0b0000_0000000000000000000000000000, // row 12
    0b0000_0000000000000000000000000000, // row 13
    0b0000_0000001001000000001001000000, // row 14
    0b0000_0000000000000000000000000000, // row 15
    0b0000_0000000000000000000000000000, // row 16
    0b0000_0000000001000010001000000000, // row 17
    0b0000_0000000000000000000000000000, // row 18
    0b0000_0000000000000000000000000000, // row 19
    0b0000_0100001001001001001001000010, // row 20
    0b0000_0000000000000000000000000000, // row 21
    0b0000_0000000000000000000000000000, // row 22
    0b0000_0101001001001001001001001010, // row 23
    0b0000_0000000000000000000000000000, // row 24
    0b0000_0000000000000000000000000000, // row 25
    0b0000_0101001001001001001001001010, // row 26
    0b0000_0000000000000000000000000000, // row 27
    0b0000_0000000000000000000000000000, // row 28
    0b0000_0100000000001001000000000010, // row 29
    0b0000_0000000000000000000000000000  // row 30
  ];

  let intersectionGrid = [];
  for (let row = 0; row < 31; row++) {
    intersectionGrid[row] = [];
    for (let col = 0; col < 28; col++) {
      intersectionGrid[row][col] = ((intersections[row] >> col) & 1) ? 1 : 0;
    }
  }

  /*
    This generates an array of wall labels
  */
  const walls = [
    0b0000_1111111111111111111111111111, // row 0
    0b0000_1000000000000110000000000001, // row 1
    0b0000_1011110111110110111110111101, // row 2
    0b0000_1011110111110110111110111101, // row 3
    0b0000_1011110111110110111110111101, // row 4
    0b0000_1000000000000000000000000001, // row 5
    0b0000_1011110110111111110110111101, // row 6
    0b0000_1011110110111111110110111101, // row 7
    0b0000_1000000110000110000110000001, // row 8
    0b0000_1111110111110110111110111111, // row 9
    0b0000_1111110111110110111110111111, // row 10
    0b0000_1111110110000000000110111111, // row 11
    0b0000_1111110110111111110110111111, // row 12
    0b0000_1111110110111111110110111111, // row 13
    0b0000_1111110000111111110000111111, // row 14
    0b0000_1111110110111111110110111111, // row 15
    0b0000_1111110110111111110110111111, // row 16
    0b0000_1111110110000000000110111111, // row 17
    0b0000_1111110110111111110110111111, // row 18
    0b0000_1111110110111111110110111111, // row 19
    0b0000_1000000000000110000000000001, // row 20
    0b0000_1011110111110110111110111101, // row 21
    0b0000_1011110111110110111110111101, // row 22
    0b0000_1000110000000000000000110001, // row 23
    0b0000_1110110110111111110110110111, // row 24
    0b0000_1110110110111111110110110111, // row 25
    0b0000_1000000110000110000110000001, // row 26
    0b0000_1011111111110110111111111101, // row 27
    0b0000_1011111111110110111111111101, // row 28
    0b0000_1000000000000000000000000001, // row 29
    0b0000_1111111111111111111111111111  // row 30
  ];

  /*
    We use a circular queue (with a fixed max capacity to keep track of the
    times of the most recent messages. For every message we receive, we should
    add this time to the queue and remove all times longer than 1ms ago. The
    length of this array will be the MPS (messages per second), which should
    be synced with the frame rate of the game engine if there is no lag.
  */
  const MPS_BUFFER_SIZE = 2 * config.GameFPS; // Allow double the specified FPS
  let mpsBuffer = new Array(MPS_BUFFER_SIZE);
  let mpsIdxLeft = 0;
  let mpsIdxRight = 1;
  let mpsAvg = 0;
  mpsBuffer[0] = Date.now();

  // Keep track of the number of ticks elapsed (from the server)
  let currTicks = 0;

  // Keep track of the ticks per update (from the server)
  let updatePeriod = 12;

  // Keep track of the game mode (from the server)
  let gameMode = 0;

  /*
    Keep track of the number of steps until the mode changes, as well as the
    mode duration (from the server)
  */
  let modeSteps = 0;
  let modeDuration = 255;

  /*
    Keep track of the number of steps until the fruit disappears, as well as
    the fruit duration (from the server)
  */
  let fruitSteps = 0;
  let fruitDuration = 30;

  // Local object to encode the possible modes
  const Modes = {
    Paused:   0,
    Scatter:  1,
    Chase:    2,
    Offline:  10,
  }

  // Keep track of the level steps (from the server)
  let levelSteps = 960;

  // Keep track of the current score (from the server)
  let currScore = 0;

  // Keep track of the current level (from the server)
  let currLevel = 0;

  // Keep track of the current lives (from the server)
  let currLives = 1;

  // Keep track of the ghost combo (from the server)
  let ghostCombo = 0;

  // Local object to encode the starting states
  const Directions = {
    Up:       0b11000000,
    Left:     0b11000000,
    Down:     0b01000000,
    Right:    0b01000000,
  }

  // Initial states for all the agents / objects
  let pacmanRowState = 23;
  let pacmanColState = 13 | Directions.Right;

  let fruitRowState = 32;
  let fruitColState = 32;

  let numActiveGhosts = config.NumActiveGhosts;

  let redRowState = 11;
  let redColState = 13 | Directions.Left; // left
  let redFrightState = 0 | 128;
  let redTrappedState = 0 | 128;

  let pinkRowState = 13 | Directions.Down; // down
  let pinkColState = 13;
  let pinkFrightState = 0 | 128;
  let pinkTrappedState = 0 | 128;

  let cyanRowState = 14 | Directions.Up; // up
  let cyanColState = 11;
  let cyanFrightState = 0 | 128;
  let cyanTrappedState = 0 | 128;

  let orangeRowState = 14 | Directions.Up; // up
  let orangeColState = 15;
  let orangeFrightState = 0 | 128;
  let orangeTrappedState = 0 | 128;

  // Handling a new connection
  socket.addEventListener('open', (_) => {
    console.log('WebSocket connection established');
    socketOpen = true;
  });

  // Handling a new connection between webClient and bot 
  botSocket.addEventListener('open', (_) => {
    console.log('WebSocket connection established with Bot');
    botSocketOpen = true;
  });

  // Message events
  socket.addEventListener('message', (event) => {
    if (event.data instanceof ArrayBuffer) {

      // Log the time
      const ts = Date.now();
      mpsBuffer[mpsIdxRight] = ts;
      mpsAvg++;
      mpsIdxRight = (mpsIdxRight + 1) % MPS_BUFFER_SIZE;
      // With a 2% leeway, calculate the number of messages in the window
      while (ts - mpsBuffer[mpsIdxLeft] > 1020 && mpsIdxLeft != mpsIdxRight) {
        mpsIdxLeft = (mpsIdxLeft + 1) % MPS_BUFFER_SIZE;
        mpsAvg--;
      }

      // Binary frame for manually parsing the input
      let view = new DataView(event.data);

      if (view) {

        // Keep track of the byte index we are reading
        let byteIdx = 0;

        // Get the current ticks from the server
        currTicks         = view.getUint16(byteIdx, false); byteIdx += 2;

        // Get the update period from the server
        updatePeriod      = view.getUint8(byteIdx++, false);

        // Get the game mode from the server
        gameMode          = view.getUint8(byteIdx++, false);

        // Get the mode steps and duration from the server
        modeSteps         = view.getUint8(byteIdx++, false);
        modeDuration      = view.getUint8(byteIdx++, false);

        // Get the level steps from the server
        levelSteps        = view.getUint16(byteIdx, false); byteIdx += 2;

        // Get the current score from the server
        currScore         = view.getUint16(byteIdx, false); byteIdx += 2;

        // Get the current level from the server
        currLevel         = view.getUint8(byteIdx++, false);

        // Get the current lives from the server
        currLives         = view.getUint8(byteIdx++, false);

        // Get the ghost combo from the server
        ghostCombo        = view.getUint8(byteIdx++, false);

        // Parse ghost data
        redRowState        = view.getUint8(byteIdx++, false);
        redColState        = view.getUint8(byteIdx++, false);
        redFrightState     = view.getUint8(byteIdx++, false);
        redTrappedState    = view.getUint8(byteIdx++, false);
        pinkRowState       = view.getUint8(byteIdx++, false);
        pinkColState       = view.getUint8(byteIdx++, false);
        pinkFrightState    = view.getUint8(byteIdx++, false);
        pinkTrappedState   = view.getUint8(byteIdx++, false);
        cyanRowState       = view.getUint8(byteIdx++, false);
        cyanColState       = view.getUint8(byteIdx++, false);
        cyanFrightState    = view.getUint8(byteIdx++, false);
        cyanTrappedState   = view.getUint8(byteIdx++, false);
        orangeRowState     = view.getUint8(byteIdx++, false);
        orangeColState     = view.getUint8(byteIdx++, false);
        orangeFrightState  = view.getUint8(byteIdx++, false);
        orangeTrappedState = view.getUint8(byteIdx++, false);

        // Parse Pacman data
        pacmanRowState    = view.getUint8(byteIdx++, false);
        pacmanColState    = view.getUint8(byteIdx++, false);

        // Parse fruit data
        fruitRowState     = view.getUint8(byteIdx++, false);
        fruitColState     = view.getUint8(byteIdx++, false);

        // Get the fruit steps and duration from the server
        fruitSteps        = view.getUint8(byteIdx++, false);
        fruitDuration     = view.getUint8(byteIdx++, false);

        // Parse pellet data
        for (let row = 0; row < 31; row++) {
          const binRow = view.getUint32(byteIdx, false);
          for (let col = 0; col < 28; col++) {

            // Super pellet condition
            let superPellet = ((row === 3) || (row === 23))
                              && ((col === 1) || (col === 26));

            // Update the pellet grid
            pelletGrid[row][col] = ((binRow >> col) & 1) ?
                                    (superPellet ? 2 : 1) : 0;
          }
          byteIdx += 4;
        }

        // Trigger an update for the pellets
        pelletGrid = pelletGrid;
      }
    }
  });

  // Message events for bot-web client connection
  botSocket.addEventListener('message', (event) => {
    if (typeof event.data == 'string') {
      let content = event.data.split(" "); 

      let command = content[0];
      switch (command) {
        case 'set_cell_color': {
          let row = content[1];
          let col = content[2];
          let newColor = content[3];
          
          const elem = document.getElementById(`grid-element-${row}-${col}`);
          elem.style.backgroundColor = newColor;
          break;
        }
        case 'set_cell_color_multiple': { // One color, multiple cells
          const newColor = content[content.length - 1];
          for (let i = 1; i < content.length - 1; i += 2) {
            let row = content[i];
            let col = content[i + 1];
            
            const elem = document.getElementById(`grid-element-${row}-${col}`);
            elem.style.backgroundColor = newColor;
          }
          break;
        }
        case 'set_cell_colors': { // Multiple colors, multiple cells
          for (let i = 1; i < content.length; i += 3) {
            let row = content[i];
            let col = content[i + 1];
            let newColor = content[i + 2];
            
            const elem = document.getElementById(`grid-element-${row}-${col}`);
            elem.style.backgroundColor = newColor;
          }
          break;
        }
        case 'reset_all_cell_colors': {
          const gridElements = document.getElementsByClassName('grid-element');
          for (let i = 0; i < gridElements.length; i++) {
            gridElements[i].style.backgroundColor = '';
          }
          break;
        }
        case 'set_path': {
          const currentPathCells = document.getElementsByClassName('path');
          for (let i = 0; i < currentPathCells.length; i++) {
            currentPathCells[i].classList.remove('path');
          }
          
          for (let i = 1; i < content.length; i += 2) {
            let row = content[i];
            let col = content[i + 1];
            
            const elem = document.getElementById(`grid-element-${row}-${col}`);
            elem.classList.add('path');
          }
          break;
        }
        case 'reset_game': {
          toggleReset();
          console.log('Bot client requested game reset');
          break;
        }
        case 'pause_game': {
          sendToSocket('p');
          console.log('Bot client requested game pause');
          break;
        }
        case 'resume_game': {
          sendToSocket('P');
          console.log('Bot client requested game resume');
          break;
        }
        default: {
          console.log('Unknown command received from robot: ' + command);
          break;
        }
      }
    }
  });

  // Event on close
  socket.addEventListener('close', (_) => {
    socketOpen = false;
    gameMode = Modes.Offline;
    console.log('WebSocket connection closed');
  });

  // Event on close for bot-web client connection
  botSocket.addEventListener('close', (_) => {
    botSocketOpen = false;
    gameMode = Modes.Offline;
    console.log('WebSocket connection with robot closed');
  });

  // Track the size of the window, to determine the grid size
  let innerWidth = 0;
  let innerHeight = 0;
  $: gridSize = Math.min(innerHeight / 31, innerWidth / 28)

  // Calculate the remainder when currTicks is divided by updatePeriod
  $: modTicks = currTicks % updatePeriod

  // Deal with media control-related (pause, play) keys
  let mediaControlKeyHeld = false;
  const mediaControlCommand = (key) => {

    /*
      If media control-related keys are pressed, reset the cooldown and
      send the command back to the keydown handler
    */
    if (key === 'p' && gameMode !== Modes.Paused) {
      mediaControlKeyHeld = true;
      return 'p';
    } else if (key === 'P' && gameMode === Modes.Paused) {
      mediaControlKeyHeld = true;
      return 'P';
    } else if (key === ' ' && !mediaControlKeyHeld) {
      mediaControlKeyHeld = true;
      return (gameMode === Modes.Paused ? 'P' : 'p');
    } else if (key === 'Escape') {
      return 'r';
    }
    return null;
  }

  // Deal with motion-related keys
  let lastMotionTicks = 0;
  const motionCommand = (key) => {

    /*
      If not enough ticks (with a threshold of 1/4 of the update period)
      have elapsed since the last motion key, ignore this key
    */
    if ((4 * (currTicks - lastMotionTicks) < updatePeriod)) {
      if (currTicks === 0) {
        lastMotionTicks = currTicks;
      }
      return null;
    }

    const checkViolation = (rowDir, colDir, steps) => {
      let newRow = (pacmanRowState & 31) + rowDir * steps;
      let newCol = (pacmanColState & 31) + colDir * steps;
      if ((walls[newRow] >> (newCol)) & 1) {
        // console.log('wall violation', (pacmanRowState & 0b11111) - 1, (pacmanColState & 0b11111))
        return true;
      }
      return false;
    };

    const checkIntersection = (rowDir, colDir, steps) => {
      let newRow = (pacmanRowState & 31) + rowDir * steps;
      let newCol = (pacmanColState & 31) + colDir * steps;
      if ((intersections[newRow] >> (newCol)) & 1) {
        // console.log('valid intersection found', (pacmanRowState & 0b11111) - 1, (pacmanColState & 0b11111))
        return [newRow, newCol];
      }
      return [32, 32];
    };

    /*
      If motion-related keys are pressed, reset the cooldown and
      send the command back to the keydown handler
    */
    let rowDir = 0;
    let colDir = 0;
    if (key === 'w' || key === 'ArrowUp') {
      lastMotionTicks = currTicks;
      rowDir = -1;
      colDir = 0;
      if (shiftKeyHeld) {
        for (let steps = 1; !checkViolation(rowDir, colDir, steps); steps++) {
          var [newRow, newCol] = checkIntersection(rowDir, colDir, steps);
          if (newRow < 32 && newCol < 32) {
            return 'x' + String.fromCharCode(newRow) + String.fromCharCode(newCol);
          }
        }
      }
      return 'w';
    } else if (key === 'a' || key === 'ArrowLeft') {
      lastMotionTicks = currTicks;
      rowDir = 0;
      colDir = -1;
      if (shiftKeyHeld) {
        for (let steps = 1; !checkViolation(rowDir, colDir, steps); steps++) {
          var [newRow, newCol] = checkIntersection(rowDir, colDir, steps);
          if (newRow < 32 && newCol < 32) {
            return 'x' + String.fromCharCode(newRow) + String.fromCharCode(newCol);
          }
        }
      }
      return 'a';
    } else if (key === 's' || key === 'ArrowDown') {
      lastMotionTicks = currTicks;
      rowDir = 1;
      colDir = 0;
      if (shiftKeyHeld) {
        for (let steps = 1; !checkViolation(rowDir, colDir, steps); steps++) {
          var [newRow, newCol] = checkIntersection(rowDir, colDir, steps);
          if (newRow < 32 && newCol < 32) {
            return 'x' + String.fromCharCode(newRow) + String.fromCharCode(newCol);
          }
        }
      }
      return 's';
    } else if (key === 'd' || key === 'ArrowRight') {
      lastMotionTicks = currTicks;
      rowDir = 0;
      colDir = 1;
      if (shiftKeyHeld) {
        for (let steps = 1; !checkViolation(rowDir, colDir, steps); steps++) {
          var [newRow, newCol] = checkIntersection(rowDir, colDir, steps);
          if (newRow < 32 && newCol < 32) {
            return 'x' + String.fromCharCode(newRow) + String.fromCharCode(newCol);
          }
        }
      }
      return 'd';
    }
    return null;
  }

  // Send message to websocket server (with error handling for closed sockets)
  const sendToSocket = (message) => {
    if (socketOpen) {
      socket.send(message);
    }
  }

  // Const toggle pause
  const togglePause = () => {

    // Send the command directly to the socket
    sendToSocket(gameMode === Modes.Paused ? 'P' : 'p');
  }

  const toggleReset = () => {
    sendToSocket('R');
  }

  // Showing intersections
  let showIntersections = false;

  // Shift key held
  let shiftKeyHeld = false;

  // Handle key presses, to send responses back to the server
  const handleKeyDown = (event) => {

    // Retrieve the key information
    const key = event.key;

    // Check if it is a pause/play command
    const control = mediaControlCommand(key);
    if (control) {
      sendToSocket(control);
    }

    // Check if it is a motion command
    const motion = motionCommand(key);
    if (motion) {
      sendToSocket(motion);
    }

    else if (key === 'i') {
      console.log('toggling intersections')
      showIntersections = !showIntersections;
    } else if (key == 'Shift') {
      // console.log('holding shift');
      shiftKeyHeld = true;
    }
  }

  // Handle key releases, for allowing toggle commands to be sent again
  const handleKeyUp = (event) => {

    // Retrieve the key information
    const key = event.key;

    if (key === 'p' || key === 'P' || key === ' ') {
      mediaControlKeyHeld = false;
    } else if (key == 'Shift') {
      // console.log('releasing shift');
      shiftKeyHeld = false;
    }
  }

</script>

<svelte:window
  on:keydown={handleKeyDown}
  on:keyup={handleKeyUp}
  bind:innerWidth
  bind:innerHeight
/>

<div class='maze-space' style:--grid-size="{gridSize}px">

  <Maze
    {gridSize}
  />

  <Pellets
    {pelletGrid}
    {gridSize}
    {botSocket}
    {intersectionGrid}
    {showIntersections}
  />

  <Fruit
    {gridSize}
    {fruitRowState}
    {fruitColState}
    {fruitSteps}
    {fruitDuration}
  />

  <Pacman
    {gridSize}
    {pacmanRowState}
    {pacmanColState}
  />

  {#if numActiveGhosts >= 1}
    <Ghost
      {gridSize}
      {modTicks}
      {updatePeriod}
      rowState={redRowState}
      colState={redColState}
      frightState={redFrightState}
      color='red'
    />
  {/if}

  {#if numActiveGhosts >= 2}
    <Ghost
      {gridSize}
      {modTicks}
      {updatePeriod}
      rowState={pinkRowState}
      colState={pinkColState}
      frightState={pinkFrightState}
      color='pink'
    />
  {/if}

  {#if numActiveGhosts >= 3}
    <Ghost
      {gridSize}
      {modTicks}
      {updatePeriod}
      rowState={cyanRowState}
      colState={cyanColState}
      frightState={cyanFrightState}
      color='cyan'
    />
  {/if}

  {#if numActiveGhosts >= 4}
    <Ghost
      {gridSize}
      {modTicks}
      {updatePeriod}
      rowState={orangeRowState}
      colState={orangeColState}
      frightState={orangeFrightState}
      color='orange'
    />
  {/if}

  <Mps
    {gridSize}
    {mpsAvg}
  />

  <Ticker
    {gridSize}
    {modTicks}
    {updatePeriod}
    {gameMode}
    {modeSteps}
    {modeDuration}
    {Modes}
    {togglePause}
  />

  <Reset
    {gridSize}
    {toggleReset}
  />

  <Score
    {gridSize}
    {currLevel}
    {currScore}
  />

  <Lives
    {gridSize}
    {currLives}
    {Directions}
  />

</div>

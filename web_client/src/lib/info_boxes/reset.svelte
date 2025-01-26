<style>

  /* Information box for ticker */
  .reset-box {

    /* Positioning */
    position: absolute;
    text-align: center;
    z-index: 1;

    /* Formatting */
    background-color: rgba(255, 255, 0);
    border: none;
    cursor: pointer;
    border-radius: 5px;

    /* Grid-size related CSS */
    width:       calc(5   * var(--grid-size));
    height:      calc(3   * var(--grid-size));
    line-height: calc(3   * var(--grid-size));
    left:        510px;
    top:         10px;
    font-size:   calc(0.9 * var(--grid-size));
    font-family: 'Courier New', Courier, monospace;
    color:rgb(0, 0, 0);
  }

  /* Info box outline (clicked), should be none */
  .reset-box:focus-visible {
    outline: 5px solid rgb(0, 0, 0); /* or any color and width you prefer */
} 

.reset-box:active {
    outline: 5px solid #000000; /* Red outline when clicked */
  }

</style>

<script>

  // Grid size attributes
  export let gridSize;
  $: pad = gridSize/20;

  // Simulated key event (space, toggle pause) when clicking the ticker box
  export let toggleReset;

</script>

<!-- Interactive button to toggle pausing -->
<button
  class='reset-box'
  style:--grid-size='{gridSize}px'
  on:click={() => toggleReset()}
>Reset

</button>
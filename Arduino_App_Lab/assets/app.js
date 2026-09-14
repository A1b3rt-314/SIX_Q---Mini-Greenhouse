/*
SPDX-FileCopyrightText: 2026 Alberto Prestes
SPDX-License-Identifier: 0BSD

SIX_Q - Local Dashboard

Interaction criteria:

- A selector may be moved in two equivalent ways:
  1. drag the large piece;
  2. click directly on a fixed groove.

- The piece never remains between positions.
  When released, it snaps to the nearest valid slot.

- OFF is grey.
- Every active selection is green.
- The rail remains neutral.

Architecture rule:

The dashboard is not the hardware authority.

Immediate requests already supported by the UNO Q are sent normally.
PROGRAM selections are currently interface state only.

Automatic schedules will not be activated from JavaScript.
They will become real only after the corresponding UNO Q rules exist.
*/


// ============================================================
// WebUI connection
// ============================================================

const socket = io(
  "http://" +
  window.location.host,
  {
    transports: [
      "websocket"
    ]
  }
);


// ============================================================
// General elements
// ============================================================

const connectionDot =
  document.getElementById(
    "connection-dot"
  );


const connectionText =
  document.getElementById(
    "connection-text"
  );


const temperatureElement =
  document.getElementById(
    "temperature"
  );


const humidityElement =
  document.getElementById(
    "humidity"
  );


const clockElement =
  document.getElementById(
    "clock"
  );


// ============================================================
// Vision
// ============================================================

const latestImage =
  document.getElementById(
    "latest-image"
  );


const imagePlaceholder =
  document.getElementById(
    "image-placeholder"
  );


const captureTime =
  document.getElementById(
    "capture-time"
  );


const captureButton =
  document.getElementById(
    "capture-button"
  );


const captureMessage =
  document.getElementById(
    "capture-message"
  );


const captureRule =
  document.getElementById(
    "capture-rule"
  );


const analyzeAiButton =
  document.getElementById(
    "analyze-ai-button"
  );


const aiAnalysisOverlay =
  document.getElementById(
    "ai-analysis-overlay"
  );


// ============================================================
// Control elements
// ============================================================

const waterState =
  document.getElementById(
    "water-state"
  );


const lightState =
  document.getElementById(
    "light-state"
  );


const waterMode =
  document.getElementById(
    "water-mode"
  );


const lightMode =
  document.getElementById(
    "light-mode"
  );


const waterSelector =
  document.getElementById(
    "water-selector"
  );


const lightSelector =
  document.getElementById(
    "light-selector"
  );


// ============================================================
// Runtime state
// ============================================================

let latestState =
  null;


let serverClockEpochMs =
  null;


let serverClockReceivedAtMs =
  null;


let aiAnalysisBusy =
  false;


let lastDisplayedImageUrl =
  null;


// ============================================================
// Number formatting
// ============================================================

function formatMeasurement(
  value
) {

  /*
   * Temperature and humidity intentionally use one decimal.
   *
   * More decimals would make the display look more precise
   * without adding useful information for this dashboard.
   */

  if (
    value === null
    ||
    value === undefined
    ||
    Number.isNaN(
      Number(value)
    )
  ) {

    return "--.-";
  }


  return Number(
    value
  ).toFixed(
    1
  );
}


// ============================================================
// Connection
// ============================================================

function setConnection(
  connected
) {

  connectionDot.classList.toggle(
    "online",
    connected
  );


  connectionDot.classList.toggle(
    "offline",
    !connected
  );


  connectionText.textContent =
    connected
      ? "Connected"
      : "Disconnected";


  refreshCaptureButton();
  refreshAiButton();
}


// ============================================================
// Actual hardware state
// ============================================================

function showHardwareState(
  element,
  on
) {

  element.textContent =
    on
      ? "ON"
      : "OFF";


  element.classList.toggle(
    "on",
    on
  );


  element.classList.toggle(
    "off",
    !on
  );
}


// ============================================================
// Image
// ============================================================

function updateImage(
  state
) {

  if (
    state.last_image_available
    &&
    state.last_image_url
  ) {

    if (
      lastDisplayedImageUrl !== null
      &&
      lastDisplayedImageUrl !==
        state.last_image_url
    ) {

      aiAnalysisOverlay.hidden =
        true;

      aiAnalysisOverlay.textContent =
        "";
    }


    lastDisplayedImageUrl =
      state.last_image_url;


    latestImage.src =
      state.last_image_url;


    latestImage.hidden =
      false;


    imagePlaceholder.hidden =
      true;

  } else {

    latestImage.hidden =
      true;


    imagePlaceholder.hidden =
      false;
  }


  captureTime.textContent =
    state.last_capture_utc
      ?
      new Date(
        state.last_capture_utc
      ).toLocaleString()
      :
      "No capture yet";
}


// ============================================================
// UNO Q clock
// ============================================================

function updateServerClock(
  state
) {

  if (
    !state.server_time_utc
  ) {

    return;
  }


  serverClockEpochMs =
    new Date(
      state.server_time_utc
    ).getTime();


  serverClockReceivedAtMs =
    Date.now();
}


function refreshClock() {

  if (
    serverClockEpochMs === null
    ||
    serverClockReceivedAtMs === null
  ) {

    clockElement.textContent =
      "--:--:--";

    return;
  }


  const elapsed =
    Date.now() -
    serverClockReceivedAtMs;


  const current =
    new Date(
      serverClockEpochMs +
      elapsed
    );


  clockElement.textContent =
    current.toLocaleTimeString(
      [],
      {
        hour:
          "2-digit",

        minute:
          "2-digit",

        second:
          "2-digit"
      }
    );
}


// ============================================================
// USER capture rules
// ============================================================

function captureUnavailableReason() {

  if (
    !socket.connected
  ) {

    return (
      "Dashboard disconnected."
    );
  }


  if (
    !latestState
  ) {

    return (
      "Waiting for SIX_Q state."
    );
  }


  if (
    latestState.capture_busy
  ) {

    return (
      "Vision is busy."
    );
  }


  const now =
    Date.now();


  if (
    latestState
      .capture_cooldown_until_epoch_ms
    &&
    now <
    latestState
      .capture_cooldown_until_epoch_ms
  ) {

    const seconds =
      Math.ceil(
        (
          latestState
            .capture_cooldown_until_epoch_ms
          -
          now
        )
        /
        1000
      );


    return (
      `Available in ${seconds}s.`
    );
  }


  if (
    Number(
      latestState
        .capture_remaining_last_hour
    )
    <=
    0
  ) {

    return (
      "Hourly USER capture limit reached."
    );
  }


  return "";
}


function refreshCaptureButton() {

  const reason =
    captureUnavailableReason();


  captureButton.disabled =
    Boolean(
      reason
    );


  if (
    latestState
  ) {

    const used =
      Number(
        latestState
          .capture_used_last_hour
        ||
        0
      );


    captureRule.textContent =
      (
        `${used} / 3 USER captures `
        +
        `used in the last hour.`
      );


    captureButton.textContent =
      reason
        ?
        "CAPTURE LOCKED"
        :
        "CAPTURE NOW";
  }
}


// ============================================================
// AI analysis
// ============================================================

function refreshAiButton() {

  if (!analyzeAiButton) {

    return;
  }


  const imageAvailable =
    Boolean(
      latestState
      &&
      latestState.last_image_available
    );


  analyzeAiButton.disabled =
    (
      !socket.connected
      ||
      !imageAvailable
      ||
      aiAnalysisBusy
    );


  analyzeAiButton.textContent =
    aiAnalysisBusy
      ? "ANALYZING..."
      : "ANALYZE WITH AI";
}


function showAiOverlay(
  text
) {

  if (!aiAnalysisOverlay) {

    return;
  }


  aiAnalysisOverlay.textContent =
    text || "";


  aiAnalysisOverlay.hidden =
    !text;
}


// ============================================================
// Rail selector helper
// ============================================================

function createRailSelector(
  selector,
  onSelection
) {

  const knob =
    selector.querySelector(
      ".selector-knob"
    );


  const slots = Array.from(
    selector.querySelectorAll(
      ".rail-slot"
    )
  );


  const railArea =
    selector.querySelector(
      ".rail-area"
    );


  let selectedIndex =
    Number(
      selector.dataset.selectedIndex
      ||
      0
    );


  let dragging =
    false;


  // ----------------------------------------------------------
  // Slot center
  // ----------------------------------------------------------

  function slotCenter(
    index
  ) {

    const slot =
      slots[index];


    const areaRect =
      railArea.getBoundingClientRect();


    const slotRect =
      slot.getBoundingClientRect();


    return (
      slotRect.left
      -
      areaRect.left
      +
      slotRect.width / 2
    );
  }


  // ----------------------------------------------------------
  // Move physical piece
  // ----------------------------------------------------------

  function drawPosition(
    index,
    animate=true
  ) {

    const x =
      slotCenter(
        index
      );


    if (!animate) {

      knob.style.transition =
        "none";

    } else {

      knob.style.removeProperty(
        "transition"
      );
    }


    knob.style.left =
      `${x}px`;


    knob.classList.toggle(
      "off",
      index === 0
    );


    knob.classList.toggle(
      "active",
      index !== 0
    );


    knob.setAttribute(
      "aria-valuenow",
      String(index)
    );


    requestAnimationFrame(
      function () {

        if (!animate) {

          knob.style.removeProperty(
            "transition"
          );
        }
      }
    );
  }


  // ----------------------------------------------------------
  // Commit selection
  // ----------------------------------------------------------

  function selectIndex(
    index,
    notify=true
  ) {

    index = Math.max(
      0,
      Math.min(
        slots.length - 1,
        index
      )
    );


    selectedIndex =
      index;


    selector.dataset.selectedIndex =
      String(
        selectedIndex
      );


    drawPosition(
      selectedIndex,
      true
    );


    if (
      notify
      &&
      typeof onSelection ===
      "function"
    ) {

      onSelection(
        slots[selectedIndex],
        selectedIndex
      );
    }
  }


  // ----------------------------------------------------------
  // Nearest slot
  // ----------------------------------------------------------

  function nearestIndex(
    pointerX
  ) {

    const areaRect =
      railArea.getBoundingClientRect();


    const localX =
      pointerX -
      areaRect.left;


    let bestIndex =
      0;


    let bestDistance =
      Infinity;


    slots.forEach(
      function (
        slot,
        index
      ) {

        const distance =
          Math.abs(
            slotCenter(index)
            -
            localX
          );


        if (
          distance <
          bestDistance
        ) {

          bestDistance =
            distance;


          bestIndex =
            index;
        }
      }
    );


    return bestIndex;
  }


  // ----------------------------------------------------------
  // Click on groove
  // ----------------------------------------------------------

  slots.forEach(
    function (
      slot,
      index
    ) {

      slot.addEventListener(
        "click",
        function () {

          selectIndex(
            index,
            true
          );
        }
      );
    }
  );


  // ----------------------------------------------------------
  // Start dragging
  // ----------------------------------------------------------

  knob.addEventListener(
    "pointerdown",
    function (event) {

      dragging =
        true;


      knob.classList.add(
        "dragging"
      );


      knob.setPointerCapture(
        event.pointerId
      );


      event.preventDefault();
    }
  );


  // ----------------------------------------------------------
  // Drag freely along rail
  // ----------------------------------------------------------

  knob.addEventListener(
    "pointermove",
    function (event) {

      if (!dragging) {

        return;
      }


      const areaRect =
        railArea.getBoundingClientRect();


      const first =
        slotCenter(
          0
        );


      const last =
        slotCenter(
          slots.length - 1
        );


      const raw =
        event.clientX -
        areaRect.left;


      const x =
        Math.max(
          first,
          Math.min(
            last,
            raw
          )
        );


      knob.style.transition =
        "none";


      knob.style.left =
        `${x}px`;
    }
  );


  // ----------------------------------------------------------
  // Release -> snap to nearest groove
  // ----------------------------------------------------------

  function finishDrag(
    event
  ) {

    if (!dragging) {

      return;
    }


    dragging =
      false;


    knob.classList.remove(
      "dragging"
    );


    const index =
      nearestIndex(
        event.clientX
      );


    selectIndex(
      index,
      true
    );
  }


  knob.addEventListener(
    "pointerup",
    finishDrag
  );


  knob.addEventListener(
    "pointercancel",
    function () {

      dragging =
        false;


      knob.classList.remove(
        "dragging"
      );


      drawPosition(
        selectedIndex,
        true
      );
    }
  );


  // ----------------------------------------------------------
  // Keyboard accessibility
  // ----------------------------------------------------------

  knob.addEventListener(
    "keydown",
    function (event) {

      if (
        event.key ===
        "ArrowRight"
      ) {

        event.preventDefault();


        selectIndex(
          selectedIndex + 1,
          true
        );
      }


      if (
        event.key ===
        "ArrowLeft"
      ) {

        event.preventDefault();


        selectIndex(
          selectedIndex - 1,
          true
        );
      }
    }
  );


  // ----------------------------------------------------------
  // Keep alignment after browser resize
  // ----------------------------------------------------------

  window.addEventListener(
    "resize",
    function () {

      drawPosition(
        selectedIndex,
        false
      );
    }
  );


  // Initial position after layout exists.

  requestAnimationFrame(
    function () {

      drawPosition(
        selectedIndex,
        false
      );
    }
  );


  // Public methods.

  return {

    select:
      function (
        index,
        notify=false
      ) {

        selectIndex(
          index,
          notify
        );
      },


    getIndex:
      function () {

        return selectedIndex;
      }
  };
}


// ============================================================
// WATER selector
// ============================================================

const waterRail =
  createRailSelector(

    waterSelector,

    function (
      slot,
      index
    ) {

      const value =
        slot.dataset.value;


      if (
        value === "off"
      ) {

        waterMode.textContent =
          "MANUAL · OFF";


        socket.emit(
          "water_request",
          {
            on:
              false
          }
        );


        return;
      }


      if (
        value === "now"
      ) {

        /*
         * This reaches the validated WATER request path.
         *
         * The UNO Q owns the complete 5 second pulse.
         *
         * The selector returns to OFF only when the UNO Q
         * reports that the irrigation cycle has completed.
         */

        waterMode.textContent =
          "MANUAL · 5s NOW";


        socket.emit(
          "water_request",
          {
            on:
              true
          }
        );


        return;
      }


      /*
       * PROGRAM selections are intentionally not emitted yet.
       *
       * The browser may show the requested program, but it is
       * not allowed to become the scheduler.
       */

      waterMode.textContent =
        (
          `PROGRAM · 5s EVERY `
          +
          `${value}h`
        );
    }
  );


// ============================================================
// LIGHT selector
// ============================================================

const lightWindows = {

  "6":
    "06:00–12:00",

  "8":
    "06:00–14:00",

  "12":
    "06:00–18:00"
};


const lightRail =
  createRailSelector(

    lightSelector,

    function (
      slot,
      index
    ) {

      const value =
        slot.dataset.value;


      if (
        value === "off"
      ) {

        lightMode.textContent =
          "MANUAL · OFF";


        socket.emit(
          "light_request",
          {
            on:
              false
          }
        );


        return;
      }


      if (
        value === "on"
      ) {

        lightMode.textContent =
          "MANUAL · ON";


        socket.emit(
          "light_request",
          {
            on:
              true
          }
        );


        return;
      }


      lightMode.textContent =
        (
          `PROGRAM · `
          +
          lightWindows[value]
        );


      /*
       * The selected window is visible, but not active yet.
       *
       * Automatic light timing belongs to the UNO Q.
       */
    }
  );


// ============================================================
// Apply authoritative system state
// ============================================================

function applyState(
  state
) {

  latestState =
    state;


  temperatureElement.textContent =
    formatMeasurement(
      state.temperature
    );


  humidityElement.textContent =
    formatMeasurement(
      state.humidity
    );


  showHardwareState(
    waterState,
    Boolean(
      state.water
    )
  );


  showHardwareState(
    lightState,
    Boolean(
      state.light
    )
  );


  captureMessage.textContent =
    (
      state.capture_message
      ||
      "No USER capture requested yet."
    );


  updateImage(
    state
  );


  updateServerClock(
    state
  );


  refreshCaptureButton();
  refreshAiButton();


  /*
   * Immediate/manual selectors follow actual hardware state
   * only while no visual PROGRAM position is selected.
   *
   * Later, when PROGRAM becomes authoritative UNO Q state,
   * the selected mode will also come from the backend.
   */


  if (
    waterRail.getIndex()
    <=
    1
  ) {

    waterRail.select(
      state.water
        ? 1
        : 0,
      false
    );
  }


  if (
    lightRail.getIndex()
    <=
    1
  ) {

    lightRail.select(
      state.light
        ? 1
        : 0,
      false
    );
  }
}


// ============================================================
// Socket events
// ============================================================

socket.on(
  "connect",
  function () {

    setConnection(
      true
    );
  }
);


socket.on(
  "disconnect",
  function () {

    setConnection(
      false
    );
  }
);


socket.on(
  "connect_error",
  function () {

    setConnection(
      false
    );
  }
);


socket.on(
  "state_update",
  function (data) {

    if (data) {

      applyState(
        data
      );
    }
  }
);


socket.on(
  "ai_analysis_status",
  function (data) {

    if (!data) {

      return;
    }


    const status =
      String(
        data.status || ""
      );


    if (status === "analyzing") {

      aiAnalysisBusy =
        true;

      showAiOverlay(
        "ANALYZING IMAGE..."
      );

    } else if (status === "complete") {

      aiAnalysisBusy =
        false;

      showAiOverlay(
        data.overlay_text
        ||
        "AI ANALYSIS COMPLETE"
      );

    } else {

      aiAnalysisBusy =
        false;

      showAiOverlay(
        data.overlay_text
        ||
        "AI ANALYSIS FAILED"
      );
    }


    refreshAiButton();
  }
);


// ============================================================
// AI analysis trigger
// ============================================================

if (analyzeAiButton) {

  analyzeAiButton.addEventListener(
    "click",
    function () {

      if (
        analyzeAiButton.disabled
        ||
        aiAnalysisBusy
      ) {

        return;
      }


      aiAnalysisBusy =
        true;


      showAiOverlay(
        "ANALYZING IMAGE..."
      );


      refreshAiButton();


      socket.emit(
        "ai_analysis_request",
        {}
      );
    }
  );
}


// ============================================================
// USER Vision trigger
// ============================================================

captureButton.addEventListener(
  "click",
  function () {

    /*
     * Disable immediately to avoid a double-click before
     * the backend answers.
     *
     * The UNO Q application still evaluates the real capture
     * limits independently.
     */

    captureButton.disabled =
      true;


    captureButton.textContent =
      "REQUESTING...";


    socket.emit(
      "user_capture",
      {}
    );
  }
);


// ============================================================
// Local visual timers
// ============================================================

setInterval(
  refreshClock,
  250
);


setInterval(
  refreshCaptureButton,
  1000
);


setConnection(
  false
);


refreshAiButton();
refreshClock();
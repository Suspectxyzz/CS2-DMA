// Bomb timer UI
//
// Shows C4 countdown bar at the top of the radar.
// - planted: countdown from m_blow_time to 0, progress bar from full to empty
// - defusing: continue countdown, show DEFUSE indicator
// - urgent: red flashing when <= 10s
// - defused/exploded/carried/dropped/planting: hide
//
// Uses requestAnimationFrame for smooth countdown based on local time delta.

let bombTimerEl = document.getElementById("bomb-timer")
let bombFillEl = bombTimerEl.querySelector(".bomb-timer-fill")
let bombTextEl = bombTimerEl.querySelector(".bomb-timer-text")
let bombDefuseEl = bombTimerEl.querySelector(".bomb-timer-defuse")
let bombDefuseFillEl = bombTimerEl.querySelector(".bomb-timer-defuse-fill")

// State for smooth countdown
let bombTimerState = {
  blowTime: 0,       // countdown value (seconds) captured at last bomb event
  eventTime: 0,      // local timestamp (ms) of last bomb event
  defuseTime: 0,     // total seconds needed to defuse (0 = not defusing)
  defuseEventTime: 0,// local timestamp (ms) when defusing started
  rafId: null
}

// Track last seen values to avoid resetting interpolation every frame
let lastCountdown = null
let lastDefuseTime = null

// C4 explosion time in CS2 is 40s, used as denominator for progress bar
const C4_TOTAL_TIME = 40

function updateBombTimer() {
  let now = Date.now()
  let elapsed = (now - bombTimerState.eventTime) / 1000
  let remaining = bombTimerState.blowTime - elapsed
  if (remaining < 0) remaining = 0

  // Update text
  bombTextEl.textContent = remaining.toFixed(1)

  // Update progress bar (from full to empty)
  let progress = remaining / C4_TOTAL_TIME
  if (progress > 1) progress = 1
  if (progress < 0) progress = 0
  bombFillEl.style.width = (progress * 100) + "%"

  // Urgent highlight when <= 10s
  if (remaining <= 10 && remaining > 0) {
    bombTimerEl.classList.add("urgent")
  } else {
    bombTimerEl.classList.remove("urgent")
  }

  // Update defuse progress bar if defusing
  if (bombTimerState.defuseTime > 0) {
    let defuseElapsed = (now - bombTimerState.defuseEventTime) / 1000
    let defuseProgress = defuseElapsed / bombTimerState.defuseTime
    if (defuseProgress > 1) defuseProgress = 1
    if (defuseProgress < 0) defuseProgress = 0
    bombDefuseFillEl.style.width = (defuseProgress * 100) + "%"
  }

  // Continue animation while there is time left
  if (remaining > 0) {
    bombTimerState.rafId = requestAnimationFrame(updateBombTimer)
  } else {
    bombTimerState.rafId = null
  }
}

function startBombTimer(countdown) {
  bombTimerState.blowTime = countdown
  bombTimerState.eventTime = Date.now()
  if (bombTimerState.rafId) cancelAnimationFrame(bombTimerState.rafId)
  bombTimerState.rafId = requestAnimationFrame(updateBombTimer)
}

function stopBombTimer() {
  if (bombTimerState.rafId) {
    cancelAnimationFrame(bombTimerState.rafId)
    bombTimerState.rafId = null
  }
  bombTimerEl.classList.remove("urgent")
  bombTimerState.defuseTime = 0
}

socket.element.addEventListener("bomb", event => {
  let bomb = event.data

  if (bomb.state === "planted" || bomb.state === "defusing") {
    bombTimerEl.hidden = false
    // Only restart interpolation when the countdown value actually changes,
    // otherwise every frame would reset eventTime and break local interpolation.
    if (lastCountdown !== bomb.countdown) {
      lastCountdown = bomb.countdown
      startBombTimer(bomb.countdown)
    }

    // Show DEFUSE indicator and fill progress bar when defusing.
    // defuseTime is the total seconds needed to defuse (from m_defuse_time).
    if (bomb.state === "defusing") {
      bombDefuseEl.hidden = false
      // Only reset defuseEventTime when defuseTime changes (e.g. defuse starts
      // or kit/no-kit changes), not every frame.
      if (lastDefuseTime !== bomb.defuseTime) {
        lastDefuseTime = bomb.defuseTime
        bombTimerState.defuseTime = bomb.defuseTime || 0
        bombTimerState.defuseEventTime = Date.now()
      }
    } else {
      bombDefuseEl.hidden = true
      bombTimerState.defuseTime = 0
      lastDefuseTime = null
    }
  } else {
    // carried, dropped, defused, exploded, planting
    bombTimerEl.hidden = true
    bombDefuseEl.hidden = true
    stopBombTimer()
    lastCountdown = null
    lastDefuseTime = null
  }
})

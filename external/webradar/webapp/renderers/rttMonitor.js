// WebRadar RTT monitor (Task 14)
// Polls /api/ping every 1.5s, computes round-trip time median over the last
// 20 samples, and updates the #conn-status indicator. Dispatches "rttupdate"
// CustomEvents on document with detail { rtt, status } where status is one of
// "connecting" | "live" | "error".

(function () {
  "use strict";

  var PING_INTERVAL = 1500;   // cadence between ping requests
  var PING_TIMEOUT = 2000;    // fetch abort threshold; >this means trouble
  var MAX_SAMPLES = 20;
  var LIVE_RTT_THRESHOLD = 2000;  // status flips to "error" at/above this RTT

  var samples = [];           // recent RTT samples (ms)
  var status = "connecting";  // current status
  var timer = null;

  var indicator = document.getElementById("conn-status");

  function median(arr) {
    if (arr.length === 0) return 0;
    var sorted = arr.slice().sort(function (a, b) { return a - b; });
    var mid = Math.floor(sorted.length / 2);
    return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
  }

  function emit(rtt) {
    var event = new CustomEvent("rttupdate", { detail: { rtt: rtt, status: status } });
    document.dispatchEvent(event);
  }

  function render() {
    if (!indicator) return;
    indicator.classList.remove("conn-status--live", "conn-status--error", "conn-status--connecting");
    if (status === "live") {
      indicator.classList.add("conn-status--live");
      indicator.textContent = "RTT: " + Math.round(median(samples)) + "ms";
    } else if (status === "error") {
      indicator.classList.add("conn-status--error");
      indicator.textContent = "OFFLINE";
    } else {
      indicator.classList.add("conn-status--connecting");
      indicator.textContent = "...";
    }
  }

  function setStatus(next, rtt) {
    if (status === next) return;
    status = next;
    render();
    emit(rtt);
  }

  function pingOnce() {
    var start = performance.now();
    var controller = null;
    var timedOut = false;
    if (typeof AbortController === "function") {
      controller = new AbortController();
      setTimeout(function () {
        timedOut = true;
        if (controller) controller.abort();
      }, PING_TIMEOUT);
    }

    fetch("/api/ping", controller ? { signal: controller.signal, cache: "no-store" } : { cache: "no-store" })
      .then(function () {
        var rtt = performance.now() - start;
        samples.push(rtt);
        if (samples.length > MAX_SAMPLES) samples.shift();
        if (rtt < LIVE_RTT_THRESHOLD) {
          setStatus("live", rtt);
        } else {
          setStatus("error", rtt);
        }
        render();
      })
      .catch(function () {
        // AbortError (timeout) or network failure — both mean we can't reach the server.
        setStatus("error", 0);
      });
  }

  function start() {
    render(); // initial "connecting" state
    pingOnce();
    timer = setInterval(pingOnce, PING_INTERVAL);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", start);
  } else {
    start();
  }
})();

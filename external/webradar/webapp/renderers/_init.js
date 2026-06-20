// Render initializer
//
// Applies default config and prepares blast radius elements.
// Scripts are loaded statically in index.html, so no dynamic
// injection (importScripts) or welcome event is needed anymore.

// If the map is ready, called by _map.js
let hasMap = false

// Default config mirroring config.json5, used when no override is
// available from localStorage. The welcome packet from the original
// BoltObserv socket is no longer emitted by the WebRadar adapter.
const defaultConfig = {
	browser: {
		transparent: false
	},
	radar: {
		hideAdvisories: false,
		showTeamPanel: true,
		highestPlayerOnTop: true,
		showBuyzones: "buytime",
		showBlastRadius: "active",
		showName: "never",
		maxNameLength: 8,
		shooting: true,
		damage: true,
		flashes: true,
		projectiles: true,
		smokeColors: true,
		plainProjectiles: false,
		playerSmoothing: 13,
		projectileSmoothing: 5,
		tombstoneOpacity: 0.4,
		playerDotScale: 0.7,
		bombDotScale: 0.7
	},
	vertIndicator: {
		type: "scale",
		colorRange: [[13, 255, 0], [255, 255, 255], [255, 0, 199]],
		scaleDelta: 1
	},
	autozoom: {
		enable: true,
		smoothing: 32,
		padding: 0.3,
		minZoom: 1.3
	}
}

/**
 * Load config from localStorage if available, otherwise fall back to
 * the default config. Allows the user to override values without a
 * welcome packet.
 */
function loadConfig() {
	let config = defaultConfig

	try {
		let stored = localStorage.getItem("boltobserv_config")
		if (stored) {
			let parsed = JSON.parse(stored)
			// Shallow-merge top-level keys so the user can override
			// entire sections (radar, autozoom, ...) at once.
			for (let key in parsed) {
				if (parsed.hasOwnProperty(key)) {
					config[key] = parsed[key]
				}
			}
		}
	}
	catch (err) {
		// Ignore malformed localStorage entries and keep defaults
	}

	return config
}

// Initialize global.config immediately so that renderers loaded after
// _init.js (projectiles.js, loopSlow.js, etc.) can safely access
// global.config.radar and global.config.autozoom even before the DOM
// is ready. DOM-dependent side effects are deferred to applyConfig().
global.config = loadConfig()

/**
 * Apply all config-driven side effects: player dot scaling, blast
 * radius element pre-creation and CSS variable injection.
 */
function applyConfig() {
	// Loop through each player dot to apply the scaling config option
	for (let playerElem of document.getElementsByClassName("dot")) {
		playerElem.style.transform = `scale(${global.config.radar.playerDotScale}) translate(-50%, 50%)`
	}

	for (let labelElement of document.getElementsByClassName("label")) {
		labelElement.style.transform = `scale(${global.config.radar.playerDotScale}) translate(-50%, 50%)`
	}

	if (global.config.radar.showBlastRadius === 'active') {
		for (let i = 0; i < 10; i++) {
			const blastEl = document.createElementNS("http://www.w3.org/2000/svg", "svg")
			blastEl.id = `blast${i}`
			blastEl.setAttribute("viewBox", "0 0 100 100")
			blastEl.innerHTML = `<path fill="none" d=""></path>`
			document.getElementById("blasts").appendChild(blastEl)

			global.playerBlasts.push(blastEl.getElementsByTagName("path"))
		}
	}

	// Insert stylesheet into head to apply some CSS settings from config
	document.documentElement.style.setProperty("--config-tombstone-opacity", global.config.radar.tombstoneOpacity)
	document.documentElement.style.setProperty("--config-bomb-dot-scale", global.config.radar.bombDotScale)
}

// Run the initializer as soon as the DOM is ready. All renderer
// scripts are already loaded via <script> tags in index.html, so we
// only need to apply the config and prepare the blast elements.
if (document.readyState === "loading") {
	document.addEventListener("DOMContentLoaded", applyConfig)
}
else {
	applyConfig()
}

// React to runtime config changes for CSS-variable-driven options
document.addEventListener("configchange", function(event) {
	if (!event.detail) return
	if (event.detail.key === "radar.bombDotScale") {
		document.documentElement.style.setProperty("--config-bomb-dot-scale", event.detail.value)
	}
	else if (event.detail.key === "radar.tombstoneOpacity") {
		document.documentElement.style.setProperty("--config-tombstone-opacity", event.detail.value)
	}
})

/**
 * Apply a single config override at runtime. Updates global.config,
 * persists the result to localStorage and dispatches a "configchange"
 * event so renderers can react.
 * @param {String} key   Dotted path, e.g. "radar.playerDotScale"
 * @param {*}      value The new value
 */
window.applySetting = function(key, value) {
	// 1. Update global.config via dotted-path traversal
	const parts = key.split(".")
	let obj = global.config
	for (let i = 0; i < parts.length - 1; i++) {
		obj = obj[parts[i]]
	}
	obj[parts[parts.length - 1]] = value

	// 2. Persist the full config to localStorage
	try {
		localStorage.setItem("boltobserv_config", JSON.stringify(global.config))
	}
	catch (err) {
		console.warn("applySetting: localStorage 写入失败", err)
	}

	// 3. Notify renderers about the change
	document.dispatchEvent(new CustomEvent("configchange", {
		detail: { key, value }
	}))
}

/**
 * Reset global.config back to defaultConfig, persist the result and
 * notify renderers. The event detail uses key=null to signal a full
 * reset rather than a single-key change.
 */
window.resetConfig = function() {
	// 1. Deep-clone defaultConfig so mutations don't leak into the
	//    const reference
	global.config = JSON.parse(JSON.stringify(defaultConfig))

	// 2. Persist the reset config
	try {
		localStorage.setItem("boltobserv_config", JSON.stringify(global.config))
	}
	catch (err) {
		console.warn("resetConfig: localStorage 写入失败", err)
	}

	// 3. Notify renderers about the full reset
	document.dispatchEvent(new CustomEvent("configchange", {
		detail: { key: null, value: null }
	}))
}

/**
 * Read a config value via dotted-path traversal.
 * @param  {String} key Dotted path, e.g. "radar.playerDotScale"
 * @return {*}          The current value, or undefined if missing
 */
window.getConfigValue = function(key) {
	const parts = key.split(".")
	let obj = global.config
	for (const part of parts) {
		if (obj == null) return undefined
		obj = obj[part]
	}
	return obj
}

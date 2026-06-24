// Initial map rendering
//
// Responsible for changing radar background on map change, loading map
// metadata and applying some general config values.

// Catch map data send by the game
socket.element.addEventListener("map", event => {
	/**
	 * Show a map error and quit
	 * @param  {String} text What error message to show
	 */
	function throwMapError(text) {
		document.getElementById("unknownMap").style.display = "flex"
		document.getElementById("unknownMap").children[0].innerHTML = text
	}

	// If map is unchanged we do not need to do anything
	if (global.currentMap == event.data) return

	let mapName = event.data
	if (mapName.indexOf("/") != -1) {
		mapName = mapName.split("/")[mapName.split("/").length - 1]
	}

	fetch(window.location.origin + `/maps/${mapName}/meta.json5`)
	.then(resp => resp.text())
	.then(data => {
		data = data.replace(/^\s*?\/\/.*?$/gm, "")
		global.mapData = JSON.parse(data)

		// Check if the map uses the expected meta format
		if (!global.mapData.version || global.mapData.version.format != 4) {
			return throwMapError(`Outdated map file for ${mapName}`)
		}

		// Make sure that the "unknown map" message is turned off for valid maps
		document.getElementById("unknownMap").style.display = "none"

		// Show the radar backdrop
		document.getElementById("radarBackground").src = `/maps/${mapName}/radar.png`

		if (global.config.radar.showBuyzones != "never") {
			document.getElementById("radarBuyZones").src = `/maps/${mapName}/overlay_buyzones.png`
		}

		// Set the map as the current map and in the window title
		global.currentMap = mapName
		document.title = t("page_title") + " - " + mapName

		// Hide advisories if you've been disabled in the config
		if (global.config.radar.hideAdvisories) {
			document.getElementById("advisory").style.display = "none"
		}
		else {
			// Otherwise, read the advisory position from config and apply it
			document.getElementById("advisory").style.left = global.mapData.advisoryPosition.x + "%"
			document.getElementById("advisory").style.bottom = global.mapData.advisoryPosition.y + "%"
			document.getElementById("advisory").style.display = "block"
		}

		// Mark the map as ready for other renderers
		hasMap = true
	})
	.catch((err) => {
		return throwMapError(`Error reading the ${mapName} map file :(`)
	})
})

if (global.config.radar.showBuyzones == "buytime") {
	socket.element.addEventListener("canbuy", event => {
		document.getElementById("radarBuyZones").style.opacity = event.data ? 1 : 0
	})
}

document.addEventListener("configchange", function(event) {
	if (!event.detail) return
	if (event.detail.key === "radar.hideAdvisories") {
		let advisoryEl = document.getElementById("advisory")
		if (advisoryEl && hasMap && global.mapData) {
			if (event.detail.value) {
				advisoryEl.style.display = "none"
			}
			else {
				advisoryEl.style.left = global.mapData.advisoryPosition.x + "%"
				advisoryEl.style.bottom = global.mapData.advisoryPosition.y + "%"
				advisoryEl.style.display = "block"
			}
		}
	}
	else if (event.detail.key === "radar.showBuyzones") {
		let buyZonesEl = document.getElementById("radarBuyZones")
		if (buyZonesEl && hasMap && global.currentMap) {
			if (event.detail.value === "never") {
				buyZonesEl.style.opacity = 0
			}
			else {
				if (buyZonesEl.src.includes("empty.webp")) {
					buyZonesEl.src = `/maps/${global.currentMap}/overlay_buyzones.png`
				}
				buyZonesEl.style.opacity = 1
			}
		}
	}
})

// +90 雷达旋转
function updateRotate90() {
	let container = document.getElementById("container")
	if (!container) return
	let rotate90 = !!(global.config && global.config.radar && global.config.radar.rotate90)
	if (rotate90) {
		container.style.transform = "rotate(90deg)"
		container.style.transformOrigin = "center center"
	} else {
		container.style.transform = ""
		container.style.transformOrigin = ""
	}
}

// 按钮点击切换 +90 旋转
document.addEventListener("DOMContentLoaded", function() {
	let btn = document.getElementById("rotate90-toggle")
	if (btn) {
		btn.addEventListener("click", function() {
			let current = !!(global.config && global.config.radar && global.config.radar.rotate90)
			applySetting("radar.rotate90", !current)
		})
	}
})

// 监听配置变化更新旋转
document.addEventListener("configchange", updateRotate90)

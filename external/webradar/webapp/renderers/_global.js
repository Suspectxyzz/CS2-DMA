// Shared render object
//
// Provides shared variables and functions for all renderers.

global = {
	// Config loaded from disk
	config: {},
	// Effects triggered by key-binds
	effects: {},
	mapData: {},
	currentMap: "none",
	// The last known game phase
	gamePhase: "freezetime",

	playerPos: [],
	playerBuffers: [],
	playerSplits: [],
	playerDots: [],
	playerLabels: [],
	playerAmmos: [],
	playerHealths: [],
	playerBlasts: [],

	projectilePos: {},
	projectileBuffer: {},
	bomb: {status: "carried", countdown: Infinity, position: {x: 0, y: 0, z: 0}},


	/**
	 * Convert in-game position units to radar percentages
	 * @param  {Array}  positionObj In-game position object with X and Y, and an optional Z
	 * @param  {String} axis        The axis to calculate position for
	 * @param  {Number} playerNum   An optional player number to wipe location buffer on split switch
	 * @return {Number}             Relative radar percentage
	 */
	positionToPerc: (positionObj, axis, playerNum) => {
		// Make sure it's an object, even when you simply give the number
		if (typeof positionObj != "object") positionObj = {[axis]: positionObj}
		// The position of the player in game, with the bottom left corner of the radar as origin (0,0)
		let gamePosition = positionObj[axis] + global.mapData.offset[axis]
		// The position of the player relative to an 1024x1024 pixel grid
		let pixelPosition = gamePosition / global.mapData.resolution
		// The position of the player as an percentage for any size
		let precPosition = pixelPosition / 1024 * 100

		// Set the split to the default map
		let currentSplit = -1
		// Check if there are splits on the map and if we have a Z position
		if (global.mapData.splits.length > 0 && typeof positionObj.z == "number") {
			// Go through each split
			for (let i in global.mapData.splits) {
				let split = global.mapData.splits[i]

				// If the position is within the split
				if (positionObj.z > split.bounds.bottom && positionObj.z < split.bounds.top) {
					// Apply the split offset and save this split
					precPosition += split.offset[axis]
					currentSplit = parseInt(i)

					// Stop checking other splits as there can only be one active split
					break
				}
			}
		}

		// If we're calculating a player position
		if (typeof playerNum == "number") {
			// Wipe the location buffer if we've changed split
			// Prevents the player from flying across the radar on split switch
			if (global.playerSplits[playerNum] != currentSplit) {
				global.playerBuffers[playerNum] = []
			}

			// Save this split as the last split id seen
			global.playerSplits[playerNum] = currentSplit
			global.playerPos[playerNum].split = currentSplit
		}

		// Apply m_calibration (rotation → scale → offset around center 50,50)
		const cal = global.calibration
		if (cal && typeof cal.rotationDeg === "number") {
			// 旋转需要双轴，仅在 positionObj 同时包含 x 和 y 时应用完整校准
			if (typeof positionObj.x === "number" && typeof positionObj.y === "number") {
				// 重新计算双轴百分比（含 split 偏移）
				let x = (positionObj.x + global.mapData.offset.x) / global.mapData.resolution / 1024 * 100
				let y = (positionObj.y + global.mapData.offset.y) / global.mapData.resolution / 1024 * 100
				if (currentSplit >= 0) {
					x += global.mapData.splits[currentSplit].offset.x
					y += global.mapData.splits[currentSplit].offset.y
				}
				// 1. 旋转（围绕中心 50,50）
				const rad = cal.rotationDeg * Math.PI / 180
				const cx = x - 50
				const cy = y - 50
				x = 50 + (cx * Math.cos(rad) - cy * Math.sin(rad))
				y = 50 + (cx * Math.sin(rad) + cy * Math.cos(rad))
				// 2. 缩放（围绕中心 50,50）
				const scale = cal.scale || 1.0
				x = 50 + (x - 50) / scale
				y = 50 + (y - 50) / scale
				// 3. 偏移（百分比单位）
				x += (cal.offsetX || 0)
				y += (cal.offsetY || 0)
				return (axis === "y") ? y : x
			}
			// 单轴场景（如 playerPosition.js 传入数字）：仅应用缩放和偏移，跳过旋转
			const scale = cal.scale || 1.0
			precPosition = 50 + (precPosition - 50) / scale
			precPosition += (axis === "y") ? (cal.offsetY || 0) : (cal.offsetX || 0)
		}

		// Return the position relative to the radar image
		return precPosition
	}
}

// Fill position and buffer arrays
for (var i = 0; i < 10; i++) {
	global.playerPos.push({
		x: null,
		y: null,
		alive: false
	})

	global.playerSplits.push(-1)
	global.playerBuffers.push([])
	global.playerDots.push(document.getElementById("dot" + i))
	global.playerLabels.push(document.getElementById("label" + i))
	global.playerAmmos.push({})
	global.playerHealths.push(0)
}

// 选中观察的玩家 num（null = 未选中）
global.followedPlayer = null
// 本地玩家 num（由 playerPosition.js 设置）
global.localPlayerNum = null

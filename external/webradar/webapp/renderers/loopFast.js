// Main loop
//
// Sets player position on screen at +/- 60fps

// 名字标签防碰撞常量
// 6 个候选位置（相对于玩家点的偏移，单位百分比，近似 vmin）
// dy 正方向 = bottom 增加 = 向上
const LABEL_OFFSETS = [
	{ dx: 0, dy: -2.5 },    // 下方
	{ dx: 0, dy: 2.5 },     // 上方
	{ dx: -3, dy: 0 },      // 左侧
	{ dx: 3, dy: 0 },       // 右侧
	{ dx: -3, dy: -2.5 },   // 左下
	{ dx: 3, dy: -2.5 }     // 右下
]
const LABEL_W = 5.6
const LABEL_H = 5.6

function rectIntersect(a, b) {
	return !(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y)
}

// Function to be executed before every frame paint
function step() {
	// Wait until _init.js has applied the config; otherwise every access to
	// global.config.radar.* throws and the render loop never fills buffers.
	if (!global.config || !global.config.radar) {
		window.requestAnimationFrame(step)
		return
	}

	// Go though every player location buffer
	for (let num in global.playerBuffers) {
		// Scale that can be changed by the vertical indicator
		let scale = global.config.radar.playerDotScale

		// If a new player position is available
		if (global.playerPos[num].x != null) {
			// We want to check if the angle value has looped around, so we need a previous value
			if (global.playerBuffers[num][0]) {
				// If the angle used to be small, but is large now
				if (global.playerPos[num].a > 270 && global.playerBuffers[num][0].a < 90) {
					// Move the old value to the other side of the 360 degree circle
					global.playerBuffers[num].forEach(buffer => {buffer.a += 360})
				}

				// If the angle used to be large, but is low now
				if (global.playerPos[num].a < 90 && global.playerBuffers[num][0].a > 270) {
					// Move the old value to the other side of the 360 degree circle
					global.playerBuffers[num].forEach(buffer => {buffer.a -= 360})
				}
			}

			// Add it to the start of the buffer
			global.playerBuffers[num].unshift({
				x: global.playerPos[num].x,
				y: global.playerPos[num].y,
				a: global.playerPos[num].a
			})

			// Limit the size of the buffer to the count specified in the config
			global.playerBuffers[num] = global.playerBuffers[num].slice(0, global.config.radar.playerSmoothing)

			// If the map and config support vertical indicator
			if (global.mapData.zRange && global.config.vertIndicator && global.config.vertIndicator.type != "none") {
				// Get the z range from the config
				let zRange = global.mapData.zRange

				// If the player is in a split, get that z range
				if (typeof global.playerPos[num].split == "number") {
					if (global.playerPos[num].split >= 0) {
						zRange = global.mapData.splits[global.playerPos[num].split].zRange
					}
				}

				// Calculate player z-height in the given range on a scale of 0 to 1
				let perc = Math.abs(global.playerPos[num].z - zRange.min) / Math.abs(zRange.max - zRange.min)
				if (global.playerPos[num].z < zRange.min) perc = 0
				perc = Math.min(1, Math.max(0, perc))

				// If the color indicator is enabled
				if (global.config.vertIndicator.type == "color") {
					// Get all colors and make them RGB values
					let bottomColor = global.config.vertIndicator.colorRange[0].join(",")
					let mediumColor = global.config.vertIndicator.colorRange[1].join(",")
					let topColor = global.config.vertIndicator.colorRange[2].join(",")

					// By default we show the bottom color
					let color = bottomColor

					// If we're over half of the range we show the top color
					if (perc > 0.5) {
						color = topColor
						perc = (perc - 0.5)
					}
					else {
						perc = 0.5 - perc
					}

					// Show the chosen color as a background color
					document.getElementById("height" + num).style.background = `rgb(${mediumColor})`
					// Overlay the middle color with a transparency to blend the colors
					document.getElementById("height" + num).style.boxShadow = `inset 0 0 0 1.5vmin rgba(${color},${perc * 2})`
				}

				// If the scale indicator is enabled
				else if (global.config.vertIndicator.type == "scale") {
					// Scale the dot by height multiplied by the configured delta
					scale *= ((perc - 0.5) / 2 + 1) * global.config.vertIndicator.scaleDelta
					global.playerLabels[num].style.transform = `scale(${scale}) translate(-50%, 50%)`
				}

				if (global.config.radar.highestPlayerOnTop) {
					global.playerDots[num].style.zIndex = Math.round(global.playerPos[num].z + 2500)
					global.playerLabels[num].style.zIndex = Math.round(global.playerPos[num].z + 2500)
				}
			}
		}

		// Take the average of the X, Y and rotation buffers
		let bufferPercX = (global.playerBuffers[num].reduce((prev, curr) => prev + curr.x, 0) / (global.playerBuffers[num].length))
		let bufferPercY = (global.playerBuffers[num].reduce((prev, curr) => prev + curr.y, 0) / (global.playerBuffers[num].length))
		let bufferAngle = (global.playerBuffers[num].reduce((prev, curr) => prev + curr.a, 0) / (global.playerBuffers[num].length))

		// Apply the calculated X and Y to the player dot
		global.playerDots[num].style.left = bufferPercX + "%"
		global.playerDots[num].style.bottom = bufferPercY + "%"
		global.playerLabels[num].style.left = bufferPercX + "%"
		global.playerLabels[num].style.bottom = bufferPercY + "%"

		// Apply the transformations to the dots
		if (global.playerPos[num].alive) {
			global.playerDots[num].style.transform = `rotate(${bufferAngle - 45}deg) scale(${scale}) translate(-50%, 50%)`
		}
		else {
			global.playerDots[num].style.transform = `rotate(0deg) scale(${global.config.radar.playerDotScale}) translate(-50%, 50%)`
		}
	}

	// ===== 名字标签防碰撞 =====
	// 收集所有 alive 玩家标签及其玩家点位置
	let visibleLabels = []
	for (let num in global.playerBuffers) {
		if (global.playerPos[num].alive && global.playerPos[num].x != null) {
			visibleLabels.push({
				num: num,
				dotX: parseFloat(global.playerLabels[num].style.left),
				dotY: parseFloat(global.playerLabels[num].style.bottom)
			})
		}
	}

	// 对每个标签尝试 6 个候选位置，选择第一个不冲突的位置
	const placedRects = []
	for (let i = 0; i < visibleLabels.length; i++) {
		const item = visibleLabels[i]
		const label = global.playerLabels[item.num]

		let placed = false
		for (const offset of LABEL_OFFSETS) {
			const testCenterX = item.dotX + offset.dx
			const testCenterY = item.dotY + offset.dy
			// 标签矩形（中心点为 testCenter，宽高 LABEL_W/H）
			const testRect = {
				x: testCenterX - LABEL_W / 2,
				y: testCenterY - LABEL_H / 2,
				w: LABEL_W,
				h: LABEL_H
			}

			let collision = false
			for (const placedRect of placedRects) {
				if (rectIntersect(testRect, placedRect)) {
					collision = true
					break
				}
			}

			if (!collision) {
				label.style.left = testCenterX + "%"
				label.style.bottom = testCenterY + "%"
				placedRects.push(testRect)
				placed = true
				break
			}
		}

		// 如果所有位置都冲突，保持默认位置（玩家点位置）
		if (!placed) {
			label.style.left = item.dotX + "%"
			label.style.bottom = item.dotY + "%"
			placedRects.push({
				x: item.dotX - LABEL_W / 2,
				y: item.dotY - LABEL_H / 2,
				w: LABEL_W,
				h: LABEL_H
			})
		}
	}

	// Go through each active projectile
	for (let id in global.projectilePos) {
		// Add the newest location to the start of the buffer
		global.projectileBuffer[id].unshift(global.projectilePos[id])

		// Limit the size of the buffer to the count specified in the config
		global.projectileBuffer[id] = global.projectileBuffer[id].slice(0, global.config.radar.projectileSmoothing)

		// Calculate the avarage position over the active buffer
		let bufferPercX = (global.projectileBuffer[id].reduce((prev, curr) => prev + curr.x, 0) / (global.projectileBuffer[id].length))
		let bufferPercY = (global.projectileBuffer[id].reduce((prev, curr) => prev + curr.y, 0) / (global.projectileBuffer[id].length))

		// Set the location of the projectile
		global.projectilePos[id].elem.style.left = bufferPercX + "%"
		global.projectilePos[id].elem.style.bottom = bufferPercY + "%"
	}

	// Wait for next repaint
	window.requestAnimationFrame(step)
}

// Request an update before the next repaint
// Maximizes FPS with the least CPU possible
window.requestAnimationFrame(step)

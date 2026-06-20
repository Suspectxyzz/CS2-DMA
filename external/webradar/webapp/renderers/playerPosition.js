// Player position calculations
//
// Sets player dot style and pushed to player location buffer, but does not set
// the location.

socket.element.addEventListener("players", event => {
	let data = event.data

	// Abort if no map has been selected yet
	if (global.currentMap == "none") return;

	// Task 7: 先找出本地玩家的队伍，用于区分敌我
	let localTeam = ""
	for (let player of data.players) {
		if (player.isLocal) {
			localTeam = player.team
			break
		}
	}

	// Loop though each player
	for (let player of data.players) {
		// Get their player element and start building the class
		let playerDot = global.playerDots[player.num]
		let playerLabel = global.playerLabels[player.num]
		let classes = [player.team]

		// CS2 游戏内玩家颜色 (color0-5)，覆盖队伍默认色
		if (typeof player.color === "number" && player.color >= 0 && player.color <= 5) {
			classes.push("color" + player.color)
		}

		// 敌方玩家外部红圈，不覆盖内部填充色
		if (localTeam && player.team && player.team !== localTeam) {
			classes.push("enemy")
		}

		// Mark dead players with a cross
		if (player.health <= 0) {
			classes.push("dead")
		}
		else {
			// Make the bomb carrier orange and and a line around the spectated player
			if (player.bomb) classes.push("bomb")
			if (player.active) classes.push("active")
			if (player.flashed > 0.5) classes.push("flashed")

			// If drawing muzzle flashes is enabled
			if (global.config.radar.shooting) {
				// 开火检测：比较同一武器的弹药差值
				let prevAmmo = global.playerAmmos[player.num] || {}
				let currAmmo = player.ammo

				// 获取当前和上一帧的活跃武器（ammo 对象只有一个键 = 活跃武器）
				let currWeapon = Object.keys(currAmmo)[0]
				let prevWeapon = Object.keys(prevAmmo)[0]

				// 同一武器才检测弹药差值（武器切换时不检测）
				if (currWeapon && currWeapon === prevWeapon) {
					let diff = prevAmmo[currWeapon] - currAmmo[currWeapon]
					// 弹药差值 > 0 且 <= 5 才判定为开火（防数据异常误判）
					if (diff > 0 && diff <= 5) {
						classes.push("shooting")
					}
				}

				// 始终更新弹药记录，确保下一帧能正确比较
				if (currWeapon) {
					global.playerAmmos[player.num] = currAmmo
				}
			}

			// If damage indicators are enabled
			if (global.config.radar.damage) {
				// If we have less health than last packet we are hurtin'
				if (player.health < global.playerHealths[player.num]) {
					classes.push("hurting")
				}

				// Save the health value for next time
				global.playerHealths[player.num] = player.health
			}

			// Save the position so the main loop can interpolate it
			global.playerPos[player.num].x = global.positionToPerc(player.position, "x", player.num)
			global.playerPos[player.num].y = global.positionToPerc(player.position, "y", player.num)
			global.playerPos[player.num].a = player.angle
			global.playerPos[player.num].z = player.position.z
		}

		// Add all classes as a class string
		let newClasses = classes.join(" ")

		// Check if the new classname is different than the one already applied
		// This prevents unnecessary className updates and CSS recalculations
		if (playerDot.className != "dot " + newClasses) playerDot.className = "dot " + newClasses
		if (playerLabel.className != "label " + newClasses) playerLabel.className = "label " + newClasses

		// Set the player alive attribute (used in autozoom)
		global.playerPos[player.num].alive = player.health > 0

		if (global.config.radar.showName == "both") {
			playerLabel.children[0].textContent = player.name.substring(0, global.config.radar.maxNameLength)
		}
		if (global.config.radar.showName == "always") {
			playerLabel.children[0].textContent = player.name.substring(0, global.config.radar.maxNameLength)
		}

		if (global.bomb.state === "planted" && global.bomb.countdown < 100 && global.config.radar.showBlastRadius === 'active' && global.mapData.survivableDistance && player.health > 0) {
			// Calculate distance between player and bomb
			const dx = player.position.x - global.bomb.position.x
			const dy = player.position.y - global.bomb.position.y
			const distance = Math.sqrt(dx * dx + dy * dy);

			const healthIndex = Math.min(global.mapData.survivableDistance.length - 1, Math.floor(player.health / 5))
			const survivableDistance = global.mapData.survivableDistance[healthIndex]

			if (distance < survivableDistance) {
				// Normalize the direction vector
				const norm = Math.sqrt(dx * dx + dy * dy)
				const dirX = dx / norm
				const dirY = dy / norm

				// Closest survivable point from player
				const safeX = global.bomb.position.x + dirX * survivableDistance
				const safeY = global.bomb.position.y + dirY * survivableDistance

				const arrowSize = 14 * global.mapData.resolution
				const arrowOffset = arrowSize * -0.25

				global.playerBlasts[player.num][0].setAttribute("stroke", (survivableDistance - distance > (global.bomb.countdown - 1) * 250) ? "rgb(195, 20, 20, .9)" : "")
				global.playerBlasts[player.num][0].setAttribute("d", `
					M ${global.positionToPerc(player.position, "x")} ${100 - global.positionToPerc(player.position, "y")}
					L ${global.positionToPerc(safeX, "x")} ${100 - global.positionToPerc(safeY, "y")}
					M ${global.positionToPerc(safeX + dirX * arrowOffset + dirY * arrowSize, "x")} ${100 - global.positionToPerc(safeY + dirY * arrowOffset - dirX * arrowSize, "y")}
					A ${arrowSize / 20},${arrowSize / 20} 0 0 0 ${global.positionToPerc(safeX + dirX * arrowOffset - dirY * arrowSize, "x")},${100 - global.positionToPerc(safeY + dirY * arrowOffset + dirX * arrowSize, "y")}
				`)
			}
			else {
				global.playerBlasts[player.num][0].setAttribute("stroke", "")
				global.playerBlasts[player.num][0].setAttribute("d", "")
			}
		}
		else if (global.playerBlasts && global.playerBlasts[player.num]) {
			global.playerBlasts[player.num][0].setAttribute("stroke", "")
			global.playerBlasts[player.num][0].setAttribute("d", "")
		}
	}
})

// On round reset
socket.element.addEventListener("roundend", event => {
	// Go through each player
	for (let num in global.playerBuffers) {
		// Empty the location buffer
		global.playerBuffers[num] = []
		// Reset the player position
		global.playerPos[num] = {
			x: null,
			y: null,
			z: null,
			a: null,
			alive: false
		}

		// Force a re-render on the dot and label, can sometimes bug out in electron
		global.playerDots[num].style.display = "none"
		global.playerDots[num].style.display = "block"
		global.playerLabels[num].style.display = "none"
		global.playerLabels[num].style.display = ""
	}
})

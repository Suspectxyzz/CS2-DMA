// Player position calculations
//
// Sets player dot style and pushed to player location buffer, but does not set
// the location.

let healthDebounce = {}

socket.element.addEventListener("players", event => {
	let data = event.data

	// Abort if no map has been selected yet
	if (global.currentMap == "none") return;

	// Task 7: 先找出本地玩家的队伍，用于区分敌我
	let localTeam = ""
	for (let player of data.players) {
		if (player.isLocal) {
			localTeam = player.team
			global.localPlayerNum = player.num
			break
		}
	}

	// Loop though each player
	for (let player of data.players) {
		// Get their player element and start building the class
		let playerDot = global.playerDots[player.num]
		let playerLabel = global.playerLabels[player.num]
		let classes = [player.team]

		// Inamici: doar clasa enemy (fill roșu din CSS). Aliați: culoare CS2 color0-5
		let isEnemy = localTeam && player.team && player.team !== localTeam
		if (isEnemy) {
			classes.push("enemy")
		} else if (typeof player.color === "number" && player.color >= 0 && player.color <= 5) {
			classes.push("color" + player.color)
		}

		// Mark dead players with a cross (防抖：连续2帧 health<=0 才标记死亡)
		if (player.health <= 0) {
			healthDebounce[player.num] = (healthDebounce[player.num] || 0) + 1
			if (healthDebounce[player.num] >= 2) {
				classes.push("dead")
				// 死亡时清空位置缓冲区并锁定位置，防止 X 标记漂移
				global.playerBuffers[player.num] = []
				global.playerPos[player.num].alive = false
			}
		} else {
			healthDebounce[player.num] = 0
			global.playerPos[player.num].alive = true
			// Make the bomb carrier orange and and a line around the spectated player
			if (player.bomb) classes.push("bomb")
			if (player.active) classes.push("active")
			if (global.config.radar.flashes && player.flashed > 0.5) classes.push("flashed")

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

			// Task 8: 基于 m_velocity 的简单线性外推，减少玩家快速移动时的拖影
			let effectivePos = player.position
			if (player.m_velocity && (player.m_velocity[0] || player.m_velocity[1] || player.m_velocity[2])) {
				let now = performance.now()
				global.playerLastUpdate = global.playerLastUpdate || {}
				let last = global.playerLastUpdate[player.num]
				if (last) {
					let dt = (now - last) / 1000
					if (dt < 0.05) dt = 0.05
					if (dt > 0.2) dt = 0.2
					effectivePos = {
						x: player.position.x + player.m_velocity[0] * dt,
						y: player.position.y + player.m_velocity[1] * dt,
						z: player.position.z + player.m_velocity[2] * dt
					}
				}
				global.playerLastUpdate[player.num] = now
			}

			// Save the position so the main loop can interpolate it
			global.playerPos[player.num].x = global.positionToPerc(effectivePos, "x", player.num)
			global.playerPos[player.num].y = global.positionToPerc(effectivePos, "y", player.num)
			global.playerPos[player.num].a = player.angle
			global.playerPos[player.num].z = effectivePos.z
		}

		// Add all classes as a class string
		let newClasses = classes.join(" ")

		// Check if the new classname is different than the one already applied
		// This prevents unnecessary className updates and CSS recalculations
		if (playerDot.className != "dot " + newClasses) playerDot.className = "dot " + newClasses
		if (playerLabel.className != "label " + newClasses) playerLabel.className = "label " + newClasses

		// Set the player alive attribute (used in autozoom)
		global.playerPos[player.num].alive = player.health > 0

		// 名字 + 武器 + 血量组合显示（非互斥）
	let infoText = ""
	let useHtml = false
	if (global.config.radar.showName == "both" || global.config.radar.showName == "always") {
		infoText = player.name.substring(0, global.config.radar.maxNameLength)
	}
	if (player.health > 0) {
		let weaponMode = global.config.radar.showWeapon
		if (weaponMode === "name") {
			let weaponName = player.weapons.m_active || ""
			let abbrev = weaponName.replace("weapon_", "")
				.replace("_silencer", "")
				.replace("ak47", "AK")
				.replace("m4a1", "M4")
				.replace("awp", "AWP")
				.replace("usp", "USP")
				.replace("glock", "Glock")
				.replace("deagle", "Deagle")
				.replace("mp5sd", "MP5")
				.replace("mp7", "MP7")
				.replace("mp9", "MP9")
				.replace("mac10", "MAC")
				.replace("ump45", "UMP")
				.replace("p90", "P90")
				.replace("bizon", "Bizon")
				.replace("nova", "Nova")
				.replace("xm1014", "XM")
				.replace("sawedoff", "Saw")
				.replace("mag7", "Mag")
				.replace("m249", "M249")
				.replace("negev", "Negev")
				.replace("ssg08", "SSG")
				.replace("sg556", "SG")
				.replace("aug", "AUG")
				.replace("famas", "Famas")
				.replace("galilar", "Galil")
				.replace("scar20", "Scar")
				.replace("g3sg1", "G3")
				.replace("knife", "Knife")
				.replace("bayonet", "Knife")
				.replace("c4", "C4")
				.toUpperCase()
			if (infoText) infoText += " "
			infoText += abbrev
		} else if (weaponMode === "icon") {
			let weaponName = player.weapons.m_active || ""
			if (weaponName) {
				let iconKey = weaponName.replace("weapon_", "")
					.replace("bayonet", "knife")
				let iconHtml = '<img class="weapon-icon" src="/img/icons/' + iconKey + '.svg" alt="' + iconKey + '" onerror="this.style.display=\'none\'">'
				if (infoText) infoText += " "
				infoText += iconHtml
				useHtml = true
			}
		}
		if (global.config.radar.showHealth) {
			if (infoText) infoText += " "
			infoText += player.health + "HP"
		}
	}
	if (useHtml) {
		playerLabel.children[0].innerHTML = infoText
	} else {
		playerLabel.children[0].textContent = infoText
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
	// 清除选中玩家
	global.followedPlayer = null
	for (let j = 0; j < 10; j++) {
		if (global.playerDots[j]) {
			global.playerDots[j].classList.remove("followed")
		}
	}

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

// 为每个玩家 dot 绑定点击事件（选中观察玩家）
for (let i = 0; i < 10; i++) {
	let dot = global.playerDots[i]
	if (!dot) continue
	dot.style.cursor = "pointer"
	dot.addEventListener("click", function(e) {
		e.stopPropagation()
		if (global.followedPlayer === i) {
			// 再次点击同一玩家，取消选中
			global.followedPlayer = null
		} else {
			global.followedPlayer = i
		}
		// 更新 followed 类
		for (let j = 0; j < 10; j++) {
			if (global.playerDots[j]) {
				global.playerDots[j].classList.remove("followed")
			}
		}
		if (global.followedPlayer !== null && global.playerDots[global.followedPlayer]) {
			global.playerDots[global.followedPlayer].classList.add("followed")
		}
	})
}

// 点击雷达空白处取消选中
document.getElementById("container").addEventListener("click", function() {
	if (global.followedPlayer !== null) {
		global.followedPlayer = null
		for (let j = 0; j < 10; j++) {
			if (global.playerDots[j]) {
				global.playerDots[j].classList.remove("followed")
			}
		}
	}
})

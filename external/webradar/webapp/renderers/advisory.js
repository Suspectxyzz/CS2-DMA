// Advisories
//
// Determines if an advisory can be shown and prioritizes the most important one.

let idToNum = {}
let advisories = {
	planting: -1,
	defuse: -1,
	solesurvivor: -1
}

function updateAdvisory() {
	for (let name in advisories) {
		if (advisories[name] != -1) {
			let advisoryEl = document.getElementById("advisory")
			advisoryEl.className = name
			advisoryEl.children[0].innerHTML = idToNum[advisories[name]] + 1
			if (advisoryEl.children[0].innerHTML == 10) {
				advisoryEl.children[0].innerHTML = 0
			}
			// 状态标签（i18n 翻译）
			let labelKey = {
				planting: "advisory_planting",
				defuse: "advisory_defuse",
				solesurvivor: "advisory_solesurvivor"
			}[name]
			if (advisoryEl.children[1]) {
				advisoryEl.children[1].textContent = labelKey ? t(labelKey) : ""
			}
			return
		}
	}

	let advisory = document.getElementById("advisory")
	advisory.className = ""
	if (advisory.children[0]) advisory.children[0].innerHTML = ""
	if (advisory.children[1]) advisory.children[1].textContent = ""
}

// 语言切换时刷新当前建议标签
document.addEventListener("langchange", updateAdvisory)

socket.element.addEventListener("players", event => {
	let data = event.data
	let ctsAlive = []
	let tsAlive = []

	for (let player of data.players) {
		if (player.health > 0) {
			if (player.team == "CT") {
				ctsAlive.push(player.id)
			}
			else {
				tsAlive.push(player.id)
			}
		}

		if (idToNum[player.id] != player.num) {
			idToNum[player.id] = player.num
		}
	}

	if (ctsAlive.length == 1) {
		advisories.solesurvivor = ctsAlive[0]
	}
	else if (tsAlive.length == 1) {
		advisories.solesurvivor = tsAlive[0]
	}
	else {
		advisories.solesurvivor = -1
	}

	updateAdvisory()
})

socket.element.addEventListener("bomb", event => {
	let data = event.data

	if (idToNum[data.player]) {
		if (data.state == "planting") {
			advisories.planting = data.player
		}
		else {
			advisories.planting = -1
		}

		if (data.state == "defusing") {
			advisories.defuse = data.player
		}
		else {
			advisories.defuse = -1
		}
	}
	else {
		advisories.planting = -1
		advisories.defuse = -1
	}

	updateAdvisory()
})

// Team panel UI
//
// Shows player cards on left (T) and right (CT) sides of the radar.
// Reference: KevqDMA viewer.js createPlayerCard / updatePlayerCard / renderRoster.
//
// Card layout:
//   status dot + model image + content(name+money / hp+armor / weapon+ammo / utilities)
//
// Cards are reused by player.id with signature-based diff to skip unchanged panels.

let teamPanelT = document.getElementById("team-panel-t")
let teamPanelCT = document.getElementById("team-panel-ct")

// Card cache: player.id -> card handle
let cardCache = {}
// Signature cache per team to skip unchanged panels
let rosterSignatures = {}

// ---- KevqDMA 资源引用 ----
// 已知武器/道具图标名称（对应 img/icons/*.svg）
const KNOWN_ICONS = new Set([
	"ak47", "aug", "awp", "bizon", "c4", "cz75a", "deagle", "decoy", "defuser",
	"elite", "famas", "fiveseven", "flashbang", "g3sg1", "galilar", "gauge",
	"glock", "health", "hegrenade", "incgrenade", "kevlar", "kevlar_helmet",
	"knife", "m249", "m4a1", "m4a1_silencer", "mac10", "mag7", "molotov",
	"mp5sd", "mp7", "mp9", "negev", "nova", "p2000", "p250", "p90", "revolver",
	"sawedoff", "scar20", "sg556", "smokegrenade", "ssg08", "taser", "tec9",
	"ump45", "usp_silencer", "xm1014"
])

// 已知玩家模型名称（对应 img/characters/*.png）
const KNOWN_MODELS = new Set(["ctm_sas", "tm_phoenix"])

// 解析玩家模型名称：优先用后端提供的 m_model_name，否则按队伍回退
function resolveModelName(player) {
	let candidate = String(player.modelName || "").trim().toLowerCase()
	if (KNOWN_MODELS.has(candidate)) return candidate
	return player.team === "CT" ? "ctm_sas" : "tm_phoenix"
}

// 解析武器图标名称：转为小写后检查是否在已知集合中
function resolveIconName(name, fallback) {
	let candidate = String(name || "").trim().toLowerCase()
	if (KNOWN_ICONS.has(candidate)) return candidate
	return fallback
}

// 使用 mask 显示 SVG 图标（参考 KevqDMA setMaskedIcon）
function setMaskedIcon(element, iconName, color) {
	let path = "./img/icons/" + iconName + ".svg"
	element.style.backgroundColor = color || "var(--radar-secondary)"
	element.style.webkitMask = "url('" + path + "') no-repeat center / contain"
	element.style.mask = "url('" + path + "') no-repeat center / contain"
}

// Weapon display name mapping (backend sends names without weapon_ prefix)
const WEAPON_DISPLAY = {
	ak47: "AK-47", m4a1: "M4A1", m4a1_silencer: "M4A1-S",
	awp: "AWP", ssg08: "SSG 08", aug: "AUG", sg556: "SG 553",
	galilar: "Galil", famas: "FAMAS", scar20: "SCAR-20",
	g3sg1: "G3SG1", nova: "Nova", xm1014: "XM1014",
	sawedoff: "Sawed-Off", mag7: "MAG-7", bizon: "PP-Bizon",
	p90: "P90", ump45: "UMP-45", mac10: "MAC-10", mp9: "MP9",
	mp7: "MP7", mp5sd: "MP5-SD", m249: "M249", negev: "Negev",
	deagle: "Desert Eagle", usp_silencer: "USP-S", glock: "Glock-18",
	p250: "P250", fiveseven: "Five-SeveN", cz75a: "CZ75-Auto",
	revolver: "Revolver", dualberettas: "Dual Berettas", tec9: "Tec-9",
	p2000: "P2000", knife: "Knife",
	smokegrenade: "Smoke", flashbang: "Flash", hegrenade: "HE",
	molotov: "Molotov", incgrenade: "Incendiary", decoy: "Decoy",
	c4: "C4"
}

function formatWeaponName(rawName) {
	if (!rawName) return ""
	if (WEAPON_DISPLAY[rawName]) return WEAPON_DISPLAY[rawName]
	return rawName
}

function truncate(name, maxLen) {
	if (!name) return ""
	if (name.length <= maxLen) return name
	return name.substring(0, maxLen)
}

function createCard(isRightSide) {
	let card = document.createElement("article")
	card.className = "player-card" + (isRightSide ? " is-right" : "")
	card.innerHTML = [
		'<span class="player-card__status"></span>',
		'<img class="player-card__model" alt="" />',
		'<div class="player-card__content">',
		'  <div class="player-card__top">',
		'    <span class="player-card__name"></span>',
		'    <span class="player-card__money"></span>',
		'  </div>',
		'  <div class="player-card__stats">',
		'    <span class="player-card__stat"><span class="player-card__stat-icon player-card__health-icon"></span><span class="player-card__stat-value player-card__health"></span></span>',
		'    <span class="player-card__stat"><span class="player-card__stat-icon player-card__armor-icon"></span><span class="player-card__stat-value player-card__armor"></span></span>',
		'  </div>',
		'  <div class="player-card__weapon">',
		'    <span class="player-card__weapon-icon"></span>',
		'    <span class="player-card__weapon-name"></span>',
		'  </div>',
		'  <div class="player-card__util"></div>',
		'</div>'
	].join("")

	return {
		root: card,
		status: card.querySelector(".player-card__status"),
		model: card.querySelector(".player-card__model"),
		name: card.querySelector(".player-card__name"),
		money: card.querySelector(".player-card__money"),
		healthIcon: card.querySelector(".player-card__health-icon"),
		armorIcon: card.querySelector(".player-card__armor-icon"),
		healthValue: card.querySelector(".player-card__health"),
		armorValue: card.querySelector(".player-card__armor"),
		weaponIcon: card.querySelector(".player-card__weapon-icon"),
		weaponName: card.querySelector(".player-card__weapon-name"),
		util: card.querySelector(".player-card__util"),
		modelKey: "",
		weaponKey: "",
		utilKey: ""
	}
}

function updateCard(card, player, isRightSide) {
	let maxLen = (global.config && global.config.radar && global.config.radar.maxNameLength) || 12

	// Accent color by team
	let accent = (player.team === "CT") ? "#5ab8f4" : (player.team === "T") ? "#f0c941" : "#888"
	card.root.style.setProperty("--card-accent", accent)
	card.root.classList.toggle("is-dead", player.health <= 0)
	card.root.classList.toggle("is-right", !!isRightSide)

	// 玩家模型图片（CT 用 ctm_sas，T 用 tm_phoenix）
	let modelName = resolveModelName(player)
	if (card.modelKey !== modelName) {
		card.modelKey = modelName
		card.model.src = "./img/characters/" + modelName + ".png"
		card.model.alt = modelName
	}

	// Name + money
	card.name.textContent = truncate(player.name, maxLen)
	card.name.title = player.name || ""
	card.money.textContent = "$" + Math.max(0, player.money || 0)

	// Health + armor with mask icons
	card.healthValue.textContent = String(Math.max(0, player.health || 0))
	card.armorValue.textContent = String(Math.max(0, player.armor || 0))
	setMaskedIcon(card.healthIcon, "health", "#e74c3c")
	setMaskedIcon(card.armorIcon, player.hasHelmet ? "kevlar_helmet" : "kevlar", "#5c9eff")
	// Hide armor stat if no armor
	card.armorValue.parentElement.style.display = (player.armor > 0) ? "" : "none"

	// Weapon: mask icon + name + ammo
	let weapons = player.weapons || {}
	let activeWeapon = weapons.m_active || ""
	let weaponIconName = resolveIconName(activeWeapon, "knife")
	if (card.weaponKey !== weaponIconName) {
		card.weaponKey = weaponIconName
		setMaskedIcon(card.weaponIcon, weaponIconName, "#b1d0e7")
	}
	let weaponName = formatWeaponName(activeWeapon)
	// Ammo: show clip count for weapons that use ammo
	let ammoText = ""
	if (activeWeapon && player.ammo && player.ammo[activeWeapon] !== undefined) {
		let clip = player.ammo[activeWeapon]
		if (clip >= 0 && !activeWeapon.includes("knife") && !activeWeapon.includes("c4")) {
			ammoText = " (" + clip + ")"
		}
	}
	card.weaponName.textContent = weaponName + ammoText
	card.weaponName.parentElement.style.display = weaponName ? "" : "none"

	// Utilities: defuser + 4 grenade slots + C4
	let grenades = Array.isArray(weapons.m_utilities) ? weapons.m_utilities : []
	let utilKey = [
		Number(!!player.hasDefuser),
		Number(!!player.bomb),
		Number(player.health <= 0),
		grenades.join(",")
	].join(":")
	if (card.utilKey !== utilKey) {
		card.utilKey = utilKey
		let fragment = document.createDocumentFragment()

		// Defuser
		if (player.hasDefuser) {
			let span = document.createElement("span")
			span.className = "player-card__util-icon"
			setMaskedIcon(span, "defuser", "#50904c")
			fragment.appendChild(span)
		}

		// 4 grenade slots
		for (let i = 0; i < 4; i++) {
			if (i < grenades.length) {
				let span = document.createElement("span")
				span.className = "player-card__util-icon"
				let grenadeIconName = resolveIconName(grenades[i], "hegrenade")
				setMaskedIcon(span, grenadeIconName, "#c8ddef")
				span.title = formatWeaponName(grenades[i])
				fragment.appendChild(span)
			} else {
				let dot = document.createElement("span")
				dot.className = "player-card__util-dot"
				fragment.appendChild(dot)
			}
		}

		// C4
		if (player.bomb && player.health > 0) {
			let span = document.createElement("span")
			span.className = "player-card__util-icon"
			setMaskedIcon(span, "c4", accent)
			fragment.appendChild(span)
		}

		card.util.replaceChildren(fragment)
	}
}

function buildRosterSignature(players) {
	return players.map(function(p) {
		return [
			p.id, Number(p.health <= 0), Number(!!p.active), p.name, p.health, p.armor,
			p.money, p.weapons ? (p.weapons.m_active || "") : "",
			p.ammo ? JSON.stringify(p.ammo) : "",
			p.weapons && p.weapons.m_utilities ? p.weapons.m_utilities.join(",") : "",
			Number(!!p.hasDefuser), Number(!!p.bomb), p.team
		].join(":")
	}).join("|")
}

function renderTeam(panel, teamKey, players, emptyText) {
	// Signature check: skip if unchanged
	let sig = buildRosterSignature(players)
	if (rosterSignatures[teamKey] === sig) return
	rosterSignatures[teamKey] = sig

	// Ensure header exists
	let header = panel.querySelector(".team-panel__header")
	let list = panel.querySelector(".team-panel__list")
	if (!header) {
		header = document.createElement("div")
		header.className = "team-panel__header"
		let nameSpan = document.createElement("span")
		nameSpan.className = "team-panel__name"
		let countSpan = document.createElement("span")
		countSpan.className = "team-panel__count"
		header.appendChild(nameSpan)
		header.appendChild(countSpan)
		panel.appendChild(header)

		list = document.createElement("div")
		list.className = "team-panel__list"
		panel.appendChild(list)
	}

	// Update header (i18n 翻译)
	let teamName = (teamKey === "t") ? t("team_t") : t("team_ct")
	let aliveCount = players.filter(function(p) { return p.health > 0 }).length
	header.querySelector(".team-panel__name").textContent = teamName
	header.querySelector(".team-panel__count").textContent = aliveCount + "/" + players.length

	// Empty state
	if (!players.length) {
		let empty = document.createElement("div")
		empty.className = "team-panel__empty"
		empty.textContent = (teamKey === "t") ? t("team_t_empty") : t("team_ct_empty")
		list.replaceChildren(empty)
		return
	}

	// Sort: alive first, then local player, then by name
	let sorted = players.slice().sort(function(a, b) {
		let aDead = a.health <= 0 ? 1 : 0
		let bDead = b.health <= 0 ? 1 : 0
		if (aDead !== bDead) return aDead - bDead
		let aLocal = a.active ? 1 : 0
		let bLocal = b.active ? 1 : 0
		if (aLocal !== bLocal) return bLocal - aLocal
		return String(a.name || "").localeCompare(String(b.name || ""))
	})

	let isRightSide = (teamKey === "ct")
	let used = {}
	let fragment = document.createDocumentFragment()

	for (let i = 0; i < sorted.length; i++) {
		let player = sorted[i]
		let key = player.id
		used[key] = true
		let card = cardCache[key]
		if (!card) {
			card = createCard(isRightSide)
			cardCache[key] = card
		}
		updateCard(card, player, isRightSide)
		fragment.appendChild(card.root)
	}

	// Remove unused cards
	for (let key in cardCache) {
		if (!used[key]) {
			delete cardCache[key]
		}
	}

	list.replaceChildren(fragment)
}

socket.element.addEventListener("players", event => {
	let data = event.data
	let tPlayers = []
	let ctPlayers = []

	for (let i = 0; i < data.players.length; i++) {
		let player = data.players[i]
		if (player.team === "T") tPlayers.push(player)
		else if (player.team === "CT") ctPlayers.push(player)
	}

	renderTeam(teamPanelT, "t", tPlayers, "No terrorists")
	renderTeam(teamPanelCT, "ct", ctPlayers, "No counter-terrorists")
})

// Handle showTeamPanel config toggle
function updatePanelVisibility() {
	let show = true
	if (global.config && global.config.radar && global.config.radar.showTeamPanel !== undefined) {
		show = global.config.radar.showTeamPanel
	}
	teamPanelT.style.display = show ? "" : "none"
	teamPanelCT.style.display = show ? "" : "none"
}

document.addEventListener("configchange", updatePanelVisibility)

// 语言切换时清空签名缓存，强制下一帧重新渲染队伍名
document.addEventListener("langchange", function() {
	rosterSignatures = {}
})

// Initial visibility once config is ready
if (document.readyState === "loading") {
	document.addEventListener("DOMContentLoaded", updatePanelVisibility)
} else {
	updatePanelVisibility()
}

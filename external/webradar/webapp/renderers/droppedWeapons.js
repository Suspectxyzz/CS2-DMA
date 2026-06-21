// Dropped weapons rendering
//
// Shows weapons dropped on the ground on the map.

// itemId -> visualKey mapping (mirrors WeaponLookup.h kWeaponLookup)
const DROPPED_WEAPON_VISUAL_KEY = {
	1: "deagle", 2: "elite", 3: "fiveseven", 4: "glock",
	7: "ak47", 8: "aug", 9: "awp", 10: "famas", 11: "g3sg1",
	13: "galilar", 14: "m249", 16: "m4a1", 17: "mac10", 19: "p90",
	23: "mp5sd", 24: "ump45", 25: "xm1014", 26: "bizon", 27: "mag7",
	28: "negev", 29: "sawedoff", 30: "tec9", 31: "taser", 32: "p2000",
	33: "mp7", 34: "mp9", 35: "nova", 36: "p250", 38: "scar20",
	39: "sg556", 40: "ssg08", 57: "health", 60: "m4a1_silencer",
	61: "usp_silencer", 63: "cz75a", 64: "revolver"
};

socket.element.addEventListener("droppedWeapons", event => {
	let weapons = event.data
	let activeIds = []

	for (let weapon of weapons) {
		let weaponID = "droppedWeapon" + weapon.id
		let visualKey = DROPPED_WEAPON_VISUAL_KEY[weapon.itemId]
		if (!visualKey) continue

		let posX = global.positionToPerc(weapon.position, "x")
		let posY = global.positionToPerc(weapon.position, "y")

		let weaponElement = document.getElementById(weaponID)

		if (!weaponElement) {
			weaponElement = document.createElement("div")
			weaponElement.id = weaponID
			weaponElement.className = "droppedWeapon"
			weaponElement.style.backgroundImage = `url('/img/icons/${visualKey}.svg')`
			document.getElementById("droppedWeapons").appendChild(weaponElement)
		}

		weaponElement.style.left = posX + "%"
		weaponElement.style.bottom = posY + "%"
		weaponElement.style.opacity = 1

		activeIds.push(weaponID)
	}

	// Remove weapons no longer in the game
	for (let elem of document.getElementById("droppedWeapons").children) {
		if (!activeIds.includes(elem.id)) {
			elem.remove()
		}
	}
})

// Clear all dropped weapons on round reset
socket.element.addEventListener("roundend", event => {
	document.getElementById("droppedWeapons").innerHTML = ""
})

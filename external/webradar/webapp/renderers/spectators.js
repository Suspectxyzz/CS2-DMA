// Spectator panel UI
//
// Shows list of spectators (observers) in a fixed panel at top-right.
// Listens to "spectators" event from _socket.js.
// Panel is hidden when the spectator list is empty.

let spectatorPanel = document.getElementById("spectator-panel")
let spectatorList = spectatorPanel ? spectatorPanel.querySelector(".spectator-list") : null

function renderSpectators(spectators) {
	if (!spectatorPanel || !spectatorList) return

	// Empty → hide panel and clear list
	if (!spectators || !spectators.length) {
		spectatorPanel.style.display = "none"
		spectatorList.replaceChildren()
		return
	}

	// Build list items with team-colored left border (T / CT)
	let fragment = document.createDocumentFragment()
	for (let i = 0; i < spectators.length; i++) {
		let s = spectators[i]
		let li = document.createElement("li")
		// CSS uses .spectator-list li.T / li.CT for border color
		li.className = s.team // "T" | "CT" | "U"
		li.textContent = s.name
		li.title = s.name
		fragment.appendChild(li)
	}
	spectatorList.replaceChildren(fragment)

	// Show panel
	spectatorPanel.style.display = ""
}

socket.element.addEventListener("spectators", event => {
	renderSpectators(event.data)
})

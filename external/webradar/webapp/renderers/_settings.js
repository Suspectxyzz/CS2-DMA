// 设置面板渲染器
// 负责控件事件绑定、状态回填、重置功能
//
// 依赖：_init.js 提供的 window.applySetting / window.resetConfig /
// window.getConfigValue，以及 _global.js 提供的 global.config。
// _init.js 中 loadConfig/applyConfig 均为同步执行，因此只要本脚本
// 在 _init.js 之后加载，初始化时 global.config 已就绪。

function initSettings() {
	const toggle = document.getElementById("settings-toggle")
	const panel = document.getElementById("settings-panel")
	const resetBtn = document.getElementById("settings-reset")

	const controls = panel.querySelectorAll("[data-config]")

	// 从 global.config 回填控件状态
	function refreshControls() {
		controls.forEach(function(ctrl) {
			const key = ctrl.getAttribute("data-config")
			const value = getConfigValue(key)
			if (value === undefined) return

			if (ctrl.type === "checkbox") {
				ctrl.checked = value
			}
			else {
				ctrl.value = value
				if (ctrl.type === "range") {
					const valueDisplay = panel.querySelector(`[data-value-for="${key}"]`)
					if (valueDisplay) {
						valueDisplay.textContent = value
					}
				}
			}
		})
	}

	// 齿轮按钮切换 popover 显隐
	toggle.addEventListener("click", function() {
		if (panel.hasAttribute("hidden")) {
			panel.removeAttribute("hidden")
			refreshControls()
		}
		else {
			panel.setAttribute("hidden", "")
		}
	})

	// 重置按钮
	resetBtn.addEventListener("click", function() {
		if (confirm("确定重置所有设置为默认值？")) {
			resetConfig()
			refreshControls()
		}
	})

	// 绑定所有控件事件，调用 applySetting 实时生效
	controls.forEach(function(ctrl) {
		const key = ctrl.getAttribute("data-config")
		const eventName = ctrl.type === "checkbox" ? "change" : "input"
		ctrl.addEventListener(eventName, function() {
			let value
			if (ctrl.type === "checkbox") {
				value = ctrl.checked
			}
			else if (ctrl.type === "range") {
				value = parseFloat(ctrl.value)
				const valueDisplay = panel.querySelector(`[data-value-for="${key}"]`)
				if (valueDisplay) {
					valueDisplay.textContent = ctrl.value
				}
			}
			else {
				value = ctrl.value
			}
			applySetting(key, value)
		})
	})

	// 监听 configchange 事件：全量重置时同步所有控件
	document.addEventListener("configchange", function(e) {
		if (e.detail.key === null) {
			refreshControls()
		}
	})

	// 初始回填（global.config 已由 _init.js 同步设置）
	refreshControls()
}

// 与 _init.js 保持一致的初始化时机
if (document.readyState === "loading") {
	document.addEventListener("DOMContentLoaded", initSettings)
}
else {
	initSettings()
}

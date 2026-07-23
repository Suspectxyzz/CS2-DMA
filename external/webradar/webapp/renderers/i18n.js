// Internationalization (i18n) module
//
// Provides bilingual (中文 / English) text switching for the WebRadar UI.
// Static text uses data-i18n attributes; dynamic text calls t() directly.
// Language choice is persisted in localStorage ("webradar_lang").

const I18N = {
	// ===== 中文（默认）=====
	zh: {
		// 设置面板 - 头部
		settings_title: "雷达设置",
		settings_reset: "重置",
		settings_reset_title: "重置为默认",
		settings_toggle_title: "设置",
		settings_language: "语言",
		settings_confirm_reset: "确定重置所有设置为默认值？",

		// 设置分组标题
		group_display: "显示",
		group_name: "名字",
		group_indicator: "指示器",
		group_advisory: "建议框",
		group_vertIndicator: "垂直指示器",
		group_autozoom: "自动缩放",
		group_smoothing: "平滑",

		// 显示组
		showTeamPanel: "显示队伍面板",
		showTeammatePanel: "显示队友面板",
		showEnemyPanel: "显示敌人面板",
		enemyPanelSide: "敌人面板位置",
		rotate90_title: "旋转90度",
		followRotation: "地图跟随旋转",
		playerDotScale: "玩家点大小",
		bombDotScale: "炸弹点大小",
		tombstoneOpacity: "死亡墓碑透明度",

		// 名字组
		showName: "显示模式",
		maxNameLength: "最大长度",

		// 指示器组
		shooting: "开火指示",
		damage: "受伤指示",
		flashes: "被闪指示",
		projectiles: "投掷物显示",
		smokeColors: "烟雾队伍色",
		plainProjectiles: "简洁投掷物图标",
		showWeapon: "显示武器",
		showHealth: "显示血量",

		// 建议框组
		hideAdvisories: "隐藏建议框",
		showBlastRadius: "爆炸半径箭头",
		showBuyzones: "购买区",

		// 垂直指示器组
		vertType: "类型",
		scaleDelta: "缩放强度",

		// 自动缩放组
		autozoomEnable: "启用",
		autozoomPadding: "边缘留白",
		autozoomMinZoom: "最小缩放",

		// 平滑组
		playerSmoothing: "玩家平滑",
		projectileSmoothing: "投掷物平滑",

		// 选项通用
		opt_never: "从不",
		opt_both: "双方",
		opt_always: "始终",
		opt_active: "激活时",
		opt_buytime: "购买时间",
		opt_off: "关闭",
		opt_name: "名称",
		opt_icon: "图标",
		opt_none: "无",
		opt_color: "颜色",
		opt_scale: "缩放",
		opt_left: "左侧",
		opt_right: "右侧",

		// 未知地图
		unsupported_map: "不支持该地图",

		// 建议标签（advisory.js）
		advisory_planting: "安放",
		advisory_defuse: "拆弹",
		advisory_solesurvivor: "独苗",

		// 队伍面板（teamPanel.js）
		team_t: "恐怖分子",
		team_ct: "反恐精英",
		team_t_empty: "无恐怖分子",
		team_ct_empty: "无反恐精英",

		// 页面标题
		page_title: "CS2 WebRadar"
	},

	// ===== English =====
	en: {
		settings_title: "Radar Settings",
		settings_reset: "Reset",
		settings_reset_title: "Reset to default",
		settings_toggle_title: "Settings",
		settings_language: "Language",
		settings_confirm_reset: "Reset all settings to default?",

		group_display: "Display",
		group_name: "Name",
		group_indicator: "Indicators",
		group_advisory: "Advisories",
		group_vertIndicator: "Vertical Indicator",
		group_autozoom: "Auto Zoom",
		group_smoothing: "Smoothing",

		showTeamPanel: "Team Panel",
		showTeammatePanel: "Show teammate panel",
		showEnemyPanel: "Show enemy panel",
		enemyPanelSide: "Enemy panel position",
		rotate90_title: "Rotate 90°",
		followRotation: "Follow Rotation",
		playerDotScale: "Player Dot Size",
		bombDotScale: "Bomb Dot Size",
		tombstoneOpacity: "Tombstone Opacity",

		showName: "Show Name",
		maxNameLength: "Max Length",

		shooting: "Shooting",
		damage: "Damage",
		flashes: "Flashed",
		projectiles: "Projectiles",
		smokeColors: "Smoke Team Colors",
		plainProjectiles: "Plain Projectile Icons",
		showWeapon: "Show Weapon",
		showHealth: "Show Health",

		hideAdvisories: "Hide Advisories",
		showBlastRadius: "Blast Radius Arrow",
		showBuyzones: "Buy Zones",

		vertType: "Type",
		scaleDelta: "Scale Strength",

		autozoomEnable: "Enable",
		autozoomPadding: "Padding",
		autozoomMinZoom: "Min Zoom",

		playerSmoothing: "Player Smoothing",
		projectileSmoothing: "Projectile Smoothing",

		opt_never: "Never",
		opt_both: "Both",
		opt_always: "Always",
		opt_active: "When Active",
		opt_buytime: "Buy Time",
		opt_off: "Off",
		opt_name: "Name",
		opt_icon: "Icon",
		opt_none: "None",
		opt_color: "Color",
		opt_scale: "Scale",
		opt_left: "Left",
		opt_right: "Right",

		unsupported_map: "Unsupported map",

		advisory_planting: "Planting",
		advisory_defuse: "Defusing",
		advisory_solesurvivor: "Last Man",

		team_t: "Terrorists",
		team_ct: "Counter-Terrorists",
		team_t_empty: "No terrorists",
		team_ct_empty: "No counter-terrorists",

		page_title: "CS2 WebRadar"
	}
}

// Current language code: "zh" or "en"
let currentLang = "zh"

/**
 * Load saved language from localStorage, falling back to "zh".
 */
function loadLang() {
	try {
		let saved = localStorage.getItem("webradar_lang")
		if (saved === "zh" || saved === "en") return saved
	} catch (err) { /* ignore */ }
	return "zh"
}

/**
 * Get translated text for a key.
 * @param  {String} key Dictionary key
 * @return {String}     Translated string (falls back to key if missing)
 */
function t(key) {
	let dict = I18N[currentLang]
	if (dict && dict[key] !== undefined) return dict[key]
	return key
}

/**
 * Apply translations to all elements with [data-i18n] attribute.
 * For <option> elements inside <select>, updates textContent.
 * For elements with [data-i18n-title], updates the title attribute.
 */
function applyI18n() {
	document.querySelectorAll("[data-i18n]").forEach(function(el) {
		el.textContent = t(el.getAttribute("data-i18n"))
	})
	document.querySelectorAll("[data-i18n-title]").forEach(function(el) {
		el.title = t(el.getAttribute("data-i18n-title"))
	})

	// Update document title prefix (map name may follow after " - ")
	if (!global.currentMap || global.currentMap === "none") {
		document.title = t("page_title")
	}

	// Notify dynamic-text renderers to re-render with new language
	document.dispatchEvent(new CustomEvent("langchange", { detail: { lang: currentLang } }))
}

/**
 * Switch language, persist choice, and re-apply translations.
 * @param {String} lang "zh" or "en"
 */
function setLang(lang) {
	if (lang !== "zh" && lang !== "en") return
	if (lang === currentLang) return
	currentLang = lang
	try {
		localStorage.setItem("webradar_lang", lang)
	} catch (err) { /* ignore */ }
	applyI18n()
}

// Initialize language on script load
currentLang = loadLang()

// Apply translations once DOM is ready
if (document.readyState === "loading") {
	document.addEventListener("DOMContentLoaded", applyI18n)
}
else {
	applyI18n()
}

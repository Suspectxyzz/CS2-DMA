#pragma once
#include <string>

class Language {
public:
	std::string tab_visuals;
	std::string tab_radar;
	std::string tab_settings;
	std::string tab_config;
	std::string tab_grenade;
	std::string tab_hotkeys;

	std::string visuals_showbox;
	std::string visuals_boxcolor;
	std::string visuals_boxtype;
	std::string visuals_showbone;
	std::string visuals_bonecolor;
	std::string visuals_showeyeray;
	std::string visuals_eyeraycolor;
	std::string visuals_showbar;
	std::string visuals_barpos;
	std::string visuals_weaponesp;
	std::string visuals_distance;
	std::string visuals_name;
	std::string visuals_line;
	std::string visuals_linecolor;

	std::string utilities_teamcheck;
	std::string utilities_closehack;
	std::string utilities_reloadhack;
	std::string utilities_language;
	std::string utilities_help;

	std::string config_newconfig;
	std::string config_create;
	std::string config_load;
	std::string config_save;
	std::string config_delete;

	const char* visuals_boxtypeselect[3];
	const char* visuals_heathbarselect[3];

	const char* utilities_langselect[2] = { "English", u8"\u4e2d\u6587" };

	std::string days;

	std::string frames;

	// Grenade Helper
	std::string grenade_enable;
	std::string grenade_showname;
	std::string grenade_showbox;
	std::string grenade_showline;
	std::string grenade_autoaim;
	std::string grenade_maxdistance;
	std::string grenade_triggerdistance;
	std::string grenade_boxsize;
	std::string grenade_currentmap;
	std::string grenade_availablethrows;
	std::string grenade_nomapdata;
	std::string grenade_flash;
	std::string grenade_smoke;
	std::string grenade_he;
	std::string grenade_molotov;

	// Grenade Recording
	std::string grenade_record_hotkey;
	std::string grenade_pending_throws;
	std::string grenade_name_throw;
	std::string grenade_throw_name;
	std::string grenade_throw_style;
	std::string grenade_throw_type;
	std::string grenade_save_throws;
	std::string grenade_delete;
	std::string grenade_clear_all;
	std::string grenade_no_pending;
	std::string grenade_position;
	std::string grenade_angle;
	std::string grenade_recorded_at;

	// Section Headers
	std::string header_playerbox;
	std::string header_skeleton;
	std::string header_health;
	std::string header_info;
	std::string header_snapline;
	std::string header_general;
	std::string header_system;
	std::string header_display;
	std::string header_recording;
	std::string header_pending;
	std::string header_savededitor;
	std::string header_bomb;
	std::string header_weapon;
	std::string header_teamfilter;
	std::string header_world_proj;
	std::string header_render_quality;
	std::string visuals_bombesp;
	std::string visuals_bombplanted;
	std::string visuals_bombdefusing;
	std::string visuals_bombcarrier;
	std::string visuals_bombdropped;

	// Visuals Extra
	std::string visuals_thickness;
	std::string visuals_rounding;
	std::string visuals_cornersize;
	std::string visuals_filled;
	std::string visuals_fillalpha;
	std::string visuals_headdot;
	std::string visuals_dotsize;
	std::string visuals_length;
	std::string visuals_barwidth;
	std::string visuals_armorbar;
	std::string visuals_size;
	std::string visuals_origin;
	std::string visuals_width;

	const char* armorbar_typeselect[2];
	const char* snapline_originselect[3];

	// Safe Zone
	std::string header_safezone;
	std::string safezone_enable;
	std::string safezone_radius;
	std::string safezone_shape;
	const char* safezone_shapeselect[2];
	std::string safezone_mode;
	const char* safezone_modeselect[2];
	std::string safezone_skip_box;
	std::string safezone_skip_bone;
	std::string safezone_skip_healthbar;
	std::string safezone_skip_armorbar;
	std::string safezone_skip_weapon;
	std::string safezone_skip_name;
	std::string safezone_skip_snapline;
	std::string safezone_skip_eyeray;
	std::string safezone_skip_headdot;
	std::string safezone_skip_distance;

	// Spectator List & Perf Monitor
	std::string visuals_spectatorlist;
	std::string settings_perfmonitor;

	// Menu Hotkey
	std::string settings_menuhotkey;

	// Debug
	std::string settings_debuglog;
	std::string settings_debuglog_tip;

	// Settings
	std::string settings_vsync;
	std::string settings_maxfps;
	std::string settings_unlimited;
	std::string settings_unlimitedtip;
	std::string settings_restarttip;

	// Display / Resolution
	std::string settings_resolution;
	std::string settings_renderautotip;
	std::string settings_monitor;
	std::string settings_monitortip;

	// Grenade Extra
	std::string grenade_pressanykey;
	std::string grenade_hotkeytip;
	std::string grenade_autosave;
	std::string grenade_autosavetip;
	std::string grenade_defaulttype;
	std::string grenade_defaultstyle;
	std::string grenade_reloadfiles;
	std::string grenade_reloadtip;
	std::string grenade_selectmap;
	std::string grenade_totalthrows;
	std::string grenade_editthrow;
	std::string grenade_update;
	std::string grenade_nomaps;
	std::string grenade_name_label;

	const char* grenade_typeselect[4];
	const char* grenade_styleselect[5];

	// Grenade Type/Style Display Names
	std::string grenade_typename_flash;
	std::string grenade_typename_smoke;
	std::string grenade_typename_he;
	std::string grenade_typename_fire;
	std::string grenade_typename_unknown;
	std::string grenade_stylename_stand;
	std::string grenade_stylename_run;
	std::string grenade_stylename_jump;
	std::string grenade_stylename_crouch;
	std::string grenade_stylename_runjump;
	std::string grenade_stylename_unknown;

	// Direction Labels
	std::string dir_forward;
	std::string dir_back;
	std::string dir_right;
	std::string dir_left;
	std::string dir_up;
	std::string dir_down;
	std::string dir_f;
	std::string dir_b;
	std::string dir_l;
	std::string dir_r;

	// Console startup messages
	std::string console_offset_mismatch;
	std::string console_version_mismatch_prefix;
	std::string console_version_mismatch_suffix;
	std::string console_fetch_offsets;
	std::string console_new_version;
	std::string console_open_releases;
	std::string console_dma_updating;
	std::string console_dma_update_ok;
	std::string console_dma_update_fail;
	std::string console_dma_dumper_missing;
	std::string console_dma_restart;

	// Offset refetch (GUI)
	std::string offset_refetch_button;
	std::string offset_current_date;
	std::string offset_local_version;
	std::string offset_latest_mismatch;
	std::string offset_latest_match;
	std::string offset_guide_title;
	std::string offset_guide_body;
	std::string offset_guide_confirm;
	std::string offset_guide_cancel;
	std::string offset_status_running;
	std::string offset_result_success_title;
	std::string offset_result_success_body;
	std::string offset_result_restart;
	std::string offset_result_fail_title;
	std::string offset_result_fail_body;
	std::string offset_result_fail_ok;

	// Status Messages
	std::string status_dma_init;
	std::string status_dma_failed;
	std::string status_searching;
	std::string status_init_game;
	std::string status_waiting_decrypt;
	std::string status_unknown;

	// Projectile ESP
	std::string proj_enable;
	std::string proj_range;
	std::string proj_rangealpha;

	// Web Radar
	std::string webradar_enable;
	std::string webradar_port;
	std::string webradar_interval;
	std::string webradar_local_access;
	std::string webradar_clients;
	std::string webradar_copy_url;
	std::string webradar_copied;
	std::string webradar_not_running;
	std::string webradar_password_enable;
	std::string webradar_password;
	std::string webradar_tunnel_section;
	std::string webradar_tunnel_enable;
	std::string webradar_tunnel_starting;
	std::string webradar_tunnel_not_installed;

	// Hotkey Bindings
	std::string hotkey_none;
	std::string hotkey_cleartip;
	std::string hotkey_header_esp;
	std::string hotkey_header_features;
	std::string hotkey_header_actions;
	const char* hotkey_action_labels[16];

	// Crosshair Overlay
	std::string header_crosshair;
	std::string crosshair_enable;
	std::string crosshair_size;
	std::string crosshair_thickness;
	std::string crosshair_gap;
	std::string crosshair_style;
	std::string crosshair_color;
	std::string crosshair_onenemycolor;
	std::string crosshair_enemycolor;
	const char* crosshair_styleselect[4];

	// ESP Gap-Closure Stage 3 (Task 7-14)
	std::string visuals_offscreen_arrows;
	std::string visuals_arrow_color;
	std::string visuals_player_flags;
	std::string visuals_flag_blind;
	std::string visuals_flag_scoped;
	std::string visuals_flag_defusing;
	std::string visuals_flag_kit;
	std::string visuals_flag_money;
	std::string visuals_flag_fontsize;
	std::string visuals_visibility;
	std::string visuals_visible_color;
	std::string visuals_hidden_color;
	std::string visuals_sound_esp;
	std::string visuals_sound_color;
	std::string visuals_bomb_timer;
	std::string visuals_world_esp;
	std::string visuals_world_timers;
	std::string visuals_world_smoke_timer;
	std::string visuals_world_inferno_timer;
	std::string visuals_world_decoy_timer;
	std::string visuals_world_color;
	std::string visuals_weapon_ammo;
	std::string visuals_ammo_fontsize;
	std::string visuals_ammo_color;
	std::string visuals_low_ammo_color;
	std::string visuals_weapon_icon;
	std::string visuals_weapon_icon_fontsize;
	std::string visuals_weapon_icon_color;
	std::string visuals_weapon_icon_noknife;
	std::string visuals_world_items;
	std::string visuals_world_item_fontsize;
	std::string visuals_item_filter;
	std::string visuals_item_filter_pistols;
	std::string visuals_item_filter_smgs;
	std::string visuals_item_filter_rifles;
	std::string visuals_item_filter_snipers;
	std::string visuals_item_filter_heavy;
	std::string visuals_item_filter_gear;
	std::string visuals_item_filter_all;
	std::string visuals_item_filter_none;
	std::string visuals_health_text;
	std::string visuals_armor_text;
	std::string visuals_bar_label_fontsize;
	std::string visuals_interpolation;
	std::string visuals_bone_reliability;

	// Task 18: independent color labels (no longer reuse generic labels)
	std::string visuals_headdot_color;
	std::string visuals_armorbar_color;
	std::string visuals_weapon_color;
	std::string visuals_name_color;
	std::string visuals_distance_color;
	std::string visuals_flag_blind_color;
	std::string visuals_flag_scoped_color;
	std::string visuals_flag_defusing_color;
	std::string visuals_flag_kit_color;
	std::string visuals_flag_money_color;

	// Task 20: independent thickness/size/width labels
	std::string visuals_box_thickness;
	std::string visuals_bone_thickness;
	std::string visuals_eyeray_thickness;
	std::string visuals_line_thickness;
	std::string visuals_name_size;
	std::string visuals_weapon_size;
	std::string visuals_distance_size;
	std::string visuals_arrow_size;
	std::string visuals_healthbar_width;
	std::string visuals_armorbar_width;

	// Task 21: Armor Bar position label (semantically independent from grenade_position)
	std::string visuals_armorbar_position;

	Language() { english(); }

	void english() {
		this->tab_visuals = "Visuals";
		this->tab_radar = "Radar";
		this->tab_settings = "Settings";
		this->tab_config = "Config";
		this->tab_grenade = "Grenade";
		this->tab_hotkeys = "Hotkeys";

		this->visuals_showbox = "Show Box";
		this->visuals_boxcolor = "Box Color";
		this->visuals_boxtype = "Box Type";
		this->visuals_showbone = "Show Bones";
		this->visuals_bonecolor = "Bones Color";
		this->visuals_showeyeray = "Show Eye Ray";
		this->visuals_eyeraycolor = "Eye Ray Color";
		this->visuals_showbar = "Show Health Bar";
		this->visuals_barpos = "Bar Position";
		this->visuals_weaponesp = "Show Weapon";
		this->visuals_distance = "Show Distance";
		this->visuals_name = "Show Name";
		this->visuals_line = "Snapline";
		this->visuals_linecolor = "Snapline Color";

		this->utilities_teamcheck = "Hide Teammates";
		this->utilities_closehack = "Close Software";
		this->utilities_reloadhack = "Reconnect";
		this->utilities_language = "Select Language";
		this->utilities_help = "Help";

		this->config_newconfig = "Config Name";
		this->config_create = "Create config";
		this->config_load = "Load";
		this->config_save = "Save";
		this->config_delete = "Delete";

		this->visuals_boxtypeselect[0] = "Normal"; this->visuals_boxtypeselect[1] = "Slim"; this->visuals_boxtypeselect[2] = "Corner";
		this->visuals_heathbarselect[0] = "Left"; this->visuals_heathbarselect[1] = "Up"; this->visuals_heathbarselect[2] = "Number";

		this->days = "days";

		this->frames = "Frames";

		// Grenade Helper
		this->grenade_enable = "Enable Grenade Helper";
		this->grenade_showname = "Show Name";
		this->grenade_showbox = "Show Box";
		this->grenade_showline = "Show Line";
		this->grenade_autoaim = "Auto Aim";
		this->grenade_maxdistance = "Max Distance";
		this->grenade_triggerdistance = "Trigger Distance";
		this->grenade_boxsize = "Box Size";
		this->grenade_currentmap = "Current Map";
		this->grenade_availablethrows = "Available Throws";
		this->grenade_nomapdata = "No data for current map";
		this->grenade_flash = "Flash";
		this->grenade_smoke = "Smoke";
		this->grenade_he = "HE";
		this->grenade_molotov = "Molotov";

		// Grenade Recording
		this->grenade_record_hotkey = "Record Hotkey";
		this->grenade_pending_throws = "Pending Throws";
		this->grenade_name_throw = "Name Throw";
		this->grenade_throw_name = "Throw Name";
		this->grenade_throw_style = "Style";
		this->grenade_throw_type = "Type";
		this->grenade_save_throws = "Save to File";
		this->grenade_delete = "Delete";
		this->grenade_clear_all = "Clear All";
		this->grenade_no_pending = "No pending throws";
		this->grenade_position = "Position";
		this->grenade_angle = "Angle";
		this->grenade_recorded_at = "Recorded at";

		this->header_playerbox = "Player Box";
		this->header_skeleton = "Skeleton / Eye Ray";
		this->header_health = "Health & Armor";
		this->header_info = "Info & Text";
		this->header_snapline = "Snapline";
		this->header_general = "General";
		this->header_system = "System";
		this->header_recording = "Recording / Hotkey";
		this->header_pending = "Pending Throws";
		this->header_savededitor = "Saved Throws Editor";
		this->header_bomb = "C4 / Bomb";
		this->header_weapon = "Weapon";
		this->header_teamfilter = "Team Filter";
		this->header_world_proj = "World & Projectiles";
		this->header_render_quality = "Render Quality";
		this->visuals_bombesp = "Show Bomb ESP";
		this->visuals_bombplanted = "Planted";
		this->visuals_bombdefusing = "Defusing";
		this->visuals_bombcarrier = "Carrier";
		this->visuals_bombdropped = "Dropped";

		this->visuals_thickness = "Thickness";
		this->visuals_rounding = "Rounding";
		this->visuals_cornersize = "Corner Size";
		this->visuals_filled = "Filled";
		this->visuals_fillalpha = "Fill Alpha";
		this->visuals_headdot = "Head Dot";
		this->visuals_dotsize = "Dot Size";
		this->visuals_length = "Length";
		this->visuals_barwidth = "Bar Width";
		this->visuals_armorbar = "Armor Bar";
		this->visuals_size = "Size";
		this->visuals_origin = "Origin";
		this->visuals_width = "Width";

		this->armorbar_typeselect[0] = "Right"; this->armorbar_typeselect[1] = "Top";
		this->snapline_originselect[0] = "Top"; this->snapline_originselect[1] = "Center"; this->snapline_originselect[2] = "Bottom";

		this->header_safezone = "Crosshair Safe Zone";
		this->safezone_enable = "Enable Safe Zone";
		this->safezone_radius = "Radius";
		this->safezone_shape = "Shape";
		this->safezone_shapeselect[0] = "Circle"; this->safezone_shapeselect[1] = "Square";
		this->safezone_mode = "Mode";
		this->safezone_modeselect[0] = "Area Mask"; this->safezone_modeselect[1] = "Skip Drawing";
		this->safezone_skip_box = "Skip Box";
		this->safezone_skip_bone = "Skip Bone";
		this->safezone_skip_healthbar = "Skip Health Bar";
		this->safezone_skip_armorbar = "Skip Armor Bar";
		this->safezone_skip_weapon = "Skip Weapon";
		this->safezone_skip_name = "Skip Name";
		this->safezone_skip_snapline = "Skip Snapline";
		this->safezone_skip_eyeray = "Skip Eye Ray";
		this->safezone_skip_headdot = "Skip Head Dot";
		this->safezone_skip_distance = "Skip Distance";

		this->visuals_spectatorlist = "Show Spectator List";
		this->settings_perfmonitor = "Performance Monitor";

		this->settings_menuhotkey = "Menu Hotkey";

		this->settings_debuglog = "Debug Log";
		this->settings_debuglog_tip = "Enable verbose TRACE/DEBUG logging for troubleshooting (impacts performance)";

		this->settings_vsync = "VSync";
		this->settings_maxfps = "Max FPS";
		this->settings_unlimited = "Unlimited";
		this->settings_unlimitedtip = "0 = Unlimited";
		this->settings_restarttip = "restart to apply";

		this->settings_resolution = "Resolution";
		this->settings_renderautotip = "Auto = use monitor native resolution, restart to apply";
		this->settings_monitor = "Monitor";
		this->settings_monitortip = "Select which monitor to render on, restart to apply";

		this->grenade_pressanykey = "Press any key...";
		this->grenade_hotkeytip = "Click button then press key | Supports mouse side buttons | ESC cancel";
		this->grenade_autosave = "Auto Save";
		this->grenade_autosavetip = "Automatically save throws after recording";
		this->grenade_defaulttype = "Default Type";
		this->grenade_defaultstyle = "Default Style";
		this->grenade_reloadfiles = "Reload Files";
		this->grenade_reloadtip = "Reload all grenade helper data from JSON files";
		this->grenade_selectmap = "Select Map";
		this->grenade_totalthrows = "Total throws";
		this->grenade_editthrow = "Edit Throw";
		this->grenade_update = "Update";
		this->grenade_nomaps = "No maps loaded";
		this->grenade_name_label = "Name";

		this->grenade_typeselect[0] = "Flash"; this->grenade_typeselect[1] = "Smoke"; this->grenade_typeselect[2] = "HE"; this->grenade_typeselect[3] = "Molotov";
		this->grenade_styleselect[0] = "Stand"; this->grenade_styleselect[1] = "Run"; this->grenade_styleselect[2] = "Jump"; this->grenade_styleselect[3] = "Crouch"; this->grenade_styleselect[4] = "Run+Jump";

		this->grenade_typename_flash = "flash";
		this->grenade_typename_smoke = "smoke";
		this->grenade_typename_he = "HE";
		this->grenade_typename_fire = "fire";
		this->grenade_typename_unknown = "unknown";
		this->grenade_stylename_stand = "stand";
		this->grenade_stylename_run = "run";
		this->grenade_stylename_jump = "jump";
		this->grenade_stylename_crouch = "crouch";
		this->grenade_stylename_runjump = "run+jump";
		this->grenade_stylename_unknown = "unknown";

		this->dir_forward = "Forward"; this->dir_back = "Back";
		this->dir_right = "Right"; this->dir_left = "Left";
		this->dir_up = "Up"; this->dir_down = "Down";
		this->dir_f = "F"; this->dir_b = "B"; this->dir_l = "L"; this->dir_r = "R";

		this->proj_enable = "Show Projectile ESP";
		this->proj_range = "Show Effect Range";
		this->proj_rangealpha = "Range Alpha";

		// Web Radar
		this->webradar_enable = "Enable Web Radar";
		this->webradar_port = "Port";
		this->webradar_interval = "Interval (ms)";
		this->webradar_local_access = "Local / LAN Access";
		this->webradar_clients = "Clients";
		this->webradar_copy_url = "Copy";
		this->webradar_copied = "Copied!";
		this->webradar_not_running = "Server not running";
		this->webradar_password_enable = "Enable Password";
		this->webradar_password = "Password";
		this->webradar_tunnel_section = "Public Access (Cloudflare Tunnel)";
		this->webradar_tunnel_enable = "Enable Tunnel";
		this->webradar_tunnel_starting = "Starting tunnel...";
		this->webradar_tunnel_not_installed = "cloudflared not installed. Run:";

		this->hotkey_none = "None";
		this->hotkey_cleartip = "Right-click to clear";
		this->hotkey_header_esp = "ESP Toggles";
		this->hotkey_header_features = "Feature Toggles";
		this->hotkey_header_actions = "Actions";
		this->hotkey_action_labels[0] = "Box ESP";
		this->hotkey_action_labels[1] = "Bone ESP";
		this->hotkey_action_labels[2] = "Health Bar";
		this->hotkey_action_labels[3] = "Weapon ESP";
		this->hotkey_action_labels[4] = "Player Name";
		this->hotkey_action_labels[5] = "Distance";
		this->hotkey_action_labels[6] = "Eye Ray";
		this->hotkey_action_labels[7] = "Snapline";
		this->hotkey_action_labels[8] = "C4 ESP";
		this->hotkey_action_labels[9] = "Projectile ESP";
		this->hotkey_action_labels[10] = "Spectator List";
		this->hotkey_action_labels[11] = "Hide Teammates";
		this->hotkey_action_labels[12] = "Web Radar";
		this->hotkey_action_labels[13] = "Safe Zone";
		this->hotkey_action_labels[14] = "Crosshair Overlay";
		this->hotkey_action_labels[15] = "Reconnect";

		this->header_crosshair = "Crosshair Overlay";
		this->crosshair_enable = "Enable Crosshair";
		this->crosshair_size = "Size";
		this->crosshair_thickness = "Thickness";
		this->crosshair_gap = "Gap";
		this->crosshair_style = "Style";
		this->crosshair_color = "Color";
		this->crosshair_onenemycolor = "Change Color on Enemy";
		this->crosshair_enemycolor = "Enemy Color";
		this->crosshair_styleselect[0] = "Cross"; this->crosshair_styleselect[1] = "Dot"; this->crosshair_styleselect[2] = "Circle"; this->crosshair_styleselect[3] = "Cross+Dot";

		// ESP Gap-Closure Stage 3 (Task 7-14)
		this->visuals_offscreen_arrows = "Offscreen Arrows";
		this->visuals_arrow_color = "Arrow Color";
		this->visuals_player_flags = "Player Flags";
		this->visuals_flag_blind = "Blind";
		this->visuals_flag_scoped = "Scoped";
		this->visuals_flag_defusing = "Defusing";
		this->visuals_flag_kit = "Defuse Kit";
		this->visuals_flag_money = "Money";
		this->visuals_flag_fontsize = "Flag Font Size";
		this->visuals_visibility = "Visibility Coloring";
		this->visuals_visible_color = "Visible Color";
		this->visuals_hidden_color = "Hidden Color";
		this->visuals_sound_esp = "Sound ESP";
		this->visuals_sound_color = "Sound Color";
		this->visuals_bomb_timer = "C4 Bomb Timer";
		this->visuals_world_esp = "World ESP";
		this->visuals_world_timers = "Projectile Timers";
		this->visuals_world_smoke_timer = "Smoke Timer";
		this->visuals_world_inferno_timer = "Inferno Timer";
		this->visuals_world_decoy_timer = "Decoy Timer";
		this->visuals_world_color = "World Color";
		this->visuals_weapon_ammo = "Weapon Ammo";
		this->visuals_ammo_fontsize = "Ammo Font Size";
		this->visuals_ammo_color = "Ammo Color";
		this->visuals_low_ammo_color = "Low Ammo Color";
		this->visuals_weapon_icon = "Weapon Icon";
		this->visuals_weapon_icon_fontsize = "Icon Font Size";
		this->visuals_weapon_icon_color = "Icon Color";
		this->visuals_weapon_icon_noknife = "Hide Knife Icon";
		this->visuals_world_items = "Dropped Weapons";
		this->visuals_world_item_fontsize = "Item Font Size";
		this->visuals_item_filter = "Item Filter";
		this->visuals_item_filter_pistols = "Pistols";
		this->visuals_item_filter_smgs = "SMG";
		this->visuals_item_filter_rifles = "Rifles";
		this->visuals_item_filter_snipers = "Snipers";
		this->visuals_item_filter_heavy = "Heavy";
		this->visuals_item_filter_gear = "Gear";
		this->visuals_item_filter_all = "All";
		this->visuals_item_filter_none = "None";
		this->visuals_health_text = "Health Text";
		this->visuals_armor_text = "Armor Text";
		this->visuals_bar_label_fontsize = "Label Font Size";
		this->visuals_interpolation = "Position Interpolation";
		this->visuals_bone_reliability = "Bone Reliability Check";

		// Task 18: independent color labels
		this->visuals_headdot_color = "Head Dot Color";
		this->visuals_armorbar_color = "Armor Bar Color";
		this->visuals_weapon_color = "Weapon Color";
		this->visuals_name_color = "Name Color";
		this->visuals_distance_color = "Distance Color";
		this->visuals_flag_blind_color = "Blind Color";
		this->visuals_flag_scoped_color = "Scoped Color";
		this->visuals_flag_defusing_color = "Defusing Color";
		this->visuals_flag_kit_color = "Defuse Kit Color";
		this->visuals_flag_money_color = "Money Color";

		// Task 20: independent thickness/size/width labels
		this->visuals_box_thickness = "Box Thickness";
		this->visuals_bone_thickness = "Bone Thickness";
		this->visuals_eyeray_thickness = "Eye Ray Thickness";
		this->visuals_line_thickness = "Snapline Thickness";
		this->visuals_name_size = "Name Size";
		this->visuals_weapon_size = "Weapon Size";
		this->visuals_distance_size = "Distance Size";
		this->visuals_arrow_size = "Arrow Size";
		this->visuals_healthbar_width = "Health Bar Width";
		this->visuals_armorbar_width = "Armor Bar Width";

		// Task 21: Armor Bar position label
		this->visuals_armorbar_position = "Position";

		this->console_offset_mismatch = "Local offsets differ from GitHub repository, may be outdated.";
		this->console_version_mismatch_prefix = "CS2 version mismatch (";
		this->console_version_mismatch_suffix = "), offsets may be expired.";
		this->console_fetch_offsets = "Update offsets via DMA? Make sure CS2 is running at main menu. (y/n): ";
		this->console_new_version = "New version available: ";
		this->console_open_releases = "Open Releases page to download latest version? (y/n): ";
		this->console_dma_updating = "Running DMA offset dumper...";
		this->console_dma_update_ok = "Offsets updated successfully via DMA.";
		this->console_dma_update_fail = "DMA offset update failed!";
		this->console_dma_dumper_missing = "cs2-dumper.exe not found. Build it: cd external\\dumper && cargo build --release";
		this->console_dma_restart = "Please restart the program to apply new offsets.";

		// Offset refetch (GUI)
		this->offset_refetch_button = "Re-acquire Offsets";
		this->offset_current_date = "Current offset date: %s";
		this->offset_local_version = "Local offset: %s (%s)";
		this->offset_latest_mismatch = "Latest game: %s (MISMATCH!)";
		this->offset_latest_match = "Latest game: %s (OK)";
		this->offset_guide_title = "Re-acquire Offsets";
		this->offset_guide_body = "Please ensure CS2 is running and you are at the MAIN MENU (not in a match).\n\nThe acquisition process will temporarily disconnect DMA. After completion, the program will need to restart.\n\nClick \"Confirm\" to start acquiring offsets.";
		this->offset_guide_confirm = "Confirm";
		this->offset_guide_cancel = "Cancel";
		this->offset_status_running = "Acquiring offsets, please wait...";
		this->offset_result_success_title = "Offsets Updated";
		this->offset_result_success_body = "Offsets have been successfully updated. The program needs to restart to apply the new offsets.";
		this->offset_result_restart = "Restart Now";
		this->offset_result_fail_title = "Acquisition Failed";
		this->offset_result_fail_body = "Failed to acquire offsets. DMA connection will be restored.";
		this->offset_result_fail_ok = "OK";

		this->status_dma_init = "Initializing DMA...";
		this->status_dma_failed = "DMA Connection Failed!";
		this->status_searching = "Searching for cs2.exe...";
		this->status_init_game = "Initializing game data...";
		this->status_waiting_decrypt = "Waiting for module decryption...";
		this->status_unknown = "Unknown state";
	}

	void chineese() {
		this->tab_visuals = u8"\u89c6\u89c9";
		this->tab_radar = u8"\u96f7\u8fbe";
		this->tab_settings = u8"\u8bbe\u7f6e";
		this->tab_config = u8"\u914d\u7f6e";
		this->tab_grenade = u8"\u6295\u63b7\u7269";
		this->tab_hotkeys = u8"\u5feb\u6377\u952e";

		this->visuals_showbox = u8"\u663e\u793a\u900f\u89c6\u6846";
		this->visuals_boxcolor = u8"\u6846\u989c\u8272";
		this->visuals_boxtype = u8"\u6846\u7c7b\u578b";
		this->visuals_showbone = u8"\u663e\u793a\u9aa8\u9abc";
		this->visuals_bonecolor = u8"\u9aa8\u9abc\u989c\u8272";
		this->visuals_showeyeray = u8"\u663e\u793a\u89c6\u7ebf";
		this->visuals_eyeraycolor = u8"\u89c6\u7ebf\u989c\u8272";
		this->visuals_showbar = u8"\u663e\u793a\u751f\u547d\u6761";
		this->visuals_barpos = u8"\u8840\u6761\u4f4d\u7f6e";
		this->visuals_weaponesp = u8"\u663e\u793a\u6b66\u5668";
		this->visuals_distance = u8"\u663e\u793a\u8ddd\u79bb";
		this->visuals_name = u8"\u663e\u793a\u540d\u79f0";
		this->visuals_line = u8"\u6307\u793a\u7ebf";
		this->visuals_linecolor = u8"\u6307\u793a\u7ebf\u989c\u8272";

		this->utilities_teamcheck = u8"\u9690\u85cf\u53cb\u519b";
		this->utilities_closehack = u8"\u5173\u95ed\u8f6f\u4ef6";
		this->utilities_language = u8"\u9009\u62e9\u8bed\u8a00";
		this->utilities_reloadhack = u8"\u91cd\u65b0\u8fde\u63a5";
		this->utilities_help = u8"\u5e2e\u52a9";

		this->config_newconfig = u8"\u914d\u7f6e\u540d\u79f0";
		this->config_create = u8"\u521b\u5efa\u914d\u7f6e";
		this->config_load = u8"\u52a0\u8f7d";
		this->config_save = u8"\u4fdd\u5b58";
		this->config_delete = u8"\u5220\u9664";

		this->visuals_boxtypeselect[0] = u8"\u666e\u901a"; this->visuals_boxtypeselect[1] = u8"\u7a84"; this->visuals_boxtypeselect[2] = u8"\u62d0\u89d2";
		this->visuals_heathbarselect[0] = u8"\u5de6\u4fa7"; this->visuals_heathbarselect[1] = u8"\u4e0a\u4fa7"; this->visuals_heathbarselect[2] = u8"\u6570\u5b57";

		this->days = u8"\u5269\u4f59\u5929\u6570";

		this->frames = u8"\u6846";

		// Grenade Helper
		this->grenade_enable = u8"\u542f\u7528\u6295\u63b7\u7269\u8f85\u52a9";
		this->grenade_showname = u8"\u663e\u793a\u540d\u79f0";
		this->grenade_showbox = u8"\u663e\u793a\u65b9\u6846";
		this->grenade_showline = u8"\u663e\u793a\u8fde\u7ebf";
		this->grenade_autoaim = u8"\u81ea\u52a8\u7784\u51c6";
		this->grenade_maxdistance = u8"\u6700\u5927\u8ddd\u79bb";
		this->grenade_triggerdistance = u8"\u89e6\u53d1\u8ddd\u79bb";
		this->grenade_boxsize = u8"\u65b9\u6846\u5927\u5c0f";
		this->grenade_currentmap = u8"\u5f53\u524d\u5730\u56fe";
		this->grenade_availablethrows = u8"\u53ef\u7528\u6295\u63b7\u70b9";
		this->grenade_nomapdata = u8"\u5f53\u524d\u5730\u56fe\u65e0\u6570\u636e";
		this->grenade_flash = u8"\u95ea\u5149";
		this->grenade_smoke = u8"\u70df\u96fe";
		this->grenade_he = u8"\u624b\u96f7";
		this->grenade_molotov = u8"\u71c3\u70e7";

		// Grenade Recording
		this->grenade_record_hotkey = u8"\u8bb0\u5f55\u5feb\u6301\u952e";
		this->grenade_pending_throws = u8"\u5f85\u547d\u540d\u70b9\u4f4d";
		this->grenade_name_throw = u8"\u547d\u540d\u70b9\u4f4d";
		this->grenade_throw_name = u8"\u70b9\u4f4d\u540d\u79f0";
		this->grenade_throw_style = u8"\u6295\u63b7\u65b9\u5f0f";
		this->grenade_throw_type = u8"\u6295\u63b7\u7c7b\u578b";
		this->grenade_save_throws = u8"\u4fdd\u5b58\u5230\u6587\u4ef6";
		this->grenade_delete = u8"\u5220\u9664";
		this->grenade_clear_all = u8"\u6e05\u7a7a\u6240\u6709";
		this->grenade_no_pending = u8"\u6ca1\u6709\u5f85\u547d\u540d\u70b9\u4f4d";
		this->grenade_position = u8"\u4f4d\u7f6e";
		this->grenade_angle = u8"\u89d2\u5ea6";
		this->grenade_recorded_at = u8"\u8bb0\u5f55\u65f6\u95f4";

		this->header_playerbox = u8"\u65b9\u6846\u900f\u89c6";
		this->header_skeleton = u8"\u9aa8\u9abc / \u89c6\u7ebf";
		this->header_health = u8"\u751f\u547d\u503c & \u62a4\u7532";
		this->header_info = u8"\u4fe1\u606f & \u6587\u5b57";
		this->header_snapline = u8"\u6307\u793a\u7ebf";
		this->header_general = u8"\u5e38\u89c4";
		this->header_system = u8"\u7cfb\u7edf";
		this->header_recording = u8"\u5f55\u5236 / \u5feb\u6377\u952e";
		this->header_pending = u8"\u5f85\u547d\u540d\u6295\u63b7\u70b9";
		this->header_savededitor = u8"\u5df2\u4fdd\u5b58\u6295\u63b7\u70b9\u7f16\u8f91";
		this->header_bomb = u8"C4 \u70b8\u5f39";
		this->header_weapon = u8"\u6b66\u5668";
		this->header_teamfilter = u8"\u961f\u4f0d\u8fc7\u6ee4";
		this->header_world_proj = u8"\u4e16\u754c\u4e0e\u6295\u63b7\u7269";
		this->header_render_quality = u8"\u6e32\u67d3\u8d28\u91cf";
		this->visuals_bombesp = u8"\u663e\u793a\u70b8\u5f39ESP";
		this->visuals_bombplanted = u8"\u5df2\u5b89\u88c5";
		this->visuals_bombdefusing = u8"\u62c6\u9664\u4e2d";
		this->visuals_bombcarrier = u8"\u643a\u5e26\u8005";
		this->visuals_bombdropped = u8"\u5df2\u6389\u843d";

		this->visuals_thickness = u8"\u7c97\u7ec6";
		this->visuals_rounding = u8"\u5706\u89d2";
		this->visuals_cornersize = u8"\u62d0\u89d2\u5927\u5c0f";
		this->visuals_filled = u8"\u586b\u5145";
		this->visuals_fillalpha = u8"\u586b\u5145\u900f\u660e\u5ea6";
		this->visuals_headdot = u8"\u5934\u90e8\u5706\u70b9";
		this->visuals_dotsize = u8"\u5706\u70b9\u5927\u5c0f";
		this->visuals_length = u8"\u957f\u5ea6";
		this->visuals_barwidth = u8"\u8840\u6761\u5bbd\u5ea6";
		this->visuals_armorbar = u8"\u62a4\u7532\u6761";
		this->visuals_size = u8"\u5927\u5c0f";
		this->visuals_origin = u8"\u8d77\u70b9";
		this->visuals_width = u8"\u5bbd\u5ea6";

		this->armorbar_typeselect[0] = u8"\u53f3\u4fa7"; this->armorbar_typeselect[1] = u8"\u4e0a\u65b9";
		this->snapline_originselect[0] = u8"\u4e0a\u65b9"; this->snapline_originselect[1] = u8"\u4e2d\u95f4"; this->snapline_originselect[2] = u8"\u4e0b\u65b9";

		this->header_safezone = u8"\u51c6\u661f\u5b89\u5168\u533a";
		this->safezone_enable = u8"\u542f\u7528\u5b89\u5168\u533a";
		this->safezone_radius = u8"\u534a\u5f84";
		this->safezone_shape = u8"\u5f62\u72b6";
		this->safezone_shapeselect[0] = u8"\u5706\u5f62"; this->safezone_shapeselect[1] = u8"\u65b9\u5f62";
		this->safezone_mode = u8"\u6a21\u5f0f";
		this->safezone_modeselect[0] = u8"\u533a\u57df\u906e\u7f69"; this->safezone_modeselect[1] = u8"\u8303\u56f4\u5185\u4e0d\u7ed8\u5236";
		this->safezone_skip_box = u8"\u8df3\u8fc7\u65b9\u6846";
		this->safezone_skip_bone = u8"\u8df3\u8fc7\u9aa8\u9abc";
		this->safezone_skip_healthbar = u8"\u8df3\u8fc7\u8840\u6761";
		this->safezone_skip_armorbar = u8"\u8df3\u8fc7\u62a4\u7532\u6761";
		this->safezone_skip_weapon = u8"\u8df3\u8fc7\u6b66\u5668";
		this->safezone_skip_name = u8"\u8df3\u8fc7\u540d\u79f0";
		this->safezone_skip_snapline = u8"\u8df3\u8fc7\u8fde\u7ebf";
		this->safezone_skip_eyeray = u8"\u8df3\u8fc7\u89c6\u7ebf";
		this->safezone_skip_headdot = u8"\u8df3\u8fc7\u5934\u90e8\u5706\u70b9";
		this->safezone_skip_distance = u8"\u8df3\u8fc7\u8ddd\u79bb";

		this->visuals_spectatorlist = u8"\u663e\u793a\u89c2\u4f17\u5217\u8868";
		this->settings_perfmonitor = u8"\u6027\u80fd\u76d1\u63a7";

		this->settings_menuhotkey = u8"\u83dc\u5355\u5feb\u6301\u952e";

		this->settings_debuglog = u8"\u8c03\u8bd5\u65e5\u5fd7";
		this->settings_debuglog_tip = u8"\u542f\u7528\u8be6\u7ec6\u7684 TRACE/DEBUG \u65e5\u5fd7\u8f93\u51fa\u7528\u4e8e\u95ee\u9898\u5b9a\u4f4d\uff08\u5f71\u54cd\u6027\u80fd\uff09";

		this->settings_vsync = u8"\u5782\u76f4\u540c\u6b65";
		this->settings_maxfps = u8"\u6700\u5927\u5e27\u7387";
		this->settings_unlimited = u8"\u65e0\u9650\u5236";
		this->settings_unlimitedtip = u8"0 = \u65e0\u9650\u5236";
		this->settings_restarttip = u8"\u91cd\u542f\u540e\u751f\u6548";

		this->settings_resolution = u8"\u5206\u8fa8\u7387";
		this->settings_renderautotip = u8"\u81ea\u52a8 = \u4f7f\u7528\u663e\u793a\u5668\u539f\u751f\u5206\u8fa8\u7387, \u91cd\u542f\u540e\u751f\u6548";
		this->settings_monitor = u8"\u663e\u793a\u5668";
		this->settings_monitortip = u8"\u9009\u62e9\u7ed8\u5236\u7684\u663e\u793a\u5668, \u91cd\u542f\u540e\u751f\u6548";

		this->grenade_pressanykey = u8"\u6309\u4efb\u610f\u952e...";
		this->grenade_hotkeytip = u8"\u70b9\u51fb\u6309\u94ae\u540e\u6309\u952e | \u652f\u6301\u9f20\u6807\u4fa7\u952e | ESC\u53d6\u6d88";
		this->grenade_autosave = u8"\u81ea\u52a8\u4fdd\u5b58";
		this->grenade_autosavetip = u8"\u5f55\u5236\u540e\u81ea\u52a8\u4fdd\u5b58\u6295\u63b7\u70b9";
		this->grenade_defaulttype = u8"\u9ed8\u8ba4\u7c7b\u578b";
		this->grenade_defaultstyle = u8"\u9ed8\u8ba4\u65b9\u5f0f";
		this->grenade_reloadfiles = u8"\u91cd\u65b0\u52a0\u8f7d";
		this->grenade_reloadtip = u8"\u4ece JSON \u6587\u4ef6\u91cd\u65b0\u52a0\u8f7d\u6240\u6709\u6295\u63b7\u7269\u6570\u636e";
		this->grenade_selectmap = u8"\u9009\u62e9\u5730\u56fe";
		this->grenade_totalthrows = u8"\u603b\u6295\u63b7\u70b9";
		this->grenade_editthrow = u8"\u7f16\u8f91\u6295\u63b7\u70b9";
		this->grenade_update = u8"\u66f4\u65b0";
		this->grenade_nomaps = u8"\u65e0\u5730\u56fe\u6570\u636e";
		this->grenade_name_label = u8"\u540d\u79f0";

		this->grenade_typeselect[0] = u8"\u95ea\u5149"; this->grenade_typeselect[1] = u8"\u70df\u96fe"; this->grenade_typeselect[2] = u8"\u624b\u96f7"; this->grenade_typeselect[3] = u8"\u71c3\u70e7";
		this->grenade_styleselect[0] = u8"\u7ad9\u7acb"; this->grenade_styleselect[1] = u8"\u8dd1\u52a8"; this->grenade_styleselect[2] = u8"\u8df3\u8dc3"; this->grenade_styleselect[3] = u8"\u8e72\u4e0b"; this->grenade_styleselect[4] = u8"\u8dd1\u8df3";

		this->grenade_typename_flash = u8"\u95ea\u5149";
		this->grenade_typename_smoke = u8"\u70df\u96fe";
		this->grenade_typename_he = u8"\u624b\u96f7";
		this->grenade_typename_fire = u8"\u71c3\u70e7";
		this->grenade_typename_unknown = u8"\u672a\u77e5";
		this->grenade_stylename_stand = u8"\u7ad9\u7acb";
		this->grenade_stylename_run = u8"\u8dd1\u52a8";
		this->grenade_stylename_jump = u8"\u8df3\u8dc3";
		this->grenade_stylename_crouch = u8"\u8e72\u4e0b";
		this->grenade_stylename_runjump = u8"\u8dd1\u8df3";
		this->grenade_stylename_unknown = u8"\u672a\u77e5";

		this->dir_forward = u8"\u524d\u65b9"; this->dir_back = u8"\u540e\u65b9";
		this->dir_right = u8"\u53f3\u65b9"; this->dir_left = u8"\u5de6\u65b9";
		this->dir_up = u8"\u4e0a\u65b9"; this->dir_down = u8"\u4e0b\u65b9";
		this->dir_f = u8"\u524d"; this->dir_b = u8"\u540e"; this->dir_l = u8"\u5de6"; this->dir_r = u8"\u53f3";

		this->proj_enable = u8"\u663e\u793a\u6295\u63b7\u7269ESP";
		this->proj_range = u8"\u663e\u793a\u751f\u6548\u8303\u56f4";
		this->proj_rangealpha = u8"\u8303\u56f4\u900f\u660e\u5ea6";

		// Web Radar
		this->webradar_enable = u8"\u542f\u7528\u7f51\u9875\u96f7\u8fbe";
		this->webradar_port = u8"\u7aef\u53e3";
		this->webradar_interval = u8"\u5237\u65b0\u95f4\u9694 (ms)";
		this->webradar_local_access = u8"\u5c40\u57df\u7f51\u8bbf\u95ee";
		this->webradar_clients = u8"\u8fde\u63a5\u6570";
		this->webradar_copy_url = u8"\u590d\u5236";
		this->webradar_copied = u8"\u5df2\u590d\u5236!";
		this->webradar_not_running = u8"\u670d\u52a1\u5668\u672a\u8fd0\u884c";
		this->webradar_password_enable = u8"\u542f\u7528\u5bc6\u7801";
		this->webradar_password = u8"\u5bc6\u7801";
		this->webradar_tunnel_section = u8"\u516c\u7f51\u8bbf\u95ee (Cloudflare \u96a7\u9053)";
		this->webradar_tunnel_enable = u8"\u542f\u7528\u96a7\u9053";
		this->webradar_tunnel_starting = u8"\u96a7\u9053\u542f\u52a8\u4e2d...";
		this->webradar_tunnel_not_installed = u8"\u672a\u5b89\u88c5 cloudflared\uff0c\u8bf7\u8fd0\u884c:";

		this->hotkey_none = u8"\u65e0";
		this->hotkey_cleartip = u8"\u53f3\u952e\u6e05\u9664";
		this->hotkey_header_esp = u8"ESP \u5f00\u5173";
		this->hotkey_header_features = u8"\u529f\u80fd\u5f00\u5173";
		this->hotkey_header_actions = u8"\u64cd\u4f5c";
		this->hotkey_action_labels[0] = u8"\u65b9\u6846 ESP";
		this->hotkey_action_labels[1] = u8"\u9aa8\u9abc ESP";
		this->hotkey_action_labels[2] = u8"\u751f\u547d\u6761";
		this->hotkey_action_labels[3] = u8"\u6b66\u5668 ESP";
		this->hotkey_action_labels[4] = u8"\u73a9\u5bb6\u540d\u79f0";
		this->hotkey_action_labels[5] = u8"\u8ddd\u79bb";
		this->hotkey_action_labels[6] = u8"\u89c6\u7ebf";
		this->hotkey_action_labels[7] = u8"\u6307\u793a\u7ebf";
		this->hotkey_action_labels[8] = u8"C4 ESP";
		this->hotkey_action_labels[9] = u8"\u6295\u63b7\u7269 ESP";
		this->hotkey_action_labels[10] = u8"\u89c2\u4f17\u5217\u8868";
		this->hotkey_action_labels[11] = u8"\u9690\u85cf\u53cb\u519b";
		this->hotkey_action_labels[12] = u8"\u7f51\u9875\u96f7\u8fbe";
		this->hotkey_action_labels[13] = u8"\u51c6\u661f\u5b89\u5168\u533a";
		this->hotkey_action_labels[14] = u8"\u51c6\u661f\u8986\u76d6\u5c42";
		this->hotkey_action_labels[15] = u8"\u91cd\u65b0\u8fde\u63a5";

		this->header_crosshair = u8"\u51c6\u661f\u8986\u76d6\u5c42";
		this->crosshair_enable = u8"\u542f\u7528\u51c6\u661f";
		this->crosshair_size = u8"\u5927\u5c0f";
		this->crosshair_thickness = u8"\u7c97\u7ec6";
		this->crosshair_gap = u8"\u95f4\u8ddd";
		this->crosshair_style = u8"\u6837\u5f0f";
		this->crosshair_color = u8"\u989c\u8272";
		this->crosshair_onenemycolor = u8"\u7784\u51c6\u654c\u4eba\u53d8\u8272";
		this->crosshair_enemycolor = u8"\u654c\u4eba\u989c\u8272";
		this->crosshair_styleselect[0] = u8"\u5341\u5b57"; this->crosshair_styleselect[1] = u8"\u5706\u70b9"; this->crosshair_styleselect[2] = u8"\u5706\u5708"; this->crosshair_styleselect[3] = u8"\u5341\u5b57+\u5706\u70b9";

		// ESP Gap-Closure Stage 3 (Task 7-14)
		this->visuals_offscreen_arrows = u8"\u5c4f\u5916\u7bad\u5934";
		this->visuals_arrow_color = u8"\u7bad\u5934\u989c\u8272";
		this->visuals_player_flags = u8"\u73a9\u5bb6\u72b6\u6001\u6807\u7b7e";
		this->visuals_flag_blind = u8"\u95ea\u5149";
		this->visuals_flag_scoped = u8"\u5f00\u955c";
		this->visuals_flag_defusing = u8"\u62c6\u5f39";
		this->visuals_flag_kit = u8"\u62c6\u5f39\u5668";
		this->visuals_flag_money = u8"\u91d1\u94b1";
		this->visuals_flag_fontsize = u8"\u6807\u7b7e\u5b57\u53f7";
		this->visuals_visibility = u8"\u53ef\u89c1\u6027\u7740\u8272";
		this->visuals_visible_color = u8"\u53ef\u89c1\u989c\u8272";
		this->visuals_hidden_color = u8"\u4e0d\u53ef\u89c1\u989c\u8272";
		this->visuals_sound_esp = u8"\u58f0\u97f3 ESP";
		this->visuals_sound_color = u8"\u58f0\u97f3\u989c\u8272";
		this->visuals_bomb_timer = u8"C4 \u5012\u8ba1\u65f6\u7a97\u53e3";
		this->visuals_world_esp = u8"\u4e16\u754c ESP";
		this->visuals_world_timers = u8"\u6295\u63b7\u7269\u8ba1\u65f6\u5668";
		this->visuals_world_smoke_timer = u8"\u70df\u96fe\u8ba1\u65f6\u5668";
		this->visuals_world_inferno_timer = u8"\u706b\u7130\u8ba1\u65f6\u5668";
		this->visuals_world_decoy_timer = u8"\u8bf1\u9975\u8ba1\u65f6\u5668";
		this->visuals_world_color = u8"\u4e16\u754c\u989c\u8272";
		this->visuals_weapon_ammo = u8"\u6b66\u5668\u5f39\u836f";
		this->visuals_ammo_fontsize = u8"\u5f39\u836f\u5b57\u53f7";
		this->visuals_ammo_color = u8"\u5f39\u836f\u989c\u8272";
		this->visuals_low_ammo_color = u8"\u4f4e\u5f39\u836f\u989c\u8272";
		this->visuals_weapon_icon = u8"\u6b66\u5668\u56fe\u6807";
		this->visuals_weapon_icon_fontsize = u8"\u56fe\u6807\u5b57\u53f7";
		this->visuals_weapon_icon_color = u8"\u56fe\u6807\u989c\u8272";
		this->visuals_weapon_icon_noknife = u8"\u9690\u85cf\u5315\u9996\u56fe\u6807";
		this->visuals_world_items = u8"\u6389\u843d\u6b66\u5668";
		this->visuals_world_item_fontsize = u8"\u7269\u54c1\u5b57\u53f7";
		this->visuals_item_filter = u8"\u7269\u54c1\u7b5b\u9009";
		this->visuals_item_filter_pistols = u8"\u624b\u67aa";
		this->visuals_item_filter_smgs = u8"\u51b2\u950b\u67aa";
		this->visuals_item_filter_rifles = u8"\u6b65\u67aa";
		this->visuals_item_filter_snipers = u8"\u72d9\u51fb\u67aa";
		this->visuals_item_filter_heavy = u8"\u91cd\u578b";
		this->visuals_item_filter_gear = u8"\u88c5\u5907";
		this->visuals_item_filter_all = u8"\u5168\u9009";
		this->visuals_item_filter_none = u8"\u5168\u4e0d\u9009";
		this->visuals_health_text = u8"\u8840\u91cf\u6570\u503c";
		this->visuals_armor_text = u8"\u62a4\u7532\u6570\u503c";
		this->visuals_bar_label_fontsize = u8"\u6807\u7b7e\u5b57\u53f7";
		this->visuals_interpolation = u8"\u4f4d\u7f6e\u63d2\u503c\u5e73\u6ed1";
		this->visuals_bone_reliability = u8"\u9aa8\u9abc\u53ef\u9760\u6027\u68c0\u67e5";

		// Task 18: independent color labels
		this->visuals_headdot_color = u8"\u5934\u90e8\u5706\u70b9\u989c\u8272";
		this->visuals_armorbar_color = u8"\u62a4\u7532\u6761\u989c\u8272";
		this->visuals_weapon_color = u8"\u6b66\u5668\u989c\u8272";
		this->visuals_name_color = u8"\u540d\u79f0\u989c\u8272";
		this->visuals_distance_color = u8"\u8ddd\u79bb\u989c\u8272";
		this->visuals_flag_blind_color = u8"\u95ea\u5149\u989c\u8272";
		this->visuals_flag_scoped_color = u8"\u5f00\u955c\u989c\u8272";
		this->visuals_flag_defusing_color = u8"\u62c6\u5f39\u989c\u8272";
		this->visuals_flag_kit_color = u8"\u62c6\u5f39\u5668\u989c\u8272";
		this->visuals_flag_money_color = u8"\u91d1\u94b1\u989c\u8272";

		// Task 20: independent thickness/size/width labels
		this->visuals_box_thickness = u8"\u65b9\u6846\u7c97\u7ec6";
		this->visuals_bone_thickness = u8"\u9aa8\u9abc\u7c97\u7ec6";
		this->visuals_eyeray_thickness = u8"\u89c6\u7ebf\u7c97\u7ec6";
		this->visuals_line_thickness = u8"\u6307\u793a\u7ebf\u7c97\u7ec6";
		this->visuals_name_size = u8"\u540d\u79f0\u5927\u5c0f";
		this->visuals_weapon_size = u8"\u6b66\u5668\u5927\u5c0f";
		this->visuals_distance_size = u8"\u8ddd\u79bb\u5927\u5c0f";
		this->visuals_arrow_size = u8"\u7bad\u5934\u5927\u5c0f";
		this->visuals_healthbar_width = u8"\u8840\u6761\u5bbd\u5ea6";
		this->visuals_armorbar_width = u8"\u62a4\u7532\u6761\u5bbd\u5ea6";

		// Task 21: Armor Bar position label
		this->visuals_armorbar_position = u8"\u4f4d\u7f6e";

		this->console_offset_mismatch = u8"\u504f\u79fb\u503c\u4e0eGitHub\u4ed3\u5e93\u4e0d\u4e00\u81f4\uff0c\u53ef\u80fd\u4e0d\u662f\u6700\u65b0\u504f\u79fb\u503c\u3002";
		this->console_version_mismatch_prefix = u8"CS2\u7248\u672c\u4e0d\u5339\u914d(";
		this->console_version_mismatch_suffix = u8"),\u504f\u79fb\u503c\u53ef\u80fd\u5df2\u8fc7\u671f\u3002";
		this->console_fetch_offsets = u8"\u662f\u5426\u901a\u8fc7DMA\u66f4\u65b0\u504f\u79fb\u503c? \u8bf7\u786e\u4fddCS2\u5df2\u542f\u52a8\u5e76\u5728\u4e3b\u83dc\u5355\u3002 (y/n): ";
		this->console_new_version = u8"\u65b0\u7248\u672c\u53ef\u7528: ";
		this->console_open_releases = u8"\u662f\u5426\u8df3\u8f6c\u5230 Releases \u9875\u9762\u4e0b\u8f7d\u6700\u65b0\u7248\u672c? (y/n): ";
		this->console_dma_updating = u8"\u6b63\u5728\u8fd0\u884cDMA\u504f\u79fb\u503c\u63d0\u53d6...";
		this->console_dma_update_ok = u8"DMA\u504f\u79fb\u503c\u66f4\u65b0\u6210\u529f\u3002";
		this->console_dma_update_fail = u8"DMA\u504f\u79fb\u503c\u66f4\u65b0\u5931\u8d25!";
		this->console_dma_dumper_missing = u8"\u672a\u627e\u5230cs2-dumper.exe\uff0c\u8bf7\u5148\u6784\u5efa: cd external\\dumper && cargo build --release";
		this->console_dma_restart = u8"\u504f\u79fb\u503c\u5df2\u66f4\u65b0\uff0c\u8bf7\u91cd\u542f\u7a0b\u5e8f\u4ee5\u751f\u6548\u3002";

		// Offset refetch (GUI)
		this->offset_refetch_button = u8"\u91cd\u65b0\u83b7\u53d6\u504f\u79fb\u503c";
		this->offset_current_date = u8"\u5f53\u524d\u504f\u79fb\u65e5\u671f: %s";
		this->offset_local_version = u8"\u672c\u5730\u504f\u79fb: %s (%s)";
		this->offset_latest_mismatch = u8"\u6700\u65b0\u6e38\u620f: %s (\u4e0d\u5339\u914d!)";
		this->offset_latest_match = u8"\u6700\u65b0\u6e38\u620f: %s (\u5339\u914d)";
		this->offset_guide_title = u8"\u91cd\u65b0\u83b7\u53d6\u504f\u79fb\u503c";
		this->offset_guide_body = u8"\u8bf7\u786e\u4fdd CS2 \u5df2\u542f\u52a8\u5e76\u505c\u7559\u5728\u4e3b\u83dc\u5355\uff08\u800c\u975e\u6e38\u620f\u5bf9\u5c40\u4e2d\uff09\u3002\n\n\u83b7\u53d6\u8fc7\u7a0b\u5c06\u4e34\u65f6\u65ad\u5f00 DMA \u8fde\u63a5\u3002\u83b7\u53d6\u5b8c\u6210\u540e\u9700\u8981\u91cd\u542f\u7a0b\u5e8f\u3002\n\n\u70b9\u51fb\"\u786e\u8ba4\"\u5f00\u59cb\u83b7\u53d6\u504f\u79fb\u503c\u3002";
		this->offset_guide_confirm = u8"\u786e\u8ba4";
		this->offset_guide_cancel = u8"\u53d6\u6d88";
		this->offset_status_running = u8"\u6b63\u5728\u83b7\u53d6\u504f\u79fb\u503c\uff0c\u8bf7\u7a0d\u5019...";
		this->offset_result_success_title = u8"\u504f\u79fb\u503c\u5df2\u66f4\u65b0";
		this->offset_result_success_body = u8"\u504f\u79fb\u503c\u5df2\u6210\u529f\u66f4\u65b0\u3002\u7a0b\u5e8f\u9700\u8981\u91cd\u542f\u4ee5\u5e94\u7528\u65b0\u504f\u79fb\u503c\u3002";
		this->offset_result_restart = u8"\u7acb\u5373\u91cd\u542f";
		this->offset_result_fail_title = u8"\u83b7\u53d6\u5931\u8d25";
		this->offset_result_fail_body = u8"\u504f\u79fb\u503c\u83b7\u53d6\u5931\u8d25\uff0cDMA \u8fde\u63a5\u5c06\u6062\u590d\u3002";
		this->offset_result_fail_ok = u8"\u786e\u5b9a";

		this->status_dma_init = u8"\u521d\u59cb\u5316DMA...";
		this->status_dma_failed = u8"DMA\u8fde\u63a5\u5931\u8d25!";
		this->status_searching = u8"\u641c\u7d22cs2.exe\u4e2d...";
		this->status_init_game = u8"\u521d\u59cb\u5316\u6e38\u620f\u6570\u636e\u4e2d...";
		this->status_waiting_decrypt = u8"\u7b49\u5f85\u6a21\u5757\u89e3\u5bc6\u4e2d...";
		this->status_unknown = u8"\u672a\u77e5\u72b6\u6001";
	}
};

inline Language lang;
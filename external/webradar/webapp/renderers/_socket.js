// WebRadar BoltObserv 协议适配层 (Phase 3 Task 9)
// 将 CS2-DMA 后端单一 JSON 消息拆分为 BoltObserv 期望的 {type, data} 多事件。
// 传输层：WebSocket (首选) → SSE → HTTP 轮询，带密码鉴权与自动重连。
// 渲染器通过 socket.element.addEventListener(eventName, cb) 监听事件，数据在 event.data。

var socket = {
  element: document.createElement("div")
};

// ---- 内部状态 ----
var state = {
  ws: null,
  es: null,
  pollTimer: null,
  reconnectTimer: null,
  disposed: false,
  backoff: 400,
  transportMode: "ws",      // "ws" | "sse" | "poll"
  numMap: {},               // m_idx(String) -> num (0-9)，跨帧稳定
  nextNum: 0,
  lastRoundPhase: null      // roundend 派发用上一帧 round phase
};

var CONNECTION_TIMEOUT = 5000;
var MAX_BACKOFF = 5000;
var POLL_INTERVAL = 67;

// ---- 事件派发 ----
function dispatchEvent(type, data) {
  var event = new Event(type);
  event.data = data;
  socket.element.dispatchEvent(event);
}

// ---- 密码鉴权 ----
function getStoredPassword() {
  try { return sessionStorage.getItem("webradar_password") || ""; } catch (e) { return ""; }
}
function setStoredPassword(pw) {
  try { sessionStorage.setItem("webradar_password", pw); } catch (e) {}
}
function clearStoredPassword() {
  try { sessionStorage.removeItem("webradar_password"); } catch (e) {}
}

function promptPassword() {
  var pw = prompt("WebRadar password:");
  if (pw === null) return false; // 用户取消
  setStoredPassword(pw);
  return true;
}

function checkAuth() {
  return fetch("/cs2_auth_status")
    .then(function (r) { return r.json(); })
    .then(function (data) { return !!data.password_required; })
    .catch(function () { return false; });
}

// ---- 协议适配：单一 JSON → 多事件 ----
// 投掷物队伍映射：2=T, 3=CT, 其他=U
function mapTeam(teamNum) {
  if (teamNum === 3) return "CT";
  if (teamNum === 2) return "T";
  return "U";
}

function handleBackendMessage(raw) {
  var data;
  try { data = JSON.parse(raw); } catch (e) {
    console.error("[WebRadar] JSON parse failed:", e.message);
    return;
  }
  if (!data) return;

  // 存储 m_calibration 校准数据供 positionToPerc 使用
  if (data.m_calibration) {
    global.calibration = data.m_calibration;
  }

  try {
    // 1. map 事件
    if (data.m_map) {
      dispatchEvent("map", data.m_map);
    }

    // 1.5. round 事件
    if (data.m_round_phase) {
      var phase = data.m_round_phase;
      dispatchEvent("round", phase);
      // Task 17: 同步更新 global.gamePhase
      global.gamePhase = phase;
      // Task 16: canbuy 事件派发（freezetime 可购买，live/over 不可购买）
      dispatchEvent("canbuy", phase === "freezetime");
    }

    var players = data.m_players || [];
    var observedIdx = (typeof data.m_observed_idx === "number") ? data.m_observed_idx : -1;

    // 2. players 事件
    var playersOut = [];
    var bombPlayer = null;
    for (var i = 0; i < players.length; i++) {
      var p = players[i];
      var idxKey = String(p.m_idx);
      // num 分配：新玩家分配下一个 0-9 循环 num，已存在则复用
      if (state.numMap[idxKey] === undefined) {
        state.numMap[idxKey] = state.nextNum;
        state.nextNum = (state.nextNum + 1) % 10;
      }
      if (p.m_has_bomb && !bombPlayer) bombPlayer = p;

      // active：observed_idx>=0 用观察目标，否则用 m_is_local
      var active = (observedIdx >= 0) ? (i === observedIdx) : !!p.m_is_local;
      var weapons = p.m_weapons || {};
      var activeWeapon = weapons.m_active || "";
      var ammo = {};
      if (activeWeapon) {
        var clip = p.m_ammo_clip;
        // Task 5: m_ammo_clip < 0 表示无效数据（武器切换/读取失败），
        // 不更新 ammo，避免前端误判为弹药用尽而触发开火提示
        if (typeof clip === "number" && clip >= 0) {
          ammo[activeWeapon] = clip;
        }
      }
      var pos = p.m_position || { x: 0, y: 0, z: 0 };

      playersOut.push({
        num: state.numMap[idxKey],
        id: idxKey,
        // Task 7: 传递 isLocal 标志供前端区分敌我
        isLocal: !!p.m_is_local,
        team: (p.m_team === 3) ? "CT" : (p.m_team === 2) ? "T" : "",
        // CS2 游戏内玩家颜色 (0-5)，供前端 color0-5 类使用
        color: (typeof p.m_color === "number") ? p.m_color : -1,
        health: p.m_health,
        armor: p.m_armor || 0,
        money: p.m_money || 0,
        hasHelmet: !!p.m_has_helmet,
        hasDefuser: !!p.m_has_defuser,
        weapons: p.m_weapons || {},
        bomb: !!p.m_has_bomb,
        active: active,
        flashed: p.m_flashed || 0,
        ammo: ammo,
        position: { x: pos.x, y: pos.y, z: pos.z },
        angle: p.m_eye_angle || 0,
        name: p.m_name || ""
      });
    }
    dispatchEvent("players", { players: playersOut });

    // 3. bomb 状态计算
    var bombData = data.m_bomb;
    var bombEvent = null;
    if (bombPlayer) {
      // 玩家携弹 → carried 或 planting，位置用携弹者位置
      var cpos = bombPlayer.m_position || { x: 0, y: 0, z: 0 };
      var bombState = "carried";
      // 后端 m_is_planting 为 true 时，表示携弹者正在安放
      if (bombData && bombData.m_is_planting) {
        bombState = "planting";
      }
      bombEvent = {
        state: bombState,
        position: { x: cpos.x, y: cpos.y, z: cpos.z },
        player: String(bombPlayer.m_idx),
        countdown: 0
      };
    } else if (bombData) {
      var bpos = { x: bombData.x, y: bombData.y, z: bombData.z };
      var blowTime = bombData.m_blow_time;
      if (blowTime !== undefined && blowTime !== null) {
        // 有 m_blow_time 表示已 planted
        if (blowTime <= 0) {
          bombEvent = { state: "exploded", position: bpos, player: "", countdown: 0 };
        } else if (bombData.m_is_defused) {
          bombEvent = { state: "defused", position: bpos, player: "", countdown: 0 };
        } else if (bombData.m_is_defusing) {
          var defuserHandle = bombData.m_defuser_handle || 0;
          bombEvent = {
            state: "defusing",
            position: bpos,
            player: defuserHandle ? String(defuserHandle) : "",
            countdown: blowTime,
            defuseTime: bombData.m_defuse_time || 0
          };
        } else {
          bombEvent = { state: "planted", position: bpos, player: "", countdown: blowTime };
        }
      } else {
        // m_bomb 存在但无 m_blow_time，且无玩家携弹 → 掉落
        bombEvent = { state: "dropped", position: bpos, player: "", countdown: 0 };
      }
    }

    // Task 18: 先派发 bomb，再派发 roundend（避免 roundend 重置后 bomb 用旧状态覆盖）
    if (bombEvent) {
      dispatchEvent("bomb", bombEvent);
    }

    // roundend 派发：round phase 从 "over" 变为 "freezetime" 或 "live" 时触发
    if (data.m_round_phase) {
      var newPhase = data.m_round_phase;
      if (state.lastRoundPhase && state.lastRoundPhase == "over" &&
        (newPhase == "freezetime" || newPhase == "live")) {
        dispatchEvent("roundend");
      }
      state.lastRoundPhase = newPhase;
    }

    // 4. 投掷物拆分：smoke / inferno / flash / explosive+decoy
    var projectiles = data.m_projectiles || [];
    var smokes = [];
    var infernos = [];
    var flashbangs = [];
    var projs = [];
    for (var k = 0; k < projectiles.length; k++) {
      var pr = projectiles[k];
      var ppos = pr.m_position || { x: 0, y: 0, z: 0 };
      var pposOut = { x: ppos.x, y: ppos.y, z: ppos.z };
      var type = pr.m_type;
      // 稳定实体 ID（后端 EntityAddr 低 32 位），避免位置变化时创建重复 DOM 元素
      var stableId = pr.m_entity_id || ("idx_" + k);
      // 后端 m_is_effect=true 表示已爆开，作为特效渲染；否则作为飞行中投掷物渲染
      var isEffect = pr.m_is_effect === true;
      if (type === "smoke") {
        if (isEffect) {
          // 已爆开：推送给烟雾特效渲染器
          smokes.push({
            id: stableId,
            team: mapTeam(pr.m_team),
            position: pposOut,
            time: 20 - (pr.m_life_remaining || 0) // 烟雾已存在时间，上限 20s
          });
        } else {
          // 飞行中：推送给投掷物图标渲染器，显示实时位置
          projs.push({
            id: "smoke_" + stableId,
            type: "smoke",
            team: mapTeam(pr.m_team),
            position: pposOut
          });
        }
      } else if (type === "inferno") {
        if (isEffect) {
          // 已爆开：推送给火焰特效渲染器，使用后端 m_flames 数组
          var flames = (pr.m_flames || []).map(function (f) {
            return { x: f.x, y: f.y, z: f.z };
          });
          infernos.push({
            id: stableId,
            flamesNum: flames.length > 0 ? flames.length : 1,
            flamesPosition: flames.length > 0 ? flames : [pposOut],
            lifeRemaining: pr.m_life_remaining || 0
          });
        } else {
          // 飞行中：推送给投掷物图标渲染器（type 用 firebomb 匹配图片资源名）
          projs.push({
            id: "inferno_" + stableId,
            type: "firebomb",
            team: mapTeam(pr.m_team),
            position: pposOut
          });
        }
      } else if (type === "flash") {
        if (isEffect) {
          // 已爆开：推送给闪光特效渲染器
          var maxDuration = pr.maxDuration || 2.5;
          var lifetime = maxDuration - (pr.m_life_remaining || 0);
          if (lifetime >= 1.4) {
            flashbangs.push({
              id: stableId,
              position: pposOut
            });
          }
        } else {
          // 飞行中：推送给投掷物图标渲染器（type 用 flashbang 匹配图片资源名）
          projs.push({
            id: "flash_" + stableId,
            type: "flashbang",
            team: mapTeam(pr.m_team),
            position: pposOut
          });
        }
      } else if (type === "explosive") {
        // HE 手雷始终作为飞行中投掷物渲染
        projs.push({
          id: "frag_" + stableId,
          type: "frag",
          team: mapTeam(pr.m_team),
          position: pposOut
        });
      } else if (type === "decoy") {
        // 诱饵弹作为飞行中投掷物渲染（使用 frag 图标作为回退）
        projs.push({
          id: "decoy_" + stableId,
          type: "frag",
          team: mapTeam(pr.m_team),
          position: pposOut
        });
      }
    }
    dispatchEvent("smokes", smokes);
    dispatchEvent("infernos", infernos);
    dispatchEvent("flashbangs", flashbangs);
    dispatchEvent("projectiles", projs);

    // 5. spectators 事件（观战列表）
    // 后端仅在 Spectators 非空时序列化 m_spectators，这里始终派发
    // （空数组时渲染器隐藏面板），与 projectiles 等事件行为一致。
    var spectatorsIn = data.m_spectators || [];
    var spectatorsOut = [];
    for (var m = 0; m < spectatorsIn.length; m++) {
      var sp = spectatorsIn[m];
      spectatorsOut.push({
        name: sp.m_name || "Unknown",
        team: mapTeam(sp.m_team),
        observerMode: sp.m_observer_mode || 0
      });
    }
    dispatchEvent("spectators", spectatorsOut);
  } catch (e) {
    console.error("[WebRadar] handleBackendMessage error:", e.message, e.stack);
  }
}

// ---- 传输层 1：WebSocket（首选）----
function connectWebSocket() {
  if (state.disposed) return;
  state.transportMode = "ws";
  var proto = window.location.protocol === "https:" ? "wss:" : "ws:";
  var pw = getStoredPassword();
  var wsUrl = proto + "//" + window.location.host + "/cs2_webradar";
  if (pw) wsUrl += "?password=" + encodeURIComponent(pw);

  var ws;
  try {
    ws = new WebSocket(wsUrl);
  } catch (e) {
    fallback();
    return;
  }
  state.ws = ws;

  var timeout = setTimeout(function () {
    if (ws.readyState === WebSocket.CONNECTING) ws.close();
  }, CONNECTION_TIMEOUT);

  ws.onopen = function () {
    clearTimeout(timeout);
    state.backoff = 400;
  };

  ws.onclose = function (event) {
    clearTimeout(timeout);
    if (event.code === 1000) {
      // 正常关闭 → 重连 WebSocket
      scheduleReconnect();
      return;
    }
    // 异常关闭：检查是否密码问题，否则降级
    checkAuth().then(function (required) {
      if (required && !getStoredPassword()) {
        if (promptPassword()) connectWebSocket();
      } else {
        fallback();
      }
    });
  };

  ws.onerror = function () {
    clearTimeout(timeout);
  };

  ws.onmessage = function (event) {
    var raw = event.data;
    if (typeof raw === "string") {
      handleBackendMessage(raw);
    } else if (raw && typeof raw.text === "function") {
      // Blob 帧
      raw.text().then(handleBackendMessage);
    }
  };
}

// ---- 传输层 2：SSE（降级 1）----
function connectSSE() {
  if (state.disposed) return;
  state.transportMode = "sse";
  var es;
  try {
    es = new EventSource("/api/stream");
  } catch (e) {
    connectPolling();
    return;
  }
  state.es = es;
  es.onopen = function () {
    state.backoff = 400;
  };
  es.onmessage = function (e) { handleBackendMessage(e.data); };
  es.onerror = function () {
    if (state.es) { state.es.close(); state.es = null; }
    if (!state.disposed) connectPolling();
  };
}

// ---- 传输层 3：HTTP 轮询（降级 2）----
function connectPolling() {
  if (state.disposed) return;
  state.transportMode = "poll";
  if (state.pollTimer) clearInterval(state.pollTimer);
  state.pollTimer = setInterval(function () {
    fetch("/api/live")
      .then(function (r) { return r.text(); })
      .then(handleBackendMessage)
      .catch(function () {});
  }, POLL_INTERVAL);
}

// 降级：ws → sse（带指数退避），sse → poll
function fallback() {
  if (state.disposed) return;
  if (state.transportMode === "ws") {
    state.reconnectTimer = setTimeout(function () {
      if (!state.disposed) connectSSE();
    }, state.backoff);
    state.backoff = Math.min(state.backoff * 2, MAX_BACKOFF);
  } else if (state.transportMode === "sse") {
    connectPolling();
  }
}

// WebSocket 正常关闭后的重连
function scheduleReconnect() {
  if (state.disposed) return;
  state.reconnectTimer = setTimeout(connectWebSocket, 3000);
}

// ---- 启动 ----
function start() {
  checkAuth().then(function (required) {
    if (required && !getStoredPassword()) {
      if (promptPassword()) connectWebSocket();
      // 用户取消密码输入，不连接
    } else {
      connectWebSocket();
    }
  });
}

window.socket = socket;
start();

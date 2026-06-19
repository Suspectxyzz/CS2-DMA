import { useEffect, useState, useRef, useMemo } from "react";
import "./App.css";
import PlayerCard from "./components/PlayerCard";
import Radar from "./components/Radar";
import { getLatency, Latency } from "./components/latency";
import MaskedIcon from "./components/maskedicon";
import { getT } from "./utilities/i18n";

const CONNECTION_TIMEOUT = 5000;

const getWsProtocol = () => window.location.protocol === "https:" ? "wss:" : "ws:";

const detectLanguage = () => {
  const lang = navigator.language || navigator.userLanguage || "cn";
  return lang.startsWith("zh") ? "cn" : "en";
};

const DEFAULT_SETTINGS = {
  dotSize: 1,
  bombSize: 0.5,
  showAllNames: false,
  showEnemyNames: true,
  showViewCones: false,
  viewConeSize: 1,
  radarZoom: 1,
  showDeadPlayers: true,
  deadPlayerOpacity: 0.4,
  showHealth: false,
  showWeapon: true,
  showPlayerCards: true,
  infoTextSize: 1,
  showBombTimer: true,
  showLatency: true,
  bgOpacity: 0.95,
  smoothTransition: true,
  language: "",
  manualRotation: 0,
  autozoom: false,
  vertIndicator: "none",
};

const loadSettings = () => {
  const savedSettings = localStorage.getItem("radarSettings");
  const parsed = savedSettings ? { ...DEFAULT_SETTINGS, ...JSON.parse(savedSettings) } : { ...DEFAULT_SETTINGS };
  if (!parsed.language) parsed.language = detectLanguage();
  return parsed;
};

const App = () => {
  const [averageLatency, setAverageLatency] = useState(0);
  const [playerArray, setPlayerArray] = useState([]);
  const [mapData, setMapData] = useState();
  const [localTeam, setLocalTeam] = useState();
  const [bombData, setBombData] = useState();
  const [projectiles, setProjectiles] = useState([]);
  const [spectators, setSpectators] = useState([]);
  const [calibration, setCalibration] = useState(null);
  const [settings, setSettings] = useState(loadSettings());
  const [passwordRequired, setPasswordRequired] = useState(false);
  const [passwordInput, setPasswordInput] = useState("");
  const [passwordError, setPasswordError] = useState(false);
  const [passwordConnecting, setPasswordConnecting] = useState(false);
  const currentMapRef = useRef(null);
  const t = useMemo(() => getT(settings.language || "cn"), [settings.language]);

  const mapRotation = useMemo(() => settings.manualRotation || 0, [settings.manualRotation]);

  useEffect(() => {
    localStorage.setItem("radarSettings", JSON.stringify(settings));
  }, [settings]);

  useEffect(() => {
    let ws = null;
    let eventSource = null;
    let pollInterval = null;
    let reconnectTimer = null;
    let disposed = false;
    let backoff = 400;
    let transportMode = "ws"; // "ws" | "sse" | "poll"

    const MAX_BACKOFF = 5000;

    // Shared message handler — used by WebSocket, SSE, and polling.
    const handleMessage = (raw) => {
      try {
        setAverageLatency(getLatency());
        const parsedData = JSON.parse(raw);
        setPlayerArray(parsedData.m_players);
        setLocalTeam(parsedData.m_local_team);
        setBombData(parsedData.m_bomb);
        setProjectiles(parsedData.m_projectiles || []);
        setSpectators(parsedData.m_spectators || []);
        setCalibration(parsedData.m_calibration || null);

        const map = parsedData.m_map;
        if (map !== "invalid" && map !== currentMapRef.current) {
          if (!/^[a-zA-Z0-9_-]+$/.test(map)) {
            console.error("[WebRadar] Invalid map name rejected");
            return;
          }
          currentMapRef.current = map;
          fetch(`data/${map}/data.json`)
            .then((r) => r.json())
            .then((mapJson) => {
              setMapData({ ...mapJson, name: map });
              document.body.style.backgroundImage = `url(./data/${map}/background.png)`;
            })
            .catch((fetchErr) => {
              console.error("[WebRadar] failed to load map data:", fetchErr);
            });
        }
      } catch (e) {
        console.error("[WebRadar] message parse error:", e);
      }
    };

    // --- Transport 3: HTTP polling ---
    const startPolling = () => {
      if (disposed) return;
      transportMode = "poll";
      console.info("[WebRadar] falling back to HTTP polling");
      pollInterval = setInterval(() => {
        fetch("/api/live")
          .then((r) => r.text())
          .then(handleMessage)
          .catch(() => {});
      }, 67);
    };

    // --- Transport 2: SSE ---
    const startSSE = () => {
      if (disposed) return;
      transportMode = "sse";
      console.info("[WebRadar] falling back to SSE");
      try {
        eventSource = new EventSource("/api/stream");
      } catch (e) {
        console.error("[WebRadar] SSE constructor error:", e);
        startPolling();
        return;
      }
      eventSource.onopen = () => { backoff = 400; };
      eventSource.onmessage = (e) => handleMessage(e.data);
      eventSource.onerror = () => {
        if (eventSource) eventSource.close();
        eventSource = null;
        if (!disposed) startPolling();
      };
    };

    // --- Transport 1: WebSocket ---
    const connect = () => {
      if (disposed) return;
      transportMode = "ws";
      console.info("[WebRadar] connecting via WebSocket...");

      const proto = getWsProtocol();
      const storedPassword = sessionStorage.getItem("webradar_password") || "";
      const wsUrl = storedPassword
        ? `${proto}//${window.location.host}/cs2_webradar?password=${encodeURIComponent(storedPassword)}`
        : `${proto}//${window.location.host}/cs2_webradar`;

      try { ws = new WebSocket(wsUrl); } catch (e) {
        console.error("[WebRadar] WebSocket constructor error:", e);
        fallback();
        return;
      }

      const connectionTimeout = setTimeout(() => {
        if (ws && ws.readyState === WebSocket.CONNECTING) ws.close();
      }, CONNECTION_TIMEOUT);

      ws.onopen = () => {
        clearTimeout(connectionTimeout);
        backoff = 400;
        console.info("[WebRadar] connected via WebSocket");
        setPasswordError(false);
        setPasswordConnecting(false);
        const el = document.getElementsByClassName("radar_message")[0];
        if (el) el.textContent = t("connected");
      };

      ws.onclose = (event) => {
        clearTimeout(connectionTimeout);
        if (event.code === 1000) {
          console.warn("[WebRadar] disconnected, reconnecting...");
          scheduleReconnect();
          return;
        }
        // Abnormal closure — check auth, then fall back
        fetch("/cs2_auth_status").then(r => r.json()).then(data => {
          if (data.password_required && !sessionStorage.getItem("webradar_password")) {
            setPasswordRequired(true);
            setPasswordError(true);
            sessionStorage.removeItem("webradar_password");
            setPasswordConnecting(false);
          } else {
            fallback();
          }
        }).catch(() => { fallback(); });
        setPasswordConnecting(false);
      };

      ws.onerror = (error) => {
        clearTimeout(connectionTimeout);
        console.error("[WebRadar] error:", error);
        setPasswordConnecting(false);
      };

      ws.onmessage = async (event) => {
        const raw = typeof event.data === "string" ? event.data : await event.data.text();
        handleMessage(raw);
      };
    };

    // Fall back from WebSocket to SSE, or SSE to polling, with backoff.
    const fallback = () => {
      if (disposed) return;
      if (transportMode === "ws") {
        reconnectTimer = setTimeout(() => {
          if (!disposed) startSSE();
        }, backoff);
        backoff = Math.min(backoff * 2, MAX_BACKOFF);
      } else if (transportMode === "sse") {
        startPolling();
      }
    };

    const scheduleReconnect = () => {
      if (disposed) return;
      reconnectTimer = setTimeout(connect, 3000);
    };

    const checkAuthAndConnect = async () => {
      try {
        const resp = await fetch("/cs2_auth_status");
        const data = await resp.json();
        if (data.password_required) {
          const storedPassword = sessionStorage.getItem("webradar_password");
          if (storedPassword) {
            connect();
          } else {
            setPasswordRequired(true);
          }
        } else {
          connect();
        }
      } catch (e) {
        console.warn("[WebRadar] auth status check failed, connecting directly");
        connect();
      }
    };

    checkAuthAndConnect();

    return () => {
      disposed = true;
      clearTimeout(reconnectTimer);
      if (ws) ws.close();
      if (eventSource) eventSource.close();
      if (pollInterval) clearInterval(pollInterval);
    };
  }, []);

  const handlePasswordSubmit = (e) => {
    e.preventDefault();
    if (!passwordInput.trim()) return;
    setPasswordConnecting(true);
    setPasswordError(false);
    sessionStorage.setItem("webradar_password", passwordInput);

    const proto = getWsProtocol();
    const wsUrl = `${proto}//${window.location.host}/cs2_webradar?password=${encodeURIComponent(passwordInput)}`;
    let testWs = null;
    try { testWs = new WebSocket(wsUrl); } catch (err) {
      setPasswordError(true);
      setPasswordConnecting(false);
      setPasswordRequired(true);
      return;
    }

    const timeout = setTimeout(() => {
      if (testWs && testWs.readyState === WebSocket.CONNECTING) testWs.close();
    }, CONNECTION_TIMEOUT);

    testWs.onopen = () => {
      clearTimeout(timeout);
      setPasswordError(false);
      setPasswordConnecting(false);
      window.location.reload();
    };

    testWs.onerror = () => {
      clearTimeout(timeout);
      setPasswordError(true);
      setPasswordConnecting(false);
      setPasswordRequired(true);
      sessionStorage.removeItem("webradar_password");
    };

    testWs.onclose = (event) => {
      clearTimeout(timeout);
      if (event.code !== 1000) {
        setPasswordError(true);
        sessionStorage.removeItem("webradar_password");
      }
      setPasswordConnecting(false);
      setPasswordRequired(true);
    };
  };

  if (passwordRequired) {
    return (
      <div className="w-screen h-screen flex items-center justify-center"
        style={{
          background: "radial-gradient(50% 50% at 50% 50%, rgba(20, 40, 55, 0.98) 0%, rgba(7, 20, 30, 0.98) 100%)",
        }}
      >
        <div className="flex flex-col items-center gap-6 p-8 rounded-xl"
          style={{ background: "rgba(20, 30, 45, 0.9)", border: "1px solid rgba(124, 92, 252, 0.3)" }}
        >
          <div className="text-2xl font-bold text-white">{t("password_title")}</div>
          <form onSubmit={handlePasswordSubmit} className="flex flex-col items-center gap-4">
            <input
              type="text"
              value={passwordInput}
              onChange={(e) => { setPasswordInput(e.target.value); setPasswordError(false); }}
              placeholder={t("password_placeholder")}
              className="px-4 py-2 rounded-lg text-white outline-none w-64"
              style={{ background: "rgba(10, 15, 25, 0.8)", border: passwordError ? "1px solid #ef4444" : "1px solid rgba(124, 92, 252, 0.4)" }}
              autoFocus
              disabled={passwordConnecting}
            />
            {passwordError && (
              <div className="text-red-400 text-sm">{t("password_wrong")}</div>
            )}
            <button
              type="submit"
              className="px-6 py-2 rounded-lg text-white font-medium transition-colors"
              style={{ background: "rgba(124, 92, 252, 0.8)" }}
              disabled={passwordConnecting || !passwordInput.trim()}
            >
              {passwordConnecting ? t("password_connecting") : t("password_submit")}
            </button>
          </form>
        </div>
      </div>
    );
  }

  return (
    <div className="w-screen h-screen flex flex-col"
      style={{
        background: `radial-gradient(50% 50% at 50% 50%, rgba(20, 40, 55, ${settings.bgOpacity ?? 0.95}) 0%, rgba(7, 20, 30, ${settings.bgOpacity ?? 0.95}) 100%)`,
        backdropFilter: `blur(7.5px)`,
      }}
    >
      <div className={`w-full h-full flex flex-col justify-center overflow-auto relative`}>
        {settings.showBombTimer !== false && bombData && bombData.m_blow_time > 0 && !bombData.m_is_defused && (
          <div className={`absolute left-1/2 -translate-x-1/2 top-2 flex-col items-center gap-1 z-50`}>
            <div className={`flex justify-center items-center gap-1`}>
              <MaskedIcon
                path={`./assets/icons/c4_sml.png`}
                height={32}
                color={
                  (bombData.m_is_defusing &&
                    bombData.m_blow_time - bombData.m_defuse_time > 0 &&
                    `bg-radar-green`) ||
                  (bombData.m_blow_time - bombData.m_defuse_time < 0 &&
                    `bg-radar-red`) ||
                  `bg-radar-secondary`
                }
              />
              <span>{`${bombData.m_blow_time.toFixed(1)}s ${(bombData.m_is_defusing &&
                `(${bombData.m_defuse_time.toFixed(1)}s)`) ||
                ""
                }`}</span>
            </div>
          </div>
        )}

        <div className={`flex items-center justify-evenly`}>
          <Latency
            value={averageLatency}
            settings={settings}
            setSettings={setSettings}
            t={t}
            showLatency={settings.showLatency}
          />

          {settings.showPlayerCards && (
            <ul id="terrorist" className="lg:flex hidden flex-col gap-7 m-0 p-0">
              {playerArray
                .filter((player) => player.m_team == 2)
                .sort((a, b) => a.m_idx - b.m_idx)
                .map((player) => (
                  <PlayerCard
                    isOnRightSide={false}
                    key={player.m_idx}
                    playerData={player}
                    settings={settings}
                    t={t}
                  />
                ))}
            </ul>
          )}

          {(playerArray.length > 0 && mapData && (
            <Radar
              playerArray={playerArray}
              radarImage={`./data/${mapData.name}/radar.png`}
              mapData={mapData}
              localTeam={localTeam}
              averageLatency={averageLatency}
              bombData={bombData}
              projectiles={projectiles}
              settings={settings}
              mapRotation={mapRotation}
              calibration={calibration}
              t={t}
            />
          )) || (
              <div id="radar" className={`relative overflow-hidden origin-center`}>
                <h1 className="radar_message">
                  {t("connecting")}
                </h1>
              </div>
            )}

          {settings.showPlayerCards && (
            <ul
              id="counterTerrorist"
              className="lg:flex hidden flex-col gap-7 m-0 p-0"
            >
              {playerArray
                .filter((player) => player.m_team == 3)
                .sort((a, b) => a.m_idx - b.m_idx)
                .map((player) => (
                  <PlayerCard
                    isOnRightSide={true}
                    key={player.m_idx}
                    playerData={player}
                    settings={settings}
                  />
                ))}
            </ul>
          )}
        </div>
      </div>
    </div>
  );
};

export default App;

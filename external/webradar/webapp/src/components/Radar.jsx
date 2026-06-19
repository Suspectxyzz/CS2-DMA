import { useRef } from "react";
import Player from "./player";
import Bomb from "./bomb";
import Projectiles from "./Projectiles";

const Radar = ({
  playerArray,
  radarImage,
  mapData,
  localTeam,
  averageLatency,
  bombData,
  projectiles,
  settings,
  mapRotation = 0,
}) => {
  const radarImageRef = useRef();
  const zoom = settings.radarZoom ?? 1;

  return (
    <div id="radar" className={`relative origin-center flex items-center justify-center`}>
      <div className="relative" style={{
        zoom: zoom,
        transform: mapRotation ? `rotate(${mapRotation}deg)` : undefined,
        transition: settings.smoothTransition !== false ? 'transform 150ms linear' : 'none',
      }}>
        <img ref={radarImageRef} className={`max-h-[90vh] w-auto h-auto`} src={radarImage} />

        <Projectiles
          projectiles={projectiles}
          radarImage={radarImageRef.current}
          mapData={mapData}
          settings={settings}
        />

        {playerArray.map((player) => (
          <Player
            key={player.m_idx}
            playerData={player}
            mapData={mapData}
            radarImage={radarImageRef.current}
            localTeam={localTeam}
            averageLatency={averageLatency}
            settings={settings}
            mapRotation={mapRotation}
          />
        ))}

        {bombData && (
          <Bomb
            bombData={bombData}
            mapData={mapData}
            radarImage={radarImageRef.current}
            localTeam={localTeam}
            averageLatency={averageLatency}
            settings={settings}
            mapRotation={mapRotation}
          />
        )}
      </div>
    </div>
  );
};

export default Radar;
import { useRef } from "react";
import RadarCanvas from "./RadarCanvas";

const Radar = ({
  playerArray,
  radarImage,
  mapData,
  localTeam,
  bombData,
  projectiles,
  settings,
  mapRotation = 0,
  calibration,
}) => {
  const radarImageRef = useRef();
  // Autozoom container ref — RadarCanvas applies a scale+translate transform
  // here every frame so the whole radar (img + canvas) follows alive players.
  const autozoomRef = useRef();
  const zoom = settings.radarZoom ?? 1;

  return (
    <div id="radar" className={`relative origin-center flex items-center justify-center`}>
      <div
        ref={autozoomRef}
        className="relative"
        style={{
          transformOrigin: "center center",
          transition: settings.smoothTransition !== false ? "transform 120ms linear" : "none",
        }}
      >
        <div className="relative" style={{
          zoom: zoom,
          transform: mapRotation ? `rotate(${mapRotation}deg)` : undefined,
          transition: settings.smoothTransition !== false ? 'transform 150ms linear' : 'none',
        }}>
          <img ref={radarImageRef} className={`max-h-[90vh] w-auto h-auto`} src={radarImage} />

          <RadarCanvas
            playerArray={playerArray}
            bombData={bombData}
            projectiles={projectiles}
            mapData={mapData}
            localTeam={localTeam}
            settings={settings}
            mapRotation={mapRotation}
            calibration={calibration}
            radarImageRef={radarImageRef}
            autozoomRef={autozoomRef}
          />
        </div>
      </div>
    </div>
  );
};

export default Radar;

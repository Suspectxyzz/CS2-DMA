const Projectiles = ({ projectiles, radarImage, mapData, settings }) => {
  if (!radarImage || !projectiles || !projectiles.length) return null;

  const radarImageBounding = {
    width: radarImage.offsetWidth,
    height: radarImage.offsetHeight,
  };

  return (
    <>
      {projectiles.map((projectile, index) => {
        const pos = projectile.m_position;
        if (!pos || pos.x == null || pos.y == null) return null;

        const type = projectile.m_type;
        const life = projectile.m_life_remaining;
        const x = radarImageBounding.width * pos.x;
        const y = radarImageBounding.height * pos.y;
        const key = `${type}-${index}`;

        if (type === "smoke") {
          const isFadingOut = life != null && life < 2;
          return (
            <div
              key={key}
              className={`projectile-smoke${isFadingOut ? " smoke-fadeout" : ""}`}
              style={{ left: `${x}px`, top: `${y}px` }}
            />
          );
        }

        if (type === "inferno") {
          return (
            <div
              key={key}
              className="projectile-inferno"
              style={{ left: `${x}px`, top: `${y}px` }}
            />
          );
        }

        if (type === "flash") {
          return (
            <div
              key={key}
              className="projectile-flash"
              style={{ left: `${x}px`, top: `${y}px` }}
            />
          );
        }

        if (type === "explosive") {
          const isExploding = life != null && life < 0.5;
          return (
            <div
              key={key}
              className={`projectile-explosive${isExploding ? " explosive-blast" : ""}`}
              style={{ left: `${x}px`, top: `${y}px` }}
            />
          );
        }

        if (type === "decoy") {
          return (
            <div
              key={key}
              className="projectile-decoy"
              style={{ left: `${x}px`, top: `${y}px` }}
            />
          );
        }

        return null;
      })}
    </>
  );
};

export default Projectiles;

export const getSplitIndex = (mapData, z) => {
  if (!mapData?.splits || z == null) return -1;
  for (let i = 0; i < mapData.splits.length; i++) {
    const split = mapData.splits[i];
    if (z > split.bounds.bottom && z < split.bounds.top) {
      return i;
    }
  }
  return -1;
};

export const getRadarPosition = (mapData, entityCoords) => {
  if (entityCoords.x == null || entityCoords.y == null) {
    return { x: 0, y: 0 };
  }

  if (mapData.x == null || mapData.y == null) {
    return { x: 0, y: 0 };
  }

  const position = {
    x: (entityCoords.x - mapData.x) / mapData.scale / 1024,
    y: (((entityCoords.y - mapData.y) / mapData.scale) * -1.0) / 1024,
  };

  if (mapData.splits && entityCoords.z != null) {
    for (const split of mapData.splits) {
      if (entityCoords.z > split.bounds.bottom && entityCoords.z < split.bounds.top) {
        position.x += split.offset.x / 100;
        position.y -= split.offset.y / 100;
        break;
      }
    }
  }

  return position;
};

export const playerColors = [
  // blue
  "#84c8ed",

  // green
  "#009a7d",

  // yellow
  "#eadd40",

  // orange
  "#df7d29",

  // purple
  "#b72b92",

  // white
  "#ffffff",
];

export const teamEnum = {
  none: 0,
  spectator: 1,
  terrorist: 2,
  counterTerrorist: 3,
};

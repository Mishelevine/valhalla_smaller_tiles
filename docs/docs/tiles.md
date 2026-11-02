# Tile Specifications

## Introduction

A **tile** represents a small rectangular section of a geographic area. The entire map or geographic region is divided into a grid of square tiles.

Tiles are _files_ in a specific format. They contain information necessary for routing: road network data, access restrictions, administrative information and so on.

## Hierarchies/Levels

Tiles are arranged into a hierarchy with three levels.

| Hierarchy level | Tile size, degrees | Content |
|---|---|---|
| 0 | 2° | Highway roads: motorway, trunk and primary. |
| 1 | 0.5° | Arterial roads: secondary and tertiary. |
| 2 | 0.125° | Local roads: unclassified, residential, service or other. |

At each level, the world is split into square tiles with a specific size using the bounding box `(-180, -90, 180, 90)`. The rows and columns start from the _bottom left_ and increase to the top right - tiles are row ordered increasing from west to east.

## From latitude and longitude to a tile ID

Let `S` be the tile size for the selected hierarchy level and `C = 360 / S` the
number of columns. For a point `(lat, lon)` inside the world bounds, calculate:

```text
column  = floor((lon + 180) / S)
row     = floor((lat + 90) / S)
tile_id = row * C + column
```

`tile_id` is therefore a row-major index: tile `0` begins at `(-180, -90)`, and
the index first increases eastward, then continues at the western edge of the next
northern row. At the inclusive east or north world boundary (`lon == 180` or
`lat == 90`), Valhalla uses the last valid column or row rather than a nonexistent
one beyond the grid.

For example, at level 2 `S = 0.125` and `C = 2880`. For `(lat=52.09, lon=5.12)`:

```text
column  = floor((5.12 + 180) / 0.125) = 1480
row     = floor((52.09 + 90) / 0.125) = 1136
tile_id = 1136 * 2880 + 1480 = 3273160
```

This tile covers longitude `[5.000, 5.125)` and latitude `[52.000, 52.125)`.
The corresponding tile `GraphId` is `(tileid=3273160, level=2, id=0)`; the last
component is zero because it identifies the tile itself, not an edge or node in it.

### From a tile ID to a graph-tile filename

The tile is stored beneath `mjolnir.tile_dir`. `GraphTile::FileSuffix` pads the
tile ID to the level's fixed width and separates it into groups of three digits.
For the example above, the suffix is `2/003/273/160.gph`, so its complete path is:

```text
<mjolnir.tile_dir>/2/003/273/160.gph
```

The intermediate numeric groups are directories only; they bound the number of
files in each directory. They do not encode latitude and longitude separately.

### The World at Level 0

The following image shows the world at level 0.

![Level 0](images/world_level0.png)
Image generated using <https://geojson.io>

Using bounding boxes for Germany, Pennsylvania, and NYC we can show how the regions would be split up into the 3 levels. Level 0 is colored in light blue. Level 1 is light green and level 2 is light red.

### Germany

![Germany](images/germany.png)
Image generated using <https://geojson.io>

### Pennsylvania

![Pennsylvania](images/pennsylvania.png)
Image generated using <https://geojson.io>

### NYC

![NYC](images/nyc.png)
Image generated using <https://geojson.io>

## Sample Tile Code

Below are some sample functions to help you obtain latitude and longitude coordinates, levels, tile ids, and lists of tiles that intersect a bounding box.

### Working with `GraphId`

See [`GraphId` page](baldr.md#code) for relevant functions.

### Working with Latitude and Longitude Coordinates

```python linenums="1"
from graphid import get_hierarchy_level, get_tile_index

level_to_size = {
    0: 2.0,
    1: 0.5,
    2: 0.125,
}


def get_latlon(graph_id: int) -> tuple[float, float]:
    """Latitude and longitude of tile's bottom-left corner."""

    hierarchy_level = get_hierarchy_level(graph_id)
    size: float = level_to_size[hierarchy_level]
    total_columns = int(360 / size)
    tile_index = get_tile_index(graph_id)
    lat = (tile_index // total_columns) * size - 90
    lon = (tile_index % total_columns) * size - 180

    return lat, lon


def get_tile_index_from_latlon(
    hierarchy_level: int,
    lat: float,
    lon: float,
) -> int:
    assert -90 <= lat <= 90
    assert -180 <= lon <= 180

    size: float = level_to_size[hierarchy_level]
    total_columns = int(360 / size)
    num_columns = int((lon + 180) / size)
    num_rows = int((lat + 90) / size)

    return num_rows * total_columns + num_columns


def tiles_for_bounding_box(
    left: float,
    bottom: float,
    right: float,
    top: float,
) -> list[tuple[int, int]]:
    """Return a list of tiles that intersect the bounding box."""

    assert -90 <= bottom <= 90
    assert -90 <= top <= 90
    assert bottom <= top
    assert -180 <= left <= 180
    assert -180 <= right <= 180

    # if this is crossing the anti meridian split it up and combine
    if left > right:
        east = tiles_for_bounding_box(left, bottom, 180.0, top)
        west = tiles_for_bounding_box(-180.0, bottom, right, top)
        return east + west

    # move these so we can compute percentages
    left += 180
    right += 180
    bottom += 90
    top += 90

    tiles = []
    # for each size of tile
    for level, size in level_to_size.items():
        # for each column
        for x in range(int(left / size), int(right / size) + 1):
            # for each row
            for y in range(int(bottom / size), int(top / size) + 1):
                # give back the level and the tile index
                tile_index = int(y * (360.0 / size) + x)
                tiles.append((level, tile_index))

    return tiles
```

```python
>>> get_tile_index_from_latlon(0, 14.601879, 120.972545)
9510
>>> get_tile_index_from_latlon(1, 14.601879, 120.972545)
151081
>>> get_tile_index_from_latlon(2, 41.413203, -73.623787)
3027731
```

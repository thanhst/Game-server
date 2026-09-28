# World coordinates and client pixels

The server stores positions, map dimensions, movement speeds, collision sizes,
and attack ranges in **world units**. World X increases to the right and world
Y increases downward. One world unit is a gameplay distance, not a screen pixel.
For example, speed `100` means 100 world units per second on every display.

`MapDefinition` provides a map's rectangular width and height. `MapInstance`
adds a unique runtime instance ID and tracks which objects are present. Its
bounds include `x = 0..width` and `y = 0..height`. The current map implementation
checks resident body circles against static convex polygon walls, including the
entire path from the old position to the new one. It does not yet handle tiles,
dynamic doors, or collision between two residents.
The position is the object's chosen world anchor (for example, its feet or
center). The client offsets a sprite around that anchor when drawing; the
sprite's top-left pixel is not the object's gameplay position. A future tile
grid can derive a cell with `floor(world.x / tileWidthWorldUnits)` and the
equivalent Y formula.

## Wall geometry

Each wall is a list of world-space points ordered around its boundary (without
repeating the first point at the end). The server validates that it is strictly
convex and inside the map. A triangle,
trapezoid, or sloped wall therefore uses the same collision code. Split a
concave wall into multiple convex polygons; a curved wall can be approximated
by several line segments. The polygon is the gameplay collider, not necessarily
the exact visual outline of the sprite.

```cpp
game::MapDefinition definition{"arena", 100, 80};
definition.walls.push_back({{{40, 10}, {60, 20}, {40, 30}}}); // triangle
```

## One-wall example

```cpp
game::MapDefinition definition{"arena", 100, 80};
definition.walls.push_back(game::ConvexPolygon::rectangle(40, 0, 2, 50));
game::MapInstance map("arena#1", definition);
game::Player hero({{1}, "hero", "arena#1", {10, 20},
    game::Vitality(100), game::Mana(50)});

map.enter(hero);
const auto result = map.move(hero, {50, 20}); // MapResult::Blocked
```

At `PPU = 16`, Unity can draw this wall with a pixel width of `2 * 16 = 32`;
the server still collides with its world polygon spanning `x = 40..42`. The collision
definition is currently supplied in C++ as this example shows. The content
file parser and Unity asset export have not been connected to it yet.

The client camera uses `pixelsPerWorldUnit` (PPU), a viewport in render-target
pixels, and a camera center in world units. The projection is:

```text
screen.x = viewport.left + viewport.width / 2 + (world.x - camera.x) * PPU
screen.y = viewport.top  + viewport.height / 2 + (world.y - camera.y) * PPU

world.x = camera.x + (screen.x - viewport.left - viewport.width / 2) / PPU
world.y = camera.y + (screen.y - viewport.top  - viewport.height / 2) / PPU
```

For a camera at `(100, 100)`, an `800 x 600` viewport beginning at `(100, 50)`,
and `PPU = 16`, the world point `(105, 102)` renders at pixel `(580, 382)`.
`Camera2D::screenToWorld({580, 382})` returns `(105, 102)` with those same camera
settings. Use the camera settings from the frame whose mouse coordinates are
being interpreted. After a resize or zoom, update the viewport/PPU first.
Rasterization may round a projected position to an integer pixel, losing its
fractional detail. Keep the original world position in state; never reconstruct
authoritative movement from a sprite's drawn pixel.

Viewport and pointer coordinates must use the same pixel space. A high-DPI
window may report logical window coordinates while rendering to a larger
framebuffer; convert one side so both refer to the render target. Letterboxing
or a UI panel is represented by the viewport's left/top offset and size.

Changing PPU changes appearance only. World positions, movement speed, attack
range, and collision remain in world units. A larger zoom-out reveals more of
the map, so a competitive game may constrain allowed zoom and visibility, but
it does not rescale gameplay statistics.
The apparent speed is `worldUnitsPerSecond * PPU` pixels per second: a character
moving at `5` world units/second appears to move at `80` pixels/second with
`PPU = 16`. The PPU value here is an example, not a default for the existing
content; choose one base art scale and tune speed, range, and hitbox sizes in
world units against it.

For player input, convert the pointer to a world aim point on the client (or
send an aim direction). Send intent to the server, not a claimed hit or final
position. The server validates movement and attacks against its world state.
Camera projection belongs to the client/view layer; `GameViewMath` is a separate
library so the server simulation does not depend on screen resolution.

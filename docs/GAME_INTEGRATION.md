# Game integration: traversals, intersections, triggers

The scene graph of R4nkine is meant to serve the game, not only the
renderer: the same graph, walked by several traversals, each with its own
masks and callbacks, as in IRIS Performer (APP, CULL, DRAW, ISECT). This
note keeps the design until it is built.

## Why

- `RSArea::getY` is wrong: it takes the nearest vertex, then interpolates in
  the cell that starts at that vertex even when the point lies on the other
  side, and adds 5 at the edges of the blocks (up to 86 units off on
  ARENAMIS). `r4bridge::groundHeight` already computes the height on the
  LVL0 triangles as they are drawn; the game should ask the graph instead.
- libRealSpace tracks its entities and finds by hand the area the player is
  in. The areas exist in the graph with their bounds; entering one should be
  an event.
- libRealSpace has a message bus. Events raised by the graph (an entity
  entering an area, a ray hitting an object) can be posted on that bus, and
  the game logic stays where it is.
- A network game: a server and its clients follow the same game through the
  same graph, each with its own views (a server without rendering, clients
  with their cameras).

## The traversals

| Traversal | Driven by | Purpose |
| --- | --- | --- |
| APP | the game's simulation step (fixed time step) | update callbacks of the nodes, positions pushed by the game, trigger volumes |
| CULL | a camera | what one view draws; never used by the game logic |
| DRAW | the renderer | drawing |
| ISECT | a query | segments and rays against the graph: ground height, collisions, landing, lines of sight, shots |

Each traversal has its own mask, as the shadow pass already has
`RenderOptions::shadowMask`: a node can be drawn but not hit (clouds), hit
but not drawn (a collision hull), or ignored by APP (pure decoration).

## Intersections (ISECT)

- `intersect(root, segment or ray, mask)`: the hit point, its normal, the
  node and the triangle hit, the nearest first or all of them.
- The ground height is a vertical segment against the terrain mask; it
  replaces `RSArea::getY` for the placement of the objects, the physics, the
  AI and the landing.
- The terrain is tested on its finest level (LVL0), whatever level is drawn,
  so the result does not depend on the camera.
- The quadtree of the terrain and the bounds of the nodes prune the search.

## APP traversal and node callbacks

- A node may carry callbacks run before and after the APP traversal of its
  children; they update animations, positions and states.
- APP runs in the simulation step of the game, at its fixed rate, never in
  the drawing of a frame: the result must not depend on the frame rate.
- The game pushes the positions of its entities into the graph in APP; the
  graph only answers queries and raises events, the game decides.

## Trigger volumes

- A trigger node holds a volume: the box of an `AREA` (position, width,
  height), a sphere, or the bounds of its children.
- The game registers trackers (the player, an AI aircraft, a missile).
- In APP, the graph compares the trackers with the volumes and raises
  `onEnter` and `onLeave`; the bridge posts them on the message bus of
  libRealSpace, which starts the scenes of the area as `SCMission` does.
- Visibility is not presence: a trigger never depends on CULL. The player
  can look at a base 20 km away without being in its area.

## Graph layout for the missions

- Spatial: each map object and mission part under the area where it really
  stands (not the area of the scene that brings it); the area nodes then have
  tight bounds, usable for culling, occlusion culling, HLOD of a base, the
  shadow pass and the triggers.
- Scenes are not a level of the graph: they show or hide parts. Each part
  carries a Switch or a mask bit for its scene; APP sets them when the game
  activates a scene.

## Network

- The server runs APP and ISECT only, with no CULL nor DRAW, on the same
  graph and the same data.
- Each client runs its own APP for prediction, then CULL and DRAW for its
  cameras; the server's state corrects the positions in APP.
- Events of the triggers are raised on the server, the authority, and sent
  to the clients through the message bus.
- Several views of one graph are already the model of the renderer (a camera
  and its mask per view); a network game adds views that never draw.

## Order of work

1. ISECT: segment and ray queries, the ground height, with a mask. Useful at
   once (placement, physics) and independent of the rest.
2. The APP traversal with node callbacks and the per-traversal masks.
3. Trigger volumes and their link to the message bus of libRealSpace.
4. The spatial layout of the missions (areas as nodes, scenes as switches).
5. Views without drawing, for a server.

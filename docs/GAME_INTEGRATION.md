# Game integration: traversals, intersections, triggers

This document gives the design for the use of the R4nkine scene graph by the game. Some parts are done. Refer to "Status".

The scene graph is not only for the renderer. The game also uses it. Different traversals go through the same graph. Each traversal has its own mask and its own callbacks. IRIS Performer uses the same model (APP, CULL, DRAW, ISECT).

## Reasons

- `RSArea::getY` gives incorrect heights:
  - It finds the nearest vertex. Then it interpolates in the cell that starts at this vertex. The point is not always in this cell.
  - It adds 5 units at the edges of the blocks.
  - On ARENAMIS, the error is up to 86 units.
- `r4bridge::groundHeight` calculates the height on the LVL0 triangles. These are the triangles that the renderer shows. The game must get the height from the graph.
- libRealSpace finds the area of the player with its own code. The graph has the areas and their bounds. Thus, the graph can send an event when the player goes into an area.
- libRealSpace has a message bus. The graph sends its events to this bus. The game logic stays in libRealSpace.
- A network game is possible in the future. The server and the clients use the same graph. Each one has its own views. The server has no renderer. Each client has its cameras.

## Traversals

| Traversal | Start | Function |
| --- | --- | --- |
| APP | the simulation step of the game (fixed time step) | It runs the update callbacks of the nodes. It sets the positions from the game. It does a check of the trigger volumes. |
| CULL | a camera | It finds the nodes that a view shows. The game logic does not use it. |
| DRAW | the renderer | It draws the nodes. |
| ISECT | a query | It finds the intersections of segments and rays with the graph: ground height, collisions, lines of sight, shots. |

Each traversal has its own mask. The shadow pass has this mask now (`RenderOptions::shadowMask`). With the masks, a node can be:

- shown but not touched by the queries (a cloud),
- touched by the queries but not shown (a collision hull),
- ignored by APP (a decoration).

## Intersections (ISECT)

- `intersect(root, segment or ray, mask)` gives these results:
  - the point of the hit,
  - the normal at this point,
  - the node and the triangle that the segment hits.
- The query gives the nearest hit or all the hits.
- The ground height is the intersection of a vertical segment with the terrain mask. It replaces `RSArea::getY` for these functions:
  - the placement of the objects,
  - the physics,
  - the AI,
  - the landing.
- The query uses the LVL0 triangles of the terrain, whatever level the renderer shows. Thus, the result is the same for all the cameras.
- The quadtree of the terrain and the bounds of the nodes make the search faster.

## APP traversal and node callbacks

- A node can have callbacks. One callback runs before the APP traversal of the children. One callback runs after it.
- The callbacks update the animations, the positions and the states.
- APP runs in the simulation step of the game, at a fixed rate. It does not run when the renderer draws a frame. Thus, the result does not change with the frame rate.
- During APP, the game sets the positions of its entities in the graph.
- The graph gives the results of the queries and sends the events. The game makes all the decisions.

## Trigger volumes

- A trigger node has a volume. The volume is one of these:
  - the box of an `AREA` (position, width, height),
  - a sphere,
  - the bounds of the children of the node.
- The game registers the trackers. Examples: the player, an AI aircraft, a missile.
- During APP, the graph compares the positions of the trackers with the volumes. It sends `onEnter` and `onLeave`.
- The bridge puts these events on the message bus of libRealSpace. Then the game starts the scenes of the area, as `SCMission` does now.
- A trigger does not use the result of CULL. The player can see a base at 20 km, but the player is not in the area of this base.

## Graph layout for the missions

- Put each map object and each mission part below the node of the area where it is. Do not use the area of the scene that adds the part. Then the area nodes have small bounds. The bounds are correct for these functions:
  - culling,
  - occlusion culling,
  - HLOD of a base,
  - shadow pass,
  - triggers.
- A scene is not a level of the graph. A scene shows or hides parts. Each part has a Switch or a mask bit for its scene. During APP, the game sets them when it starts a scene.

## Network

- The server runs APP and ISECT only. It does not run CULL or DRAW. It uses the same graph and the same data as the clients.
- Each client runs its own APP to predict the positions. Then it runs CULL and DRAW for its cameras. The state of the server corrects the positions during APP.
- The server is the authority for the trigger events. The message bus sends these events to the clients.
- The renderer has one camera and one mask for each view now. A network game adds views that do not draw.

## Status

The code that connects libRealSpace to R4nkine is in libRealSpace (branch `r4nkine`): `src/r4bridge` and `src/r4game`.

Done in R4nkine:

- The APP traversal: `Node::setAppCallbacks` (before and after the children), `Node::setAppMask`, `scene::app(root, mask)`.
- `r4::scene::Trigger` and `checkTriggers`: the three volumes of the areas of a mission (sphere, cylinder, box), the trackers and the enter and leave callbacks.

Done in libRealSpace:

- `SCMission` loads and runs the mission. `R4Mission : SCMission` keeps the graph of the mission. It makes the graph when the mission is loaded. The graph has:
  - the terrain of the area,
  - a group for the planes (a plane goes from an area to another),
  - a node for each area of the mission, with the objects that are in it,
  - a Switch for each scene of an area, with the objects of the scene. The Switch is on when the scene is activated,
  - a trigger for each area of the mission, with its volume.
- Each layer has a mask: `LAYER_TERRAIN`, `LAYER_ACTORS`, `LAYER_SHOTS`, `LAYER_PLAYER`, `LAYER_TRIGGERS`.
- `R4Mission::app` makes the nodes of the objects that appear and removes the nodes of the objects that are gone. Then `scene::app` runs the callback of each node. The callback copies the state of its object into the node: the matrix, the wheels, the afterburner, the stores and the model.
- The triggers put `MissionEventAreaEntered` and `MissionEventAreaLeft` of the planes on the message bus.
- `R4Strike : SCStrike` keeps the view: the display, the order of the passes, the keys and the cameras. It runs the APP step one time in each frame, after the camera. It puts the graph in the pass of the renderer with the effects and the cockpit.
- The ground height: `RSArea::ground` is a hook of `RSArea::getY`. `R4Mission` sets it to `r4bridge::groundHeight`, on the LVL0 triangles. It is not an ISECT query of the graph.

Not done:

- ISECT: the queries of segments and rays on the graph (`intersect`).
- APP at the fixed time step of the simulation. Now, APP runs one time in each frame of the game.
- The views that do not draw, for a server.

## Sequence of work

1. ISECT: segment and ray queries, ground height, mask. The ground height is done without ISECT (refer to "Status"). The queries are not done.
2. APP traversal: node callbacks and one mask for each traversal. **Done.**
3. Trigger volumes and their connection to the message bus of libRealSpace. **Done.**
4. Spatial layout of the missions: areas as nodes, scenes as switches. **Done.**
5. Views that do not draw, for a server.

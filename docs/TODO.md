# TODO

Roadmap from the state analysis of 2026-10-22. Stages are ordered by dependency;
prototype items each lean on an engine item (in brackets).

## Stage 0 — fixes

- [x] `SceneEventsSystem` in `pipelines/begin.systems` (a schedule without the render pipeline needs it for `PhysicsSyncSystem`)
- [x] Render: fallback texture while a texture is not loaded; descriptor heap overflow is an error in every build
- [x] Render: back-face culling by default (glTF `doubleSided`, transparent materials: none); normal matrix (inverse-transpose)
- [x] Physics: `restitution` 0 by default; SpringJoin removed (joints later as Jolt constraints)
- [x] Input: mouse moves no longer push keys out of the event buffer; mouse delta accumulated per frame; wheel reaches `Devices`
- [x] Any number of component types (no 128 cap); reflection id collision is an error
- [x] Unknown components are kept on load and written back on save
- [x] Leftovers: `typed_system.h` example, contract test skips without the Windows SDK, smoke script flags, `CSyren.vcxproj` items

## Stage 1 — core foundations

- [x] Entity IDs with generations; `EntityRef` field remapped on load / duplicate / prefab; `Spawn` returns the root
- [ ] Events delivered in publish order (no Create/Destroy reordering on ID reuse)
- [ ] Time: max delta, pause / single step, fixed-step phase for gameplay
- [ ] Disabled entities; `Without<T>`, `single()` in queries; empty tag components; transient (not saved) fields
- [ ] Structural commits: skip empty ones, one commit point per phase
- [ ] `Spawn` copies prepared values instead of parsing JSON per copy
- [ ] Runtime does not depend on the editor (serializer, scene loader)

## Stage 2 — engine mechanisms from the prototype

- [ ] Physics triggers; contact enter/exit events per entity
- [ ] Gameplay event channel between systems (`Emit<E>` / `Events<E>`)
- [ ] Input actions as polled state, bindings as project data, cursor capture as a service
- [ ] Runtime overlay / HUD layer (not tied to the editor)
- [ ] `Lifetime` in the engine; `damp()` / spring helpers; Random service with seeds
- [ ] Physics: character visible to queries (inner body), `MoveKinematic`, collider scale, velocity changes without rebuilding the body
- [ ] Debug draw (lines, shapes, Jolt debug renderer)

## Stage 3 — content and rendering

- [ ] `.material` assets, per-instance parameters; glTF normal / metal-rough / emissive maps, alpha mode, mips, embedded images
- [ ] Culling, render list, instancing; paged upload buffers; meshes in the default heap; 32-bit indices
- [ ] Shadows (CSM), bloom, spot lights, samplers declared by shaders
- [ ] Prefab instances (link to the file, overrides, nesting)
- [ ] Migration of saved data (upgrade hooks, renames)
- [ ] Particles / VFX
- [ ] Skinning, animation blending; mesh / convex colliders

## Prototype

- [ ] Own project in `samples/` (`new_project.py`): CS_* markers, no `game_registration.h`
- [ ] Per-entity state in components; movement in a fixed step [time]
- [ ] Time trial with checkpoints and respawn volumes [triggers]
- [ ] Bots shoot back; player health and death [event channel, HUD, inner body]
- [ ] Weapons as data assets: N slots, aim-down-sights FOV, shotgun
- [ ] Debug draw of movement probes, speed graph [debug draw]

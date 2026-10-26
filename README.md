# CSyren

C++20 / DirectX 12 engine with an ECS, a frame-graph scheduler, Jolt physics and an ImGui editor.

- Build `CSyren.sln` (x64). `CSyren.vcxproj` is the engine's own sandbox project; Debug runs with `--console`.
- Projects live in their own folders and are bound to an engine by an id: create one with
  [CSyrenHub](https://github.com/zonelder/CSyrenHub) (`csyren-hub new`, `csyren-hub open`) or
  `py -3 tools/csyren.py new <folder> <Name>`. See `docs/project_structure.md` (layout,
  generated files, `CS_COMPONENT` / `CS_SYSTEM` markers, generated registry, `.systems` includes).
- Projects on CSyren: SyrenMovement (movement / shooter prototype), SyrenBoard (board game).
- Other notes: `docs/rendering.md`, `docs/reflection.md`, `docs/physics_layers.md`,
  `docs/asset_pipeline.md`, `docs/frame_graph.md`.
- Roadmap: `docs/TODO.md`.

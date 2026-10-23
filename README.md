# CSyren

C++20 / DirectX 12 engine with an ECS, a frame-graph scheduler, Jolt physics and an ImGui editor.

- Build `CSyren.sln` (x64). `CSyren.vcxproj` is the engine's own sandbox project; Debug runs with `--console`.
- New project: `python tools/new_project.py <folder> <Name>` — see `docs/project_structure.md`
  (layout, `CS_COMPONENT` / `CS_SYSTEM` markers, generated registry, `.systems` includes).
- Other notes: `docs/rendering.md`, `docs/reflection.md`, `docs/physics_layers.md`,
  `docs/asset_pipeline.md`, `docs/frame_graph.md`.
- Roadmap: `docs/TODO.md`.

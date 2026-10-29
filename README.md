# CSyren

A C++20 / DirectX 12 game engine with a typed ECS, a frame-graph scheduler, Jolt physics
and an ImGui editor. Games live in their own project folders and use the engine as an
external module.

> **Status:** early development. APIs and file formats change without notice.

<!-- Screenshot placeholder: add docs/images/editor.png and uncomment the next line.
![CSyren editor](docs/images/editor.png)
-->

## Features

### ECS and scheduling
- **Typed systems:** the parameters of `execute()` declare its access:
  `Select`, `Update<...>::Where<...>`, `Insert`, `Delete`, `Spawn`, `Res<T>`, `Exclusive`.
  The compiler rejects an access the signature does not grant. An optional strict mode
  also rejects unused declarations.
- **Frame graph** built from those declarations. Systems that do not conflict run in
  parallel on a job system (enkiTS). Order comes from string phases, `before`/`after`
  and named CPU resource states.
- No cap on the number of component types. Entity IDs carry generations. `EntityRef`
  fields are remapped on load, duplicate and prefab spawn.
- **Prefabs:** `.prefab` files, spawned from systems with
  `spawn(prefab, pose).with(...).edit<T>(...)`.
- **Event bus:** lock-free publishing from any thread, one channel per event type,
  keyed subscriptions, delivered in publication order at one point of the frame.
- **No RTTI:** the engine builds with `/GR-`, and the registry generator fails the build
  on `typeid` / `dynamic_cast` / `std::any` in engine code.

→ [docs/frame_graph.md](docs/frame_graph.md)

### Rendering
- Deferred GBuffer plus a forward pass for unlit and transparent materials (sorted
  back-to-front).
- Cook-Torrance GGX PBR (albedo, metallic, roughness, emissive). Directional lights and
  **clustered point lights** (16×8×24 froxels, compute light assignment).
- HDR → exposure → ACES → gamma. Reverse-Z depth. 7 debug views (albedo, normals,
  roughness/metallic, depth, light heat map, emissive, lighting only).
- The frame graph inserts D3D12 barriers, records passes in parallel and runs per-camera
  passes once per rendered view. Render target formats come from the pass, so one
  material works in any pass.
- Shaders are HLSL with reflection annotations (`//@editable`, `//@range`, `//@semantic`)
  and a compiled-shader cache.

→ [docs/rendering.md](docs/rendering.md)

### Physics (Jolt)
- Rigid bodies (static / kinematic / dynamic) with box, sphere and capsule colliders.
  Continuous collision for fast bodies. Contact events for the last frame.
- Kinematic **character controller**: slopes, steps, crouch height, ground and wall
  contacts, moving platforms.
- Fixed-step simulation. Raycast, sphere cast and overlap queries with layer filters.
- **Collision layers:** the engine provides up to 32 named layers and a collision matrix.
  The project defines their names and rules, from code (`CS_ENUM`) or from JSON.

→ [docs/physics_layers.md](docs/physics_layers.md)

### Assets and animation
- Model import through `IModelImporter`. glTF 2.0 (`.gltf` / `.glb`) is implemented.
- Resource references by selector: `models/rifle.glb#mesh:Body`, `#mat:Steel`, `#anim:reload`.
- Node (TRS) animation clips played over an entity subtree by `Animator`.
- Texture loading from DDS and WIC formats (PNG, JPEG, ...) on upload tasks, with a
  fallback texture until the load finishes.

→ [docs/asset_pipeline.md](docs/asset_pipeline.md)

### Reflection and serialization
- `describe()`-based reflection. JSON scene files, plus a compact archive.
- `CS_ENUM` and `MaskOf<E>`: stored by name, shown in the inspector as combos and
  checkboxes, with rename aliases.
- Components the build does not know are kept on load and written back on save.

→ [docs/reflection.md](docs/reflection.md)

### Editor
- Scene hierarchy, inspector (any reflected field, `EntityRef` picker), move / rotate /
  scale gizmos, mouse picking, undo / redo.
- Scene and Game views. **Play mode** runs the project's systems on a copy of the scene,
  and Stop discards the copy. Editing is locked while playing.
- Duplicate, delete, model import, "Save as prefab", prefab instantiation.
- *systems* window: the current schedule with per-task timings.

### Profiler
- Lock-free per-thread marks (`CS_PROFILE_SCOPE`, `CS_PROFILE_VALUE`). Frame graph tasks,
  GPU submission, physics and editor windows are marked automatically.
- Editor window with a per-thread timeline, sortable marks table and thread load.
  Captures export as Chrome traces (Perfetto / `chrome://tracing`).
- Compiled out with `CSYREN_PROFILER=0`.

→ [docs/profiler.md](docs/profiler.md)

### Projects and tooling
- A project is its own folder or repository, bound to an engine by id. The engine is
  never copied into it.
- Types are registered by markers (`CS_COMPONENT`, `CS_SYSTEM`, `CS_SERVICE`, `CS_EDITOR`,
  `CS_REFLECT`). A pre-build step generates one registry file, so registration does not
  depend on static initializers.
- `tools/csyren.py`: `new`, `generate`, `restore`, `build-engine`, `build`, `run`.
- [CSyrenHub](https://github.com/zonelder/CSyrenHub): registers engines on a machine,
  creates and opens projects.

→ [docs/project_structure.md](docs/project_structure.md)

## Requirements

- Windows 10/11 x64, a GPU with Direct3D 12
- Visual Studio 2022 (MSVC v143, C++20) with the Windows 10/11 SDK (10.0.22621 or newer)
- Python 3 (`py -3`) for the project tool and the pre-build registry step
- Git (Jolt is a submodule)

NuGet packages (DirectXTex, DirectXTK12, Direct3D 12 Agility SDK, googletest) are restored
automatically.

## Getting started

```bat
git clone --recurse-submodules https://github.com/zonelder/CSyren
cd CSyren
py -3 tools/csyren.py restore
```

`restore` checks out missing submodules and NuGet packages, so it is also the fix for a
clone made without `--recurse-submodules`.

Open `CSyren.sln`, select **x64**, and build. The startup project `CSyren` is the
engine's sandbox: it opens `assets/scenes/sandbox.scene` in the editor. Debug runs with
`--console`, which mirrors the log to a console window.

## Creating a project

```bat
py -3 tools/csyren.py new D:\Games\MyGame MyGame
py -3 tools/csyren.py run D:\Games\MyGame
```

or, with the hub: `csyren-hub new` / `csyren-hub open MyGame`.

`new` writes `MyGame.csyrenproj`, `MyGame.vcxproj`, `source/` and `assets/`, with an
example `Spinner` component and `SpinnerSystem` (`--bare` skips them). Generated files
(`MyGame.sln`, `build/`) are machine-local and stay out of source control.

A component and a system, in any header under `source/`:

```cpp
#pragma once
#include "core/registration.h"
#include "core/typed_system.h"
#include "core/time.h"

namespace my_game
{
    struct Health
    {
        float value = 100.0f;
        float regen = 5.0f;
        static void describe(); // reflected fields, defined in a .cpp (below)
    };
    CS_COMPONENT(Health);

    class RegenSystem : public csyren::core::TypedSystem<RegenSystem>
    {
    public:
        void execute(csyren::core::Update<Health> health,
                     csyren::core::Res<const csyren::core::Time> time)
        {
            for (auto [id, h] : health)
                h.value += h.regen * time->deltaTime();
        }
    };
    CS_SYSTEM(RegenSystem);
}
```

```cpp
#include "health.h"
#include "core/meta.h"

void my_game::Health::describe()
{
    using namespace csyren::core::reflection;
    MetaFactory<Health>{}.type("Health"_hs)
        .data<&Health::value>("value"_hs)
        .data<&Health::regen>("regen"_hs);
}
```

Add `{ "class": "RegenSystem" }` to the project's `assets/app.systems`. The next build
registers both types.

## Tests

- C++ (googletest): `core_tests`, `cstdmf_tests`, `scene_workflow_tests`,
  `dx12_graphic_tests` in `CSyren.sln`.
- D3D12 integration smoke test (after a Debug build): `python test/render_tests/run_frame_graph_smoke.py`.
- Project tool, registry generator, compile-time system contract and source encoding:
  `python -m unittest discover -s tools/tests`.

Engine tests check the engine only and are not part of project solutions.

## Repository layout

```
lib/        engine libraries: core (ECS, scheduler, events, reflection), cstdmf (containers,
            job system, profiler, log), math, dx12_graphic, physics; imGui, Jolt, third_party
source/     runtime (render/animation systems, serializer), editor, host (Application, WinMain)
assets/     engine assets ("engine:" mount): shaders, textures, pipelines/*.systems
tools/      csyren.py, csyren_registry.py, tests/
test/       engine tests
docs/       design notes and roadmap
```

## Built with CSyren

- **SyrenMovement**: Titanfall-style movement and shooter prototype
- **SyrenBoard**: board game

## Roadmap

Up next: fixed-step gameplay phase and pause / single step, disabled entities and
`Without<T>` queries, physics trigger events, input actions, debug draw, a runtime
HUD layer. Later: `.material` assets, shadows, spot lights, prefab instances with
overrides, skinning.

Full list: [docs/TODO.md](docs/TODO.md)

## Third-party

| Library | Use | License |
| --- | --- | --- |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | physics | MIT |
| [Dear ImGui](https://github.com/ocornut/imgui) | editor UI | MIT |
| [enkiTS](https://github.com/dougbinks/enkiTS) | job scheduler | zlib |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON | MIT |
| [DirectXTex](https://github.com/microsoft/DirectXTex), [DirectXTK12](https://github.com/microsoft/DirectXTK12) | textures | MIT |
| [Direct3D 12 Agility SDK](https://devblogs.microsoft.com/directx/directx12agility/) | D3D12 runtime | Microsoft |
| [googletest](https://github.com/google/googletest) | tests | BSD-3-Clause |

## License

[MIT](LICENSE.txt)

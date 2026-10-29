# CSyren

A C++20 / DirectX 12 game engine with a typed ECS, a frame-graph scheduler, Jolt physics
and an ImGui editor. Games live in their own project folders and use the engine as an
external module.

> **Status:** early development. APIs and file formats change without notice.

<!-- Screenshot placeholder: add docs/images/editor.png and uncomment the next line.
![CSyren editor](docs/images/editor.png)
-->

## How it works

The examples below use `namespace csyren` and made-up game types.

### Components

A component is a plain struct. `describe()` lists the fields that the inspector shows
and scene files store. `CS_COMPONENT` registers the struct: a build step collects the
markers, so no registration list has to be kept by hand.

```cpp
struct Health
{
    float value = 100.0f;
    float max = 100.0f;
    static void describe();
};
CS_COMPONENT(Health);

void Health::describe()   // in a .cpp
{
    using namespace core::reflection;
    MetaFactory<Health>{}.type("Health"_hs)
        .data<&Health::value>("value"_hs)
        .data<&Health::max>("max"_hs);
}
```

→ [docs/project_structure.md](docs/project_structure.md), [docs/reflection.md](docs/reflection.md)

### Typed systems

A system's `execute()` parameters say what it reads and writes, and the engine needs
no other declaration. Touching anything the parameters do not list is a compile
error. The scheduler uses the same parameters to decide which systems can run in
parallel.

```cpp
class RegenSystem : public core::TypedSystem<RegenSystem>
{
public:
    // Writes Health, reads the Time service. Visits entities that also have a Player.
    void execute(core::Update<Health>::Where<Player> health,
                 core::Res<const core::Time> time)
    {
        for (auto [id, h] : health)
            h.value = std::min(h.max, h.value + 5.0f * time->deltaTime());

        // health.get<core::Transform>(id);   // does not compile: Transform is not declared
    }
};
CS_SYSTEM(RegenSystem);
```

| Parameter | Access |
| --- | --- |
| `Select<Cs...>` / `Update<Cs...>` | read / write components (`const C` inside `Update` is read-only) |
| `::Where<Ts...>` | only entities that also have `Ts` |
| `Insert<Cs...>` / `Delete` | create entities and add components / destroy and remove, applied at the task's commit |
| `Spawn` | copy a prefab (below) |
| `Res<T>` / `Res<const T>` | a service, write / read |

→ [docs/frame_graph.md](docs/frame_graph.md)

### Scheduling

Static members only order a system. The data it touches still comes from its parameters.
A game's frame is a list of systems in a `.systems` file.

```cpp
static constexpr auto phase = "simulation";
static constexpr std::array after{ "PhysicsSyncSystem" };
```

```json
{ "systems": [
    { "include": "engine:/pipelines/begin.systems" },
    { "include": "engine:/pipelines/simulation.systems" },
    { "class": "RegenSystem" },
    { "include": "engine:/pipelines/render.systems" } ] }
```

### Events

Events report what has already happened. Any thread can publish without locks. The
engine delivers events at one point in the frame, in publication order. A key lets a
subscriber receive only one entity's events.

```cpp
struct Died { core::Entity::ID who; static constexpr auto key = &Died::who; };
// Members used below: core::Broadcast<Died> _died; core::Subscription _all, _boss;

// Publisher: take the channel in onAttach, publish from execute().
void onAttach() override { _died = sceneBus().createEvent<Died>(); }
//   if (h.value <= 0) _died.publish({ id });

// Subscriber: every death, or only the boss's.
void onAttach() override
{
    auto died = sceneBus().createEvent<Died>();
    _all  = died.subscribe([this](const Died&) { ++_kills; });
    _boss = died.subscribe(bossId, [](const Died&) { /* level complete */ });
}
```

→ [docs/project_structure.md#events](docs/project_structure.md#events)

### Prefabs and spawning

A `.prefab` file is a saved entity branch. A system spawns copies and can change the
copy's root first. The copy appears at the task's commit, so systems ordered after the
spawner see it in the same frame.

```cpp
struct Turret { core::PrefabRef shell; math::Vector3 velocity; static void describe(); };
CS_COMPONENT(Turret);

void execute(core::Select<Turret, core::Transform> turrets, core::Spawn spawn)
{
    for (auto [id, turret, tr] : turrets)
        spawn(turret.shell, { tr.position, tr.rotation })
            .edit<physics::RigidBody>([v = turret.velocity](physics::RigidBody& b) { b.linearVelocity = v; });
}
```

### Physics

The physics is Jolt with box, sphere and capsule colliders, a kinematic character
controller and fixed-step simulation. The engine provides up to 32 collision layers.
The game names them and sets which pairs collide.

```cpp
CS_ENUM(Layer, uint8_t, Default, Player, Projectile, Wall)

class LayerSetup : public core::OnceSystem
{
public:
    void init() override
    {
        auto* layers = core::Services::get<physics::PhysicsLayers>();
        layers->defineEnum<Layer>();
        layers->setCollision(Layer::Projectile, Layer::Projectile, false);
    }
};
CS_SYSTEM(LayerSetup);

// In any system with core::Res<const physics::PhysicsEngine> physics:
auto hit = physics->raycast(origin, direction, 100.0f,
    { .layerMask = physics::PhysicsLayers::toMask(core::MaskOf<Layer>{ Layer::Default, Layer::Wall }),
      .ignore = shooter });
```

→ [docs/physics_layers.md](docs/physics_layers.md)

### Rendering

The frame runs these passes: GBuffer → light clusters (compute) → deferred light pass
(PBR, GGX) → forward pass for unlit and transparent materials → HDR tonemap. The frame
graph inserts the D3D12 barriers. A material is an HLSL file, and its annotated fields
show up in the inspector.

```hlsl
#include "gbuffer.hlsli"

//@update material
cbuffer material
{
    //@editable
    float4 tint;
    //@editable
    //@range 0 1
    float roughness;
}

GBufferOutput PSMain(PS_Input input)
{
    return packGBuffer(srgbToLinear(tint.rgb), 0.0f, input.normal, roughness, 0.0f);
}
```

→ [docs/rendering.md](docs/rendering.md)

### Assets

Models are imported through an importer interface. glTF 2.0 (`.gltf` / `.glb`) is
implemented. A resource name is a path plus a selector, and scene files store these names.

```cpp
rm.get<Mesh>("models/crate.glb#mesh:Body");
rm.get<Material>("models/crate.glb#mat:Wood");
rm.get<AnimationClip>("models/door.glb#anim:open");   // played by an Animator component
```

→ [docs/asset_pipeline.md](docs/asset_pipeline.md)

### Profiler

A mark is one line. Every system and frame-graph task is already marked. The editor
shows a per-thread timeline and can export a Chrome trace.

```cpp
CS_PROFILE_SCOPE("Pathfinding");
CS_PROFILE_VALUE("Agents", agents.size());
```

→ [docs/profiler.md](docs/profiler.md)

### Editor

- Hierarchy, inspector for any reflected field, move / rotate / scale gizmos, picking, undo / redo.
- Play runs the project's systems on a copy of the scene. Stop throws the copy away.
- Model import, Save as prefab, prefab instantiation.
- Windows: *profiler*, *systems* (the current schedule with per-task time), *statistic*.

## Requirements

- Windows 10/11 x64, a GPU with Direct3D 12
- Visual Studio 2022 (MSVC v143, C++20), Windows SDK 10.0.22621 or newer
- Python 3 (`py -3`): the project tool and the pre-build registry step
- Git (Jolt is a submodule)

NuGet packages are restored automatically.

## Getting started

```bat
git clone --recurse-submodules https://github.com/zonelder/CSyren
cd CSyren
py -3 tools/csyren.py restore
```

Open `CSyren.sln` (x64) and run the `CSyren` project. It is the engine's sandbox and
opens `assets/scenes/sandbox.scene` in the editor.

## Creating a project

```bat
py -3 tools/csyren.py new D:\Games\MyGame MyGame
py -3 tools/csyren.py run D:\Games\MyGame
```

Or use [CSyrenHub](https://github.com/zonelder/CSyrenHub): `csyren-hub new`, `csyren-hub open MyGame`.
The project gets `source/`, `assets/` and an example component and system. The engine
is linked as libraries, never copied.

→ [docs/project_structure.md](docs/project_structure.md)

## Tests

- C++ (googletest): `core_tests`, `cstdmf_tests`, `scene_workflow_tests`, `dx12_graphic_tests` in `CSyren.sln`
- D3D12 smoke test after a Debug build: `python test/render_tests/run_frame_graph_smoke.py`
- Tools and the compile-time system contract: `python -m unittest discover -s tools/tests`

## Roadmap

Ordered by expected effort, smallest first. The dependency order is in [docs/TODO.md](docs/TODO.md).

**Quick**
- `Lifetime` component, `damp()` / spring helpers, seeded Random service
- `Without<T>` and `single()` in queries
- Time: max delta, pause, single step
- Skip empty structural commits
- Physics triggers (`isTrigger` as Jolt sensors) and contact enter / exit events
- Gameplay events as system parameters (`Emit<E>` / `Events<E>`)

**Medium**
- Fixed-step gameplay phase, one structural commit point per phase
- Empty tag components, disabled entities, transient (not saved) fields
- Input actions: polled state, bindings as data, cursor capture as a service
- Debug draw: lines, shapes, Jolt debug renderer
- Physics: character visible to queries, `MoveKinematic`, collider scale, velocity changes without rebuilding the body
- Spot lights, samplers declared by shaders
- `Spawn` copies prepared values instead of parsing JSON per copy
- Runtime without editor dependencies (serializer, scene loader)
- Runtime HUD layer
- Migration of saved data (upgrade hooks, renames)

**Large**
- `.material` assets with per-instance parameters; glTF normal / metal-rough / emissive maps, alpha mode, embedded images
- Prefab instances: link to the file, overrides, nesting
- Skinning, animation blending, mesh / convex colliders
- Particles / VFX
- Shadows, bloom, GPU-driven culling and instancing (the World track in TODO.md)
- Hub: GUI, prebuilt engine releases, engine version check

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

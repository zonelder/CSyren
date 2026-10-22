# Systems and frame scheduling

Every system derives from one of two bases (`core/typed_system.h`,
`core/system_base.h`):

- `core::TypedSystem<Derived>`: per-frame work in `execute(params...)`; the
  parameters are the access declaration of its frame graph task.
- `core::OnceSystem`: lifecycle work only (`init`, `shutdown`, attach/detach),
  e.g. registering resource factories. It is configured like any system (order,
  editor list) but never enters the frame graph, so it cannot affect the schedule;
  the editor's system window lists it as init-only.

`System`'s constructor is private, so a hand-written `describe()` does not
compile. `init()` and `shutdown()` run on the owning main thread outside the
frame graph, like `onAttach()`/`onDetach()`; the `SystemManager` rejects attach,
detach and schedule edits while a frame executes. `system.run()` executes the
frame work once (the frame graph calls it; tests may too).

## Typed systems

The parameters of `execute()` are the access declaration, so there is no
separate list to keep in sync and an access the signature does not grant does
not compile.

```cpp
class MoveSystem : public core::TypedSystem<MoveSystem> {
public:
    static constexpr auto phase = "simulation";
    static constexpr std::array after{"InputSystem"};

    void execute(core::Update<Position, const Velocity>::Where<Movable> movers,
                 core::Res<const Time> time) {
        for (auto [id, position, velocity] : movers)
            position.x += velocity.x * time->deltaTime();
    }
};
```

| Parameter | Meaning |
|---|---|
| `Select<Cs...>` | read `Cs`. Iteration visits entities that have every `C`; `get<C>(id)` reads one component of any entity |
| `Update<Cs...>` | the same with write access; a `const C` inside `Update` is read-only. `patch<C>(id)` notifies derived data |
| `::Where<Ts...>` | presence filter on a `Select`/`Update`; never conflicts with other systems |
| `Insert<Cs...>` | `create()` entities and `add<C>()` components; staged until the structural commit, never conflicts with readers or writers of `Cs` |
| `Delete` | `destroy(id)` entities and `remove<C>(id)` components, applied in the structural commit |
| `Spawn` | `spawn(prefab, pose)` creates a copy of a prefab (`core/spawn.h`); `.with(T{...})` / `.edit<T>(fn)` change the copy's root first. Staged like `Insert`, but declares no component types: nobody sees the copy before the commit, so it conflicts with no reader or writer |
| `Res<T>` | a service: `Res<const T>` reads, `Res<T>` writes; empty (`false`) when the service is not registered |
| `Exclusive` | the whole world: any component, structural changes and any service (`scene()`, `bus()`, `Services::get`). It conflicts with every task that declares an access. Only for systems whose types are known at run time |

Any `Select`/`Update` also reads the entity hierarchy (`entities()`,
`hierarchyRevision()`); it changes only in structural commits. `worldMatrix`,
`worldRotation`, `parentOf`, `setWorldPose` and `WorldTransforms::update` accept
such a parameter instead of `Scene&`. For derived caches a query also offers
`select<T>()` (every entity with a listed `T`), `componentRevision<T>()` and
`sceneIdentity()`. Public static members set the schedule: `phase` (default
`"simulation"`), `after`, `before` (a name or a `std::array` of names),
`affinity`, `states` (CPU resource states, below), and for GPU passes `gpu`,
`gpuScope` and `gpuQueue` (see GPU recording); a private one is a compile error.
The rule: parameters are what `execute()` touches, static members only order it.
Several parameters may name the same type; declarations merge per type (write
wins). A parameter that is declared for its callees and not used in the body is
left unnamed (e.g. `core::Res<DescriptorManager>` in `FrameAcquireSystem`), so
strict mode (below) still holds.

Libraries add parameter kinds: a type with `static void describeParam(core::ParamAccess&)`
(what it declares) and `static P makeParam(core::Scene&)`, or `makeParam()` when
it does not use the scene. `render::Recorder` (`dx12_graphic/recorder.h`) is one.
A system whose parameters do not use the scene (only `Res`, `Recorder`, ...) can
be run directly without a scene, e.g. `system.run()` in a test (`SystemManager`
still schedules systems only while a scene is bound).

Two systems that `Insert` the same type are ordered (registration order breaks
the tie), so the later one sees the earlier commit and can skip an entity that
already has the component. A reader in the same phase that is not ordered after
an inserter sees the new components only in the next frame. A reference returned
by `Insert::add` is for the current `execute()` only.

`Spawn` is for entities whose components come from data (a prefab), so the
system cannot list their types. Prefabs are `.prefab` files under the assets
(`core::PrefabRef` fields hold the path; an empty reference spawns nothing),
loaded and cached by the `core::PrefabLibrary` service. A prefab is validated and
its resources are resolved when it is loaded, and the prefabs it refers to are
loaded with it; load them in `onAttach` so spawning reads no files:

```cpp
void execute(Update<Launcher, const Transform> launchers, Spawn spawn) {
    for (auto [id, launcher, tr] : launchers)
        spawn(launcher.projectile, { tr.position, tr.rotation })
            .edit<RigidBody>([velocity = launcher.velocity](RigidBody& body) { body.linearVelocity = velocity; })
            .with(Projectile{ .owner = id });
}
```

The copy (the whole branch) is created in the task's structural commit, in order
with its other staged changes; a system ordered after the spawner sees it in the
same frame. `with`/`edit` run in that commit, after `execute()` returned, so their
lambdas capture by value. The spawn is scheduled when the request object returned
by `spawn(...)` ends (the end of the statement); it cannot be stored or moved,
and one destroyed by an exception spawns nothing. Spawners are not ordered against each other or against
readers and writers of the prefab's components; only their commits are exclusive.
A prefab that cannot be loaded, or an `edit` of a component the prefab's root
lacks, is logged once and skipped.

The compiler rejects: reading a type the parameters do not list, writing through
a `Select` or a `const` type, `add<T>` of a type the `Insert` does not list, and
`patch` without write access, a parameter passed by reference or of an unknown
kind, a private schedule member, a hand-written `describe()` and a direct
`System` subclass. Wrap typed system classes (after the `#include`s) in
`CS_STRICT_SYSTEMS_BEGIN`/`CS_STRICT_SYSTEMS_END` to make an unused parameter,
i.e. an unused declaration, a compile error (C4100 on MSVC, `-Wunused-parameter`
elsewhere). `python -m unittest discover -s tools/tests` compiles these cases
(`test_typed_contract.py`) with `cl`, `clang++` or `g++` to show how each one
reads under that compiler. Global access through `Services::get` or `scene()`
inside `execute()` is not caught by the compiler; runtime validation (below)
reports it.

Visual Studio IntelliSense (`__INTELLISENSE__`) sees only the parameter types
and their members: the signature deduction, the access collection, the kind and
uniqueness checks and the strict markers are compiled out for it
(`CSYREN_INTELLISENSE` in `typed_system.h`), and `TypedSystem` binds the
generated `describe()`/`run` in its constructor, so the base class never needs
the system class to be complete. Errors in these checks are reported by the
compiler only; the per-call checks (`get`, `patch`, `add`) stay visible.

Lifecycle hooks keep using `scene()` and `Services` directly. In the engine,
`SceneEventsSystem` and `EditorSystem` take `Exclusive` (event handlers and the
ImGui editors touch types known only at run time); `GameSetupSystem` and
`RenderEnvironmentSystem` are `OnceSystem`s. `TaskDescription` is the frame
graph's input format: `TypedSystem` generates it, the GPU backend extends it and
the frame graph tests build it directly; systems never write it.

## Phases, ordering and resource states

`system.config` declares arbitrary string phases and their dependency graph:

```json
"phases": [
    { "name": "input" },
    { "name": "simulation", "after": ["input"] }
]
```

There is no built-in phase enumeration. Unrelated phases may overlap. A phase
dependency includes every task in the predecessor, including structural commits.
`before` and `after` system dependencies take precedence over registration order.
Registration order breaks otherwise unordered read/write conflicts. Read/read
accesses and disjoint component/resource accesses can run concurrently. Moving a
system in the editor changes this tie-breaking priority, not its explicit
dependencies or phase.

CPU resource state names are strings. A typed system lists them in a static
`states` array: `core::StateUse::read<T>("ready")` requires that state,
`core::StateUse::write<T>("empty", "ready")` describes a transition and
`write<T>("", "ready")` produces the state from any other (on the description
these are `readResource<T>("ready")` / `writeResource<T>(...)`). A state is
merged into a `Res<T>` access of the same type; a type with no service object
(e.g. the `FrameRecording` token of `FrameAcquireSystem` and
`PresentationSystem`) is a pure ordering token. Declare external initial states
with `SystemManager::resourceState<T>("empty")`. A unique
state producer becomes a dependency even when registered later. Unknown names,
cycles, duplicate accesses and missing/ambiguous producers are configuration
errors before execution starts. States are logical scheduling contracts; the
system must actually establish the state it declares. Initial CPU states describe
the externally established condition at the start of each execution.

## Scene changes

`Insert`, `Delete`, `Spawn` and `Exclusive` make a task structural: it stages entity and
component creation/removal in its own transaction (`createEntity`,
`addComponent`, `removeComponent`, `destroyEntity` underneath; no command-buffer
API). The returned entity ID is final, and a returned component reference is
immediately writable. The task's own queries see its staged component changes.

New views also include that task's staged components. Each view keeps a stable
candidate snapshot during iteration; structural edits cannot relocate its live
storage. Other tasks observe changes after the automatic commit. The graph inserts this commit and
includes it in an `after` dependency on the producing system. Calling `flush()`
inside a task cannot relocate storage under a live view. Disjoint structural
tasks can prepare changes concurrently. Applying changes requires a safe point
for all scene users because the existing dense storage and pool registry can
relocate. Raw component pointers/references and views must not escape a task;
retain `ComponentRef` or entity IDs instead. `ComponentRef` checks the component's
incarnation and cannot retarget a reused ID.

With runtime validation on (`CSYREN_ACCESS_VALIDATION`, default in Debug builds)
undeclared accesses (a direct `Services::get` or `scene()` call inside `execute()`)
throw; Release builds skip these checks. Structural changes outside a transaction
are rejected in every build. Every `Select`/`Update` declares the entity
hierarchy (`SceneEntities`) read. `Exclusive` sets `exclusiveScene` and
`dynamicResources`, which introduce the necessary exclusive edges; it is not the
default for ordinary systems.

Physics follows the same model. `PhysicsSystem` (simulation) steps the world
through `Update<RigidBody, Transform>`, the collider `Select` and
`Update<CharacterController>`; the Jolt world is built from the scene when it
attaches. Later scene changes (bodies, colliders and characters created, patched
or destroyed) are queued by the event handlers and applied by `PhysicsSyncSystem`
(phase `events`, after `SceneEventsSystem`, which must be in the same schedule),
so bodies from changes made during a frame are visible to physics queries for
the whole next frame. `PhysicsSystem` applies whatever is still queued before it
steps (changes dispatched between frames, or a schedule without
`PhysicsSyncSystem`), so the simulation never misses a change. The engine's
`pipelines/simulation.systems` lists both.

## GPU recording and submission

A typed GPU pass takes `render::Recorder` (the `Renderer` bound to the task's
command list) and lists its GPU resources in a static `gpu` array:

```cpp
class TonemapSystem : public core::TypedSystem<TonemapSystem> {
public:
    static constexpr auto phase = "render";
    static constexpr std::array after{"ForwardRenderSystem"};
    static constexpr core::GpuTaskScope gpuScope = core::GpuTaskScope::View;
    static constexpr std::array gpu{
        core::GpuUse{"scene.hdr", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, core::AccessMode::Read},
        core::GpuUse{"scene.color", D3D12_RESOURCE_STATE_RENDER_TARGET, core::AccessMode::Write}};

    void execute(render::Recorder recorder, core::Res<const render::ResourceManager> resources);
};
```

`describe()` throws when a system takes a `Recorder` but declares no `gpu`
resources. The backend makes `Renderer` and `PSOFactory` task-local for GPU tasks,
so passes record in parallel; `Res<Renderer>` in a pass would be a shared write.

GPU access ordering is separate from CPU recording dependencies. Each GPU task
gets its own command list, allocator, upload memory, material constants and engine
parameters. `Renderer` routes recording calls to this task-owned storage; it does
not expose frame/output ownership. GPU barriers and interqueue fence waits are
generated from declarations. CPU completion does not imply GPU completion.
Allocator/upload reuse waits for the corresponding retired frame slot only.
Graph edits drain old GPU storage before replacing it.

`View` passes run once per camera view that renders this frame (`TonemapSystem@2`
for view 2). A view renders when it is due (every frame, or an on-demand render
is pending) and shown. The editor shows a view only while its window is visible:
the Scene view sets `CameraRenderView::shown`, the Game view
`Renderer::showPrimaryCamera` (the primary camera, unless its output is
presented). A hidden view records no passes, but its frame constants stay
current (picking uses them) and a pending render waits until it is shown; its
passes keep their recording storage, so switching tabs does not reallocate it.
The change applies from the next frame: for one frame the window shows the last
image.

Use `GpuResources::import` during initialization for persistent resources, supplying
their actual initial and exported final D3D12 states. The registry retains COM
ownership. One-time initialization establishes the final state, and each frame's
plan returns resources to that state. Imports and queue declarations are sealed
when the graph starts. `gpuQueue` selects a named queue registered with
`GpuResources::queue`; `graphics` is provided by the backend. This backend
currently accepts **direct queues**. Compute/copy queues need explicit capability
and ownership-transfer rules before being enabled. Resource states currently
cover the whole resource, not individual mip/array subresources.

`FrameAcquireSystem` owns frame-slot/mailbox acquisition. Its private recording
state is automatically added as a dependency of GPU tasks. `PresentationSystem`
submits the GPU plan and publishes a completed render fence to the mailbox.
The output worker retains the swap-chain-associated presentation queue and never
moves backbuffer writes to another queue. ImGui and window systems use
`TaskAffinity::MainThread`. Jolt currently retains its own four worker threads;
the engine job pool reserves a bounded CPU budget alongside them.

## Type identity without RTTI

The engine builds with `/GR-` (`CSyren.engine.props`; the test projects set
`<CSyrenRuntimeTypeInfo>true</CSyrenRuntimeTypeInfo>` because googletest is
prebuilt with RTTI). Use `core::TypeKey::of<T>()` as a type key and
`core::typeName<T>()` for logs and UI (`core/type_key.h`); keys identify types
within one running image and must not be serialized. To downcast, store a
`TypeKey` of the concrete type in the base (see `editor::PendingFieldEdit::as`).
`tools/csyren_registry.py --rtti-roots` fails the build with CSR030 on
`typeid`, `std::type_info`, `std::type_index`, `dynamic_cast`,
`dynamic_pointer_cast`, `std::any`, `any_cast`, `make_any` and
`<typeinfo>`/`<typeindex>`/`<any>` in `lib/`, `source/` and `game/`, including
macro definitions (comments and string literals are ignored). This check is the
real guard: under `/GR-` MSVC only warns (C4541) on `typeid`/`dynamic_cast` of a
polymorphic type, which then fails at run time.

## Profiling

Every graph task is a profiler mark named like its node (`WeaponSystem`,
`WeaponSystem/apply`, per-view GPU passes `TonemapSystem@2`), and the frame graph
as a whole is `Frame graph`. Engine code adds marks with `CS_PROFILE_SCOPE` /
`CS_PROFILE_VALUE` (`cstdmf/profiler.h`). The editor's *profiler* window shows the
timeline of a frame per thread and the time of each mark; the *systems* window
shows each task's time per frame. See [profiler.md](profiler.md).

## Completion and failures

All dependencies are linked before any root is submitted. The scheduler waits for
one explicit terminal job covering the whole graph, then clears wrappers. It does
not use `WaitforAll`. Exceptions are contained inside jobs; remaining jobs drain
before the exception returns to the owner. Unapplied scene changes are discarded.
Writes already made to existing component values are not rolled back.

Do not start uncovered asynchronous/nested jobs from a system, keep thread-local
scratch across a scheduler wait, or mutate service/phase/system registries while
a graph is running. Job-local scopes are restored with RAII. Existing services
may hold internal worker pools, but those operations must finish before their
system callback completes.

## Validation

`core_tests` covers graph compilation, state producers, hazards, affinity,
exception draining, scene commits, component generations and GPU planning.
After building Debug libraries and the application, run
`python test/render_tests/run_frame_graph_smoke.py` for the Windows/D3D12 test.
It checks parallel recording, two actual queues, GPU readback, background-color
changes and recompilation while previous GPU work is in flight, with the debug
layer and GPU validator enabled.

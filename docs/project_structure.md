# Project structure and type registration

## Engine layout

```
CSyren/
  engine.json         engine name, version and project tool (read by CSyrenHub)
  lib/                core, cstdmf, math, dx12_graphic, physics (+ third_party, imGui, JoltSolution)
  source/
    runtime/          MeshFilter, Animator, render/animation systems, SceneLoader, serializer
    editor/           editor windows, gizmos, SceneSession, inspector, field editors, EditorSystem
    host/             Application, WinMain, startup options, project settings
    csyren_host.vcxproj   static library built from source/**
  assets/             engine assets ("engine:" mount): shaders, default textures, pipelines/*.systems
  tools/              csyren.py (project tool), csyren_registry.py, tests/
  CSyren.vcxproj      the engine's own project executable (sandbox, assets/scenes/sandbox.scene)
  CSyren.project.props / .targets   settings shared by every project executable
```

Components of a subsystem stay in its library (`Transform` in core, `RigidBody` in
physics, lights in dx12_graphic): their systems use them, so they cannot live above
the libraries. Projects do not list engine types anywhere; the registry picks them up.

Sources (headers, `.cpp`, shaders) are UTF-8 without a BOM (`.editorconfig`) and are
compiled with `/utf-8` (`CSyren.engine.props`, so projects get it too). Keep it that way:
IntelliSense reads every file as UTF-8, and one file saved in another code page
(Windows-1251) breaks it in every translation unit that includes the file, standard
headers included (cl only warns, C4828). `tools/tests/test_source_encoding.py` lists
such files.

## A project

A project lives in its own folder, usually its own repository, and is bound to an
engine by an id; the engine is never copied into it.

```
MyGame/
  MyGame.csyrenproj    engine id, start scene, systems files, assets folder    committed
  MyGame.vcxproj       imports CSyren.project.props / .targets, source\**      committed
  source/              components, systems, services (any subfolders)          committed
  assets/              "project:" mount: scenes, models, prefabs, *.systems    committed
  MyGame.sln           the project's .vcxproj files + the engine projects      generated
                       (solution folder "CSyren")
  MyGame.vcxproj.user  debugger arguments (--project MyGame.csyrenproj)        generated
  build/               engine.props, generated registry, binaries, cache       generated
```

`tools/csyren.py` is the engine's project tool (`engine.json` names it):

| command | effect |
| --- | --- |
| `new <folder> <Name> [--engine-id <id>] [--bare]` | writes the committed files, with an example `Spinner` component and `SpinnerSystem` unless `--bare`, then runs `generate` |
| `generate <project>` | writes the generated files for this machine: the solution, `build/engine.props` (`CSyrenRoot` = the engine the tool belongs to) and the `.vcxproj.user` when it is missing |
| `restore` | restores the engine's NuGet packages into `<engine>/packages` (`msbuild -t:restore`) |
| `build <project> [--config Debug\|Release]` | `generate`, `restore` when packages are missing, then MSBuild |
| `run <project> [--config ...] [arguments]` | `build`, then starts the editor with `--project` |

`<project>` is a `.csyrenproj` file or its folder. [CSyrenHub](https://github.com/zonelder/CSyrenHub)
keeps the engines of a machine (`%LOCALAPPDATA%\CSyren\engines.json`: id → folder) and runs
the tool of the engine named by the project's `"engine"` field: `csyren-hub open MyGame`
is `generate` with the right engine, then Visual Studio. Without the hub, run the tool of
the engine you want: `py -3 <CSyren>\tools\csyren.py generate MyGame`.

The `.vcxproj` finds the engine through `build\engine.props`; without it, through the
`CSYREN_ROOT` environment variable; otherwise the build stops and says how to generate
it. Engine libraries build into the engine's `Build/` folder from any solution, so the
projects bound to one engine share its binaries. The sandbox (`CSyren.vcxproj`) builds
with the committed `CSyren.sln`; `generate` leaves it alone.

Other `.vcxproj` files in a project folder (a library, tests) join the generated
solution as well (`build/`, `.vs/`, `x64/` and `packages/` are skipped); reference
them from `MyGame.vcxproj` with a `ProjectReference`. Keys of the `.csyrenproj` that
the engine does not read (`"engine"`) are kept when the editor saves the file.

New `.cpp` files dropped into `source/` are compiled (wildcard). Visual Studio may
expand the wildcard into a file list when files are added or removed through its
menus; keep it as `source\**\*.cpp` if that happens.

## Registration

Types are registered by markers in headers (`lib/core/registration.h`), written at
namespace scope after the type:

```cpp
#include "core/registration.h"

namespace my_game
{
    struct Health { float value = 100.0f; static void describe(); };
    CS_COMPONENT(Health);

    class RegenSystem : public csyren::core::TypedSystem<RegenSystem> { /* ... */ };
    CS_SYSTEM(RegenSystem);

    struct Score { int value = 0; };
    CS_SERVICE(Score);
}
```

| marker | effect |
| --- | --- |
| `CS_REFLECT(T)` | `T::describe()` — reflected type that is not a component |
| `CS_COMPONENT(T)` | `T::describe()` + component registration under the name `"T"` |
| `CS_SYSTEM(T)` | system factory under the name `"T"` (used by `.systems` files) |
| `CS_EDITOR(T, E)` | inspector editor `E` for fields of type `T` |
| `CS_SERVICE(T)` | `core::Services` entry created after the engine services |

Names are unqualified type names and must be unique (scene and `.systems` files
use them). The markers only check the type at compile time; they do not register
anything by themselves.

A component in a scene or prefab whose name is not registered (a renamed or
removed type, code that is not built in) is kept on the entity as data
(`core::UnknownComponents`, listed read-only in the inspector) and written back by
save, Duplicate and "Save as prefab"; a warning names it once. Two reflected types
with the same id stop the program at registration (`std::logic_error`).

Before every build `CSyren.project.targets` runs

```
python tools/csyren_registry.py --out build/generated/registry.gen.cpp --roots <engine lib;engine source;project source> ...
```

which scans the headers and writes one file that includes every marked header and
defines `csyren::generated::registerTypes()` and `createServices()`.
`Application` calls them at startup (`registerTypes` before
`EditorRegistry::resolveAll`, `createServices` after the engine services). The file
is rewritten only when it changes; the project disables Visual Studio's fast
up-to-date check so the scan runs on every build. Errors are reported as
`file(line): error CSRxxx` in the Error List (marker inside a class or function, in
an anonymous namespace, duplicate names, ...). `CSyrenPython` selects the
interpreter (`python` by default, e.g. `py -3`).

Because every registration is an explicit call from the executable, nothing
depends on static initializers surviving in a static library. Built-in field
editors (float, int, bool, string, vectors, color, quaternion) are installed by
`EditorRegistry::resolveAll()` itself; `REGISTER_SYSTEM` remains only for systems
defined in a `.cpp` compiled straight into an executable (tests).
Tests register the engine types they use explicitly, as before.

Generator tests: `python -m unittest discover -s tools/tests`.

## Entity IDs and references

`Entity::ID` is a slot (low 32 bits) and the slot's generation (high 32 bits),
`lib/core/entity.h`. Destroying an entity raises the generation of its slot, and
every lookup (entity storage, component pools, queries, staged commands) compares
the whole ID: an ID kept after its entity was destroyed finds nothing, even when
the slot holds a new entity. Physics, render caches, events and the editor
selection key their data by the whole ID, so they never mix the two up either.
A slot whose generation runs out is retired.

| field type | saved | after load / Duplicate / prefab copy |
| --- | --- | --- |
| `Entity::ID` | no (runtime only) | - |
| `core::EntityRef` | the entity's number in the document, `-1` for none | points at the new entities |

Duplicate keeps `EntityRef`s that leave the copied branch on the originals; a
prefab cannot refer outside its branch ("Save as prefab" fails naming the
component). A reference to a destroyed entity is saved as none. Systems read
through a reference with `get<T>(ref)` / `contains(ref)` on `Select`/`Update`;
the inspector edits it with a list of the scene's entities.
`spawn(...).root()` gives the copy's root ID right away; the entity exists after
the task's commit.

## Events

Each scene has an event bus, `lib/core/event_bus.h` (`sceneBus()` in a system,
`bus()` on `Exclusive`): deferred notifications about what has already happened,
delivered at one point of the frame. An event type has one channel per bus:

```cpp
struct PlayerDied { Entity::ID player; static constexpr auto key = &PlayerDied::player; };

auto died = bus.createEvent<PlayerDied>();   // every caller gets the same channel
died.publish({ id });                         // any thread, no locks
auto all = died.subscribe([](const PlayerDied& e) { ... });       // unsubscribes when reset or destroyed
auto mine = died.subscribe(id, [](const PlayerDied& e) { ... });  // only this player's deaths
```

`publish()` appends to the publishing thread's queue; `dispatch()` (in
`SceneEventsSystem`, phase `events`) delivers the events in publication order,
whatever their type and thread, on the calling thread. Events published by
handlers are delivered in the same `dispatch()`. An event whose channel has no
subscribers is not kept. A `key` (a data member or a const member function) lets
a subscription receive only the events with that key; the scene's events
(`EntityCreateEvent`, `EntityDestroyEvent`, `EntityReparentEvent`,
`ComponentCreateEvent<T>`, `ComponentPatchEvent<T>`, `ComponentDestroyEvent<T>`)
are keyed by entity ID. A handler that should run once resets its own
subscription. `reserve(n)` on a channel makes the next `n` publications on that
thread allocation-free. Creating channels, subscribing, resetting and
`dispatch()` run one at a time; the full contract is in the header of
`event_bus.h` (internals in `event_bus.inl`).

## Systems files

`.systems` entries are `{"class": "Name"}` or `{"include": "<systems file>"}`; an
include is replaced in place by that file's systems and its phases are merged.
The engine pipeline is split into pieces under `assets/pipelines/`:

| file | contents |
| --- | --- |
| `begin.systems` | frame phases, `InputDispatchSystem`, `SceneEventsSystem` (dispatches scene events in phase `events`) |
| `simulation.systems` | `PhysicsSystem`, `PhysicsSyncSystem`, `AnimationSystem` |
| `render.systems` | `RenderEnvironmentSystem` … `PresentationSystem` |
| `editor.systems` | the complete editor list |

A project's `app.systems`:

```json
{ "systems": [
    { "include": "engine:/pipelines/begin.systems" },
    { "class": "SpinnerSystem" },
    { "include": "engine:/pipelines/simulation.systems" },
    { "include": "engine:/pipelines/render.systems" } ] }
```

`Serializer::saveSystems` writes the expanded list (includes are not preserved).

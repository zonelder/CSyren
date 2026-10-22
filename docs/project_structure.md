# Project structure and type registration

## Engine layout

```
CSyren/
  lib/                core, cstdmf, math, dx12_graphic, physics (+ third_party, imGui, JoltSolution)
  source/
    runtime/          MeshFilter, Animator, render/animation systems, SceneLoader, serializer
    editor/           editor windows, gizmos, SceneSession, inspector, field editors, EditorSystem
    host/             Application, WinMain, startup options, project settings
    csyren_host.vcxproj   static library built from source/**
  assets/             engine assets ("engine:" mount): shaders, default textures, pipelines/*.systems
  tools/              csyren_registry.py, new_project.py, ...
  CSyren.vcxproj      the engine's own project executable (sandbox; still hosts game/ for now)
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

```
MyGame/
  MyGame.csyrenproj   start scene, systems files, assets folder
  MyGame.sln          engine projects (solution folder "CSyren") + MyGame.vcxproj
  MyGame.vcxproj      imports CSyren.project.props / .targets, compiles source\**\*.cpp
  source/             components, systems, services (any subfolders)
  assets/             "project:" mount — scenes, models, app.systems, editor.systems
  build/              generated registry, binaries, cache (not in source control)
```

Create one with

```
python tools/new_project.py <folder> <Name> [--engine <path to CSyren>]
```

It writes the files above with an example `Spinner` component and `SpinnerSystem`,
and a start scene. The engine is found through `CSyrenRoot` in the `.vcxproj`
(relative to the project when possible); the `CSYREN_ROOT` environment variable
overrides it. Build the engine solution (`CSyren.sln`) once first: it restores the
NuGet packages the project executable links against. Engine libraries build into
the engine's `Build/` folder from any solution.

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

    class RegenSystem : public csyren::core::System { /* ... */ };
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
defined in a `.cpp` compiled straight into an executable (tests, `game/`).
Tests register the engine types they use explicitly, as before.

Generator tests: `python -m unittest discover -s tools/tests`.

## Systems files

`.systems` entries are `{"class": "Name"}` or `{"include": "<systems file>"}`; an
include is replaced in place by that file's systems and its phases are merged.
The engine pipeline is split into pieces under `assets/pipelines/`:

| file | contents |
| --- | --- |
| `begin.systems` | frame phases + `InputDispatchSystem` |
| `simulation.systems` | `PhysicsSystem`, `SpringJoinSystem`, `AnimationSystem` |
| `render.systems` | `RenderEnvironmentSystem` … `PresentationSystem`, `SceneEventsSystem` |
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

## Transitional: game/

`game/` (the movement prototype) still lives in the engine repository and is
compiled into `CSyren.vcxproj`. Its systems use `REGISTER_SYSTEM`; its components
and services are listed as markers in `game_registration.h`, which
`CSyren.vcxproj` adds to the scan roots. `mesh_filter.h`, `animation.h`,
`editor_overlays.h` and `editor_viewport_state.h` in the repository root forward
to `source/` for the `"../x.h"` includes in `game/`. All of these go away when
`game/` moves to its own project with CS_* markers.

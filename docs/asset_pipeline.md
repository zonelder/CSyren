# Asset pipeline: models and animation

## Overview

```
file (.glb / .gltf / …)
   │  IModelImporter (picked by extension)        lib/dx12_graphic/import/
   ▼
ModelData  — format-independent: materials, meshes, nodes (TRS tree), TRS animations
   │
   ├─ Mesh          ← rm.get<Mesh>("models/rifle.glb#0")            (mesh 0, primitive 0)
   ├─ Material      ← rm.get<Material>("models/rifle.glb#mat:Steel") (lit shader, base color, texture)
   ├─ AnimationClip ← rm.get<AnimationClip>("models/rifle.glb#anim:reload")
   └─ entities      ← instantiateModel() / editor "File → Import model…"
```

Engine space is left-handed, +Y up, +Z forward, meters. Importers convert to
it (the glTF importer mirrors Z and fixes winding), so nothing downstream
knows about source conventions.

## Resource references

A resource name is `<asset path>#<selector>`. The asset path is relative to
the assets mount (`assets/`), exactly like shaders and textures.

| Selector | Meaning |
| --- | --- |
| *(none)* or `0` | mesh 0, primitive 0 |
| `2:1` | mesh 2, primitive 1 |
| `mesh:Body` / `mesh:Body:1` | mesh by name (+ primitive) |
| `mat:1` / `mat:Steel` | material by index / name |
| `anim:0` / `anim:reload` | animation by index / name |

`ModelReference::parse()` / `make*()` build and parse these strings. Scene
files store them in the `resources` section like any other resource path,
so a scene referencing `models/rifle.glb#0` loads the model on demand.

## Adding an importer

1. Implement `IModelImporter` (`import/model_importer.h`): `supports(".ext")`
   and `import(physicalPath, assetPath, ModelData&, error)`.
2. Fill `ModelData` in engine space. Normals and UVs must exist per vertex
   (generate them if the format lacks them). Indices are `uint32`; the mesh
   resource rejects primitives above 65535 vertices (16-bit index buffers).
3. Register it: `ModelImporterRegistry::instance().add(std::make_unique<MyImporter>())`
   (the glTF importer registers itself in the registry constructor).
4. Nothing else changes: resources, the instantiator and the editor import
   work through `ModelData`.

Candidate next importer: FBX via **ufbx** (single-file, MIT) for asset-store models.

## Adding a resource type (what AnimationClip did)

1. Class with a default constructor, move support and a private
   `bool init(const std::string& name)` befriending `ResourceStorage<T>`.
2. `forward_decl.h`: declare the class; `resource_handle.h`: `using XHandle = THandle<X>;`.
3. `resource_manager.h`: a `ResourceStorage<X>` member, `getX(handle)` accessors,
   `getXName(handle)`, the `getStorage<X>` / `getFactoryMap<X>` specialisations;
   `resource_manager.cpp`: clear/unload in `shutdown()`.
4. `resource_handle_meta.cpp`: one `MetaFactory<XHandle>{}.primitiveType("XHandle"_hs, "x").archive(referenceArchive<X, &ResourceManager::getXName>())`
   line gives serialization of the handle by resource name.
5. Use it from components (`Animator::clip`) — the inspector and scene files
   pick it up through reflection.

## Meshes created at runtime

`Mesh::init(filepath)` builds imported meshes as `Usage::Dynamic` (upload heap,
no command list), so models can be loaded while playing or from the editor.
Procedural primitives still use the default-heap path that needs the startup
upload window (`Renderer::beginResourceUpload`).

## Lit shaders

`shaders/lit.shader` (tint) and `shaders/lit_texture.shader` (tint × diffuse)
do Lambert lighting from the engine `LightDirection` semantic. Imported
materials and procedural flat materials (SyrenMovement's) use them; `Primitives::getLitShader()`
/ `getLitMaterial()` expose them for code.

## Node animation

`AnimationClip` holds TRS tracks keyed by **node name**. `Animator` on an
entity plays a clip over that entity's subtree (`AnimationSystem` matches
tracks to descendant entity names and patches their Transforms). SyrenMovement
plays its weapons' `reload` clips this way (`WeaponDef::reloadClip`; authoring
notes are in its `docs/prototype.md`).

Skinning is intentionally out of scope for now: it needs a skinned vertex
format (`BLENDINDICES/BLENDWEIGHT`), a bone palette constant buffer and a
skinning shader. The `ModelData` layer is where skins would be added.

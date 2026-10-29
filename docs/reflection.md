# Reflection: enums and enum masks

## Declaring

```cpp
#include "core/reflected_enum.h"

CS_ENUM(WeaponKind, int, Hitscan, Projectile)   // enum class WeaponKind : int { ... }
```

* Namespace scope only; enumerators are sequential (no `= value`, checked at compile time).
* The macro also emits a name table (`csyrenEnumSource`, found by ADL), so the
  header stays free of the reflection system.
* Helpers: `core::enumName(v)`, `core::enumFromName<E>("Name")`,
  `core::enumValues<E>()`, `core::enumCount<E>()`.

## Masks

```cpp
core::MaskOf<Layer> hitLayers{ Layer::Default, Layer::Player };
hitLayers.test(Layer::Player); hitLayers.set(Layer::Trigger); hitLayers - Layer::Player;
MaskOf<Layer>::all(); MaskOf<Layer>::none(); mask.forEach([](Layer l) { ... });
```

Bit i = enumerator value i, so at most 64 enumerators.

## What the engine does with them

* **Registration is automatic.** A reflected field whose type is a `CS_ENUM`
  or `MaskOf<E>` registers both types the first time `MetaFactory::data<>` sees
  it. `reflection::registerEnum<E>()` does it by hand (enums only used from code).
* **Inspector.** Every type with `MetaType::enumInfo` gets the generic
  `EnumEditor`: a combo for a value, checkboxes (+ All / None) for a mask.
  Edits go through `ArchivedFieldEdit`, so undo/redo works without knowing the
  C++ type.
* **Serialization.**

  | archive | value | mask |
  | --- | --- | --- |
  | text (JSON) | `"kind": "Projectile"` | `"hitLayers": ["Default", "Player"]` |
  | compact (`ArchiveWriter::compact()`) | 32-bit hash of the name | list of hashes |

  Reading also accepts the old integer form (value, or bit set for masks), so
  scenes saved before the migration load unchanged. Unknown names/hashes keep
  the field's default and log a warning; unknown mask elements are skipped and
  the rest of the mask survives.
* **Renames.** `CS_ENUM_ALIAS(WeaponKind, "Rocket", Projectile)` in a .cpp adds
  a read-only name; files are rewritten with the new name on save.

## Named enums (runtime lists)

`reflection::registerNamedEnum<T>("Name", provider)` reflects a value type
holding `std::string name` whose choices come from a function at runtime.
It is saved as the name (unknown names are kept as they are), and the
inspector shows the provider's list. `physics::CollisionLayer` uses it with the
`PhysicsLayers` registry.

## Migrated in this pass

`BodyType`, `ProjectionType` (now `enum class`: `ProjectionType::Perspective`),
`CameraRenderType`, `LightingDebugView`, and the movement prototype's `PilotMove`,
`WeaponKind`, `BotBehaviour` (previously stored as `int`; now in SyrenMovement, whose
scene generator writes names).

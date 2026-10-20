# Physics layers: engine mechanism vs. game rules

**Engine (`lib/physics/physics_layers.h`)** knows nothing about players or grenades:

* `physics::PhysicsLayers` (service) — up to 32 named layers and a symmetric
  collision matrix: `define(name)`, `setCollision(a, b, collide)`,
  `collides(a, b)`, `mask({...})`, `maskExcept({...})`, `load(json)`.
  Layer 0 is always `"Default"`; a fresh registry lets everything collide.
* **Reflected enums as layer sets** (see `docs/reflection.md`):
  `defineEnum<E>()` defines every enumerator by name and remembers the
  indices, then `index(E)`, `bit(E)`, `toMask(MaskOf<E>)`,
  `setCollision(E, E, bool)` and `collides(E, E)` work without strings.
* `physics::CollisionLayer` — the layer *name* used by `RigidBody::layer` and
  `CharacterController::layer`. Reflected as a named enum: the inspector shows
  a combo with the registered layers, scenes store `"layer": "Player"`.
  Unknown names behave like `Default` at runtime (one warning per name) and are
  kept when the scene is saved again. `CollisionLayer::from(E)` builds one from an enum value.
* `RigidBody::continuousCollision` — Jolt `LinearCast` for fast small bodies.
* `QueryFilter::layerMask` — bit `i` = layer index `i`.
* Static-vs-static never collides regardless of the matrix; "static" comes
  from `BodyType`, not from the layer. Jolt object layer =
  `(index << 1) | movingBit`; the broadphase keeps two layers (moving / non-moving).

Data-driven alternative to code registration (additive):

```json
{ "layers": ["Default", "Player", "Projectile"],
  "ignore": [["Player", "Projectile"], ["Projectile", "Projectile"]] }
```

**Game (`game/game_physics.h`)** owns the names and the rules:

```cpp
CS_ENUM(Layer, uint8_t, Default, Player, Projectile, Trigger)
using LayerMask = core::MaskOf<Layer>;

registry.defineEnum<Layer>();                                   // GameSetupSystem::init
registry.setCollision(Layer::Player, Layer::Projectile, false); // + the other rules
body.layer = layers::of(Layer::Projectile);
engine.raycast(origin, dir, range, layers::filter(weapon.hitLayers, shooter));
```

`WeaponDef::hitLayers` / `Projectile::hitLayers` are `MaskOf<Layer>` fields:
checkboxes in the inspector, `["Default"]` in the scene file.

Adding a layer: add the enumerator to `Layer`, add its rules in
`layers::registerAll`, pick it in the inspector. The engine side needs no change.

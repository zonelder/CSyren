#!/usr/bin/env python3
"""Generates assets/movement_polygon.scene: a test range for the pilot movement
and weapon prototype. Run from the repository root:

    python tools/gen_polygon_scene.py

Units are meters, Y is up, the player spawns at the south end looking north (+Z).
Enum fields are written by name (CS_ENUM reflection), masks as name lists.
"""
import json
import math
import os
import struct
import sys

MATERIALS = {
    "ground": "__game_mat_ground__",
    "wall": "__game_mat_wall__",
    "wall_alt": "__game_mat_wall_alt__",
    "platform": "__game_mat_platform__",
    "ramp": "__game_mat_ramp__",
    "accent": "__game_mat_accent__",
    "bot": "__game_mat_bot__",
    "metal": "__game_mat_metal__",
    "lamp": "__game_mat_lamp__",
    "glass": "__game_mat_glass__",
}
MESHES = {"cube": "__primitive_cube__", "sphere": "__primitive_sphere__"}


class SceneBuilder:
    def __init__(self):
        self.entities = []
        self.resources = []
        self._resource_ids = {}

    def resource(self, kind, path):
        key = (kind, path)
        if key not in self._resource_ids:
            self._resource_ids[key] = len(self.resources)
            self.resources.append({"id": len(self.resources), "path": path, "type": kind})
        return self._resource_ids[key]

    def entity(self, name, components, parent=None):
        entry = {"id": len(self.entities), "name": name, "components": components}
        if parent is not None:
            entry["parent"] = parent
        self.entities.append(entry)
        return entry["id"]

    @staticmethod
    def transform(pos, rot=(0.0, 0.0, 0.0, 1.0), scale=(1.0, 1.0, 1.0)):
        return {"position": list(map(float, pos)), "rotation": list(map(float, rot)), "scale": list(map(float, scale))}

    def box(self, name, center, size, material, rot=(0.0, 0.0, 0.0, 1.0), body="static"):
        body_type = {"static": "Static", "kinematic": "Kinematic", "dynamic": "Dynamic"}[body]
        components = {
            "Transform": self.transform(center, rot, size),
            "BoxCollider": {"size": list(map(float, size)), "offset": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "isTrigger": False},
            "RigidBody": {"type": body_type, "mass": 1.0, "useGravity": True, "friction": 0.6, "restitution": 0.0,
                          "linearVelocity": [0.0, 0.0, 0.0], "angularVelocity": [0.0, 0.0, 0.0], "layer": "Default"},
            "MeshFilter": {"mesh": self.resource("mesh", MESHES["cube"])},
            "MeshRenderer": {"material": self.resource("material", MATERIALS[material])},
        }
        return self.entity(name, components)

    def dump(self):
        return {"$version": 2, "entities": self.entities, "resources": self.resources}


def read_glb_nodes(path):
    """Returns (nodes, root_indices) of a .glb (JSON chunk only)."""
    data = open(path, "rb").read()
    magic, version, _ = struct.unpack("<III", data[:12])
    assert magic == 0x46546C67 and version == 2, path
    json_length, _ = struct.unpack("<II", data[12:20])
    doc = json.loads(data[20:20 + json_length])
    roots = doc["scenes"][doc.get("scene", 0)]["nodes"]
    return doc["nodes"], roots


def gltf_to_engine_translation(t):
    return [t[0], t[1], -t[2]]


def gltf_to_engine_rotation(q):
    return [-q[0], -q[1], q[2], q[3]]


def instantiate_glb(b, asset_path, glb_path, parent, extra_root_components, root_transform):
    """Mirrors model_instantiator.h: one entity per node, meshes referenced as <asset>#<mesh>."""
    nodes, roots = read_glb_nodes(glb_path)
    assert len(roots) == 1, "weapon models must have a single root node"
    created = {}

    def visit(index, parent_entity, is_root):
        node = nodes[index]
        components = {}
        if is_root:
            components["Transform"] = root_transform
            components.update(extra_root_components)
        else:
            components["Transform"] = b.transform(
                gltf_to_engine_translation(node.get("translation", [0, 0, 0])),
                gltf_to_engine_rotation(node.get("rotation", [0, 0, 0, 1])),
                node.get("scale", [1, 1, 1]))
        if "mesh" in node:
            components["MeshFilter"] = {"mesh": b.resource("mesh", "%s#%d" % (asset_path, node["mesh"]))}
            # generator meshes have one primitive each; material from that primitive
            components["MeshRenderer"] = {"material": b.resource("material", "%s#mat:%d" % (asset_path, node_material(index)))}
        entity = b.entity(node.get("name", "node%d" % index), components, parent=parent_entity)
        created[index] = entity
        for child in node.get("children", []):
            visit(child, entity, False)
        return entity

    doc_meshes = None

    def node_material(index):
        nonlocal doc_meshes
        if doc_meshes is None:
            data = open(glb_path, "rb").read()
            json_length, _ = struct.unpack("<II", data[12:20])
            doc_meshes = json.loads(data[20:20 + json_length])["meshes"]
        return doc_meshes[nodes[index]["mesh"]]["primitives"][0].get("material", 0)

    return visit(roots[0], parent, True)


def look_rotation(forward, up=(0.0, 1.0, 0.0)):
    """Quaternion (x, y, z, w) whose +Z axis points along `forward` (engine/DirectX convention)."""
    fx, fy, fz = forward
    length = math.sqrt(fx * fx + fy * fy + fz * fz)
    f = (fx / length, fy / length, fz / length)
    # right = up x forward (left-handed), up = forward x right
    r = (up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0])
    rl = math.sqrt(sum(c * c for c in r)) or 1.0
    r = tuple(c / rl for c in r)
    u = (f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0])
    m = (r, u, f)  # rows: right, up, forward
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0:
        s_ = math.sqrt(trace + 1.0) * 2
        return ((m[1][2] - m[2][1]) / s_, (m[2][0] - m[0][2]) / s_, (m[0][1] - m[1][0]) / s_, 0.25 * s_)
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s_ = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        return (0.25 * s_, (m[0][1] + m[1][0]) / s_, (m[2][0] + m[0][2]) / s_, (m[1][2] - m[2][1]) / s_)
    if m[1][1] > m[2][2]:
        s_ = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        return ((m[0][1] + m[1][0]) / s_, 0.25 * s_, (m[1][2] + m[2][1]) / s_, (m[2][0] - m[0][2]) / s_)
    s_ = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
    return ((m[2][0] + m[0][2]) / s_, (m[1][2] + m[2][1]) / s_, 0.25 * s_, (m[0][1] - m[1][0]) / s_)


def rot_x(degrees):
    half = math.radians(degrees) * 0.5
    return (math.sin(half), 0.0, 0.0, math.cos(half))


def rot_y(degrees):
    half = math.radians(degrees) * 0.5
    return (0.0, math.sin(half), 0.0, math.cos(half))


def build():
    b = SceneBuilder()

    # ------------------------------------------------------------------ floor + perimeter
    floor_w, floor_d = 90.0, 110.0
    floor_z = 25.0
    b.box("ground", (0.0, -0.5, floor_z), (floor_w, 1.0, floor_d), "ground")
    wall_h = 9.0
    b.box("perimeter west", (-floor_w / 2 - 0.5, wall_h / 2, floor_z), (1.0, wall_h, floor_d), "wall")
    b.box("perimeter east", (floor_w / 2 + 0.5, wall_h / 2, floor_z), (1.0, wall_h, floor_d), "wall")
    b.box("perimeter south", (0.0, wall_h / 2, floor_z - floor_d / 2 - 0.5), (floor_w + 2, wall_h, 1.0), "wall")
    b.box("perimeter north", (0.0, wall_h / 2, floor_z + floor_d / 2 + 0.5), (floor_w + 2, wall_h, 1.0), "wall")

    # spawn pad
    b.box("spawn pad", (0.0, 0.05, -22.0), (4.0, 0.1, 4.0), "accent")

    # ------------------------------------------------------------------ wallrun corridor (north of spawn)
    # Alternating wall segments force a wallrun -> jump -> wallrun chain. Height 5 m.
    # 6.5 m between the wall faces (walls are 1 m thick), 3 m gaps between segments.
    corridor_x = 3.75
    for name, x, z0, z1 in (
        ("corridor wall R1", corridor_x, 0.0, 10.0),
        ("corridor wall L1", -corridor_x, 8.0, 18.0),
        ("corridor wall R2", corridor_x, 16.0, 26.0),
        ("corridor wall L2", -corridor_x, 24.0, 34.0),
    ):
        b.box(name, (x, 2.5, (z0 + z1) / 2), (1.0, 5.0, z1 - z0), "wall_alt")
    # finishing platform: only reachable by jumping off the last wall
    b.box("corridor finish platform", (0.0, 1.75, 38.0), (6.0, 3.5, 6.0), "platform")
    b.box("corridor finish marker", (0.0, 3.6, 38.0), (1.0, 0.2, 1.0), "accent")

    # ------------------------------------------------------------------ slide ramp (west)
    ramp_len, ramp_angle = 16.0, 18.0
    ramp_x = -22.0
    # top platform with stairs for mantling, ramp descends towards +Z
    b.box("ramp top platform", (ramp_x, 2.0, -16.0), (10.0, 4.0, 8.0), "platform")
    rise = math.sin(math.radians(ramp_angle)) * ramp_len
    run = math.cos(math.radians(ramp_angle)) * ramp_len
    b.box("slide ramp", (ramp_x, 4.0 - rise / 2 - 0.5, -12.0 + run / 2), (10.0, 1.0, ramp_len), "ramp", rot=rot_x(ramp_angle))
    # stairs up to the ramp platform (mantle practice: 1.0 / 1.8 / 2.6 m ledges)
    for i, h in enumerate((1.0, 1.9, 2.8)):
        b.box(f"ramp stair {i}", (ramp_x - 8.0 - i * 3.0, h / 2, -16.0), (3.0, h, 8.0), "platform")
    # small bumps on the landing zone for slide hops
    for i in range(3):
        b.box(f"slide bump {i}", (ramp_x + (i - 1) * 3.0, 0.15, 10.0 + i * 4.0), (2.0, 0.3, 2.0), "accent")

    # ------------------------------------------------------------------ jump towers (east)
    tower_x = 22.0
    for i, (dz, h) in enumerate(((0.0, 1.2), (6.0, 2.2), (12.0, 3.3), (18.0, 4.6))):
        b.box(f"jump tower {i}", (tower_x, h / 2, -18.0 + dz), (4.0, h, 4.0), "platform")
    # a tall wall next to the last tower for a wallrun approach
    b.box("tower wall", (tower_x + 4.5, 3.5, -3.0), (1.0, 7.0, 16.0), "wall_alt")
    # gap jump: two platforms 7 m apart at 2 m height (double jump needed)
    b.box("gap platform A", (34.0, 1.0, 10.0), (5.0, 2.0, 5.0), "platform")
    b.box("gap platform B", (34.0, 1.0, 20.0), (5.0, 2.0, 5.0), "platform")

    # ------------------------------------------------------------------ long wallrun wall (west edge)
    b.box("long wall", (-34.0, 3.0, 35.0), (1.0, 6.0, 40.0), "wall_alt")
    b.box("long wall exit platform", (-30.0, 2.5, 58.0), (6.0, 5.0, 6.0), "platform")

    # ------------------------------------------------------------------ shooting arena (north)
    arena_z = 58.0
    for i, (x, z) in enumerate(((-10.0, 50.0), (10.0, 50.0), (-6.0, 62.0), (6.0, 62.0), (0.0, 70.0))):
        b.box(f"cover {i}", (x, 1.0, z), (3.0, 2.0, 1.5), "wall")
    b.box("arena pillar", (0.0, 3.0, arena_z), (2.0, 6.0, 2.0), "wall_alt")

    # ------------------------------------------------------------------ lighting
    b.entity("lighting settings", {
        "Transform": b.transform((0.0, 0.0, 0.0)),
        "LightingSettings": {"ambientColor": [0.42, 0.47, 0.58, 1.0], "ambientIntensity": 0.35, "exposure": 1.0,
                             "clustered": True, "debugView": "Lit"},
    })
    b.entity("sun", {
        "Transform": b.transform((0.0, 30.0, 0.0), look_rotation((-0.45, -1.0, 0.35))),
        "DirectionalLight": {"color": [1.0, 0.95, 0.88, 1.0], "intensity": 3.0, "enabled": True},
    })

    def lamp(name, pos, color, intensity=25.0, radius=14.0, size=0.35):
        """Emissive block with a point light (the visible lamp)."""
        b.entity(name, {
            "Transform": b.transform(pos, scale=(size, size, size)),
            "MeshFilter": {"mesh": b.resource("mesh", MESHES["cube"])},
            "MeshRenderer": {"material": b.resource("material", MATERIALS["lamp"])},
            "PointLight": {"color": list(map(float, color)) + [1.0], "intensity": float(intensity), "radius": float(radius), "enabled": True},
        })

    def panel(name, center, size, material, rot=(0.0, 0.0, 0.0, 1.0)):
        """Visual-only box (no collider)."""
        b.entity(name, {
            "Transform": b.transform(center, rot, size),
            "MeshFilter": {"mesh": b.resource("mesh", MESHES["cube"])},
            "MeshRenderer": {"material": b.resource("material", MATERIALS[material])},
        })

    # corridor: alternating warm/cold lamps on top of each wall segment
    for i, (x, z, color) in enumerate(((corridor_x, 5.0, (1.0, 0.55, 0.25)), (-corridor_x, 13.0, (0.3, 0.7, 1.0)),
                                       (corridor_x, 21.0, (1.0, 0.55, 0.25)), (-corridor_x, 29.0, (0.3, 0.7, 1.0)))):
        lamp(f"corridor lamp {i}", (x * 0.75, 5.4, z), color, 30.0, 16.0)
    lamp("corridor finish lamp", (0.0, 6.0, 38.0), (0.4, 1.0, 0.5), 40.0, 18.0)
    # spawn + range
    lamp("spawn lamp", (0.0, 4.0, -22.0), (1.0, 0.9, 0.8), 30.0, 16.0)
    lamp("range lamp L", (-8.0, 3.5, -8.0), (0.9, 0.3, 0.3), 25.0, 14.0)
    lamp("range lamp R", (8.0, 3.5, -8.0), (0.3, 0.4, 1.0), 25.0, 14.0)
    # ramp / towers / long wall
    lamp("ramp lamp", (ramp_x, 6.0, -16.0), (1.0, 0.8, 0.5), 35.0, 18.0)
    for i, (dz, h) in enumerate(((0.0, 1.2), (6.0, 2.2), (12.0, 3.3), (18.0, 4.6))):
        lamp(f"tower lamp {i}", (tower_x - 2.6, h + 1.0, -18.0 + dz), (0.5 + 0.12 * i, 0.8, 1.0 - 0.15 * i), 20.0, 12.0)
    for i in range(4):
        lamp(f"long wall lamp {i}", (-32.5, 4.5, 20.0 + i * 10.0), (1.0, 0.7, 0.35), 25.0, 14.0)
    # arena: four warm posts + a cold center light; metal pillar for reflections
    for i, (x, z) in enumerate(((-16.0, 46.0), (16.0, 46.0), (-16.0, 74.0), (16.0, 74.0))):
        b.box(f"arena post {i}", (x, 2.5, z), (0.4, 5.0, 0.4), "wall_alt")
        lamp(f"arena lamp {i}", (x, 5.4, z), (1.0, 0.75, 0.45), 45.0, 22.0)
    lamp("arena center lamp", (0.0, 7.0, arena_z), (0.5, 0.75, 1.0), 60.0, 26.0)
    b.box("arena pillar cap", (0.0, 6.3, arena_z), (2.4, 0.6, 2.4), "metal")
    # material showcase: metal spheres + glass panels next to the spawn
    for i in range(3):
        b.entity(f"metal sphere {i}", {
            "Transform": b.transform((-5.0 + i * 5.0, 0.8, -26.0), scale=(1.2, 1.2, 1.2)),
            "MeshFilter": {"mesh": b.resource("mesh", MESHES["sphere"])},
            "MeshRenderer": {"material": b.resource("material", MATERIALS["metal"])},
        })
    panel("glass panel L", (-6.0, 1.5, -14.0), (3.0, 3.0, 0.1), "glass")
    panel("glass panel R", (6.0, 1.5, -14.0), (3.0, 3.0, 0.1), "glass", rot_y(25.0))

    def bot(name, pos, behaviour, speed=3.0, patrol=(0.0, 0.0, 0.0)):
        components = {
            "Transform": b.transform((pos[0], 0.9, pos[1]), scale=(0.8, 1.8, 0.8)),
            "BoxCollider": {"size": [0.8, 1.8, 0.8], "offset": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "isTrigger": False},
            "RigidBody": {"type": "Kinematic", "mass": 80.0, "useGravity": False, "friction": 0.6, "restitution": 0.0,
                          "linearVelocity": [0.0, 0.0, 0.0], "angularVelocity": [0.0, 0.0, 0.0], "layer": "Default"},
            "MeshFilter": {"mesh": b.resource("mesh", MESHES["cube"])},
            "MeshRenderer": {"material": b.resource("material", MATERIALS["bot"])},
            "Health": {"max": 100.0, "current": 100.0, "respawnTime": 3.0},
            "Bot": {"behaviour": ["Idle", "Patrol", "Chase"][behaviour], "speed": speed, "patrolOffset": list(map(float, patrol)), "chaseRange": 25.0, "stopDistance": 2.5},
        }
        return b.entity(name, components)

    bot("bot idle 0", (-14.0, 56.0), 0)
    bot("bot idle 1", (14.0, 56.0), 0)
    bot("bot idle 2", (-3.0, 76.0), 0)
    bot("bot idle 3", (3.0, 76.0), 0)
    bot("bot patrol 0", (-12.0, 66.0), 1, 3.0, (24.0, 0.0, 0.0))
    bot("bot patrol 1", (12.0, 72.0), 1, 4.0, (-24.0, 0.0, 0.0))
    bot("bot patrol 2", (18.0, 45.0), 1, 2.5, (0.0, 0.0, 20.0))
    bot("bot chase 0", (-6.0, 68.0), 2, 2.0)
    bot("bot chase 1", (6.0, 68.0), 2, 2.0)
    # a target near spawn to test weapons immediately
    bot("bot range 0", (8.0, -14.0), 1, 2.0, (0.0, 0.0, 10.0))
    bot("bot range 1", (-8.0, -14.0), 0)

    # ------------------------------------------------------------------ player
    rifle = {
        "name": "R-201 Carbine", "kind": "Hitscan", "hitLayers": ["Default"], "automatic": True, "damage": 25.0, "rpm": 720.0, "magazine": 30,
        "reloadClip": "models/rifle.glb#anim:reload",
        "reloadTime": 1.6, "spread": 0.5, "spreadMoving": 1.6, "recoilPitch": 0.32, "recoilYaw": 0.22, "range": 300.0,
        "projectileSpeed": 0.0, "projectileRadius": 0.1, "projectileLifetime": 0.0, "projectileGravity": False,
        "explosionRadius": 0.0, "explosionImpulse": 0.0,
    }
    launcher = {
        "name": "Softball Launcher", "kind": "Projectile", "hitLayers": ["Default"], "automatic": False, "damage": 120.0, "rpm": 110.0, "magazine": 6,
        "reloadClip": "models/launcher.glb#anim:reload",
        "reloadTime": 2.2, "spread": 0.2, "spreadMoving": 0.6, "recoilPitch": 1.6, "recoilYaw": 0.4, "range": 300.0,
        "projectileSpeed": 24.0, "projectileRadius": 0.14, "projectileLifetime": 4.0, "projectileGravity": True,
        "explosionRadius": 3.5, "explosionImpulse": 7.0,
    }
    player = b.entity("pilot", {
        "Transform": b.transform((0.0, 0.3, -22.0)),
        "PlayerTag": {"version": 1},
        "CharacterController": {"radius": 0.4, "height": 1.8, "maxSlopeDegrees": 50.0, "stepHeight": 0.45, "stickToFloor": 0.5, "mass": 70.0, "layer": "Player"},
        "PilotConfig": {},
        "PilotState": {"yaw": 0.0, "pitch": 0.0, "spawnPoint": [0.0, 0.3, -22.0], "spawnYaw": 0.0},
        "WeaponInventory": {"primary": rifle, "secondary": launcher, "active": 0},
    })
    camera = b.entity("pilot camera", {
        "Transform": b.transform((0.0, 1.62, 0.0)),
        "Camera": {"render_type": "EachFrame", "far": 600.0, "near": 0.03, "aspectRatio": 1.7777778, "fov": 68.0, "priority": 10,
                   "projection": "Perspective", "background": [0.52, 0.66, 0.86, 1.0], "viewportRect": {"x": 0.0, "y": 0.0, "w": 1.0, "h": 1.0}},
    }, parent=player)

    # First-person view model: weapon models imported under the camera.
    viewmodel = b.entity("viewmodel", {
        "Transform": b.transform((0.24, -0.22, 0.42)),
        "ViewModel": {},
    }, parent=camera)
    models_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "models")
    for slot, (asset, rest) in enumerate((("models/rifle.glb", (0.0, 0.0, 0.0)), ("models/launcher.glb", (0.0, -0.01, 0.0)))):
        glb = os.path.join(models_dir, os.path.basename(asset))
        if not os.path.exists(glb):
            print("missing %s, run tools/make_weapon_models.py first" % glb)
            continue
        instantiate_glb(b, asset, glb, viewmodel,
                        {"ViewModelSlot": {"slot": slot, "muzzleNode": "muzzle"}, "Animator": {"time": 0.0, "speed": 1.0, "loop": False, "playing": False}},
                        b.transform(rest))

    return b.dump()


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "assets", "movement_polygon.scene")
    scene = build()
    with open(target, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(scene, handle, indent=4)
        handle.write("\n")
    print(f"wrote {target}: {len(scene['entities'])} entities")


if __name__ == "__main__":
    main()

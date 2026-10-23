#!/usr/bin/env python3
"""Generates placeholder weapon models as glTF binaries (.glb) with node
hierarchies and reload animations, so the import pipeline can be tested
without a DCC package. Replace them with Blender exports that keep the same
node names ("magazine", "bolt", "drum", "muzzle") and clip name ("reload").

    python tools/make_weapon_models.py            # writes assets/models/*.glb

glTF conventions: right-handed, +Y up, the barrel points along -Z. The engine
importer mirrors Z, so the barrel ends up pointing +Z (forward) in game.
"""
import json
import math
import os
import struct
import sys


def quat_axis(axis, degrees):
    x, y, z = axis
    n = math.sqrt(x * x + y * y + z * z) or 1.0
    h = math.radians(degrees) * 0.5
    s = math.sin(h) / n
    return [x * s, y * s, z * s, math.cos(h)]


class GlbBuilder:
    def __init__(self):
        self.buffer = bytearray()
        self.buffer_views = []
        self.accessors = []
        self.meshes = []
        self.materials = []
        self.nodes = []
        self.animations = []
        self._material_cache = {}

    # ------------------------------------------------------------------ buffers
    def _view(self, data, target=None):
        while len(self.buffer) % 4:
            self.buffer.append(0)
        offset = len(self.buffer)
        self.buffer += data
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            view["target"] = target
        self.buffer_views.append(view)
        return len(self.buffer_views) - 1

    def _accessor(self, data, component_type, count, kind, target=None, minmax=False, values=None):
        view = self._view(data, target)
        acc = {"bufferView": view, "componentType": component_type, "count": count, "type": kind}
        if minmax and values:
            n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[kind]
            acc["min"] = [min(v[i] for v in values) for i in range(n)]
            acc["max"] = [max(v[i] for v in values) for i in range(n)]
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def floats(self, values, kind, target=None, minmax=False):
        flat = [c for v in values for c in v] if kind != "SCALAR" else list(values)
        data = struct.pack("<%df" % len(flat), *flat)
        return self._accessor(data, 5126, len(values), kind, target, minmax, values if kind != "SCALAR" else None)

    def indices(self, values):
        data = struct.pack("<%dH" % len(values), *values)
        return self._accessor(data, 5123, len(values), "SCALAR", 34963)

    # ------------------------------------------------------------------ materials / meshes
    def material(self, color, name=None):
        key = tuple(color)
        if key in self._material_cache:
            return self._material_cache[key]
        self.materials.append({
            "name": name or "mat%d" % len(self.materials),
            "pbrMetallicRoughness": {"baseColorFactor": [color[0], color[1], color[2], 1.0], "metallicFactor": 0.2, "roughnessFactor": 0.7},
        })
        self._material_cache[key] = len(self.materials) - 1
        return self._material_cache[key]

    def mesh(self, name, positions, normals, triangles, color):
        prim = {
            "attributes": {
                "POSITION": self.floats(positions, "VEC3", 34962, minmax=True),
                "NORMAL": self.floats(normals, "VEC3", 34962),
            },
            "indices": self.indices(triangles),
            "material": self.material(color),
            "mode": 4,
        }
        self.meshes.append({"name": name, "primitives": [prim]})
        return len(self.meshes) - 1

    def box(self, name, size, color, center=(0.0, 0.0, 0.0)):
        hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
        cx, cy, cz = center
        faces = [
            ((0, 0, 1), [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]),
            ((0, 0, -1), [(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)]),
            ((-1, 0, 0), [(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)]),
            ((1, 0, 0), [(hx, -hy, hz), (hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz)]),
            ((0, 1, 0), [(-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz), (-hx, hy, -hz)]),
            ((0, -1, 0), [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)]),
        ]
        positions, normals, tris = [], [], []
        for normal, corners in faces:
            base = len(positions)
            for p in corners:
                positions.append((p[0] + cx, p[1] + cy, p[2] + cz))
                normals.append(normal)
            tris += [base, base + 1, base + 2, base, base + 2, base + 3]
        return self.mesh(name, positions, normals, tris, color)

    def cylinder(self, name, radius, length, color, axis="z", segments=14, center=(0.0, 0.0, 0.0)):
        """Cylinder along the given axis, centered at `center`."""
        positions, normals, tris = [], [], []
        cx, cy, cz = center

        def place(u, v, w):
            # (u, v) in the circle plane, w along the axis
            if axis == "z":
                return (u, v, w)
            if axis == "x":
                return (w, v, u)
            return (u, w, v)

        half = length / 2
        for i in range(segments):
            a0 = 2 * math.pi * i / segments
            a1 = 2 * math.pi * (i + 1) / segments
            n0 = (math.cos(a0), math.sin(a0))
            n1 = (math.cos(a1), math.sin(a1))
            base = len(positions)
            for (nx, ny), w in ((n0, -half), (n1, -half), (n1, half), (n0, half)):
                p = place(nx * radius, ny * radius, w)
                positions.append((p[0] + cx, p[1] + cy, p[2] + cz))
                nn = place(nx, ny, 0.0)
                normals.append(nn)
            tris += [base, base + 1, base + 2, base, base + 2, base + 3]
        for sign in (-1, 1):
            cap_normal = place(0.0, 0.0, float(sign))
            center_index = len(positions)
            cp = place(0.0, 0.0, sign * half)
            positions.append((cp[0] + cx, cp[1] + cy, cp[2] + cz))
            normals.append(cap_normal)
            ring = []
            for i in range(segments):
                a = 2 * math.pi * i / segments
                p = place(math.cos(a) * radius, math.sin(a) * radius, sign * half)
                positions.append((p[0] + cx, p[1] + cy, p[2] + cz))
                normals.append(cap_normal)
                ring.append(center_index + 1 + i)
            for i in range(segments):
                a, b = ring[i], ring[(i + 1) % segments]
                tris += [center_index, a, b] if sign > 0 else [center_index, b, a]
        if axis != "z":
            # place() swaps two coordinates for "x" / "y": a mirror, which flips the winding.
            tris = [i for t in range(0, len(tris), 3) for i in (tris[t], tris[t + 2], tris[t + 1])]
        return self.mesh(name, positions, normals, tris, color)

    # ------------------------------------------------------------------ nodes / animation
    def node(self, name, mesh=None, translation=None, rotation=None, scale=None, children=None):
        node = {"name": name}
        if mesh is not None:
            node["mesh"] = mesh
        if translation:
            node["translation"] = list(translation)
        if rotation:
            node["rotation"] = list(rotation)
        if scale:
            node["scale"] = list(scale)
        if children:
            node["children"] = list(children)
        self.nodes.append(node)
        return len(self.nodes) - 1

    def animation(self, name, channels):
        """channels: list of (node, path, [(time, value)], interpolation)."""
        samplers, chans = [], []
        for node, path, keys, interpolation in channels:
            times = [k[0] for k in keys]
            values = [k[1] for k in keys]
            kind = "VEC4" if path == "rotation" else "VEC3"
            sampler = {
                "input": self.floats(times, "SCALAR"),
                "output": self.floats(values, kind),
                "interpolation": interpolation,
            }
            # glTF requires min/max on animation input accessors
            self.accessors[sampler["input"]]["min"] = [min(times)]
            self.accessors[sampler["input"]]["max"] = [max(times)]
            samplers.append(sampler)
            chans.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path}})
        self.animations.append({"name": name, "samplers": samplers, "channels": chans})

    # ------------------------------------------------------------------ output
    def write(self, path, roots):
        while len(self.buffer) % 4:
            self.buffer.append(0)
        doc = {
            "asset": {"version": "2.0", "generator": "csyren make_weapon_models.py"},
            "scene": 0,
            "scenes": [{"nodes": list(roots)}],
            "nodes": self.nodes,
            "meshes": self.meshes,
            "materials": self.materials,
            "accessors": self.accessors,
            "bufferViews": self.buffer_views,
            "buffers": [{"byteLength": len(self.buffer)}],
        }
        if self.animations:
            doc["animations"] = self.animations
        json_bytes = json.dumps(doc, separators=(",", ":")).encode("utf-8")
        while len(json_bytes) % 4:
            json_bytes += b" "
        total = 12 + 8 + len(json_bytes) + 8 + len(self.buffer)
        with open(path, "wb") as handle:
            handle.write(struct.pack("<III", 0x46546C67, 2, total))
            handle.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))
            handle.write(json_bytes)
            handle.write(struct.pack("<II", len(self.buffer), 0x004E4942))
            handle.write(bytes(self.buffer))
        print("wrote %s: %d nodes, %d meshes, %d animations" % (path, len(self.nodes), len(self.meshes), len(self.animations)))


DARK = (0.16, 0.17, 0.19)
STEEL = (0.32, 0.34, 0.38)
MAG = (0.24, 0.25, 0.27)
ORANGE = (0.95, 0.55, 0.15)
TEAL = (0.20, 0.65, 0.70)


def build_rifle(path):
    b = GlbBuilder()
    receiver = b.node("receiver", b.box("receiver", (0.06, 0.07, 0.30), DARK))
    handguard = b.node("handguard", b.box("handguard", (0.05, 0.05, 0.22), STEEL), translation=(0, 0, -0.24))
    barrel = b.node("barrel", b.cylinder("barrel", 0.012, 0.32, DARK, "z"), translation=(0, 0.012, -0.44))
    stock = b.node("stock", b.box("stock", (0.05, 0.06, 0.22), STEEL), translation=(0, -0.01, 0.25))
    grip = b.node("grip", b.box("grip", (0.035, 0.10, 0.045), DARK), translation=(0, -0.08, 0.05), rotation=quat_axis((1, 0, 0), -15))
    sight = b.node("sight", b.box("sight", (0.02, 0.03, 0.05), ORANGE), translation=(0, 0.05, -0.04))
    magazine = b.node("magazine", b.box("magazine", (0.03, 0.14, 0.06), MAG, center=(0, -0.07, 0)), translation=(0, -0.03, -0.05))
    bolt = b.node("bolt", b.box("bolt", (0.015, 0.015, 0.05), ORANGE), translation=(0.036, 0.025, -0.02))
    muzzle = b.node("muzzle", translation=(0, 0.012, -0.61))
    root = b.node("rifle", children=[receiver, handguard, barrel, stock, grip, sight, magazine, bolt, muzzle])

    identity = [0, 0, 0, 1]
    tilt = quat_axis((1, 0, 0), -22)
    b.animation("reload", [
        (magazine, "translation", [(0.0, (0, -0.03, -0.05)), (0.30, (0, -0.21, -0.02)), (0.55, (0, -0.24, -0.01)),
                                   (0.80, (0, -0.24, -0.01)), (1.10, (0, -0.03, -0.05)), (1.60, (0, -0.03, -0.05))], "LINEAR"),
        (magazine, "rotation", [(0.0, identity), (0.30, tilt), (0.80, tilt), (1.10, identity), (1.60, identity)], "LINEAR"),
        (bolt, "translation", [(0.0, (0.036, 0.025, -0.02)), (1.15, (0.036, 0.025, -0.02)), (1.30, (0.036, 0.025, 0.03)),
                               (1.45, (0.036, 0.025, -0.02)), (1.60, (0.036, 0.025, -0.02))], "LINEAR"),
    ])
    b.write(path, [root])


def build_launcher(path):
    b = GlbBuilder()
    body = b.node("body", b.box("body", (0.08, 0.09, 0.26), DARK))
    barrel = b.node("barrel", b.cylinder("barrel", 0.032, 0.30, STEEL, "z"), translation=(0, 0.02, -0.28))
    drum = b.node("drum", b.cylinder("drum", 0.065, 0.08, MAG, "z"), translation=(0, -0.01, -0.09))
    drum_mark = b.node("drum_mark", b.box("drum_mark", (0.015, 0.02, 0.085), TEAL), translation=(0, 0.055, 0))
    b.nodes[drum]["children"] = [drum_mark]
    grip = b.node("grip", b.box("grip", (0.035, 0.10, 0.05), DARK), translation=(0, -0.09, 0.06), rotation=quat_axis((1, 0, 0), -15))
    stock = b.node("stock", b.box("stock", (0.05, 0.06, 0.20), STEEL), translation=(0, -0.01, 0.22))
    sight = b.node("sight", b.box("sight", (0.02, 0.03, 0.05), ORANGE), translation=(0, 0.06, -0.02))
    muzzle = b.node("muzzle", translation=(0, 0.02, -0.44))
    root = b.node("launcher", children=[body, barrel, drum, grip, stock, sight, muzzle])

    spin = [(0.0, [0, 0, 0, 1]), (0.35, [0, 0, 0, 1])]
    for i in range(1, 9):
        spin.append((0.45 + 1.0 * i / 8, quat_axis((0, 0, 1), 45 * i)))
    spin += [(2.0, quat_axis((0, 0, 1), 360)), (2.2, quat_axis((0, 0, 1), 360))]
    b.animation("reload", [
        (drum, "translation", [(0.0, (0, -0.01, -0.09)), (0.35, (0.10, -0.01, -0.09)), (1.6, (0.10, -0.01, -0.09)),
                               (2.0, (0, -0.01, -0.09)), (2.2, (0, -0.01, -0.09))], "LINEAR"),
        (drum, "rotation", spin, "LINEAR"),
    ])
    b.write(path, [root])


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "assets", "models")
    os.makedirs(target, exist_ok=True)
    build_rifle(os.path.join(target, "rifle.glb"))
    build_launcher(os.path.join(target, "launcher.glb"))


if __name__ == "__main__":
    main()

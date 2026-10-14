#!/usr/bin/env python3
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Reads a binary glTF (.glb) with numpy: its nodes, meshes (positions,
normals, UVs, colors, joints and weights, triangles), materials (colors and
textures), skins and animations. `python3 glb.py FILE.glb` prints a summary."""
import json
import struct
import sys

import numpy as np

COMP = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
SIZE = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


class Glb:
    def __init__(self, path):
        data = open(path, "rb").read()
        magic, _version, _length = struct.unpack_from("<4sII", data, 0)
        assert magic == b"glTF", path
        at, self.bin = 12, b""
        while at < len(data):
            n, kind = struct.unpack_from("<I4s", data, at)
            chunk = data[at + 8 : at + 8 + n]
            if kind == b"JSON":
                self.j = json.loads(chunk)
            elif kind == b"BIN\x00":
                self.bin = chunk
            at += 8 + n

    def accessor(self, i):
        a = self.j["accessors"][i]
        v = self.j["bufferViews"][a["bufferView"]]
        dt = np.dtype(COMP[a["componentType"]])
        k = SIZE[a["type"]]
        start = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = v.get("byteStride", 0) or dt.itemsize * k
        out = np.empty((a["count"], k), dt)
        for c in range(a["count"]):
            out[c] = np.frombuffer(self.bin, dt, k, start + c * stride)
        if a.get("normalized") and dt != np.float32:
            out = out.astype(np.float32) / np.iinfo(dt).max
        return out

    def image(self, i):
        """An image's PNG/JPEG bytes."""
        im = self.j["images"][i]
        v = self.j["bufferViews"][im["bufferView"]]
        o = v.get("byteOffset", 0)
        return self.bin[o : o + v["byteLength"]]


def summary(path):
    g = Glb(path)
    j = g.j
    print(path)
    for m in j.get("meshes", []):
        tris = 0
        for p in m["primitives"]:
            n = j["accessors"][p["indices"]]["count"] // 3 if "indices" in p else j["accessors"][p["attributes"]["POSITION"]]["count"] // 3
            tris += n
        print(f"  mesh {m.get('name')}: {tris} triangles, attributes {sorted(m['primitives'][0]['attributes'])}")
    for i, mat in enumerate(j.get("materials", [])):
        pbr = mat.get("pbrMetallicRoughness", {})
        print(f"  material {i} {mat.get('name')}: color {pbr.get('baseColorFactor')} texture {pbr.get('baseColorTexture')}")
    for s in j.get("skins", []):
        print(f"  skin {s.get('name')}: {len(s['joints'])} joints")
    for a in j.get("animations", []):
        frames = max(j["accessors"][c["input"]]["max"][0] for c in a["samplers"])
        print(f"  animation {a.get('name')}: {frames:.2f} s, {len(a['channels'])} channels")
    print(f"  images {len(j.get('images', []))}, nodes {len(j.get('nodes', []))}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        summary(p)

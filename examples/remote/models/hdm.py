#!/usr/bin/env python3
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Turns a glTF model (.glb) into go-link HD's model file (.hdm), integers
only, so the engine stays exact (src/3d/model3d.h):

  python3 hdm.py IN.glb OUT.hdm [--clips Idle,Walk,...] [--height METERS] [--fps 30]

- Every triangle gets one color: its material's, or its texture's at the
  triangle's middle (low-poly packs paint with palettes).
- A skinned model keeps its main bones (fingers and helpers go to their
  nearest kept parent) and its animations, sampled `fps` times a second as
  each bone's turn and place relative to its parent.
- Positions in meters with y up and the model's front toward +z (glTF's);
  --height scales it to that height.
"""
import argparse
import struct
import zlib

import numpy as np

from glb import Glb

DROP = ("Index", "Middle", "Ring", "Pinky", "Thumb", "PT.")


# ---- small math ----

def quat_to_mat(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def mat_to_quat(m):
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = np.sqrt(t + 1) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, s / 4]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [s / 4, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, s / 4, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, s / 4, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def slerp(a, b, t):
    a, b = np.array(a, float), np.array(b, float)
    d = np.dot(a, b)
    if d < 0:
        b, d = -b, -d
    if d > 0.9995:
        r = a + (b - a) * t
        return r / np.linalg.norm(r)
    th = np.arccos(d)
    return (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / np.sin(th)


def trs(t=None, r=None, s=None):
    m = np.eye(4)
    if s is not None:
        m[:3, :3] = np.diag(s)
    if r is not None:
        m[:3, :3] = quat_to_mat(r) @ m[:3, :3]
    if t is not None:
        m[:3, 3] = t
    return m


# ---- PNG (8-bit RGB or RGBA, not interlaced), for palette textures ----

def png(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "only PNG textures"
    at, idat, w = 8, b"", 0
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at : at + 8])
        body = data[at + 8 : at + 8 + n]
        if kind == b"IHDR":
            w, h, depth, color = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and color in (2, 6), "PNG must be 8-bit RGB or RGBA"
            ch = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat += body
        at += 12 + n
    raw = zlib.decompress(idat)
    out = np.zeros((h, w * ch), np.int32)
    stride = w * ch
    prev = np.zeros(stride, np.int32)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = np.frombuffer(raw, np.uint8, stride, y * (stride + 1) + 1).astype(np.int32)
        cur = np.zeros(stride, np.int32)
        for x in range(stride):
            a = cur[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 0:
                v = line[x]
            elif f == 1:
                v = line[x] + a
            elif f == 2:
                v = line[x] + b
            elif f == 3:
                v = line[x] + (a + b) // 2
            else:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                v = line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)
            cur[x] = v & 255
        out[y] = cur
        prev = cur
    return out.reshape(h, w, ch)


def lin_to_srgb(c):
    c = max(0.0, min(1.0, c))
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


# ---- the scene's nodes, at rest or at a time of an animation ----

class Scene:
    def __init__(self, g):
        self.g = g
        self.nodes = g.j["nodes"]
        self.parent = {}
        for i, n in enumerate(self.nodes):
            for c in n.get("children", []):
                self.parent[c] = i

    def locals_at(self, anim=None, t=0.0):
        """Each node's (translation, rotation, scale) at time t of an animation (None: at rest)."""
        out = [[np.array(n.get("translation", [0, 0, 0]), float), np.array(n.get("rotation", [0, 0, 0, 1]), float),
                np.array(n.get("scale", [1, 1, 1]), float)] for n in self.nodes]
        if anim is None:
            return out
        for ch in anim["channels"]:
            node, path = ch["target"].get("node"), ch["target"]["path"]
            if node is None or path not in ("translation", "rotation", "scale"):
                continue
            s = anim["samplers"][ch["sampler"]]
            times = self.g.accessor(s["input"])[:, 0]
            vals = self.g.accessor(s["output"])
            if s.get("interpolation") == "CUBICSPLINE":
                vals = vals[1::3]
            if t <= times[0]:
                v = vals[0]
            elif t >= times[-1]:
                v = vals[-1]
            else:
                k = int(np.searchsorted(times, t) - 1)
                f = (t - times[k]) / (times[k + 1] - times[k])
                if s.get("interpolation") == "STEP":
                    v = vals[k]
                elif path == "rotation":
                    v = slerp(vals[k], vals[k + 1], f)
                else:
                    v = vals[k] * (1 - f) + vals[k + 1] * f
            out[node][{"translation": 0, "rotation": 1, "scale": 2}[path]] = np.array(v, float)
        return out

    def worlds(self, locs):
        memo = {}

        def world(i):
            if i not in memo:
                m = trs(*locs[i])
                memo[i] = world(self.parent[i]) @ m if i in self.parent else m
            return memo[i]

        return [world(i) for i in range(len(self.nodes))]


def rigid(m):
    """A world matrix without its scale: (rotation, translation)."""
    r = m[:3, :3].copy()
    for k in range(3):
        r[:, k] /= np.linalg.norm(r[:, k])
    return r, m[:3, 3].copy()


def obj_model(path):
    """An OBJ and its MTL: (vertices, normals, triangles with colors), no bones (furniture)."""
    import os

    mats, cur, kd = {}, None, {}
    v, vn, verts, norms, tris = [], [], [], [], []
    for line in open(path, encoding="utf-8", errors="ignore"):
        w = line.split()
        if not w:
            continue
        if w[0] == "mtllib":
            for ml in open(os.path.join(os.path.dirname(path), " ".join(w[1:])), encoding="utf-8", errors="ignore"):
                m = ml.split()
                if m and m[0] == "newmtl":
                    name = " ".join(m[1:])
                elif m and m[0] == "Kd":
                    kd[name] = [float(x) for x in m[1:4]]
        elif w[0] == "v":
            v.append([float(x) for x in w[1:4]])
        elif w[0] == "vn":
            vn.append([float(x) for x in w[1:4]])
        elif w[0] == "usemtl":
            cur = " ".join(w[1:])
        elif w[0] == "f":
            corners = []
            for c in w[1:]:
                parts = c.split("/")
                pi = int(parts[0]) - 1 if int(parts[0]) > 0 else len(v) + int(parts[0])
                ni = int(parts[2]) - 1 if len(parts) > 2 and parts[2] else None
                verts.append(v[pi])
                norms.append(vn[ni] if ni is not None and ni < len(vn) else [0, 1, 0])
                corners.append(len(verts) - 1)
            c = kd.get(cur, [0.8, 0.8, 0.8])
            rgb = [int(round(lin_to_srgb(x) * 255)) for x in c]
            color = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2]
            for k in range(1, len(corners) - 1):
                tris.append((corners[0], corners[k], corners[k + 1], color))
    return np.array(verts, float), np.array(norms, float), tris


def convert(path, out, clips, height, fps):
    if path.lower().endswith(".obj"):
        return convert_static(path, out, height)
    g = Glb(path)
    j = g.j
    sc = Scene(g)
    rest = sc.worlds(sc.locals_at())
    skin = j["skins"][0] if j.get("skins") else None
    # the bones kept, each with its parent among them
    if skin:
        joints = skin["joints"]
        names = [j["nodes"][i].get("name", "") for i in joints]
        keep = [k for k, n in enumerate(names) if not n.startswith(DROP)]
        node_to_k = {joints[k]: k for k in range(len(joints))}

        def kept_parent(k):
            node = joints[k]
            while node in sc.parent:
                node = sc.parent[node]
                if node in node_to_k and node_to_k[node] in keep:
                    return keep.index(node_to_k[node])
            return -1

        def kept_self(k):
            node = joints[k]
            while True:
                if node in node_to_k and node_to_k[node] in keep:
                    return keep.index(node_to_k[node])
                node = sc.parent[node]

        parents = [kept_parent(k) for k in keep]
        remap = [kept_self(k) for k in range(len(joints))]
        ibm = g.accessor(skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
    # vertices at rest in the world, triangles with their colors
    verts, norms, jw, tris = [], [], [], []
    for ni, n in enumerate(j["nodes"]):
        if "mesh" not in n:
            continue
        for p in j["meshes"][n["mesh"]]["primitives"]:
            a = p["attributes"]
            pos = g.accessor(a["POSITION"]).astype(float)
            nrm = g.accessor(a["NORMAL"]).astype(float) if "NORMAL" in a else np.zeros_like(pos)
            idx = g.accessor(p["indices"])[:, 0] if "indices" in p else np.arange(len(pos))
            if skin and "JOINTS_0" in a:
                jj = g.accessor(a["JOINTS_0"]).astype(int)
                ww = g.accessor(a["WEIGHTS_0"]).astype(float)
                skinm = [rest[joints[k]] @ ibm[k] for k in range(len(joints))]
                wp, wn = np.zeros_like(pos), np.zeros_like(nrm)
                for c in range(4):
                    for v in range(len(pos)):
                        if ww[v, c] > 0:
                            m = skinm[jj[v, c]]
                            wp[v] += ww[v, c] * (m[:3, :3] @ pos[v] + m[:3, 3])
                            wn[v] += ww[v, c] * (m[:3, :3] @ nrm[v])
                pos, nrm = wp, wn
                vj = np.zeros((len(pos), 4), int)
                vw = np.zeros((len(pos), 4), float)
                for v in range(len(pos)):
                    acc = {}
                    for c in range(4):
                        if ww[v, c] > 0:
                            k = remap[jj[v, c]]
                            acc[k] = acc.get(k, 0) + ww[v, c]
                    best = sorted(acc.items(), key=lambda kv: -kv[1])[:4]
                    for c, (k, w_) in enumerate(best):
                        vj[v, c], vw[v, c] = k, w_
            else:
                m = rest[ni]
                pos = (m[:3, :3] @ pos.T).T + m[:3, 3]
                nrm = (m[:3, :3] @ nrm.T).T
                vj = np.zeros((len(pos), 4), int)
                vw = np.zeros((len(pos), 4), float)
                vw[:, 0] = 1
            nrm = nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
            # the color: the material's, times its texture at each triangle's middle, times the vertex colors
            mat = j["materials"][p["material"]] if "material" in p else {}
            pbr = mat.get("pbrMetallicRoughness", {})
            base = np.array(pbr.get("baseColorFactor", [1, 1, 1, 1])[:3], float)
            tex = None
            if "baseColorTexture" in pbr and "TEXCOORD_0" in a:
                im = j["textures"][pbr["baseColorTexture"]["index"]]["source"]
                tex = png(g.image(im)).astype(float) / 255
                uv = g.accessor(a["TEXCOORD_0"]).astype(float)
            col = g.accessor(a["COLOR_0"]).astype(float)[:, :3] if "COLOR_0" in a else None
            first = len(verts)
            verts += list(pos)
            norms += list(nrm)
            for v in range(len(pos)):
                total = vw[v].sum() or 1
                jw.append((vj[v], vw[v] / total))
            for t in range(0, len(idx) - 2, 3):
                c = base.copy()
                tri = idx[t : t + 3]
                if tex is not None:
                    u, v_ = uv[tri].mean(0)
                    h, w_ = tex.shape[:2]
                    px = tex[int(min(h - 1, max(0, (v_ % 1) * h))), int(min(w_ - 1, max(0, (u % 1) * w_)))]
                    # texture pixels are sRGB: to linear, to multiply
                    c = c * np.array([((x + 0.055) / 1.055) ** 2.4 if x > 0.04045 else x / 12.92 for x in px[:3]])
                if col is not None:
                    c = c * col[tri].mean(0)
                rgb = [int(round(lin_to_srgb(x) * 255)) for x in c]
                tris.append((first + tri[0], first + tri[1], first + tri[2], (rgb[0] << 16) | (rgb[1] << 8) | rgb[2]))
    verts = np.array(verts)
    lo, hi = verts.min(0), verts.max(0)
    scale = height / (hi[1] - lo[1]) if height else 1.0
    # stand on y = 0, centered in x and z
    shift = np.array([-(lo[0] + hi[0]) / 2, -lo[1], -(lo[2] + hi[2]) / 2])
    verts = (verts + shift) * scale

    def fx(v):
        return int(round(v * 65536))

    def q14(q):
        return [int(round(x * 16384)) for x in q]

    def pose_of(world_list):
        """Each kept bone's turn and place relative to its kept parent (the root: in the model's space)."""
        out = []
        g_r, g_t = [], []
        for k in keep:
            r, t = rigid(world_list[joints[k]])
            g_r.append(r)
            g_t.append((t + shift) * scale)
        for i, k in enumerate(keep):
            p = parents[i]
            if p < 0:
                r, t = g_r[i], g_t[i]
            else:
                r = g_r[p].T @ g_r[i]
                t = g_r[p].T @ (g_t[i] - g_t[p])
            out.append((mat_to_quat(r), t))
        return out

    blob = bytearray()
    nj = len(keep) if skin else 0
    anims = []
    if skin:
        want = [c for c in clips.split(",") if c] if clips else None
        for a in j.get("animations", []):
            name = a.get("name", "").split("|")[-1]
            if want is not None and name not in want:
                continue
            end = max(j["accessors"][s["input"]]["max"][0] for s in a["samplers"])
            frames = max(1, int(round(end * fps)))
            anims.append((name, [pose_of(sc.worlds(sc.locals_at(a, f / fps))) for f in range(frames)]))
    blob += struct.pack("<4s5i", b"HDM1", 1, len(verts), len(tris), nj, len(anims))
    for v in range(len(verts)):
        n = norms[v]
        jj, ww = jw[v]
        wq = [int(round(x * 255)) for x in ww]
        wq[0] += 255 - sum(wq)
        blob += struct.pack("<3i3h4B4B", *[fx(x) for x in verts[v]], *[int(round(x * 16384)) for x in n], *[int(x) for x in jj], *wq)
    for a, b, c, color in tris:
        blob += struct.pack("<3HHI", a, b, c, 0, color)
    if skin:
        rest_pose = pose_of(rest)
        for i, k in enumerate(keep):
            nm = names[k].encode()[:23]
            q, t = rest_pose[i]
            blob += struct.pack("<i24s4h3i", parents[i], nm, *q14(q), *[fx(x) for x in t])
        for name, frames in anims:
            blob += struct.pack("<24s2i", name.encode()[:23], len(frames), fps)
            for pose in frames:
                for q, t in pose:
                    blob += struct.pack("<4h3i", *q14(q), *[fx(x) for x in t])
    open(out, "wb").write(bytes(blob))
    print(f"{out}: {len(verts)} vertices, {len(tris)} triangles, {nj} bones, clips {[a[0] for a in anims]}, "
          f"{(hi[1] - lo[1]) * scale:.2f} m tall, {len(blob)} bytes")


def convert_static(path, out, height):
    """Furniture: its triangles only, standing on y 0 and centered in x and z."""
    verts, norms, tris = obj_model(path)
    lo, hi = verts.min(0), verts.max(0)
    scale = height / (hi[1] - lo[1]) if height else 1.0
    shift = np.array([-(lo[0] + hi[0]) / 2, -lo[1], -(lo[2] + hi[2]) / 2])
    verts = (verts + shift) * scale
    norms = norms / np.maximum(np.linalg.norm(norms, axis=1, keepdims=True), 1e-9)
    assert len(verts) <= 65535, "too many vertices"
    blob = bytearray(struct.pack("<4s5i", b"HDM1", 1, len(verts), len(tris), 0, 0))
    for p, n in zip(verts, norms):
        blob += struct.pack("<3i3h4B4B", *[int(round(x * 65536)) for x in p], *[int(round(x * 16384)) for x in n], 0, 0, 0, 0, 255, 0, 0, 0)
    for a, b, c, color in tris:
        blob += struct.pack("<3HHI", a, b, c, 0, color)
    open(out, "wb").write(bytes(blob))
    size = (hi - lo) * scale
    print(f"{out}: {len(verts)} vertices, {len(tris)} triangles, {size[0]:.2f} x {size[1]:.2f} x {size[2]:.2f} m, {len(blob)} bytes")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--clips", default="")
    ap.add_argument("--height", type=float, default=0)
    ap.add_argument("--fps", type=int, default=30)
    a = ap.parse_args()
    convert(a.inp, a.out, a.clips, a.height, a.fps)

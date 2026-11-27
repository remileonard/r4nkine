#!/usr/bin/env python3
"""Writes the small binary test files of the loaders (written for the
tests, not taken from any demo): flip .bin, pfb, dwb, OpenFlight, 5551E.
Run from tests/data: python3 make_data.py"""
import struct

def be(fmt, *v):
    return struct.pack('>' + fmt, *v)

# flip: a unit square in the XY plane (one quad), normals +Z
pts = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]
d = be('iii', 0x5423, 4, 0)
for p in pts:
    d += be('fff', 0, 0, 1) + be('fff', *p)
open('square.bin', 'wb').write(d)

# pfb version 1: a geode with one geoset of 2 triangles (TRIS), coordinates
# per vertex, an overall red color
def chunk(t, records, count):
    body = b''.join(records)
    return be('iii', t, count, len(body)) + body
coords = [(0, 0, 0), (2, 0, 0), (2, 1, 0), (0, 0, 0), (2, 1, 0), (0, 1, 0)]
lst5 = be('iii', len(coords), 0, -1) + b''.join(be('fff', *c) for c in coords)
lst6 = be('iii', 1, 0, -1) + be('ffff', 1, 0, 0, 1)
gset = be('iii', 2, 2, -1)                       # TRIS, 2 prims, no lengths
gset += be('iii', 2, 0, -1)                      # coords per vertex, list 0
gset += be('iii', 4, 0, -1)                      # colors overall, list 0
gset += be('iii', 0, -1, -1) + be('iii', 0, -1, -1)
gset += be('iiiii', 0, 0, 0, -1, 0) + be('ff', 1, 1) + be('iIii', 0, 0xffffffff, 0, 0)
gset += be('ffffff', 0, 0, 0, 2, 1, 0) + be('i', -1)
assert len(gset) == 132
node = be('i', 3) + be('iii', 2, 1, 0) + be('i', -1)   # words: GEODE, 1 gset, gset 0
data = be('IIII', 0xDB0ACE00, 1, 1, 16)
data += chunk(5, [lst5], 1) + chunk(6, [lst6], 1) + chunk(10, [gset], 1) + chunk(12, [node], 1)
open('quad.pfb', 'wb').write(data)

# dwb: a header (meters, Z up) and a polygon of 4 vertices, green
def rec(op, body):
    return be('HH', op, len(body)) + body
hdr = bytearray(0x88)
struct.pack_into('>h', hdr, 0, 0)
struct.pack_into('>f', hdr, 4, 5.0)
struct.pack_into('>h', hdr, 0x86, 1)
poly = bytearray(0x38)
struct.pack_into('>h', poly, 4, 4)
struct.pack_into('>h', poly, 0x10, -1)
struct.pack_into('>I', poly, 0x2c, 0xff00ff00)
struct.pack_into('>h', poly, 0x36, -1)
verts = b''
for p in [(0, 0, 0), (3, 0, 0), (3, 1, 0), (0, 1, 0)]:
    verts += be('i', 0) + be('fff', *p) + be('fff', 0, 0, 1) + be('ff', 0, 0) + be('ff', 0, 0)
open('quad.dwb', 'wb').write(rec(0x1771, bytes(hdr)) + rec(0x1775, bytes(poly)) + rec(0x17ca, b'') +
                              rec(0x177a, verts) + rec(0x17cb, b''))

# OpenFlight 14.2: header, a vertex palette of 3 vertices with normals, a
# group with a face (packed color blue), its vertex list
def frec(op, body):
    return be('HH', op, len(body) + 4) + body
header = bytearray(320)
header[0:8] = b'db\0\0\0\0\0\0'
struct.pack_into('>i', header, 8, 1420)          # format revision at +12 of the record
struct.pack_into('>h', header, 56, 1)            # unit multiplier at +60 of the record
vp = []
for p in [(0, 0, 0), (1, 0, 0), (0, 1, 0)]:
    v = bytearray(52)
    struct.pack_into('>hh', v, 0, 0, 0x2000)     # color index, flags: no color
    struct.pack_into('>ddd', v, 4, *p)
    struct.pack_into('>fff', v, 28, 0, 0, 1)
    vp.append(frec(69, bytes(v)))
vpal = frec(67, be('i', 8 + sum(len(r) for r in vp))) + b''.join(vp)
group = frec(2, b'g1\0\0\0\0\0\0' + bytes(36))
face = bytearray(76)
face[0:8] = b'f1\0\0\0\0\0\0'
struct.pack_into('>h', face, 24, -1)            # texture (+28)
struct.pack_into('>h', face, 26, -1)            # material (+30)
struct.pack_into('>I', face, 40, 0x10000000)    # flags (+44): packed color
face[44] = 0                                    # light mode (+48)
struct.pack_into('>BBBB', face, 52, 255, 255, 0, 0)  # packed color ABGR (+56): blue
offsets = []
o = 8
for r in vp:
    offsets.append(o)
    o += len(r)
vlist = frec(72, b''.join(be('i', x) for x in offsets))
flt = frec(1, bytes(header)) + vpal + group + frec(10, b'') + frec(5, bytes(face)) + frec(10, b'') + vlist + \
      frec(11, b'') + frec(11, b'')
open('tri.flt', 'wb').write(flt)

# PNG: 2x2 RGBA, rows from the top: red, green / blue, white (alpha 128 on
# the white pixel)
import zlib, base64, json
def png_chunk(t, body):
    c = t + body
    return struct.pack('>I', len(body)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
raw = b'\0' + bytes([255, 0, 0, 255, 0, 255, 0, 255]) + b'\0' + bytes([0, 0, 255, 255, 255, 255, 255, 128])
png = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, 6, 0, 0, 0)) + \
      png_chunk(b'IDAT', zlib.compress(raw)) + png_chunk(b'IEND', b'')
open('rgba2x2.png', 'wb').write(png)

# glTF: a triangle in a node translated by (0, 0, 5), indices, normals,
# texture coordinates, a material with a base color and the PNG texture
pos = struct.pack('<9f', 0, 0, 0, 1, 0, 0, 0, 1, 0)
nrm = struct.pack('<9f', 0, 0, 1, 0, 0, 1, 0, 0, 1)
uv = struct.pack('<6f', 0, 0, 1, 0, 0, 1)
ind = struct.pack('<3H', 0, 1, 2) + b'\0\0'
buf = pos + nrm + uv + ind
gltf = {
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "tri", "mesh": 0, "translation": [0, 0, 5]}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                "indices": 3, "material": 0}]}],
    "materials": [{"name": "half", "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.5, 0.5, 1],
                   "baseColorTexture": {"index": 0}, "roughnessFactor": 1, "metallicFactor": 0},
                   "doubleSided": True}],
    "textures": [{"source": 0}],
    "images": [{"uri": "rgba2x2.png"}],
    "buffers": [{"byteLength": len(buf),
                 "uri": "data:application/octet-stream;base64," + base64.b64encode(buf).decode()}],
    "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36},
                    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
                    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
                    {"buffer": 0, "byteOffset": 96, "byteLength": 6}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                   "min": [0, 0, 0], "max": [1, 1, 0]},
                  {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
                  {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
                  {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}],
}
open('tri.gltf', 'w').write(json.dumps(gltf, indent=1))

# glTF: the same triangle in glass (KHR_materials_transmission) that glows (KHR_materials_emissive_strength)
glass = dict(gltf)
glass["extensionsUsed"] = ["KHR_materials_transmission", "KHR_materials_emissive_strength"]
glass["materials"] = [{"name": "glass", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "roughnessFactor": 0, "metallicFactor": 0},
                       "emissiveFactor": [0.1, 0.2, 0.3],
                       "extensions": {"KHR_materials_transmission": {"transmissionFactor": 1},
                                      "KHR_materials_emissive_strength": {"emissiveStrength": 2}}}]
del glass["textures"]
del glass["images"]
open('glass.gltf', 'w').write(json.dumps(glass, indent=1))

# glTF: a quad with the normal map of a bump (left) beside a real hemisphere (right), lit the same way when the normal map is
# right; the normal map scaled by 0.5
import math
N = 128
rows = []
for r in range(N):
    row = []
    for c in range(N):
        x = (c + 0.5) / N * 2 - 1
        y = -((r + 0.5) / N * 2 - 1)
        d = x * x + y * y
        n = (0.0, 0.0, 1.0)
        if d < 0.64:
            z = math.sqrt(0.64 - d)
            k = math.sqrt(x * x + y * y + z * z)
            n = (x / k, y / k, z / k)
        row += [int((v * 0.5 + 0.5) * 255 + 0.5) for v in n]
    rows.append(row)
bump_png = b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB', N, N, 8, 2, 0, 0, 0)) + \
    png_chunk(b'IDAT', zlib.compress(b''.join(b'\0' + bytes(r) for r in rows))) + png_chunk(b'IEND', b'')
bbuf = b''
views = []
accessors = []
def accessor(fmt, values, kind, component, count, bounds=None):
    global bbuf
    data = struct.pack('<%d%s' % (len(values), fmt), *values)
    views.append({"buffer": 0, "byteOffset": len(bbuf), "byteLength": len(data)})
    bbuf += data
    while len(bbuf) % 4:
        bbuf += b'\0'
    a = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind}
    if bounds:
        a["min"], a["max"] = bounds
    accessors.append(a)
    return len(accessors) - 1
qp, qn, qu = [], [], []
for (x, z, u, v) in [(-1, -1, 0, 0), (1, -1, 1, 0), (1, 1, 1, 1), (-1, 1, 0, 1)]:
    qp += [x - 1.2, 0, z]
    qn += [0, 1, 0]
    qu += [u, v]
hp, hn, hi = [], [], []
for a in range(25):
    th = a / 24 * math.pi / 2
    for b in range(49):
        ph = b / 48 * 2 * math.pi
        nx, ny, nz = math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph)
        hp += [nx * 0.8 + 1.2, ny * 0.8, nz * 0.8]
        hn += [nx, ny, nz]
for a in range(24):
    for b in range(48):
        i0 = a * 49 + b
        hi += [i0, i0 + 1, i0 + 49, i0 + 1, i0 + 50, i0 + 49]
A = [accessor('f', qp, 'VEC3', 5126, 4, ([-2.2, 0, -1], [-0.2, 0, 1])), accessor('f', qn, 'VEC3', 5126, 4),
     accessor('f', qu, 'VEC2', 5126, 4), accessor('H', [0, 2, 1, 0, 3, 2], 'SCALAR', 5123, 6),
     accessor('f', hp, 'VEC3', 5126, len(hp) // 3, ([0.4, 0, -0.8], [2.0, 0.8, 0.8])), accessor('f', hn, 'VEC3', 5126, len(hn) // 3),
     accessor('H', hi, 'SCALAR', 5123, len(hi))]
bump = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0, 1]}], "nodes": [{"mesh": 0}, {"mesh": 1}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": A[0], "NORMAL": A[1], "TEXCOORD_0": A[2]}, "indices": A[3], "material": 0}]},
                   {"primitives": [{"attributes": {"POSITION": A[4], "NORMAL": A[5]}, "indices": A[6], "material": 1}]}],
        "materials": [{"name": "mapped", "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 1], "metallicFactor": 0, "roughnessFactor": 0.9},
                       "normalTexture": {"index": 0, "scale": 0.5}},
                      {"name": "real", "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 1], "metallicFactor": 0, "roughnessFactor": 0.9}}],
        "textures": [{"source": 0}], "images": [{"uri": "data:image/png;base64," + base64.b64encode(bump_png).decode()}],
        "buffers": [{"byteLength": len(bbuf), "uri": "data:application/octet-stream;base64," + base64.b64encode(bbuf).decode()}],
        "bufferViews": views, "accessors": accessors}
open('bump.gltf', 'w').write(json.dumps(bump))

# OBJ without a .mtl: two levels of detail (g lod0, g lod1) and the named materials rgb_RRGGBB, glass and body
lods = """# two levels of detail
v 0 0 0
v 1 0 0
v 0 1 0
v 1 1 0
g lod0
usemtl rgb_FF8000
f 1 2 3
usemtl glass
f 2 4 3
g lod1
usemtl body
f 1 2 4
"""
open('lods.obj', 'w').write(lods)

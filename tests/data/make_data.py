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

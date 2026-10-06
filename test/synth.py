"""Build the synthetic rasters from 4x6.ras (cups raster v3, native-endian header).

  2p.ras    two pages (4x6.ras twice)
  ramp.ras  rows 0-1023: every pixel = row % 256 (finds the grey threshold);
            later rows: (7x + row) % 256 (checks the threshold per pixel)
  nc3.ras   header NumCopies = 3 (offsetof(cups_page_header2_t, NumCopies) = 340)
"""
import struct

SYNC, HDR = 4, 1796
d = open("4x6.ras", "rb").read()
hdr = d[SYNC:SYNC + HDR]
width, height = struct.unpack("<II", hdr[372:380])
bpl = struct.unpack("<I", hdr[392:396])[0]

open("2p.ras", "wb").write(d + d[SYNC:])

rows = [bytes([r % 256]) * bpl if r < 1024 else bytes((x * 7 + r) % 256 for x in range(bpl))
        for r in range(height)]
open("ramp.ras", "wb").write(d[:SYNC + HDR] + b"".join(rows))

nc3 = bytearray(d)
nc3[SYNC + 340:SYNC + 344] = struct.pack("<I", 3)
open("nc3.ras", "wb").write(nc3)

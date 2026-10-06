"""Decode the BITMAP payload from a TSPL job into a PNG (bit 0 = black dot)."""
import re, sys, zlib, struct
d = open(sys.argv[1], 'rb').read()
m = re.search(rb'BITMAP (\d+),(\d+),(\d+),(\d+),(\d+),', d)
wb, h = int(m[3]), int(m[4])
bm = d[m.end():m.end() + wb * h]
rows = b''.join(b'\0' + bm[r*wb:(r+1)*wb] for r in range(h))
def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', wb*8, h, 1, 0, 0, 0, 0)) \
      + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
open(sys.argv[2], 'wb').write(png)
print(sys.argv[2], wb*8, 'x', h, m[0].decode())

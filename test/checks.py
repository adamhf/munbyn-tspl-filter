"""Behaviour tests for rastertotspl where it deliberately differs from the
vendor filter: malformed input, colour spaces, missing PPD, truncation,
rotation, cancel and copies. Run from the test directory: python3 checks.py
"""
import os, re, signal, struct, subprocess, sys, time

FILTER = os.path.abspath(os.environ.get("FILTER", "../rastertotspl"))
PPD = os.environ.get("PPD") or subprocess.run(["./vendor-ppd.sh"], capture_output=True, text=True, check=True).stdout.strip()
SYNC, HDR = 4, 1796
# offsetof(cups_page_header2_t, ...)
WIDTH, HEIGHT, BPC, BPP, BPL, ORDER, CSPACE = 372, 376, 384, 388, 392, 396, 400
CS_W, CS_RGB, CS_K, CS_SW = 0, 1, 3, 18

failures = 0


def check(name, ok, detail=""):
    global failures
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else f"  ({detail})"))
    failures += not ok


def run(data, opts="", ppd=PPD, copies="1"):
    env = dict(os.environ)
    env.pop("PPD", None)
    if ppd:
        env["PPD"] = ppd
    p = subprocess.run([FILTER, "1", "u", "t", copies, opts], input=data,
                       capture_output=True, env=env, timeout=60)
    return p.returncode, p.stdout, p.stderr.decode(errors="replace")


def header(base, **fields):
    """Copy of a raster's sync + header with some uint32 fields replaced."""
    h = bytearray(base[:SYNC + HDR])
    for off, value in fields.items():
        o = SYNC + globals()[off.upper()]
        h[o:o + 4] = struct.pack("<I", value)
    return bytes(h)


def field(data, off):
    return struct.unpack("<I", data[SYNC + off:SYNC + off + 4])[0]


def bitmaps(out):
    """(width_bytes, height, payload) for each BITMAP in a TSPL job."""
    res = []
    for m in re.finditer(rb"BITMAP 0,0,(\d+),(\d+),1,", out):
        wb, h = int(m[1]), int(m[2])
        res.append((wb, h, out[m.end():m.end() + wb * h]))
    return res


def dots(bm):
    """Set of black (x, y) dots in a bitmap."""
    wb, h, data = bm
    return {(x, y) for y in range(h) for x in range(wb * 8)
            if not data[y * wb + (x >> 3)] & (0x80 >> (x & 7))}


small = open("sz_w90h18.ras", "rb").read()        # 248 x 39, 8-bit W
w, h, bpl = field(small, WIDTH), field(small, HEIGHT), field(small, BPL)
pixels = small[SYNC + HDR:]
rc, base_out, _ = run(small)
base = bitmaps(base_out)[0]

# --- malformed or unsupported headers fail the job without reading past buffers
bad = {
    "width wraps to 0 bytes": header(small, width=0xFFFFFFF9) + pixels,
    "bytes per line < width": header(small, width=798, bpl=100) + pixels,
    "huge page, no data": header(small, width=65535, height=65535, bpl=8),
    "zero width": header(small, width=0) + pixels,
    "banded RGB": header(small, cspace=CS_RGB, order=1, bpl=bpl * 3) + pixels * 3,
    "16 bits per pixel": header(small, bpc=16, bpp=16, bpl=bpl * 2) + pixels * 2,
}
for name, data in bad.items():
    rc, out, err = run(data)
    check(f"rejects {name}", rc == 1 and "ERROR:" in err and len(out) < 4096,
          f"rc={rc} out={len(out)}B")

# A bad page after a good one fails the job instead of silently dropping pages.
rc, out, err = run(small + bad["16 bits per pixel"][SYNC:] + small[SYNC:])
check("bad later page fails the job", rc == 1 and out.count(b"PRINT 1,1") == 1 and "ERROR:" in err,
      f"rc={rc} prints={out.count(b'PRINT 1,1')}")

# --- colour spaces: same picture must give the same dots
rc, out, _ = run(header(small, cspace=CS_SW) + pixels)
check("sGray prints like W", rc == 0 and bitmaps(out)[0] == base)
rc, out, _ = run(header(small, cspace=CS_K) + bytes(255 - b for b in pixels))
check("K (inverted data) prints like W", rc == 0 and bitmaps(out)[0] == base)

bits = bytearray()
for y in range(h):
    row = pixels[y * bpl:y * bpl + w]
    packed = bytearray((w + 7) // 8)
    for x, v in enumerate(row):
        if v >= 201:                                     # white = 1 in 1-bit W
            packed[x >> 3] |= 0x80 >> (x & 7)
    bits += packed
rc, out, _ = run(header(small, bpc=1, bpp=1, bpl=(w + 7) // 8) + bytes(bits))
check("1-bit W prints like 8-bit W", rc == 0 and bitmaps(out)[0] == base)

# --- missing PPD fails like the vendor filter, rather than guessing media settings
for label, ppd in (("unset", None), ("missing", "/nonexistent.ppd")):
    rc, out, err = run(small, ppd=ppd)
    check(f"PPD {label} fails the job", rc == 1 and not out and "PPD" in err, f"rc={rc}")

# --- truncated input: complete the BITMAP with white, no PRINT, job fails
rc, out, err = run(small[:SYNC + HDR + bpl * 10])
bm = bitmaps(out)
check("truncated raster fails, BITMAP completed",
      rc == 1 and "ERROR:" in err and len(bm) == 1 and len(bm[0][2]) == bm[0][0] * bm[0][1]
      and b"PRINT" not in out, f"rc={rc}")

# --- Rotate 90/270 rotate the bitmap and send a valid DIRECTION
src = dots(base)
for opt, clockwise in (("Rotate=2", True), ("Rotate=3", False)):
    rc, out, _ = run(small, opt)
    bm = bitmaps(out)[0] if rc == 0 else None
    want = {((h - 1 - y, x) if clockwise else (y, w - 1 - x)) for x, y in src}
    check(f"{opt} rotates the bitmap",
          bm is not None and b"DIRECTION 0,0" in out and bm[1] == w and dots(bm) == want,
          f"rc={rc}")
rc, out, _ = run(open("4x6.ras", "rb").read(), "Rotate=2")
check("Rotate=2 rejects a page wider than the head once rotated", rc == 1)

# --- copies come from CUPS (cupsManualCopies), not argv[4]
check("copies=3 output equals copies=1", run(small, copies="3")[1] == base_out)

# --- SIGTERM between pages: don't start the next page
p = subprocess.Popen([FILTER, "1", "u", "t", "1", ""], stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, env={**os.environ, "PPD": PPD})
p.stdin.write(small)
p.stdin.flush()
got = b""
deadline = time.time() + 10
while b"PRINT 1,1" not in got and time.time() < deadline:
    got += os.read(p.stdout.fileno(), 65536)
time.sleep(0.2)
p.send_signal(signal.SIGTERM)
time.sleep(0.2)
p.stdin.write(small[SYNC:])
p.stdin.close()
got += p.stdout.read()
err = p.stderr.read().decode()
p.wait()
check("cancel between pages stops before page 2",
      got.count(b"BITMAP") == 1 and "PAGE: 2" not in err, f"bitmaps={got.count(b'BITMAP')}")

print("checks:", "all passed" if not failures else f"{failures} failed")
sys.exit(1 if failures else 0)

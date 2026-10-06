"""Write small test PDFs (no deps) for driving the CUPS raster pipeline."""

def pdf(w, h, ops):
    content = "\n".join(ops).encode()
    objs = [
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {w} {h}] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>".encode(),
        b"<< /Length %d >>\nstream\n" % len(content) + content + b"\nendstream",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>",
    ]
    out = b"%PDF-1.4\n"
    offs = []
    for i, o in enumerate(objs, 1):
        offs.append(len(out))
        out += b"%d 0 obj\n" % i + o + b"\nendobj\n"
    x = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    out += b"".join(b"%010d 00000 n \n" % o for o in offs)
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, x)
    return out

def page(w, h):
    ops = ["0 g", "4 w", f"6 6 {w-12} {h-12} re S",           # border
           f"BT /F1 {min(w,h)/8:.0f} Tf 14 {h-14-min(w,h)/8:.0f} Td (Munbyn TSPL) Tj ET",
           f"20 20 {w/4:.0f} {h/6:.0f} re f",                  # solid block
           "0.25 w", f"{w/2:.0f} 10 m {w/2:.0f} {h-10} l S"]   # hairline
    steps = 8                                                  # grey ramp
    for i in range(steps):
        ops.append(f"{i/(steps-1):.3f} g {w/2+6+i*(w/2-20)/steps:.1f} {h/2:.0f} {(w/2-20)/steps:.1f} {h/6:.0f} re f")
    return ops

for name, w, h in [("4x6", 288, 432), ("3x2", 212, 142), ("odd", 171, 396)]:
    open(f"{name}.pdf", "wb").write(pdf(w, h, page(w, h)))

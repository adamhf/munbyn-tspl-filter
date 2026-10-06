"""Print a TSPL job's text parts, eliding BITMAP payloads and NUL preambles."""
import re, sys
d = open(sys.argv[1], 'rb').read()
out, pos = [], 0
for m in re.finditer(rb'BITMAP \d+,\d+,(\d+),(\d+),1,', d):
    if m.start() < pos: continue
    out.append(d[pos:m.end()].decode(errors='replace'))
    pos = m.end() + int(m[1]) * int(m[2])
    out.append(f'<{int(m[1])*int(m[2])} bitmap bytes>')
out.append(d[pos:].decode(errors='replace'))
text = re.sub('\0+', lambda m: f'<{len(m[0])} NUL>', ''.join(out))
print(text.replace('\r\n', '⏎\n').replace('\n\n', '\n'))

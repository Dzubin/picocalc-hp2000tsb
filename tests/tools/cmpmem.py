import re, sys
ref = {}
for l in open(sys.argv[1]):
    m = re.match(r'(\d+):\s+(\d+)', l)
    if m:
        ref[int(m.group(1), 8)] = int(m.group(2), 8)
mine = {}
for l in open(sys.argv[2]):
    a, v = l.split(':')
    mine[int(a, 8)] = int(v, 8)
top = int(sys.argv[3], 8) if len(sys.argv) > 3 else 0o40000
bad = [(a, ref.get(a, 0), mine.get(a, 0)) for a in range(2, top) if ref.get(a, 0) != mine.get(a, 0)]
print(len(ref), 'reference words listed;', len(bad), 'differences')
for a, r, m in bad[:12]:
    print('%05o ref %06o mine %06o' % (a, r, m))

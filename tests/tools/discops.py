import re, sys
out = []
lines = open(sys.argv[1]).read().split('\n')
for i, l in enumerate(lines):
    m = re.match(r'>>(DPD|DPC) iobus: Received data (\d+) with signals (.*)', l)
    if not m: continue
    card, val, sig = m.groups()
    sigs = [x.strip() for x in sig.split('|')]
    if 'IOO' in sigs: out.append('%s IOO %s' % (card, val))
    if 'IOI' in sigs:
        m2 = re.match(r'>>(DPD|DPC) iobus: Returned data (\d+)', lines[i + 1])
        out.append('%s IOI %s' % (card, m2.group(2)))
    if 'STC' in sigs: out.append('%s STC' % card)
    if 'CLC' in sigs: out.append('%s CLC' % card)
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
print(len(out))

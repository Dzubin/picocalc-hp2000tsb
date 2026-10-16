import sys
# Usage: collapse.py ref.trace mine.trace out_ref out_mine
# Finds the polling loops (instruction pairs repeated with unchanged registers
# for several cycles) in either trace and removes every execution of those
# addresses from both, so differences in device timing do not show up.
def load(p):
    return [l for l in open(p)]
def poll_addresses(lines):
    addrs = set()
    run = 0
    for i in range(len(lines) - 3):
        if lines[i] == lines[i + 2] and lines[i + 1] == lines[i + 3]:
            run += 1
            if run >= 3:
                addrs.add(lines[i].split()[0])
                addrs.add(lines[i + 1].split()[0])
        else:
            run = 0
    return addrs
a = load(sys.argv[1]); b = load(sys.argv[2])
bad = poll_addresses(a) | poll_addresses(b)
print('polling addresses removed:', ' '.join(sorted(bad)))
for src, dst in ((a, sys.argv[3]), (b, sys.argv[4])):
    f = [l for l in src if l.split()[0] not in bad]
    open(dst, 'w').write(''.join(f))
    print(dst, len(f), 'lines')

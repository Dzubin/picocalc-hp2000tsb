import re, sys
# Convert a SIMH hp2100 "instr;reg" debug trace to lines "PPPPP IIIIII AAAAAA BBBBBB E O"
# (state before each instruction).
reg = None
out = []
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'>>CPU\s+reg: - \S+ (\*{5}|[0-9]{5})\s+\S+\s+A (\d{6}), B (\d{6}), ([Ee]) ([Oo])', line)
    if m:
        reg = (m.group(2), m.group(3), 1 if m.group(4) == 'E' else 0, 1 if m.group(5) == 'O' else 0)
        continue
    m = re.match(r'>>CPU instr: - \S+ (\d{5})  (\d{6})  (.*)', line)
    if m and reg and 'simulation stop' not in line:
        out.append('%s %s %s %s %d %d' % (m.group(1), m.group(2), reg[0], reg[1], reg[2], reg[3]))
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
print(len(out), 'instructions')

# AI agent: edit the test queue (aiwork/queue.txt) - run it under flock aiwork/queue.lock.
#   qedit.py drop LINE        take the first line equal to LINE off
#   qedit.py top LINE...      put lines at the top
#   qedit.py add LINE...      put lines at the end
#   qedit.py show             print it
import sys
Q = '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork/queue.txt'
try:
    lines = [l.rstrip('\n') for l in open(Q)]
except FileNotFoundError:
    lines = []
cmd, args = sys.argv[1], sys.argv[2:]
if cmd == 'drop':
    if args[0] in lines:
        lines.remove(args[0])
elif cmd == 'top':
    lines = list(args) + lines
elif cmd == 'add':
    lines = lines + list(args)
elif cmd == 'show':
    print('\n'.join(lines))
    sys.exit(0)
tmp = Q + '.tmp'
with open(tmp, 'w') as f:
    f.write(''.join(l + '\n' for l in lines))
import os
os.replace(tmp, Q)

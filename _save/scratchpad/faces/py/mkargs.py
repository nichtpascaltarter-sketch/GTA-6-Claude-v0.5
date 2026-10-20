# usage: mkargs.py PREFIX people dists times [start] [yaw]  -> viewer shot args (one --shot/--facecam pair per shot)
#   people "6,27,35"  dists "0.5,1,4"  times "day,night"   (day = 15.5 h, night = 22 h); start = first position index
#   (every shot needs its own camera position: the viewer matches a facecam to the first shot at the camera's position);
#   yaw (degrees) turns the person (35 = three-quarter, 90 = profile); the name gets a "_y<yaw>" suffix
import sys
pre, people, dists, times = sys.argv[1], sys.argv[2].split(','), sys.argv[3].split(','), sys.argv[4].split(',')
k = int(sys.argv[5]) if len(sys.argv) > 5 else 0
yaw = sys.argv[6] if len(sys.argv) > 6 else '0'
hours = {'day': '15.5', 'night': '22'}
suf = '' if yaw == '0' else '_y' + yaw
out = []
for p in people:
    for t in times:
        for d in dists:
            x, y = -280.0 + 0.37 * k, 1480.0 - 0.29 * k
            tag = d.replace('.', 'p')
            out.append('--shot %.3f,%.3f,6.000,-132.0,0,%s,%s%s_%sm_%s%s --facecam %s,%s,%s' % (x, y, hours[t], pre, p, tag, t, suf, p, d, yaw))
            k += 1
print(' '.join(out))

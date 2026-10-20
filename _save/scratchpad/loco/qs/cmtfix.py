import sys
old = "                        // swing: a foot down early there would be planted again at once - it steps)\n"
new = "                        // swing: there the early touchdown plants it again at once - down and up, frame after frame)\n"
for p in sys.argv[1:]:
    s = open(p).read()
    n = s.count(old)
    if n != 1:
        print("SKIP", n, p); continue
    open(p, "w").write(s.replace(old, new))
    print("fixed", p)

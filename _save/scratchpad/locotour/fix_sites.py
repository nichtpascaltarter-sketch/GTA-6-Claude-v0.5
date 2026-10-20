T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
s=open(T+'block_player.cpp').read()
a=s.index("    // a flat, open spot beside the stair's foot for the switchback")
b=s.index("    void lqBuildSwitchback() {")
new_site = open(T+'site_lq.inc').read()
s = s[:a] + new_site + s[b:]
assert s.count("        const vec2 park = lqW(-3.5f, -4.f);\n")==1
s = s.replace("        const vec2 park = lqW(-3.5f, -4.f);\n", "        const vec2 park = lqW(-1.5f, -7.0f);\n")
a=s.index("    // a flat, open 6 x 6 m spot beside the stair's foot, clear of the switchback")
b=s.index("    void spBuild() {")
s = s[:a] + open(T+'site_sp.inc').read() + s[b:]
assert s.count("        const vec2 park = spC + vec2(3.2f, -3.2f);\n")==1
s = s.replace("        const vec2 park = spC + vec2(3.2f, -3.2f);\n", "        const vec2 park = spC + vec2(1.95f, -4.5f);\n")
open(T+'block_player.cpp','w').write(s)
print('site search replaced;', s.count('lqRings('), 'uses')

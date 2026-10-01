p='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/edgeinfo_full.cpp'
s=open(p).read()
s=s.replace('''    Jobs::shutdown();
}''','''    BuildingSet bs; bs.generate(map, roads); gBuildings = &bs;
    if (argc > 3 && argv[argc - 3][0] == 'p') {
        float px = atof(argv[argc - 2]), py = atof(argv[argc - 1]);
        int cx = (int)floorf((px + kWorldHalf) / kCellSize), cy = (int)floorf((py + kWorldHalf) / kCellSize);
        CellGeometry geo;
        generateCell(cx, cy, true, geo);
        for (auto& b : geo.collision)
            if (length(b.c.xy() - vec2(px, py)) < 30.f)
                printf("  box c (%.1f, %.1f, %.1f) ax (%.2f, %.2f) he (%.1f, %.2f, %.2f)\\n", b.c.x, b.c.y, b.c.z, b.ax.x, b.ax.y, b.he.x, b.he.y, b.he.z);
    }
    Jobs::shutdown();
}''')
s=s.replace("        int ei = atoi(argv[i]);","        if (argv[i][0] == 'p') break;\n        int ei = atoi(argv[i]);")
open(p,'w').write(s)

    // a spot for the spiral: its disc and 0.4 m round it (3.2 m: the platform at the top), the way in and the walkers'
    // waiting line (to 6 m back), clear of the switchback and its waiting line
    bool spFindSite() {
        std::vector<vec2> pts;
        pts.push_back(vec2(0.f));
        for (int ir = 1; ir <= 5; ir++)
            for (int it = 0; it < 12; it++) pts.push_back(vec2(cosf(0.5236f * (float)it), sinf(0.5236f * (float)it)) * (0.64f * (float)ir));
        const int nFlat = (int)pts.size();
        for (int iy = 0; iy < 5; iy++)
            for (int ix = 0; ix < 3; ix++) pts.push_back(vec2(0.55f + 0.7f * (float)ix, -2.8f - 0.85f * (float)iy));
        return lqRings([&](vec2 c, float z) {
            if (lqSiteOk && length(c - (lqO + lqA * (-1.8f))) < 12.f) return false;
            if (!lqOpenAt(c, z, pts, nFlat, 0.04f)) return false;
            spC = c;
            spZ0 = z;
            return true;
        });
    }


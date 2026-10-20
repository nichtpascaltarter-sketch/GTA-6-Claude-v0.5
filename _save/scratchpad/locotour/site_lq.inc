    // a static collider in the way at q: its footprint within m of q, its height range overlapping z0 .. z1
    bool lqBlocked(vec2 q, float m, float z0, float z1, const std::vector<int>& ids) const {
        for (int id : ids) {
            const Phys::Collider& k = collision.collider(id);
            const vec2 d = q - vec2(k.c.x, k.c.y);
            if (k.kind == Phys::COL_CYLINDER) {
                if (k.c.z > z1 || k.c.z + k.he.z < z0) continue;
                if (length(d) < k.he.x + m) return true;
            } else {
                if (k.c.z - k.he.z > z1 || k.c.z + k.he.z < z0) continue;
                if (fabsf(dot(d, k.ax)) < k.he.x + m && fabsf(dot(d, perp(k.ax))) < k.he.y + m) return true;
            }
        }
        return false;
    }
    bool lqCarNear(vec2 c, float z, float r) const {
        for (const Vehicle& v : game.vehicles)
            if (v.used) {
                const vec3 d = rel(v.sim.body.pos, dvec3(c.x, c.y, (double)z));
                if (d.x * d.x + d.y * d.y < r * r) return true;
            }
        return false;
    }
    // an open, flat piece of ground: every point of pts (relative to c) level with c's ground to tol (the first nFlat;
    // the rest to 0.15 m), no static collider over any of them (0.4 m round, below 4 m) and no car within 9 m. With the
    // population off the roads are as open as anywhere; the search goes out in rings from the stair's foot, 2 m apart,
    // to 80 m
    bool lqOpenAt(vec2 c, float z, const std::vector<vec2>& pts, int nFlat, float tol) {
        for (int k = 0; k < (int)pts.size(); k++) {
            const vec2 q = c + pts[k];
            const float zq = game.groundHeight(q.x, q.y, z + 1.f);
            if (zq < -1e8f || fabsf(zq - z) > (k < nFlat ? tol : 0.15f)) return false;
        }
        std::vector<int> ids;
        collision.collidersNear(c, 9.f, ids);
        for (const vec2& p : pts)
            if (lqBlocked(c + p, 0.4f, z + 0.05f, z + 4.f, ids)) return false;
        return !lqCarNear(c, z, 9.f);
    }
    template <class F> bool lqRings(F&& tryAt) {
        for (int ring = 0; ring <= 40; ring++)
            for (int i = -ring; i <= ring; i++)
                for (int j = -ring; j <= ring; j++) {
                    if (Max(abs(i), abs(j)) != ring) continue;
                    const vec2 c = lsFootC.xy() + lsDir * (6.f + 2.f * (float)i) + lsRight * (2.f * (float)j);
                    const float z = game.groundHeight(c.x, c.y, lsFootC.z + 3.f);
                    if (z < -1e8f || fabsf(z - lsFootC.z) > 6.f) continue;
                    if (tryAt(c, z)) return true;
                }
        return false;
    }
    // the switchback's ground (relative to its centre, flights along A, across X): its footprint and 0.4 m round it, the
    // way up to flight 1 and the walkers' waiting line behind it (to 7.2 m back), where the player stands aside
    bool lqFindSite() {
        const vec2 dirs[2] = {lsDir, lsRight};
        for (int o = 0; o < 2; o++) {
            const vec2 A = dirs[o], X(A.y, -A.x);
            std::vector<vec2> pts;
            for (int ia = 0; ia < 9; ia++)
                for (int ix = 0; ix < 5; ix++) pts.push_back(X * (-1.25f + 0.625f * (float)ix) + A * (-3.3f + 0.82f * (float)ia));
            const int nFlat = (int)pts.size();
            for (int ia = 0; ia < 5; ia++)
                for (int ix = 0; ix < 4; ix++) pts.push_back(X * (-1.6f + 0.9f * (float)ix) + A * (-4.2f - 0.75f * (float)ia));
            const bool found = lqRings([&](vec2 c, float z) {
                if (!lqOpenAt(c, z, pts, nFlat, 0.04f)) return false;
                // (the switchback's flights run along A; it reaches 3.1 m back from the foot of flight 1, under the top
                // platform and the way to the flight, and 3.5 m on to the landing's end wall: centred on the spot)
                lqA = A;
                lqX = X;
                lqO = c - lqA * 0.17f;
                lqZ0 = z;
                return true;
            });
            if (found) return true;
        }
        return false;
    }


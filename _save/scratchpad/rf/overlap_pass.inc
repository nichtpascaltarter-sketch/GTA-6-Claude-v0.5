    // ============================================================= Roads laid along each other
    // The named roads (Bayshore Boulevard on the shore line, Old Cutler Road, the causeways, the county roads) are laid
    // over the districts' grids. Where one runs a few degrees off a grid line, the planariser joins the two at a node and
    // leaves their carriageways on top of each other for tens of metres (a Y split), or alongside each other between two
    // junctions. Their lanes then block each other with nothing to resolve it (the AI agent's traffic_sim --overlaps: the
    // deadlocks at Bayshore Boulevard and 9th Avenue, and where the Solano Causeway meets 35th Street). So the lower road
    // gives way: wherever it runs inside the higher one's carriageway (centrelines closer than the two half widths plus
    // 1 m, at under 30 degrees, beside the higher road rather than past its end), that stretch goes, and the cut end is
    // joined to the higher road at about 65 degrees, from a new junction on it. A piece left with a free end under 20 m
    // goes too. Every other road keeps its index and seed (its lots, buildings and signals hang on them): the new pieces
    // take the slots of dropped edges first, and take their seeds from the edge they were cut from.
    {
        const int NE0 = (int)edges.size();
        auto atGrade = [&](const RoadEdge& e) { return e.cls != RC_HIGHWAY && e.cls != RC_RAMP && e.pts.size() >= 2; };
        std::vector<std::vector<vec2>> P2((size_t)NE0);
        std::vector<std::vector<float>> C2((size_t)NE0);
        for (int i = 0; i < NE0; i++) {
            for (const vec3& p : edges[i].pts) P2[i].push_back(p.xy());
            C2[i].assign(P2[i].size(), 0.f);
            for (size_t k = 1; k < P2[i].size(); k++) C2[i][k] = C2[i][k - 1] + length(P2[i][k] - P2[i][k - 1]);
        }
        auto lenOf = [&](int i) { return C2[i].empty() ? 0.f : C2[i].back(); };
        auto pointAt = [&](int i, float s) {
            const std::vector<vec2>& q = P2[i];
            const std::vector<float>& c = C2[i];
            if (s <= 0.f) return q.front();
            if (s >= c.back()) return q.back();
            size_t k = (size_t)(std::upper_bound(c.begin(), c.end(), s) - c.begin());
            float seg = c[k] - c[k - 1];
            return lerp(q[k - 1], q[k], seg > 1e-5f ? (s - c[k - 1]) / seg : 0.f);
        };
        // the arc length of the point of edge i nearest to p
        auto arcOf = [&](int i, vec2 p) {
            float bestD = 1e18f, bestS = 0.f;
            for (size_t k = 0; k + 1 < P2[i].size(); k++) {
                float t, d = distPointSegment2D(p, P2[i][k], P2[i][k + 1], &t);
                if (d < bestD) {
                    bestD = d;
                    bestS = Lerp(C2[i][k], C2[i][k + 1], t);
                }
            }
            return bestS;
        };
        // who gives way: the lower class (a rural road ranks above a street), a bridge before the rest of its class, then
        // the shorter road (all the edges of its name), then the higher index
        static const int kGiveWayRank[RC_COUNT] = {0, 1, 2, 4, 5, 3, 6, 7};   // highway, boulevard, avenue, street, lane, rural, dirt, ramp
        std::unordered_map<std::string, float> roadLen;
        for (int i = 0; i < NE0; i++) roadLen[edges[i].name] += lenOf(i);
        auto wins = [&](int a, int b) {
            const RoadEdge &A = edges[a], &B = edges[b];
            int ra = kGiveWayRank[A.cls] * 2 + ((A.flags & RF_BRIDGE) ? 0 : 1), rb = kGiveWayRank[B.cls] * 2 + ((B.flags & RF_BRIDGE) ? 0 : 1);
            if (ra != rb) return ra < rb;
            float la = A.name.empty() ? lenOf(a) : roadLen[A.name], lb = B.name.empty() ? lenOf(b) : roadLen[B.name];
            if (fabsf(la - lb) > 1.f) return la > lb;
            return a < b;
        };
        const float kG = 32.f;
        auto gkey = [](int x, int y) { return (long long)(y + 100000) * 400000LL + (x + 100000); };
        std::unordered_map<long long, std::vector<std::pair<int, int>>> grid;   // (edge, segment)
        float maxHw = 0.f;
        for (int i = 0; i < NE0; i++) {
            if (!atGrade(edges[i])) continue;
            maxHw = Max(maxHw, edges[i].halfWidth);
            for (size_t k = 0; k + 1 < P2[i].size(); k++) {
                vec2 a = P2[i][k], c = P2[i][k + 1];
                int x0 = (int)floorf(Min(a.x, c.x) / kG), x1 = (int)floorf(Max(a.x, c.x) / kG);
                int y0 = (int)floorf(Min(a.y, c.y) / kG), y1 = (int)floorf(Max(a.y, c.y) / kG);
                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++) grid[gkey(x, y)].push_back({i, (int)k});
            }
        }
        // ---- find the stretches: every 2 m along each road, the winning road whose carriageway it lies in
        const float kStep = 2.f, kParallel = 0.866f;
        struct Cut {
            float s0, s1;          // the stretch (arc length from n0), first and last sample inside
            int win0, win1;        // the road it lies in at its two ends
            bool keep = false;     // given up (it would join a road that is being cut there itself)
        };
        std::vector<std::vector<Cut>> cuts((size_t)NE0);
        std::vector<int> inside;
        for (int bi = 0; bi < NE0; bi++) {
            const RoadEdge& B = edges[bi];
            if (!atGrade(B)) continue;
            float L = lenOf(bi);
            if (L < 8.f) continue;
            int n = (int)(L / kStep) + 1;
            inside.assign((size_t)n, -1);
            bool any = false;
            for (int j = 0; j < n; j++) {
                float s = Min(j * kStep, L);
                vec2 p = pointAt(bi, s);
                vec2 t = pointAt(bi, Min(s + 1.f, L)) - pointAt(bi, Max(s - 1.f, 0.f));
                float tl = length(t);
                if (tl < 1e-4f) continue;
                t = t / tl;
                float r = B.halfWidth + maxHw + 1.f, bestD = 1e9f;
                int x0 = (int)floorf((p.x - r) / kG), x1 = (int)floorf((p.x + r) / kG);
                int y0 = (int)floorf((p.y - r) / kG), y1 = (int)floorf((p.y + r) / kG);
                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++) {
                        auto it = grid.find(gkey(x, y));
                        if (it == grid.end()) continue;
                        for (const auto& ref : it->second) {
                            int ai = ref.first;
                            if (ai == bi) continue;
                            const RoadEdge& A = edges[ai];
                            size_t k = (size_t)ref.second;
                            vec2 a = P2[ai][k], c = P2[ai][k + 1];
                            float tt, d = distPointSegment2D(p, a, c, &tt);
                            if (d >= A.halfWidth + B.halfWidth + 1.f || d >= bestD) continue;
                            if ((k == 0 && tt <= 1e-3f) || (k + 2 == P2[ai].size() && tt >= 1.f - 1e-3f)) continue;   // past its end
                            vec2 ad = c - a;
                            float al = length(ad);
                            if (al < 1e-4f || fabsf(dot(ad / al, t)) < kParallel || !wins(ai, bi)) continue;
                            bestD = d;
                            inside[(size_t)j] = ai;
                        }
                    }
                any = any || inside[(size_t)j] >= 0;
            }
            if (!any) continue;
            for (int j = 0; j < n;) {
                if (inside[(size_t)j] < 0) {
                    j++;
                    continue;
                }
                int j1 = j;   // one clear sample between two inside ones does not end the stretch
                while (j1 + 1 < n && (inside[(size_t)j1 + 1] >= 0 || (j1 + 2 < n && inside[(size_t)j1 + 2] >= 0))) j1++;
                Cut c;
                c.s0 = j * kStep;
                c.s1 = Min(j1 * kStep, L);
                c.win0 = inside[(size_t)j];
                c.win1 = inside[(size_t)j1];
                if (c.s1 - c.s0 >= 10.f) cuts[(size_t)bi].push_back(c);
                j = j1 + 1;
            }
        }
        // ---- the joins: each cut end meets the winning road at a node on it, ~65 degrees back toward the stretch
        struct Join {
            int edge;      // the winning road (-1: dropped with its cut)
            float s;       // where on it: an arc length, or one of its ends (0 or its length) when that is within 3 m
            int node;
        };
        std::vector<Join> joins;
        auto ownCutAt = [&](int ai, float s) {
            for (const Cut& c : cuts[(size_t)ai])
                if (s > c.s0 - 5.f && s < c.s1 + 5.f) return true;
            return false;
        };
        auto planJoin = [&](int bi, int ai, float sP, float sInside) {
            vec2 p = pointAt(bi, sP), q = pointAt(bi, sInside);
            float sF = arcOf(ai, p), sQ = arcOf(ai, q), LA = lenOf(ai);
            float back = length(p - pointAt(ai, sF)) * 0.4663f;   // cot 65 degrees
            float sM = sQ < sF ? sF - back : sF + back;
            if (sM < 3.f) sM = 0.f;
            else if (sM > LA - 3.f) sM = LA;
            if (ownCutAt(ai, sM)) return -1;   // that road gives way there itself
            joins.push_back({ai, sM, -1});
            return (int)joins.size() - 1;
        };
        // ---- the pieces each road keeps: stretches between its cuts, with the links to their joins
        struct Piece {
            int src;                       // the edge it comes from
            float a, b;                    // its stretch of that edge
            int joinA, joinB;              // a link from a join to its start / from its end to a join (-1: none)
        };
        std::vector<Piece> kept;
        std::vector<char> wasCut((size_t)NE0, 0);
        int cutsMade = 0, cutsLeft = 0;
        float metresCut = 0.f;
        for (int i = 0; i < NE0; i++) {
            float L = lenOf(i), cur = 0.f;
            int curJoin = -1;
            bool alive = true;
            for (Cut& c : cuts[(size_t)i]) {
                bool atStart = c.s0 < 2.5f, atEnd = c.s1 > L - 2.5f;
                float sEnd = Max(c.s0 - kStep, 0.f), sStart = Min(c.s1 + kStep, L);
                int jEnd = atStart ? -1 : planJoin(i, c.win0, sEnd, c.s0);
                int jStart = atEnd ? -1 : planJoin(i, c.win1, sStart, c.s1);
                if ((!atStart && jEnd < 0) || (!atEnd && jStart < 0)) {
                    if (jEnd >= 0) joins[(size_t)jEnd].edge = -1;
                    if (jStart >= 0) joins[(size_t)jStart].edge = -1;
                    cutsLeft++;
                    continue;
                }
                cutsMade++;
                metresCut += c.s1 - c.s0;
                wasCut[(size_t)i] = 1;
                if (!atStart && sEnd > cur + 0.5f) kept.push_back({i, cur, sEnd, curJoin, jEnd});
                if (atEnd) {
                    alive = false;
                    break;
                }
                cur = sStart;
                curJoin = jStart;
            }
            if (alive && L > cur + 0.5f) kept.push_back({i, cur, L, curJoin, -1});
        }
        // ---- the join nodes: a road's ends where a join is at one, else new nodes (joins within 3 m share one)
        std::vector<std::vector<int>> joinsOn((size_t)NE0);
        for (size_t k = 0; k < joins.size(); k++)
            if (joins[k].edge >= 0) joinsOn[(size_t)joins[k].edge].push_back((int)k);
        for (int i = 0; i < NE0; i++) {
            std::vector<int>& js = joinsOn[(size_t)i];
            std::sort(js.begin(), js.end(), [&](int x, int y) { return joins[(size_t)x].s < joins[(size_t)y].s; });
            float L = lenOf(i);
            int last = -1;
            for (int k : js) {
                Join& J = joins[(size_t)k];
                if (J.s <= 0.f) J.node = edges[i].n0;
                else if (J.s >= L) J.node = edges[i].n1;
                else if (last >= 0 && J.s - joins[(size_t)last].s < 3.f && joins[(size_t)last].s > 0.f) {
                    J.s = joins[(size_t)last].s;
                    J.node = joins[(size_t)last].node;
                } else {
                    RoadNode rn;
                    rn.p = pointAt(i, J.s);
                    J.node = (int)nodes.size();
                    nodes.push_back(rn);
                }
                last = k;
            }
        }
        // ---- the new edges: the kept pieces, split where joins land on them
        struct NewEdge {
            int src, part;
            RoadEdge e;
            bool fromCut;
        };
        std::vector<NewEdge> made;
        std::vector<int> partsOf((size_t)NE0, 0);
        auto untouched = [&](int i) { return !wasCut[(size_t)i] && joinsOn[(size_t)i].empty(); };
        for (const Piece& pc : kept) {
            int i = pc.src;
            if (untouched(i)) continue;
            std::vector<float> at;
            std::vector<int> atNode;
            at.push_back(pc.a);
            atNode.push_back(pc.joinA >= 0 ? joins[(size_t)pc.joinA].node : (pc.a <= 0.f ? edges[i].n0 : -1));
            for (int k : joinsOn[(size_t)i]) {
                const Join& J = joins[(size_t)k];
                if (J.s <= pc.a + 0.5f || J.s >= pc.b - 0.5f || (!at.empty() && fabsf(J.s - at.back()) < 0.01f)) continue;
                at.push_back(J.s);
                atNode.push_back(J.node);
            }
            at.push_back(pc.b);
            atNode.push_back(pc.joinB >= 0 ? joins[(size_t)pc.joinB].node : (pc.b >= lenOf(i) ? edges[i].n1 : -1));
            for (size_t k = 0; k + 1 < at.size(); k++) {
                RoadEdge e = edges[i];
                e.pts.clear();
                float x = at[k], y = at[k + 1];
                if (k == 0 && pc.joinA >= 0) e.pts.push_back(vec3(nodes[atNode[0]].p, 0.f));   // the link from the join
                e.pts.push_back(vec3(pointAt(i, x), 0.f));
                for (size_t v = 0; v < P2[i].size(); v++)
                    if (C2[i][v] > x + 0.01f && C2[i][v] < y - 0.01f) e.pts.push_back(vec3(P2[i][v], 0.f));
                e.pts.push_back(vec3(pointAt(i, y), 0.f));
                if (k + 2 == at.size() && pc.joinB >= 0) e.pts.push_back(vec3(nodes[atNode.back()].p, 0.f));
                e.n0 = atNode[k];
                e.n1 = atNode[k + 1];
                if (e.n0 < 0 || e.n1 < 0) continue;   // (cannot happen: every piece end is a node, a join or a split)
                int part = partsOf[(size_t)i]++;
                if (part > 0) e.seed = hash32(edges[i].seed ^ (0x9E3779B9u * (u32)part));
                made.push_back({i, part, std::move(e), wasCut[(size_t)i] != 0});
            }
        }
        // ---- a cut-off piece with a free end under 20 m goes
        int piecesDropped = 0;
        for (int pass = 0; pass < 2; pass++) {
            std::vector<int> deg(nodes.size(), 0);
            for (int i = 0; i < NE0; i++)
                if (untouched(i)) {
                    deg[(size_t)edges[i].n0]++;
                    deg[(size_t)edges[i].n1]++;
                }
            for (const NewEdge& m : made)
                if (m.src >= 0) {
                    deg[(size_t)m.e.n0]++;
                    deg[(size_t)m.e.n1]++;
                }
            for (NewEdge& m : made) {
                if (m.src < 0 || !m.fromCut || (deg[(size_t)m.e.n0] > 1 && deg[(size_t)m.e.n1] > 1)) continue;
                float len = 0.f;
                for (size_t k = 0; k + 1 < m.e.pts.size(); k++) len += length(m.e.pts[k + 1].xy() - m.e.pts[k].xy());
                if (len >= 20.f) continue;
                m.src = -1;
                piecesDropped++;
            }
        }
        // ---- every edge's first piece keeps its slot; the other pieces fill the slots of dropped edges, then go last
        if (cutsMade > 0) {
            std::vector<RoadEdge> out((size_t)NE0);
            std::vector<char> filled((size_t)NE0, 0);
            std::vector<RoadEdge> extra;
            for (NewEdge& m : made) {
                if (m.src < 0) continue;
                if (!filled[(size_t)m.src]) {
                    out[(size_t)m.src] = std::move(m.e);
                    filled[(size_t)m.src] = 1;
                } else {
                    extra.push_back(std::move(m.e));
                }
            }
            // (a slot whose edge lost every piece is free)
            // the edges no cut or join touched come back as they were
            for (int i = 0; i < NE0; i++)
                if (untouched(i)) {
                    out[(size_t)i] = std::move(edges[(size_t)i]);
                    filled[(size_t)i] = 1;
                }
            size_t next = 0;
            int edgesDropped = 0;
            for (int i = 0; i < NE0; i++) {
                if (filled[(size_t)i]) continue;
                edgesDropped++;
                if (next < extra.size()) {
                    out[(size_t)i] = std::move(extra[next++]);
                    filled[(size_t)i] = 1;
                }
            }
            for (; next < extra.size(); next++) {
                out.push_back(std::move(extra[next]));
                filled.push_back(1);
            }
            // slots still empty: the last edges move into them
            for (size_t i = 0; i < out.size(); i++) {
                if (filled[i]) continue;
                while (!out.empty() && !filled.back()) {
                    out.pop_back();
                    filled.pop_back();
                }
                if (i >= out.size()) break;
                out[i] = std::move(out.back());
                filled[i] = 1;
                out.pop_back();
                filled.pop_back();
            }
            edges.swap(out);
            for (auto& n : nodes) n.edges.clear();
            for (size_t i = 0; i < edges.size(); i++) {
                nodes[(size_t)edges[i].n0].edges.push_back((int)i);
                nodes[(size_t)edges[i].n1].edges.push_back((int)i);
            }
            int joined = 0;
            for (const Join& J : joins) joined += J.edge >= 0;
            LOG("Road gen: %d stretches of road laid inside a bigger road's carriageway cut out (%.0f m; %d joins back onto it, %d short pieces and "
                "%d whole edges dropped, %d left where the bigger road gives way itself)",
                cutsMade, metresCut, joined, piecesDropped, edgesDropped, cutsLeft);
        }
    }


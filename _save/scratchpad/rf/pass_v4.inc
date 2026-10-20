    // ============================================================= Roads laid along each other
    // The named roads (Bayshore Boulevard on the shore line, Old Cutler Road, the causeways, the county roads) are laid over
    // the districts' grids. Where one runs a few degrees off a grid line, or along it a few metres over, the planariser joins
    // the two at nodes and leaves their carriageways on top of each other for tens of metres. Their lanes then block each
    // other with nothing to resolve it (the AI agent's traffic_sim --overlaps; the deadlocks where 9th Avenue runs inside
    // Bayshore Boulevard and where the Solano Causeway comes in along 35th Street). So the lower road gives way where it lies
    // on top of the higher one: a stretch of it beside the higher road at under 32 degrees, with its centreline inside 0.8 of
    // the wider half width for 8 m or more outside both roads' junction areas (the cut-backs the intersection pass sets
    // below), goes. The whole run where the carriageways overlap goes with it, and the rest of the edge too when less than
    // 20 m of it would be left. A cut end is joined back to the higher road at about 65 degrees, at a new junction on it.
    // What the lower road leaves behind is tidied: a road left with a free end under 20 m goes, a junction left inside the
    // higher road's carriageway moves onto it (into its junction there when that is close), and a dead end left further out
    // gets a link to it.
    // Lots, buildings, bus stops and parked cars hang on the edges' seeds and their places in the order, so every other edge
    // keeps both. Of an edge's pieces, the one from its start keeps its slot and seed (its lots stay where they were); the
    // other new edges take the slots of the edges that went, then go last, with seeds made from their edge's. When fewer new
    // edges come than went, the road nearest to a gone edge is split just short of its far junction (where no lots fit) until
    // every slot is filled: no edge after a gone one moves.
    {
        const int NE0 = (int)edges.size();
        auto atGrade = [&](const RoadEdge& e) { return e.cls != RC_HIGHWAY && e.cls != RC_RAMP && !(e.flags & RF_ELEVATED) && e.pts.size() >= 2; };
        std::vector<std::vector<vec2>> P2((size_t)NE0);
        std::vector<std::vector<float>> C2((size_t)NE0);
        for (int i = 0; i < NE0; i++) {
            for (const vec3& p : edges[i].pts) P2[i].push_back(p.xy());
            C2[i].assign(P2[i].size(), 0.f);
            for (size_t k = 1; k < P2[i].size(); k++) C2[i][k] = C2[i][k - 1] + length(P2[i][k] - P2[i][k - 1]);
        }
        auto lenOf = [&](int i) { return C2[i].empty() ? 0.f : C2[i].back(); };
        // the junction areas, as the intersection pass will cut them back: a node of 3 or more edges clears the widest
        // road's half width (and some sidewalk) plus 2 m, at most 0.45 of an edge's length
        std::vector<float> nodeR(nodes.size(), 0.f);
        std::vector<int> nodeDeg(nodes.size(), 0);
        for (const RoadEdge& e : edges) {
            float r = e.halfWidth + e.sidewalk * 0.3f;
            nodeR[(size_t)e.n0] = Max(nodeR[(size_t)e.n0], r);
            nodeR[(size_t)e.n1] = Max(nodeR[(size_t)e.n1], r);
            nodeDeg[(size_t)e.n0]++;
            nodeDeg[(size_t)e.n1]++;
        }
        for (size_t n = 0; n < nodes.size(); n++) nodeR[n] = nodeDeg[n] >= 3 ? nodeR[n] + 2.f : 0.f;
        auto cutBack = [&](int i, int end) { return Min(nodeR[(size_t)(end ? edges[i].n1 : edges[i].n0)], lenOf(i) * 0.45f); };
        auto pointAt = [&](int i, float s) {
            const std::vector<vec2>& q = P2[i];
            const std::vector<float>& c = C2[i];
            if (s <= 0.f) return q.front();
            if (s >= c.back()) return q.back();
            size_t k = (size_t)(std::upper_bound(c.begin(), c.end(), s) - c.begin());
            float seg = c[k] - c[k - 1];
            return lerp(q[k - 1], q[k], seg > 1e-5f ? (s - c[k - 1]) / seg : 0.f);
        };
        // the arc length of the point of edge i's stretch [a, b] nearest to p (and its distance)
        auto arcOn = [&](int i, vec2 p, float a, float b, float* dist) {
            float bestD = 1e18f, bestS = a;
            for (size_t k = 0; k + 1 < P2[i].size(); k++) {
                if (C2[i][k + 1] < a || C2[i][k] > b) continue;
                float t;
                distPointSegment2D(p, P2[i][k], P2[i][k + 1], &t);
                float s = Clamp(Lerp(C2[i][k], C2[i][k + 1], t), a, b), d = length(p - pointAt(i, s));
                if (d < bestD) {
                    bestD = d;
                    bestS = s;
                }
            }
            if (dist) *dist = bestD;
            return bestS;
        };
        auto arcOf = [&](int i, vec2 p) { return arcOn(i, p, 0.f, lenOf(i), nullptr); };
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
        // ---- find the stretches: every 2 m along each road, the winning road whose carriageway it overlaps, and whether
        // it lies on top of it (deep)
        const float kStep = 2.f, kParallel = 0.85f, kDeepMin = 8.f, kMinPiece = 20.f;
        struct Cut {
            float s0, s1;          // the stretch (arc length from n0), first and last sample inside
            int win0, win1;        // the road it lies in at its two ends
        };
        std::vector<std::vector<Cut>> cuts((size_t)NE0);
        std::vector<int> inside;
        std::vector<char> deep;
        for (int bi = 0; bi < NE0; bi++) {
            const RoadEdge& B = edges[bi];
            if (!atGrade(B)) continue;
            float L = lenOf(bi);
            if (L < 8.f) continue;
            int n = (int)(L / kStep) + 1;
            inside.assign((size_t)n, -1);
            deep.assign((size_t)n, 0);
            bool any = false;
            const float bLo = cutBack(bi, 0) + 1.f, bHi = L - cutBack(bi, 1) - 1.f;
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
                            if (d >= A.halfWidth + B.halfWidth || d >= bestD) continue;
                            if ((k == 0 && tt <= 1e-3f) || (k + 2 == P2[ai].size() && tt >= 1.f - 1e-3f)) continue;   // past its end
                            vec2 ad = c - a;
                            float al = length(ad);
                            if (al < 1e-4f || fabsf(dot(ad / al, t)) < kParallel || !wins(ai, bi)) continue;
                            bestD = d;
                            inside[(size_t)j] = ai;
                            float sA = Lerp(C2[ai][k], C2[ai][k + 1], tt);
                            deep[(size_t)j] = d <= 0.8f * Max(A.halfWidth, B.halfWidth) && s > bLo && s < bHi && sA > cutBack(ai, 0) + 1.f &&
                                              sA < lenOf(ai) - cutBack(ai, 1) - 1.f;
                        }
                    }
                any = any || deep[(size_t)j];
            }
            if (!any) continue;
            for (int j = 0; j < n;) {
                if (inside[(size_t)j] < 0) {
                    j++;
                    continue;
                }
                int j1 = j;   // one clear sample between two inside ones does not end the stretch
                while (j1 + 1 < n && (inside[(size_t)j1 + 1] >= 0 || (j1 + 2 < n && inside[(size_t)j1 + 2] >= 0))) j1++;
                int deepN = 0;
                for (int k = j; k <= j1; k++) deepN += deep[(size_t)k];
                Cut c;
                c.s0 = j * kStep;
                c.s1 = Min(j1 * kStep, L);
                c.win0 = inside[(size_t)j];
                c.win1 = inside[(size_t)j1];
                if (deepN * kStep >= kDeepMin) cuts[(size_t)bi].push_back(c);
                j = j1 + 1;
            }
        }
        // ---- a cut that would leave less than 20 m of the edge before or after it, or between it and the next one, takes
        // that too: the road then gives way from its junction
        for (int bi = 0; bi < NE0; bi++) {
            std::vector<Cut>& cs = cuts[(size_t)bi];
            if (cs.empty()) continue;
            float L = lenOf(bi);
            std::vector<Cut> merged;
            for (Cut c : cs) {
                if (c.s0 - kStep < kMinPiece) c.s0 = 0.f;
                if (L - (c.s1 + kStep) < kMinPiece) c.s1 = L;
                if (!merged.empty() && (c.s0 - kStep) - (merged.back().s1 + kStep) < kMinPiece) {
                    merged.back().s1 = c.s1;
                    merged.back().win1 = c.win1;
                } else {
                    merged.push_back(c);
                }
            }
            cs.swap(merged);
        }
        // ---- the joins: each cut end meets the winning road at a node on it, ~65 degrees back toward the stretch
        struct Join {
            int edge;      // the winning road (-1: given up with its cut)
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
        // ---- the pieces of road: every edge's stretches between its cuts, with the links from and to their joins
        struct Piece {
            int src;                 // the edge it comes from
            float a, b;              // its stretch of that edge
            int n0, n1;              // its end nodes
            int joinA, joinB;        // a link from a join to its start / from its end to a join (-1: none)
            std::vector<vec2> own;   // a link of its own instead of a stretch (a dead end joined to the bigger road)
            bool alive;
        };
        std::vector<Piece> pcs;
        pcs.reserve((size_t)NE0 + 256);
        auto addPiece = [&](int src, float a, float b, int jA, int jB) {
            Piece p;
            p.src = src;
            p.a = a;
            p.b = b;
            p.joinA = jA;
            p.joinB = jB;
            p.n0 = jA >= 0 ? -1 : edges[src].n0;   // (a join's node comes below)
            p.n1 = jB >= 0 ? -1 : edges[src].n1;
            p.alive = true;
            pcs.push_back(p);
        };
        struct Absorbed {
            int node, winner;   // a node of a cut edge inside its cut, and the road it lay in there
        };
        std::vector<Absorbed> absorbed;
        int cutsMade = 0, cutsLeft = 0;
        float metresCut = 0.f;
        for (int i = 0; i < NE0; i++) {
            float L = lenOf(i), cur = 0.f;
            int curJoin = -1;
            bool alive = true;
            for (const Cut& c : cuts[(size_t)i]) {
                bool atStart = c.s0 <= 0.f, atEnd = c.s1 >= L;
                float sEnd = c.s0 - kStep, sStart = c.s1 + kStep;
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
                if (atStart) absorbed.push_back({edges[i].n0, c.win0});
                else addPiece(i, cur, sEnd, curJoin, jEnd);
                if (atEnd) {
                    absorbed.push_back({edges[i].n1, c.win1});
                    alive = false;
                    break;
                }
                cur = sStart;
                curJoin = jStart;
            }
            if (alive) addPiece(i, cur, L, curJoin, -1);
        }
        if (cutsMade > 0) {
            // ---- the join nodes: an end of the winning road within 3 m, a node shared with another join within 3 m, or a
            // new node that splits it
            std::vector<char> joinUsed(joins.size(), 0);
            for (const Piece& p : pcs) {
                if (p.joinA >= 0) joinUsed[(size_t)p.joinA] = 1;
                if (p.joinB >= 0) joinUsed[(size_t)p.joinB] = 1;
            }
            std::vector<std::vector<int>> joinsOn((size_t)NE0);
            for (size_t k = 0; k < joins.size(); k++)
                if (joins[k].edge >= 0 && joinUsed[k]) joinsOn[(size_t)joins[k].edge].push_back((int)k);
            int joined = 0;
            for (int i = 0; i < NE0; i++) {
                std::vector<int>& js = joinsOn[(size_t)i];
                std::sort(js.begin(), js.end(), [&](int x, int y) { return joins[(size_t)x].s < joins[(size_t)y].s; });
                float L = lenOf(i);
                int last = -1;
                for (int k : js) {
                    Join& J = joins[(size_t)k];
                    joined++;
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
            for (Piece& p : pcs) {
                if (p.joinA >= 0) p.n0 = joins[(size_t)p.joinA].node;
                if (p.joinB >= 0) p.n1 = joins[(size_t)p.joinB].node;
            }
            // splitting a piece at s (of its edge) with a node there; the part after it is a new piece
            auto splitPiece = [&](int pi, float s, int node) {
                Piece q = pcs[(size_t)pi];
                pcs[(size_t)pi].b = s;
                pcs[(size_t)pi].n1 = node;
                pcs[(size_t)pi].joinB = -1;
                q.a = s;
                q.n0 = node;
                q.joinA = -1;
                pcs.push_back(q);
                return (int)pcs.size() - 1;
            };
            auto pieceAt = [&](int i, float s) {
                for (size_t k = 0; k < pcs.size(); k++) {
                    const Piece& p = pcs[k];
                    if (p.alive && p.src == i && p.own.empty() && s > p.a && s < p.b) return (int)k;
                }
                return -1;
            };
            for (size_t k = 0; k < joins.size(); k++) {
                const Join& J = joins[k];
                if (J.edge < 0 || !joinUsed[k] || J.s <= 0.f || J.s >= lenOf(J.edge)) continue;
                int pi = pieceAt(J.edge, J.s);
                if (pi >= 0) splitPiece(pi, J.s, J.node);
            }
            auto pieceLen = [&](const Piece& p) {
                float l = 0.f;
                if (!p.own.empty()) {
                    for (size_t k = 0; k + 1 < p.own.size(); k++) l += length(p.own[k + 1] - p.own[k]);
                    return l;
                }
                l = p.b - p.a;
                if (p.joinA >= 0) l += length(nodes[(size_t)p.n0].p - pointAt(p.src, p.a));
                if (p.joinB >= 0) l += length(nodes[(size_t)p.n1].p - pointAt(p.src, p.b));
                return l;
            };
            // the pieces at each node, and its junction radius as the intersection pass will make it
            std::vector<std::vector<int>> at;
            auto rebuildAt = [&]() {
                at.assign(nodes.size(), std::vector<int>());
                for (size_t k = 0; k < pcs.size(); k++)
                    if (pcs[k].alive) {
                        at[(size_t)pcs[k].n0].push_back((int)k);
                        if (pcs[k].n1 != pcs[k].n0) at[(size_t)pcs[k].n1].push_back((int)k);
                    }
            };
            auto radiusAt = [&](int n) {
                if (at[(size_t)n].size() < 3) return 0.f;
                float r = 0.f;
                for (int k : at[(size_t)n]) r = Max(r, edges[pcs[(size_t)k].src].halfWidth + edges[pcs[(size_t)k].src].sidewalk * 0.3f);
                return r + 2.f;
            };
            // ---- a road the cuts leave with a free end under 20 m (the rest of a cross street inside the bigger road) goes
            std::vector<char> affected(nodes.size(), 0);
            for (const Absorbed& ab : absorbed) affected[(size_t)ab.node] = 1;
            int hanging = 0;
            for (bool again = true; again;) {
                again = false;
                rebuildAt();
                for (size_t k = 0; k < pcs.size(); k++) {
                    Piece& p = pcs[k];
                    if (!p.alive) continue;
                    bool free0 = affected[(size_t)p.n0] && at[(size_t)p.n0].size() == 1, free1 = affected[(size_t)p.n1] && at[(size_t)p.n1].size() == 1;
                    if ((!free0 && !free1) || pieceLen(p) >= kMinPiece) continue;
                    p.alive = false;
                    affected[(size_t)p.n0] = affected[(size_t)p.n1] = 1;
                    hanging++;
                    again = true;
                }
            }
            // ---- a junction the lower road leaves inside the bigger road's carriageway moves onto it (into its junction there
            // when it lies in that junction's area), unless a short road still joins it to it; a dead end the cut leaves
            // further out gets a link to it, square to it
            auto sameRoad = [&](int i, int w) { return i == w || (!edges[w].name.empty() && edges[i].name == edges[w].name && atGrade(edges[i])); };
            int movedOn = 0, linked = 0;
            std::vector<char> seen(nodes.size(), 0);
            for (const Absorbed& ab : absorbed) {
                int N = ab.node, W = ab.winner;
                if (seen[(size_t)N]) continue;
                seen[(size_t)N] = 1;
                rebuildAt();
                const std::vector<int> here = at[(size_t)N];
                if (here.empty()) continue;
                bool onW = false, grade = true;
                for (int k : here) {
                    onW = onW || sameRoad(pcs[(size_t)k].src, W);
                    grade = grade && atGrade(edges[pcs[(size_t)k].src]);
                }
                if (onW || !grade) continue;
                int best = -1;
                float bestD = 1e9f, bestS = 0.f;
                for (size_t k = 0; k < pcs.size(); k++) {
                    const Piece& p = pcs[k];
                    if (!p.alive || !p.own.empty() || !sameRoad(p.src, W)) continue;
                    float d, s = arcOn(p.src, nodes[(size_t)N].p, p.a, p.b, &d);
                    if (d < bestD) {
                        bestD = d;
                        best = (int)k;
                        bestS = s;
                    }
                }
                if (best < 0) continue;
                const Piece P = pcs[(size_t)best];
                // the node on the winning road it goes to: one of the piece's ends when the point is in that end's junction
                // area (or within 3.5 m of it), else a node that splits the piece
                auto landing = [&](int node) {
                    if (P.joinA < 0 && bestS - P.a < Max(3.5f, radiusAt(P.n0) + 5.f)) return P.n0;
                    if (P.joinB < 0 && P.b - bestS < Max(3.5f, radiusAt(P.n1) + 5.f)) return P.n1;
                    if (node < 0) {
                        RoadNode rn;
                        rn.p = pointAt(P.src, bestS);
                        node = (int)nodes.size();
                        nodes.push_back(rn);
                        affected.push_back(0);
                        seen.push_back(0);
                    } else {
                        nodes[(size_t)node].p = pointAt(P.src, bestS);
                    }
                    splitPiece(best, bestS, node);
                    return node;
                };
                if (bestD < edges[W].halfWidth) {
                    bool stub = false;
                    for (int k : here) {
                        const Piece& p = pcs[(size_t)k];
                        if (pieceLen(p) >= kMinPiece) continue;
                        int o = p.n0 == N ? p.n1 : p.n0;
                        for (int q : at[(size_t)o]) stub = stub || sameRoad(pcs[(size_t)q].src, W);
                    }
                    if (stub) continue;
                    int to = landing(N);
                    if (to != N) {
                        for (int k : here) {
                            Piece& p = pcs[(size_t)k];
                            if (p.n0 == N) p.n0 = to;
                            if (p.n1 == N) p.n1 = to;
                            if (p.n0 == p.n1) p.alive = false;
                        }
                    }
                    movedOn++;
                } else if (here.size() == 1) {
                    const Piece rest = pcs[(size_t)here[0]];
                    int to = landing(-1);
                    Piece l;
                    l.src = rest.src;
                    l.a = l.b = 0.f;
                    l.joinA = l.joinB = -1;
                    l.alive = true;
                    // (the way the road runs, when it is one way)
                    bool rev = (edges[rest.src].flags & RF_ONEWAY) && rest.n0 == N;
                    l.n0 = rev ? to : N;
                    l.n1 = rev ? N : to;
                    l.own = {nodes[(size_t)l.n0].p, nodes[(size_t)l.n1].p};
                    pcs.push_back(l);
                    linked++;
                }
            }
            // ---- the slots. An edge's piece from its start (else its first) keeps the edge's slot; the other pieces fill the
            // slots of the edges that went, in order, then go last
            std::vector<int> keeper((size_t)NE0, -1);
            for (size_t k = 0; k < pcs.size(); k++) {
                const Piece& p = pcs[k];
                if (!p.alive || !p.own.empty()) continue;
                int& kp = keeper[(size_t)p.src];
                if (kp < 0 || pcs[(size_t)kp].a > p.a) kp = (int)k;
            }
            std::vector<int> freed, extra;
            for (int i = 0; i < NE0; i++)
                if (keeper[(size_t)i] < 0) freed.push_back(i);
            for (size_t k = 0; k < pcs.size(); k++)
                if (pcs[k].alive && keeper[(size_t)pcs[k].src] != (int)k) extra.push_back((int)k);
            // fewer new edges than went: the road nearest to a gone edge is split just short of its far junction, far
            // enough out that the junction keeps its full cut-back and where no lot fits
            int splitForSlots = 0;
            rebuildAt();
            for (size_t f = extra.size(); f < freed.size(); f++) {
                int gone = freed[f];
                vec2 mid = pointAt(gone, lenOf(gone) * 0.5f);
                int best = -1;
                float bestD = 1e18f, bestS = 0.f;
                for (size_t k = 0; k < pcs.size(); k++) {
                    const Piece& p = pcs[k];
                    const RoadEdge& e = edges[p.src];
                    if (!p.alive || !p.own.empty() || p.joinB >= 0 || !atGrade(e) || (e.flags & RF_BRIDGE)) continue;
                    vec2 lo = P2[p.src].front(), hi = lo;
                    for (const vec2& q : P2[p.src]) {
                        lo = vec2(Min(lo.x, q.x), Min(lo.y, q.y));
                        hi = vec2(Max(hi.x, q.x), Max(hi.y, q.y));
                    }
                    float boxD = length(vec2(Max(Max(lo.x - mid.x, mid.x - hi.x), 0.f), Max(Max(lo.y - mid.y, mid.y - hi.y), 0.f)));
                    if (boxD >= bestD) continue;
                    float s = p.b - Max(radiusAt(p.n1) / 0.45f + 1.f, 12.f);
                    if (s - p.a < Max(kMinPiece, radiusAt(p.n0) + 10.f)) continue;
                    float d;
                    arcOn(p.src, mid, p.a, p.b, &d);
                    if (d < bestD) {
                        bestD = d;
                        best = (int)k;
                        bestS = s;
                    }
                }
                if (best < 0) break;   // (cannot happen: there are always long roads; the gone edge would come back as it was)
                RoadNode rn;
                rn.p = pointAt(pcs[(size_t)best].src, bestS);
                int node = (int)nodes.size();
                nodes.push_back(rn);
                int q = splitPiece(best, bestS, node);
                extra.push_back(q);
                at.push_back(std::vector<int>());
                at[(size_t)node] = {best, q};
                splitForSlots++;
            }
            // ---- the edges: each piece's stretch of its edge (the edges no cut or join touched come back as they were); seeds:
            // the slot keeper keeps its edge's, the others get new ones made from it
            std::vector<int> partOf(pcs.size(), 0), parts((size_t)NE0, 0);
            {
                std::vector<int> order;
                for (size_t k = 0; k < pcs.size(); k++)
                    if (pcs[k].alive) order.push_back((int)k);
                std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
                    const Piece &X = pcs[(size_t)x], &Y = pcs[(size_t)y];
                    if (X.src != Y.src) return X.src < Y.src;
                    if (X.own.empty() != Y.own.empty()) return X.own.empty();
                    return X.a < Y.a;
                });
                for (int k : order)
                    if (keeper[(size_t)pcs[(size_t)k].src] != k) partOf[(size_t)k] = ++parts[(size_t)pcs[(size_t)k].src];
            }
            auto build = [&](int k) {
                const Piece& p = pcs[(size_t)k];
                RoadEdge e = edges[(size_t)p.src];
                e.pts.clear();
                e.dist.clear();
                std::vector<vec2> q;
                if (!p.own.empty()) {
                    q = p.own;
                } else {
                    q.push_back(nodes[(size_t)p.n0].p);
                    if (p.joinA >= 0) q.push_back(pointAt(p.src, p.a));
                    for (size_t v = 0; v < P2[p.src].size(); v++)
                        if (C2[p.src][v] > p.a + 0.01f && C2[p.src][v] < p.b - 0.01f) q.push_back(P2[p.src][v]);
                    if (p.joinB >= 0) q.push_back(pointAt(p.src, p.b));
                    q.push_back(nodes[(size_t)p.n1].p);
                }
                for (size_t v = 0; v < q.size(); v++) {
                    bool lastPt = v + 1 == q.size();
                    if (!e.pts.empty() && length(q[v] - e.pts.back().xy()) < 0.05f) {
                        if (lastPt && e.pts.size() > 1) e.pts.back() = vec3(q[v], 0.f);
                        continue;
                    }
                    e.pts.push_back(vec3(q[v], 0.f));
                }
                if (e.pts.size() < 2) e.pts.push_back(vec3(q.back(), 0.f));
                e.n0 = p.n0;
                e.n1 = p.n1;
                if (partOf[(size_t)k] > 0) e.seed = hash32(edges[(size_t)p.src].seed ^ (0x9E3779B9u * (u32)partOf[(size_t)k]));
                return e;
            };
            // a piece that is its whole edge, between the edge's own nodes where they were
            auto unchanged = [&](int k) {
                const Piece& p = pcs[(size_t)k];
                const RoadEdge& e = edges[(size_t)p.src];
                return p.own.empty() && p.joinA < 0 && p.joinB < 0 && p.a <= 0.f && p.b >= lenOf(p.src) && p.n0 == e.n0 && p.n1 == e.n1 &&
                       length(nodes[(size_t)p.n0].p - P2[p.src].front()) < 1e-3f && length(nodes[(size_t)p.n1].p - P2[p.src].back()) < 1e-3f;
            };
            std::vector<RoadEdge> out((size_t)NE0);
            std::vector<char> outOld((size_t)NE0, 0);
            for (int i = 0; i < NE0; i++) {
                int k = keeper[(size_t)i];
                if (k < 0) continue;
                if (unchanged(k)) outOld[(size_t)i] = 1;
                else out[(size_t)i] = build(k);
            }
            size_t next = 0;
            for (int i : freed) {
                if (next < extra.size()) out[(size_t)i] = build(extra[next++]);
                else outOld[(size_t)i] = 1;
            }
            int appended = (int)(extra.size() - next);
            for (; next < extra.size(); next++) out.push_back(build(extra[next]));
            for (int i = 0; i < NE0; i++)
                if (outOld[(size_t)i]) out[(size_t)i] = std::move(edges[(size_t)i]);
            edges.swap(out);
            for (auto& n : nodes) n.edges.clear();
            for (size_t i = 0; i < edges.size(); i++) {
                nodes[(size_t)edges[i].n0].edges.push_back((int)i);
                nodes[(size_t)edges[i].n1].edges.push_back((int)i);
            }
            LOG("Road gen: %d stretches of road laid inside a bigger road's carriageway cut out (%.0f m; %d joins back onto it, %d junctions "
                "left inside it moved onto it, %d dead ends linked to it, %d roads left hanging dropped; %d edges gone, %d split to keep "
                "the edge order, %d new at the end; %d left where the bigger road gives way itself)",
                cutsMade, metresCut, joined, movedOn, linked, hanging, (int)freed.size(), splitForSlots, appended, cutsLeft);
        }
    }

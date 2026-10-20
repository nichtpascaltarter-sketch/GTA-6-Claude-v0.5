// Contact generation (chassis vs ground / buildings / props / other vehicles) and damage. Included by vehicle_sim.cpp.
namespace Vehicles {
namespace vsim {

// ---------------------------------------------------------------------------------------------------------------
// Damage

// Zone from a COM-relative world point: 0 front, 1 rear, 2 left, 3 right, 4 roof, 5 underside.
int damageZone(const VehicleState& s, const mat3& R, vec3 rCom) {
    const VehicleTuning& t = s.tune;
    vec3 l = vec3(dot(R.c[0], rCom), dot(R.c[1], rCom), dot(R.c[2], rCom)) + t.com - t.boxC;
    float nx = l.x / t.boxH.x, ny = l.y / t.boxH.y, nz = l.z / t.boxH.z;
    if (fabsf(ny) >= fabsf(nx) * 0.9f && fabsf(ny) >= fabsf(nz) * 0.8f) return ny > 0.f ? 0 : 1;
    if (fabsf(nx) >= fabsf(nz) * 0.8f) return nx > 0.f ? 3 : 2;
    return nz > 0.f ? 4 : 5;
}

inline int engineZone(VehicleClass c) { return c == VC_SUPER || c == VC_BUS ? 1 : 0; }

void zoneDamage(VehicleState& s, float dmg, int zone, float side = 0.f) {
    if (dmg <= 0.f) return;
    s.health -= dmg;
    s.damageZones[zone] = Saturate(s.damageZones[zone] + dmg / 550.f);
    // hard front / side hits bend the steering: the car pulls toward the damaged side
    if (!isBikeClass(s.cls) && isRoadClass(s.cls) && (zone == 0 || zone == 2 || zone == 3)) {
        float sgn = zone == 2 ? -1.f : (zone == 3 ? 1.f : side);
        s.alignPull = Clamp(s.alignPull + sgn * 0.006f * dmg / 550.f, -0.004f, 0.004f);
    }
    bool engineHit = zone == engineZone(s.cls) || isBikeClass(s.cls) || s.cls == VC_JETSKI;
    s.engineHealth -= dmg * (engineHit ? 0.75f : 0.22f);
    if (s.health <= 0.f) {
        s.health = 0.f;
        s.wrecked = true;
    }
    s.engineHealth = Max(s.engineHealth, -100.f);
}

// Impact damage from an impulse (N*s) applied at rCom (COM-relative, world).
void impactDamage(VehicleState& s, const mat3& R, float invMass, float impulse, vec3 rCom) {
    float dv = impulse * invMass;
    if (dv < 3.f) return;
    float dmg = Min(0.9f * Sq(dv - 3.f), 900.f) * classParams(s.cls).damage;
    float lx = dot(R.c[0], rCom) + s.tune.com.x - s.tune.boxC.x;
    float side = fabsf(lx) > 0.2f * s.tune.boxH.x ? (lx > 0.f ? 1.f : -1.f) : 0.f;
    zoneDamage(s, dmg, damageZone(s, R, rCom), side);
}

void recordImpact(VehicleState& s, float impulse, vec3 pointRelOrigin, vec3 normal, int collider) {
    if (impulse <= s.impactImpulse) return;
    s.impactImpulse = impulse;
    s.impactPoint = pointRelOrigin;
    s.impactNormal = normal;
    s.impactCollider = collider;
}

// ---------------------------------------------------------------------------------------------------------------
// Chassis box corners vs the walkable ground (terrain, roads, roofs): lets flipped vehicles rest on the roof,
// bottomed-out chassis scrape, boats beach and helicopter skids land.
void chassisGroundContacts(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    Body& b = x.b;
    if (!x.groundValid) {
        Phys::GroundHit g = Phys::gCollision->ground(x.comW.x, x.comW.y, x.comW.z + 0.5f, 0.f);
        x.groundP = vec3(x.comW.x, x.comW.y, g.z);
        x.groundN = g.normal;
        x.groundValid = true;
    }
    vec3 P0 = x.groundP, N0 = x.groundN;
    bool bike = isBikeClass(s.cls), plane = s.cls == VC_PLANE;
    float margin = 0.08f;
    // Aircraft do not touch down like boxes: belly points, an upswept tail (allows ~12 degrees of rotation),
    // wingtips at mid height and the top of the fin/cabin for inverted crashes.
    vec3 planePts[8];
    if (plane) {
        float zb = t.boxC.z - t.boxH.z, zt = t.boxC.z + t.boxH.z, h = zt - zb;
        float yf = t.boxC.y + 0.8f * t.boxH.y, yr = t.boxC.y - 0.95f * t.boxH.y;
        planePts[0] = vec3(-0.12f * t.boxH.x, yf, zb);
        planePts[1] = vec3(0.12f * t.boxH.x, yf, zb);
        planePts[2] = vec3(-0.12f * t.boxH.x, yr, zb + 0.42f * h);
        planePts[3] = vec3(0.12f * t.boxH.x, yr, zb + 0.42f * h);
        planePts[4] = vec3(-t.boxH.x, t.com.y, zb + 0.45f * h);
        planePts[5] = vec3(t.boxH.x, t.com.y, zb + 0.45f * h);
        planePts[6] = vec3(0.f, yr, zt);
        planePts[7] = vec3(0.f, yf, zt - 0.2f * h);
    }
    for (int k = 0; k < 8; k++) {
        vec3 lp = plane ? planePts[k] : t.boxC + vec3(k & 1 ? t.boxH.x : -t.boxH.x, k & 2 ? t.boxH.y : -t.boxH.y, k & 4 ? t.boxH.z : -t.boxH.z);
        if (bike) {
            // footpegs / fairing: a bike scrapes at ~60 degrees of lean and lies on its side when down
            lp.x *= 0.45f;
            lp.z = Max(lp.z, t.com.z * 0.5f);
        }
        vec3 r = b.R * (lp - t.com);
        vec3 p = x.comW + r;
        vec3 v = b.velAt(r);
        float spec = Max(0.f, -dot(v, N0)) * x.dt;
        float h = dot(p - P0, N0);
        if (h > margin + spec) continue;
        Phys::GroundHit g = Phys::gCollision->ground(p.x, p.y, p.z + 0.35f + spec, 0.f);
        if (g.z < -1e8f) continue;
        float depth = (g.z - p.z) * g.normal.z;
        if (depth < -(spec + 0.01f)) continue;
        float mu = surfaceScrape(g.surface);
        if (s.cls == VC_AIRBOAT) mu = (g.surface == Phys::SURF_MUD || g.surface == Phys::SURF_GRASS) ? 0.1f : 0.25f;
        else if (isBoatClass(s.cls)) mu = 0.6f;
        else if (s.cls == VC_HELI) mu = 0.7f;
        x.cs.add(r, g.normal, depth, mu, 0.12f, -1, CK_GROUND, g.surface);
    }
}

// Breakable prop hit hard enough: knock it down, bleed a little speed, report it for debris.
bool tryBreak(StepCtx& x, int id, const Phys::Collider& c, vec3 r, vec3 n) {
    if (!(c.flags & 1)) return false;
    VehicleState& s = *x.s;
    Body& b = x.b;
    vec3 v = b.velAt(r);
    float vn = -dot(v, n);  // approach speed
    if (vn < 4.f) return false;
    vec3 base = c.c;
    float vol = c.kind == Phys::COL_BOX ? 8.f * c.he.x * c.he.y * c.he.z : kPi * c.he.x * c.he.x * c.he.z;
    float mProp = Clamp(vol * 250.f, 30.f, 250.f);
    if (!Phys::gCollision->breakCollider(id)) return true;
    // momentum exchange with a light prop (inelastic along n)
    float j = mProp * b.mass / (mProp + b.mass) * Min(vn, 25.f);
    b.impulse(n * j, r);
    if (s.brokenCount < 4) {
        s.brokenIds[s.brokenCount] = id;
        s.brokenPos[s.brokenCount] = vec3(base.x, base.y, c.kind == Phys::COL_BOX ? c.c.z - c.he.z : c.c.z);
        s.brokenVel[s.brokenCount] = v * 0.85f + vec3(0.f, 0.f, 1.5f + 0.1f * vn);
        s.brokenCount++;
    }
    vec3 rOrigin = r + b.R * s.tune.com;
    recordImpact(s, j, rOrigin, n, id);
    impactDamage(s, b.R, b.invMass, j * 0.6f, r);
    return true;
}

// Vehicle OBB vs a static box collider (vertical prism): SAT normal + manifold points.
void boxVsCollider(StepCtx& x, int id, const Phys::Collider& c, vec3 bc) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    Body& b = x.b;
    vec3 cax(c.ax.x, c.ax.y, 0.f), cay(-c.ax.y, c.ax.x, 0.f);
    mat3 Rc(cax, cay, vec3(0, 0, 1));
    vec3 nAB, pt;
    float depth;
    // speculative skin for fast approach
    vec3 H = t.boxH;
    if (!Phys::obbObb(bc, b.R, H + vec3(0.03f), c.c, Rc, c.he, nAB, depth, pt)) return;
    depth -= 0.03f;
    vec3 n = -nAB;
    float ext = fabsf(b.R.c[0].z) * H.x + fabsf(b.R.c[1].z) * H.y + fabsf(b.R.c[2].z) * H.z;
    float top = c.c.z + c.he.z;
    if (n.z > 0.6f && bc.z - ext > top - 0.35f) return;   // resting on the roof: ground() handles it
    if (fabsf(n.z) > 0.5f) {
        // World boxes stand on the ground: never push a vehicle down into the terrain (or up through a wall).
        // Separate horizontally along the collider face with the least penetration instead.
        vec3 d = bc - c.c;
        float ex = fabsf(dot(b.R.c[0], cax)) * H.x + fabsf(dot(b.R.c[1], cax)) * H.y + fabsf(dot(b.R.c[2], cax)) * H.z;
        float ey = fabsf(dot(b.R.c[0], cay)) * H.x + fabsf(dot(b.R.c[1], cay)) * H.y + fabsf(dot(b.R.c[2], cay)) * H.z;
        float dx = dot(d, cax), dy = dot(d, cay);
        float px = c.he.x + ex - fabsf(dx), py = c.he.y + ey - fabsf(dy);
        if (px < py) {
            n = cax * (dx >= 0.f ? 1.f : -1.f);
            depth = px;
        } else {
            n = cay * (dy >= 0.f ? 1.f : -1.f);
            depth = py;
        }
        if (depth <= 0.f) return;
    }
    if (tryBreak(x, id, c, pt - x.comW, n)) return;
    float hB = c.he.x * fabsf(dot(n, cax)) + c.he.y * fabsf(dot(n, cay)) + c.he.z * fabsf(n.z);
    float mu = 0.35f;
    int added = 0;
    for (int k = 0; k < 8; k++) {
        vec3 p = bc + b.R * vec3(k & 1 ? H.x : -H.x, k & 2 ? H.y : -H.y, k & 4 ? H.z : -H.z);
        vec3 d = p - c.c;
        if (fabsf(dot(d, cax)) > c.he.x + 0.03f || fabsf(dot(d, cay)) > c.he.y + 0.03f || fabsf(d.z) > c.he.z + 0.03f) continue;
        float pd = hB - dot(d, n);
        if (pd < -0.03f) continue;
        x.cs.add(p - x.comW, n, Min(pd, depth + 0.05f), mu, 0.1f, id, CK_STATIC, c.surface);
        added++;
    }
    // collider's vertical edges poking into the vehicle (building corners into a door)
    for (int k = 0; k < 4 && added < 4; k++) {
        vec3 q = c.c + cax * (k & 1 ? c.he.x : -c.he.x) + cay * (k & 2 ? c.he.y : -c.he.y);
        q.z = Clamp(bc.z, c.c.z - c.he.z, c.c.z + c.he.z);
        vec3 l = b.local(q - bc);
        if (fabsf(l.x) > H.x || fabsf(l.y) > H.y || fabsf(l.z) > H.z) continue;
        x.cs.add(q - x.comW, n, depth, mu, 0.1f, id, CK_STATIC, c.surface);
        added++;
    }
    if (!added) {
        // deepest vehicle vertex toward the obstacle
        vec3 p = bc;
        for (int a = 0; a < 3; a++) p += b.R.c[a] * (dot(b.R.c[a], n) > 0.f ? -H[a] : H[a]);
        x.cs.add(p - x.comW, n, depth, mu, 0.1f, id, CK_STATIC, c.surface);
    }
}

// Vehicle OBB vs a vertical cylinder (poles, trees, hydrants).
void cylVsCollider(StepCtx& x, int id, const Phys::Collider& c, vec3 bc) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    Body& b = x.b;
    vec3 H = t.boxH;
    float rad = c.he.x;
    if (rad <= 0.f) return;
    vec3 dirL(b.R.c[0].z, b.R.c[1].z, b.R.c[2].z);  // world up in local axes
    vec3 L0 = b.local(vec3(c.c.x, c.c.y, bc.z) - bc);
    float zw = bc.z;
    if (fabsf(dirL.z) > 0.2f) zw = bc.z - L0.z / dirL.z;
    zw = Clamp(zw, c.c.z, c.c.z + c.he.z);
    vec3 Q = L0 + dirL * (zw - bc.z);
    if (fabsf(Q.z) > H.z + 0.05f) return;
    vec3 cp(Clamp(Q.x, -H.x, H.x), Clamp(Q.y, -H.y, H.y), Clamp(Q.z, -H.z, H.z));
    float dx = Q.x - cp.x, dy = Q.y - cp.y;
    float d = sqrtf(dx * dx + dy * dy);
    vec3 nL, pL;
    float depth;
    if (d < 1e-5f) {
        float px = H.x - fabsf(Q.x), py = H.y - fabsf(Q.y);
        if (px < py) {
            nL = vec3(Q.x > 0.f ? -1.f : 1.f, 0.f, 0.f);
            pL = vec3(Q.x > 0.f ? H.x : -H.x, Q.y, cp.z);
            depth = px + rad;
        } else {
            nL = vec3(0.f, Q.y > 0.f ? -1.f : 1.f, 0.f);
            pL = vec3(Q.x, Q.y > 0.f ? H.y : -H.y, cp.z);
            depth = py + rad;
        }
    } else {
        if (d > rad + 0.03f) return;
        nL = vec3(-dx / d, -dy / d, 0.f);
        pL = cp;
        depth = rad - d;
    }
    vec3 n = b.R * nL;
    n.z = 0.f;
    float nl = length(n);
    if (nl < 0.3f) return;
    n = n / nl;
    vec3 r = bc + b.R * pL - x.comW;
    if (tryBreak(x, id, c, r, n)) return;
    x.cs.add(r, n, depth, 0.3f, 0.15f, id, CK_STATIC, c.surface);
}

void staticContacts(StepCtx& x) {
    VehicleState& s = *x.s;
    const VehicleTuning& t = s.tune;
    Body& b = x.b;
    vec3 bc = x.comW + b.R * (t.boxC - t.com);
    thread_local std::vector<int> ids;
    float reach = length(t.boxH) + x.speed * x.dt + 0.3f;
    Phys::gCollision->collidersNear(bc.xy(), reach, ids);
    if (ids.empty()) return;
    float ext = fabsf(b.R.c[0].z) * t.boxH.x + fabsf(b.R.c[1].z) * t.boxH.y + fabsf(b.R.c[2].z) * t.boxH.z;
    for (int id : ids) {
        const Phys::Collider& c = Phys::gCollision->collider(id);
        if (c.owner < 0) continue;
        float cz0 = c.kind == Phys::COL_BOX ? c.c.z - c.he.z : c.c.z;
        float cz1 = c.kind == Phys::COL_BOX ? c.c.z + c.he.z : c.c.z + c.he.z;
        if (bc.z - ext > cz1 || bc.z + ext < cz0) continue;
        // coarse horizontal reject
        float cr = c.kind == Phys::COL_BOX ? sqrtf(c.he.x * c.he.x + c.he.y * c.he.y) : c.he.x;
        float dx = bc.x - c.c.x, dy = bc.y - c.c.y;
        if (dx * dx + dy * dy > Sq(cr + reach)) continue;
        if (c.kind == Phys::COL_BOX) boxVsCollider(x, id, c, bc);
        else cylVsCollider(x, id, c, bc);
    }
}

// After the velocity solve: impacts, damage, scrapes, rider ejection.
void contactEvents(StepCtx& x) {
    VehicleState& s = *x.s;
    Body& b = x.b;
    float totJ = 0.f, staticJ = 0.f;
    vec3 accP(0.f), accN(0.f);
    int bestCollider = -1;
    float bestJ = 0.f;
    float scrape = 0.f;
    vec3 scrapeP(0.f);
    for (int i = 0; i < x.cs.n; i++) {
        const Contact& c = x.cs.c[i];
        if (c.kind == CK_BUMP || c.kind == CK_STAND || c.jn <= 0.f) continue;
        totJ += c.jn;
        if (c.kind == CK_STATIC) staticJ += c.jn;
        accP += c.r * c.jn;
        accN += c.n * c.jn;
        if (c.jn > bestJ) {
            bestJ = c.jn;
            bestCollider = c.collider;
        }
        vec3 v = b.velAt(c.r);
        vec3 vt = v - c.n * dot(v, c.n);
        float vtl = length(vt);
        if (vtl > 1.5f) {
            float press = Saturate(c.jn * b.invMass / (x.dt * kGrav * 0.25f));
            float sc = Saturate((vtl - 1.5f) / 12.f) * press;
            if (sc > scrape) {
                scrape = sc;
                scrapeP = c.r;
            }
        }
    }
    vec3 comOff = b.R * s.tune.com;
    if (scrape > s.scrape) {
        s.scrape = scrape;
        s.scrapePoint = scrapeP + comOff;
    }
    if (totJ <= 0.f) return;
    vec3 p = accP / totJ;
    recordImpact(s, totJ, p + comOff, normalize(accN), bestCollider);
    impactDamage(s, b.R, b.invMass, totJ, p);
    x.maxStaticImpulse = Max(x.maxStaticImpulse, staticJ);
    float dv = totJ * b.invMass;
    if ((isBikeClass(s.cls) || s.cls == VC_JETSKI) && x.c->hasDriver && !s.riderOff) {
        if ((staticJ > 0.f && staticJ * b.invMass > 4.5f) || dv > 7.f) {
            s.ejectRider = true;
            s.riderOff = true;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Vehicle vs vehicle

Body bodyFromState(const VehicleState& s) {
    Body b;
    b.rot = normalize(s.body.rot);
    b.R = mat3FromQuat(b.rot);
    vec3 off = b.R * s.tune.com;
    b.pos = s.body.pos + off;
    b.angVel = s.body.angVel;
    b.vel = s.body.vel + cross(b.angVel, off);
    b.mass = s.body.mass;
    b.invMass = s.body.invMass;
    b.invI = s.body.invInertiaLocal;
    return b;
}

void bodyToState(const Body& b, VehicleState& s) {
    vec3 off = b.R * s.tune.com;
    s.body.pos = b.pos - off;
    s.body.rot = b.rot;
    s.body.angVel = b.angVel;
    s.body.vel = b.vel - cross(b.angVel, off);
}

}  // namespace vsim

bool collideVehicles(VehicleState& a, VehicleState& b) {
    using namespace vsim;
    if (!a.model || !b.model || &a == &b) return false;
    Body A = bodyFromState(a), B = bodyFromState(b);
    const VehicleTuning &ta = a.tune, &tb = b.tune;
    vec3 d = rel(B.pos, A.pos);  // everything relative to A's COM
    vec3 ca = A.R * (ta.boxC - ta.com), cb = d + B.R * (tb.boxC - tb.com);
    float ra = length(ta.boxH), rb = length(tb.boxH);
    if (length2(cb - ca) > Sq(ra + rb)) return false;
    vec3 n, pt;
    float depth;
    if (!Phys::obbObb(ca, A.R, ta.boxH, cb, B.R, tb.boxH, n, depth, pt)) return false;
    // manifold: corners of each box inside the other
    vec3 pts[16];
    float deps[16];
    int np = 0;
    float hA = ta.boxH.x * fabsf(dot(n, A.R.c[0])) + ta.boxH.y * fabsf(dot(n, A.R.c[1])) + ta.boxH.z * fabsf(dot(n, A.R.c[2]));
    float hB = tb.boxH.x * fabsf(dot(n, B.R.c[0])) + tb.boxH.y * fabsf(dot(n, B.R.c[1])) + tb.boxH.z * fabsf(dot(n, B.R.c[2]));
    for (int k = 0; k < 8; k++) {
        vec3 sgn(k & 1 ? 1.f : -1.f, k & 2 ? 1.f : -1.f, k & 4 ? 1.f : -1.f);
        vec3 pa = ca + A.R * (ta.boxH * sgn);
        vec3 la = B.local(pa - cb);
        if (fabsf(la.x) <= tb.boxH.x && fabsf(la.y) <= tb.boxH.y && fabsf(la.z) <= tb.boxH.z && np < 16) {
            pts[np] = pa;
            deps[np] = Min(hB + dot(pa - cb, n), depth + 0.05f);  // depth of a's corner into b along n
            np++;
        }
        vec3 pb = cb + B.R * (tb.boxH * sgn);
        vec3 lb = A.local(pb - ca);
        if (fabsf(lb.x) <= ta.boxH.x && fabsf(lb.y) <= ta.boxH.y && fabsf(lb.z) <= ta.boxH.z && np < 16) {
            pts[np] = pb;
            deps[np] = Min(hA - dot(pb - ca, n), depth + 0.05f);
            np++;
        }
    }
    if (np == 0) {
        pts[0] = pt;
        deps[0] = depth;
        np = 1;
    }
    // sequential impulses between the two bodies
    float jn[16] = {}, jt1[16] = {}, jt2[16] = {}, tgt[16], mN[16], mT1[16], mT2[16];
    vec3 t1 = normalize(anyPerp(n)), t2 = cross(n, t1);
    auto eff = [&](vec3 r1, vec3 r2, vec3 dir) {
        vec3 c1 = cross(r1, dir), c2 = cross(r2, dir);
        return 1.f / Max(A.invMass + B.invMass + dot(c1, A.invIw(c1)) + dot(c2, B.invIw(c2)), 1e-9f);
    };
    for (int i = 0; i < np; i++) {
        vec3 r1 = pts[i], r2 = pts[i] - d;
        mN[i] = eff(r1, r2, n);
        mT1[i] = eff(r1, r2, t1);
        mT2[i] = eff(r1, r2, t2);
        float vn = dot(B.velAt(r2) - A.velAt(r1), n);
        tgt[i] = vn < -1.5f ? -0.15f * vn : 0.f;
    }
    const float mu = 0.35f;
    for (int it = 0; it < 8; it++)
        for (int i = 0; i < np; i++) {
            vec3 r1 = pts[i], r2 = pts[i] - d;
            float vn = dot(B.velAt(r2) - A.velAt(r1), n);
            float j = Max(jn[i] + (tgt[i] - vn) * mN[i], 0.f);
            float dj = j - jn[i];
            jn[i] = j;
            A.impulse(-n * dj, r1);
            B.impulse(n * dj, r2);
            vec3 vr = B.velAt(r2) - A.velAt(r1);
            float a1 = jt1[i] - dot(vr, t1) * mT1[i], a2 = jt2[i] - dot(vr, t2) * mT2[i];
            float lim = mu * jn[i];
            float l2 = a1 * a1 + a2 * a2;
            if (l2 > lim * lim) {
                float sc = lim / sqrtf(l2);
                a1 *= sc;
                a2 *= sc;
            }
            vec3 dt = t1 * (a1 - jt1[i]) + t2 * (a2 - jt2[i]);
            jt1[i] = a1;
            jt2[i] = a2;
            A.impulse(-dt, r1);
            B.impulse(dt, r2);
        }
    // positional separation (split by inverse mass)
    float maxDepth = 0.f;
    for (int i = 0; i < np; i++) maxDepth = Max(maxDepth, deps[i]);
    float corr = Max(maxDepth - 0.01f, 0.f) * 0.5f;
    float wsum = A.invMass + B.invMass;
    if (corr > 0.f && wsum > 0.f) {
        A.pos = A.pos - n * (corr * A.invMass / wsum);
        B.pos = B.pos + n * (corr * B.invMass / wsum);
    }
    float J = 0.f;
    vec3 acc(0.f);
    for (int i = 0; i < np; i++) {
        J += jn[i];
        acc += pts[i] * jn[i];
    }
    vec3 cp = J > 0.f ? acc / J : pt;
    bodyToState(A, a);
    bodyToState(B, b);
    if (J > 30.f) {
        a.sleeping = false;
        b.sleeping = false;
    }
    vec3 offA = A.R * ta.com, offB = B.R * tb.com;
    recordImpact(a, J, cp + offA, -n, -1);
    recordImpact(b, J, cp - d + offB, n, -1);
    impactDamage(a, A.R, A.invMass, J, cp);
    impactDamage(b, B.R, B.invMass, J, cp - d);
    for (VehicleState* v : {&a, &b}) {
        if ((isBikeClass(v->cls) || v->cls == VC_JETSKI) && !v->riderOff && J * v->body.invMass > 5.f) {
            v->ejectRider = true;
            v->riderOff = true;
        }
    }
    return true;
}

void applyDamage(VehicleState& s, float amount, vec3 pointRel, vec3 impulse) {
    if (!s.model) return;
    mat3 R = mat3FromQuat(s.body.rot);
    vec3 rCom = pointRel - R * s.tune.com;
    vsim::zoneDamage(s, Max(amount, 0.f), vsim::damageZone(s, R, rCom));
    if (length2(impulse) > 0.f) {
        vsim::Body b = vsim::bodyFromState(s);
        b.impulse(impulse, rCom);
        vsim::bodyToState(b, s);
    }
    s.sleeping = false;
}

}  // namespace Vehicles

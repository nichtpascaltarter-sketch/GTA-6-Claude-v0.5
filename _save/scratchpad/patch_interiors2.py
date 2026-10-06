import sys
root='/home/user/GTA-6-Claude-v0.5/src/game/'
# ---- Repo Man: Rook briefs Dex inside Rook's Garage
p=root+'story_act1.cpp'
s=open(p).read()
a='''        rook = spawnCast(g, CAST_ROOK, shopDoor, P.rookShop.yaw + kPi, FAC_FRIEND);
        provideRide(g, P.rookShop, -14.f);
        vec3 rp = pedPos(g, rook);
        std::vector<CutsceneShot> shots;
        establish(g, shots, rp, P.rookShop.yaw, 34.f, 14.f, 4.f);
        shots.push_back(shotTwo(rp, playerPos(g), 7.f));'''
b='''        // inside Rook's Garage, by the car on the lift (the street outside when the world has no garage interior)
        InteriorStage in = interiorStage("Rook's Garage");
        vec3 liftL;
        inGarage = in.ok() && in.marker(World::IM_CAR, liftL);
        std::vector<CutsceneShot> shots;
        if (inGarage) {
            vec3 f(liftL.x, liftL.y - 4.6f, 0.f);   // the open floor between the roll-up door and the lift
            rook = spawnCast(g, CAST_ROOK, in.at(f + vec3(1.1f, 1.7f, 0.f)), in.yaw(kPi), FAC_FRIEND);
            placePlayer(g, in.at(f + vec3(-0.5f, -0.9f, 0.f)), in.yaw(0.f));
            float side = liftL.x > (in.d->x0 + in.d->x1) * 0.5f ? -1.f : 1.f;
            shots.push_back(shotRoom(in, vec3(liftL.x + side * 4.6f, liftL.y - 7.8f, 2.4f), pedPos(g, rook), playerPos(g), 4.5f));
        } else {
            rook = spawnCast(g, CAST_ROOK, shopDoor, P.rookShop.yaw + kPi, FAC_FRIEND);
        }
        provideRide(g, P.rookShop, -14.f);
        vec3 rp = pedPos(g, rook);
        if (!inGarage) establish(g, shots, rp, P.rookShop.yaw, 34.f, 14.f, 4.f);
        shots.push_back(shotTwo(rp, playerPos(g), 7.f));'''
assert s.count(a)==1, 'repo start'
s=s.replace(a,b)
a='''                    if (rook >= 0) setGoto(g, rook, shopDoor + vec3(gPlaces.rookShop.outward * 5.f, 0.f), 1.2f);
                    goTo(g, hangout, 30.f, "Go to the Cuervo hangout in ~y~Calle Luna~s~.", false, false);'''
b='''                    if (rook >= 0 && !inGarage) setGoto(g, rook, shopDoor + vec3(gPlaces.rookShop.outward * 5.f, 0.f), 1.2f);   // inside, he stays at work
                    goTo(g, hangout, 30.f, "Go to the Cuervo hangout in ~y~Calle Luna~s~.", false, false);'''
assert s.count(a)==1, 'repo stage0'
s=s.replace(a,b)
a='''    std::vector<int> loiterers;
    const char* title() const override { return "Repo Man"; }'''
b='''    std::vector<int> loiterers;
    bool inGarage = false;   // the briefing is staged inside Rook's Garage
    const char* title() const override { return "Repo Man"; }'''
assert s.count(a)==1, 'repo member'
s=s.replace(a,b)
# ---- Pressure Cooker: the fixer leans on Lucha across her own counter while Mari walks in
a='''        lucha = spawnCast(g, CAST_LUCHA, P.diner.door, P.diner.yaw + kPi, FAC_FRIEND);
        fixer = spawnCast(g, CAST_BANKER, placeOffset(g, P.diner, 1.5f, 1.5f), P.diner.yaw, FAC_CIVILIAN);
        if (lucha >= 0) g.peds[lucha].invincible = true;
        if (fixer >= 0) g.peds[fixer].invincible = true;
        facePed(g, lucha, pedPos(g, fixer));
        facePed(g, fixer, pedPos(g, lucha));'''
b='''        // inside Mama Lucha's: Lucha behind her counter, the fixer across it, Mari just in the door
        InteriorStage in = interiorStage("Mama Lucha's");
        vec3 counterL, waiterL;
        inDiner = in.ok() && in.marker(World::IM_COUNTER, counterL) && in.scenario(World::SR_WAITER, 0, waiterL);
        if (inDiner) {
            lucha = spawnCast(g, CAST_LUCHA, in.at(vec3(counterL.x, waiterL.y, 0.f)), in.yaw(kPi), FAC_FRIEND);
            fixer = spawnCast(g, CAST_BANKER, in.at(counterL), in.yaw(0.f), FAC_CIVILIAN);
        } else {
            lucha = spawnCast(g, CAST_LUCHA, P.diner.door, P.diner.yaw + kPi, FAC_FRIEND);
            fixer = spawnCast(g, CAST_BANKER, placeOffset(g, P.diner, 1.5f, 1.5f), P.diner.yaw, FAC_CIVILIAN);
        }
        if (lucha >= 0) g.peds[lucha].invincible = true;
        if (fixer >= 0) g.peds[fixer].invincible = true;
        facePed(g, lucha, pedPos(g, fixer));
        facePed(g, fixer, pedPos(g, lucha));'''
assert s.count(a)==1, 'pressure start'
s=s.replace(a,b)
a='''        vec3 lp = pedPos(g, lucha), fp = pedPos(g, fixer);
        placePlayer(g, placeOffset(g, P.diner, -8.f, 0.f), yawTo(placeOffset(g, P.diner, -8.f, 0.f).xy(), lp.xy()));
        std::vector<CutsceneShot> shots;
        establish(g, shots, lp, P.diner.yaw, 28.f, 10.f, 3.5f);
        shots.push_back(shotTwo(lp, fp, 7.f));'''
b='''        vec3 lp = pedPos(g, lucha), fp = pedPos(g, fixer);
        std::vector<CutsceneShot> shots;
        if (inDiner) {
            vec3 me = in.at(in.entry() + vec3(0.5f, 0.3f, 0.f));
            placePlayer(g, me, yawTo(me.xy(), lp.xy()));
            float side = counterL.x > (in.d->x0 + in.d->x1) * 0.5f ? -1.f : 1.f;   // along the counter from its long side
            shots.push_back(shotRoom(in, vec3(counterL.x + side * 4.2f, Max(0.9f, counterL.y - 2.6f), 2.1f), lp, fp, 4.5f));
        } else {
            placePlayer(g, placeOffset(g, P.diner, -8.f, 0.f), yawTo(placeOffset(g, P.diner, -8.f, 0.f).xy(), lp.xy()));
            establish(g, shots, lp, P.diner.yaw, 28.f, 10.f, 3.5f);
        }
        shots.push_back(shotTwo(lp, fp, 7.f));'''
assert s.count(a)==1, 'pressure shots'
s=s.replace(a,b)
a='''                    if (lucha >= 0) setGoto(g, lucha, gPlaces.diner.door + vec3(gPlaces.diner.outward * 3.f, 0.f), 1.2f);
                    next();'''
b='''                    if (lucha >= 0 && !inDiner) setGoto(g, lucha, gPlaces.diner.door + vec3(gPlaces.diner.outward * 3.f, 0.f), 1.2f);   // inside, she stays at her counter
                    next();'''
assert s.count(a)==1, 'pressure stage0'
s=s.replace(a,b)
a='''    int talkLine = 0;
    float outside = 0.f;
    const char* title() const override { return "Pressure Cooker"; }'''
b='''    int talkLine = 0;
    float outside = 0.f;
    bool inDiner = false;    // the opening is staged inside Mama Lucha's
    const char* title() const override { return "Pressure Cooker"; }'''
assert s.count(a)==1, 'pressure member'
s=s.replace(a,b)
open(p,'w').write(s)
print('patched')

#include "input.h"

namespace Game {

namespace input_detail {

Button key(const InputState& in, int k) {
    Button b;
    b.down = in.down(k);
    b.pressed = in.pressed(k);
    b.released = in.released(k);
    return b;
}
Button pad(const InputState& in, u16 m) {
    Button b;
    b.down = in.pad.down(m);
    b.pressed = in.pad.pressed(m);
    b.released = in.pad.released(m);
    return b;
}
Button either(Button a, Button b) {
    Button r;
    r.down = a.down || b.down;
    r.pressed = a.pressed || b.pressed;
    r.released = (a.released || b.released) && !r.down;
    return r;
}
struct TriggerEdge {
    bool prevR = false, prevL = false;
};
TriggerEdge gTrig;

}  // namespace input_detail

using namespace input_detail;

void readControls(const InputState& in, const InputConfig& cfg, bool inVehicle, bool aircraft, float dt, Controls& c) {
    c = Controls();
    const GamepadState& p = in.pad;
    c.usingPad = in.lastInputWasPad && p.connected;

    // ---- Movement
    vec2 kb(0, 0);
    if (in.down(KEY_W)) kb.y += 1;
    if (in.down(KEY_S)) kb.y -= 1;
    if (in.down(KEY_D)) kb.x += 1;
    if (in.down(KEY_A)) kb.x -= 1;
    if (length2(kb) > 1.f) kb = normalize(kb);
    c.move = kb;
    if (length2(p.leftStick) > length2(c.move)) c.move = p.leftStick;
    if (length2(c.move) > 1.f) c.move = normalize(c.move);

    // ---- Camera look
    float ms = 0.0022f * cfg.mouseSensitivity;
    vec2 mouse(in.mouseDelta.x * ms, -in.mouseDelta.y * ms);
    float padRate = 2.6f * cfg.padSensitivity;
    vec2 rs = p.rightStick;
    // response curve for fine aiming
    rs = rs * (0.35f + 0.65f * length(rs));
    vec2 padLook(rs.x * padRate * dt, rs.y * padRate * 0.75f * dt);
    c.look = mouse + padLook;
    if (cfg.invertY) c.look.y = -c.look.y;
    c.lookActive = length2(in.mouseDelta) > 0.5f || length2(p.rightStick) > 0.01f;

    // ---- On-foot buttons
    c.sprint = either(key(in, KEY_SHIFT), pad(in, PAD_A));
    c.jump = either(key(in, KEY_SPACE), pad(in, PAD_X));
    c.walkToggle = key(in, KEY_ALT);
    c.crouch = either(key(in, KEY_CONTROL), pad(in, PAD_LTHUMB));
    c.enter = either(key(in, KEY_F), pad(in, PAD_Y));
    bool rt = p.rightTrigger > 0.35f, lt = p.leftTrigger > 0.35f;
    Button rtb, ltb;
    rtb.down = rt;
    rtb.pressed = rt && !gTrig.prevR;
    rtb.released = !rt && gTrig.prevR;
    ltb.down = lt;
    ltb.pressed = lt && !gTrig.prevL;
    ltb.released = !lt && gTrig.prevL;
    gTrig.prevR = rt;
    gTrig.prevL = lt;
    c.attack = either(key(in, KEY_MOUSE_LEFT), rtb);
    c.aim = either(key(in, KEY_MOUSE_RIGHT), ltb);
    c.reload = either(key(in, KEY_R), pad(in, PAD_B));
    c.weaponWheel = either(key(in, KEY_TAB), pad(in, PAD_LB));
    c.cover = either(key(in, KEY_Q), pad(in, PAD_RB));
    c.phone = either(key(in, KEY_UP), pad(in, PAD_UP));
    if (!inVehicle) {
        if (in.wheelDelta > 0.5f) c.weaponScroll = -1;
        if (in.wheelDelta < -0.5f) c.weaponScroll = 1;
        if (p.pressed(PAD_RIGHT)) c.weaponScroll = 1;
        if (p.pressed(PAD_LEFT)) c.weaponScroll = -1;
    }
    for (int k = 0; k < 9; k++)
        if (in.pressed(KEY_1 + k)) c.weaponSlot = k;

    // ---- Vehicle
    float kbAccel = in.down(KEY_W) ? 1.f : 0.f, kbBrake = in.down(KEY_S) ? 1.f : 0.f;
    float kbSteer = (in.down(KEY_D) ? 1.f : 0.f) - (in.down(KEY_A) ? 1.f : 0.f);
    c.accel = Max(kbAccel, p.rightTrigger);
    c.brake = Max(kbBrake, p.leftTrigger);
    c.steer = fabsf(p.leftStick.x) > fabsf(kbSteer) ? p.leftStick.x : kbSteer;
    c.handbrake = either(key(in, KEY_SPACE), pad(in, PAD_RB));
    c.horn = either(key(in, KEY_E), pad(in, PAD_LTHUMB));
    c.lights = either(key(in, KEY_H), pad(in, PAD_DOWN));
    c.lookBehind = either(key(in, KEY_C), pad(in, PAD_RTHUMB));
    c.camMode = either(key(in, KEY_V), pad(in, PAD_BACK));
    c.special = either(key(in, KEY_G), pad(in, PAD_UP));
    {
        Button caps = key(in, 0x14);  // VK_CAPITAL
        Button sticks;
        bool both = p.down(PAD_LTHUMB) && p.down(PAD_RTHUMB);
        bool bothPrev = (p.prevButtons & PAD_LTHUMB) && (p.prevButtons & PAD_RTHUMB);
        sticks.down = both;
        sticks.pressed = both && !bothPrev;
        c.focus = either(caps, sticks);
    }
    if (inVehicle) {
        Button nx = key(in, KEY_PGUP), pv = key(in, KEY_PGDN);
        c.radioNext = either(either(nx, pad(in, PAD_RIGHT)), key(in, KEY_Q));
        c.radioPrev = either(pv, pad(in, PAD_LEFT));
        if (in.wheelDelta > 0.5f) c.radioNext.pressed = true;
        if (in.wheelDelta < -0.5f) c.radioPrev.pressed = true;
    }
    if (aircraft) {
        // Keyboard: W/S throttle-collective, A/D yaw, arrow keys (or mouse) pitch/roll. Pad: LS pitch/roll, LB/RB yaw.
        float kp = (in.down(KEY_DOWN) ? 1.f : 0.f) - (in.down(KEY_UP) ? 1.f : 0.f);
        float kr = (in.down(KEY_RIGHT) ? 1.f : 0.f) - (in.down(KEY_LEFT) ? 1.f : 0.f);
        c.pitch = Clamp(kp - p.leftStick.y, -1.f, 1.f);
        c.roll = Clamp(kr + p.leftStick.x, -1.f, 1.f);
        c.yaw = Clamp(kbSteer + (p.down(PAD_RB) ? 1.f : 0.f) - (p.down(PAD_LB) ? 1.f : 0.f), -1.f, 1.f);
        c.lift = Clamp(c.accel - c.brake, -1.f, 1.f);
        c.handbrake = Button();
    }

    // ---- Global
    c.pause = either(key(in, KEY_ESCAPE), pad(in, PAD_START));
    c.map = key(in, KEY_M);
    c.skip = either(key(in, KEY_SPACE), pad(in, PAD_A));
    c.confirm = either(key(in, KEY_ENTER), pad(in, PAD_A));
    c.back = either(key(in, KEY_BACK), pad(in, PAD_B));
    vec2 nav(0, 0);
    if (in.pressed(KEY_UP) || p.pressed(PAD_UP)) nav.y = 1;
    if (in.pressed(KEY_DOWN) || p.pressed(PAD_DOWN)) nav.y = -1;
    if (in.pressed(KEY_LEFT) || p.pressed(PAD_LEFT)) nav.x = -1;
    if (in.pressed(KEY_RIGHT) || p.pressed(PAD_RIGHT)) nav.x = 1;
    c.menuNav = nav;
}

}  // namespace Game

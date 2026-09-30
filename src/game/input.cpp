#include "input.h"

// bindable actions (UI::InputAction) exist only with the game UI; without it every action uses its default key
#ifdef HAVE_GAME_UI
#define UI_IA(a) ((int)UI::a)
#else
#define UI_IA(a) 0
#endif

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
struct Latches {   // hold-vs-toggle options
    bool aim = false, sprint = false;
};
Latches gLatch;

// Keyboard / mouse source of a bindable action: the player's binding table (Settings > Key Bindings) when there is
// one, else the default key.
struct Binds {
    const InputState& in;
    const void* table;
    Button get(int action, int defaultKey) const {
#ifdef HAVE_GAME_UI
        if (table) {
            const UI::GameSettings& s = *(const UI::GameSettings*)table;
            Button b;
            b.down = UI::bindingDown(s, in, (UI::InputAction)action);
            b.pressed = UI::bindingPressed(s, in, (UI::InputAction)action);
            b.released = UI::bindingReleased(s, in, (UI::InputAction)action);
            return b;
        }
#else
        (void)action;
#endif
        return key(in, defaultKey);
    }
};

// A toggle option turns presses into a latched "down" state (released by the next press).
Button latched(Button raw, bool& latch) {
    Button b;
    bool was = latch;
    if (raw.pressed) latch = !latch;
    b.down = latch;
    b.pressed = latch && !was;
    b.released = !latch && was;
    return b;
}

}  // namespace input_detail

using namespace input_detail;

void readControls(const InputState& in, const InputConfig& cfg, bool inVehicle, bool aircraft, float dt, Controls& c) {
    c = Controls();
    GamepadState p = in.pad;
    c.usingPad = in.lastInputWasPad && p.connected;
    Binds kb{in, cfg.bindings};
    // controller layouts: southpaw swaps the sticks (and their clicks); alternate swaps jump and sprint
    u16 padSprint = PAD_A, padJump = PAD_X, padLThumb = PAD_LTHUMB, padRThumb = PAD_RTHUMB;
    if (cfg.padLayout == 1) padSprint = PAD_X, padJump = PAD_A;
    if (cfg.padLayout == 2) {
        vec2 t = p.leftStick;
        p.leftStick = p.rightStick;
        p.rightStick = t;
        padLThumb = PAD_RTHUMB, padRThumb = PAD_LTHUMB;
    }

    // ---- Movement (on foot: bound keys or the left stick)
    vec2 mv(0, 0);
    if (kb.get(UI_IA(IA_MOVE_FORWARD), KEY_W).down) mv.y += 1;
    if (kb.get(UI_IA(IA_MOVE_BACK), KEY_S).down) mv.y -= 1;
    if (kb.get(UI_IA(IA_MOVE_RIGHT), KEY_D).down) mv.x += 1;
    if (kb.get(UI_IA(IA_MOVE_LEFT), KEY_A).down) mv.x -= 1;
    if (length2(mv) > 1.f) mv = normalize(mv);
    c.move = mv;
    if (length2(p.leftStick) > length2(c.move)) c.move = p.leftStick;
    if (length2(c.move) > 1.f) c.move = normalize(c.move);

    // ---- Camera look
    float ms = 0.0022f * cfg.mouseSensitivity, msY = 0.0022f * cfg.mouseSensitivityY;
    vec2 mouse(in.mouseDelta.x * ms, -in.mouseDelta.y * msY);
    float padRate = 2.6f * cfg.padSensitivity, padRateY = 2.6f * cfg.padSensitivityY;
    vec2 rs = p.rightStick;
    // response curve for fine aiming
    rs = rs * (0.35f + 0.65f * length(rs));
    vec2 padLook(rs.x * padRate * dt, rs.y * padRateY * 0.75f * dt);
    c.look = mouse + padLook;
    if (cfg.invertY) c.look.y = -c.look.y;
    c.lookActive = length2(in.mouseDelta) > 0.5f || length2(p.rightStick) > 0.01f;

    // ---- On-foot buttons
    Button sprintRaw = either(kb.get(UI_IA(IA_SPRINT), KEY_SHIFT), pad(in, padSprint));
    c.sprint = cfg.sprintToggle ? latched(sprintRaw, gLatch.sprint) : sprintRaw;
    if (cfg.sprintToggle && length2(c.move) < 0.04f) gLatch.sprint = false;   // toggled sprint ends when you stop
    c.jump = either(kb.get(UI_IA(IA_JUMP), KEY_SPACE), pad(in, padJump));
    c.walkToggle = kb.get(UI_IA(IA_WALK), KEY_ALT);
    c.crouch = either(kb.get(UI_IA(IA_CROUCH), KEY_CONTROL), pad(in, padLThumb));
    c.crouchHold = !cfg.crouchToggle;
    c.enter = either(kb.get(UI_IA(IA_ENTER_VEHICLE), KEY_F), pad(in, PAD_Y));
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
    c.attack = either(kb.get(UI_IA(IA_ATTACK), KEY_MOUSE_LEFT), rtb);
    Button aimRaw = either(kb.get(UI_IA(IA_AIM), KEY_MOUSE_RIGHT), ltb);
    if (inVehicle) gLatch.aim = false;
    c.aim = cfg.aimToggle && !inVehicle ? latched(aimRaw, gLatch.aim) : aimRaw;
    c.reload = either(kb.get(UI_IA(IA_RELOAD), KEY_R), pad(in, PAD_B));
    c.weaponWheel = either(kb.get(UI_IA(IA_WEAPON_WHEEL), KEY_TAB), pad(in, PAD_LB));
    c.cover = either(kb.get(UI_IA(IA_COVER), KEY_Q), pad(in, PAD_RB));
    c.phone = either(kb.get(UI_IA(IA_PHONE), KEY_UP), pad(in, PAD_UP));
    if (!inVehicle) {
        if (in.wheelDelta > 0.5f) c.weaponScroll = -1;
        if (in.wheelDelta < -0.5f) c.weaponScroll = 1;
        if (p.pressed(PAD_RIGHT)) c.weaponScroll = 1;
        if (p.pressed(PAD_LEFT)) c.weaponScroll = -1;
    }
    for (int k = 0; k < 9; k++)
        if (in.pressed(KEY_1 + k)) c.weaponSlot = k;

    // ---- Vehicle
    float kbAccel = kb.get(UI_IA(IA_ACCELERATE), KEY_W).down ? 1.f : 0.f, kbBrake = kb.get(UI_IA(IA_BRAKE), KEY_S).down ? 1.f : 0.f;
    float kbSteer = (kb.get(UI_IA(IA_STEER_RIGHT), KEY_D).down ? 1.f : 0.f) - (kb.get(UI_IA(IA_STEER_LEFT), KEY_A).down ? 1.f : 0.f);
    c.accel = Max(kbAccel, p.rightTrigger);
    c.brake = Max(kbBrake, p.leftTrigger);
    c.steer = fabsf(p.leftStick.x) > fabsf(kbSteer) ? p.leftStick.x : kbSteer;
    c.handbrake = either(kb.get(UI_IA(IA_HANDBRAKE), KEY_SPACE), pad(in, PAD_RB));
    c.horn = either(kb.get(UI_IA(IA_HORN), KEY_E), pad(in, padLThumb));
    c.lights = either(kb.get(UI_IA(IA_HEADLIGHTS), KEY_H), pad(in, PAD_DOWN));
    c.lookBehind = either(kb.get(UI_IA(IA_LOOK_BEHIND), KEY_C), pad(in, padRThumb));
    c.camMode = either(kb.get(UI_IA(IA_CAMERA_VIEW), KEY_V), pad(in, PAD_BACK));
    c.special = either(kb.get(UI_IA(IA_INTERACT), KEY_G), pad(in, PAD_UP));
    {
        Button caps = kb.get(UI_IA(IA_FOCUS), 0x14);  // VK_CAPITAL
        Button sticks;
        bool both = p.down(PAD_LTHUMB) && p.down(PAD_RTHUMB);
        bool bothPrev = (p.prevButtons & PAD_LTHUMB) && (p.prevButtons & PAD_RTHUMB);
        sticks.down = both;
        sticks.pressed = both && !bothPrev;
        c.focus = either(caps, sticks);
    }
    if (inVehicle) {
        Button nx = kb.get(UI_IA(IA_RADIO_NEXT), KEY_PGUP), pv = kb.get(UI_IA(IA_RADIO_PREV), KEY_PGDN);
        c.radioNext = either(either(nx, pad(in, PAD_RIGHT)), cfg.bindings ? Button() : key(in, KEY_Q));   // Q: default second key
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

    // ---- Global (fixed keys: Esc / Enter / Backspace / menu navigation are not rebindable)
    c.pause = either(key(in, KEY_ESCAPE), pad(in, PAD_START));
    c.map = kb.get(UI_IA(IA_MAP), KEY_M);
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

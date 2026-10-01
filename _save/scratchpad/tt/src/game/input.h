// Semantic game controls built each frame from raw keyboard/mouse/XInput state.
#pragma once
#include "../platform/platform.h"

namespace Game {

struct Button {
    bool down = false, pressed = false, released = false;
};

struct Controls {
    bool usingPad = false;
    // On foot
    vec2 move;             // x right, y forward, magnitude 0..1 (keyboard: 1)
    vec2 look;             // camera rotation this frame (radians): x = yaw right, y = pitch up
    bool lookActive = false;
    Button sprint, jump, walkToggle, crouch, enter, attack, aim, reload, weaponWheel, cover, phone;
    int weaponScroll = 0;  // +1 next / -1 previous
    int weaponSlot = -1;   // direct selection (keys 1..9), -1 none
    // Vehicle
    float accel = 0.f, brake = 0.f, steer = 0.f;
    float pitch = 0.f, roll = 0.f, yaw = 0.f, lift = 0.f;   // aircraft
    Button handbrake, horn, lights, radioNext, radioPrev, lookBehind, camMode, special;
    // Global
    Button pause, map, skip, confirm, back;
    vec2 menuNav;          // edge-triggered (-1/0/1) menu navigation
};

struct InputConfig {
    float mouseSensitivity = 1.f;
    float padSensitivity = 1.f;
    bool invertY = false;
};

void readControls(const InputState& in, const InputConfig& cfg, bool inVehicle, bool aircraft, float dt, Controls& out);

}  // namespace Game

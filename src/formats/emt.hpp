// Particle emitters (.emt, indexed by [Paths] Effects = effects.mgr): the
// effects the engine draws as particles, among them every ranged weapon's
// bolts (a weapon names its emitter by id). An emitter is a header, a
// physics description (how particles start and move) and a particle
// description (how one is drawn).
// Format notes and evidence: docs/formats/emitter.md
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace harbinger::emt {

// GBasicDesc (physics type 1; 0x4e93a0 reads it, GBasicDesc_v1 starts a
// particle, GBasicDesc_v2 moves it). Angles are in 256ths of a turn.
struct Basic {
    uint16_t version = 0;
    uint32_t lifeMs = 0, lifeVarMs = 0;  // +8, +0xc: a particle lives life + rand * var
    uint8_t polar = 0, polarVar = 0;     // +0x10, +0x12: from the emitter's axis; 64 is level
    uint8_t azimuth = 0, azimuthVar = 0; // +0x11, +0x13
    double speed = 0, speedVar = 0;      // +0x18, +0x20: map cells per ms (scaled by the emitter)
    double spread[3] = {};               // +0x28..+0x38: random start offset per axis
    double accel[3] = {};                // +0x40..+0x50: added to the velocity per ms
    int16_t fan = 0;                     // +0x5c: with flag bit 1, particles fan out evenly in this many steps
    uint8_t flags = 0;                   // bit 0: accelerate towards the emitter; bit 1: fan; bit 2: spread along the heading
};

// GAnimatedImageDesc (0x4e70d0) and GBillboardDesc (particle type 1,
// 0x4e7980): a picture, optionally cut into frames, drawn facing the screen.
struct Billboard {
    uint16_t version = 0;
    std::string image;          // a .tga path (served from the .gim)
    double frameTime = 0;       // +0x20
    uint8_t imageFlags = 0;     // bit 0 -> +0x2a, bit 1 -> +0x28, bit 2 -> +0x29
    int16_t frameW = 0, frameH = 0, columns = 0, rows = 0, firstFrame = 0;  // +0x14..+0x1c
    double sizes[4] = {};       // +0x30..+0x48
    uint8_t byte50 = 0;
    double more[4] = {};        // +0x58..+0x70
    uint8_t flags = 0;          // bit 0 -> +0x78; bit 1 clear -> +0x79
};

struct Emitter {
    uint16_t version = 0;       // 1 or 2
    uint16_t wordD0 = 0;        // +0xd0
    uint32_t count = 0;         // particles per burst (a weapon fires this many; 0 means 1)
    double rate = 0, d108 = 0;  // +0x100, +0x108
    uint32_t unknown = 0;
    uint8_t byteD2 = 0;
    uint8_t flags = 0;          // bit 0 -> +0xd3; bit 1 clears +0xd5
    uint16_t physicsType = 0;   // 0 GPhysicsDesc, 1 GBasicDesc, 2 GCircularDesc, 3 GLinearDesc
    uint16_t particleType = 0;  // 1 GBillboardDesc, 2 GRadialDesc, 3 GLineDesc
    std::optional<Basic> basic;
    std::optional<Billboard> billboard;
    // Version 2: an emitter to start alongside (a trail), and two doubles.
    uint16_t trail = 0;         // +0xd6, effects.mgr id
    double d110 = 0, d118 = 0;
    // True when every part was decoded (physics type 1 with a billboard, or
    // type 0); the other descriptions are not decoded yet.
    bool complete = false;
};

// Throws std::runtime_error on malformed data.
Emitter parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::emt

// The per-map NPC-properties table: every field NPC's placement and behaviour
// (npc_prop.asm, ROM c4/1d52). 2,193 records of 9 packed bytes, shared across
// maps through the per-map offset table (the same MapTriggerOffsetEntry shape as
// the event-trigger and map-trigger families). The row data is generated
// (src/data/generated/npc_prop_*.inc); this header owns the record type, its
// three builders, and the per-map accessors.
//
// A record is variant-polymorphic: the same nine bytes mean different things for
// a normal NPC, an animated NPC, and an NPC with special graphics. field InitNPCs
// (field/obj.asm:254) discriminates the variant at load time and decodes each
// field; the accessors below mirror that decode, and isSpecial()/isAnimated()
// expose the discrimination. Write records through the three named builders
// (npc / animated / special), which pack to the exact ROM bytes (the full-corpus
// memcmp test + the parser's ROM cross-check prove byte-identity); read them
// through the accessors.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "ostinato/event_dir.h"
#include "ostinato/event_script_ref.h"
#include "ostinato/event_trigger.h"  // kEventReturnScript
#include "ostinato/event_vehicle.h"
#include "ostinato/map_sprite_gfx.h"
#include "ostinato/map_sprite_pal.h"
#include "ostinato/npc_anim_frame.h"
#include "ostinato/npc_anim_speed.h"
#include "ostinato/npc_anim_type.h"
#include "ostinato/npc_layer_priority.h"
#include "ostinato/npc_master_offset_dir.h"
#include "ostinato/npc_movement.h"
#include "ostinato/npc_react.h"
#include "ostinato/npc_scroll.h"
#include "ostinato/npc_sprite_priority.h"
#include "ostinato/object_speed.h"
#include "data/map_triggers.h"  // MapTriggerOffsetEntry

namespace ostinato {

// One NPC record: nine bytes, byte-identical to a ROM NPCProp entry. Bytes 0-2,
// 7, and 8 are interpreted per variant (see the accessors). The switch id is a
// 10-bit value stored as (id - $0300); a stored 0 means "no gating switch".
struct NpcProperties {
    std::array<std::uint8_t, 9> bytes;

    static constexpr std::uint16_t kSwitchBias = 0x0300;

    // --- variant discrimination (matches the InitNPCs / special-init split) --
    // Special graphics: byte 4 bit 7 set with no vehicle in byte 7 (a normal NPC
    // that shows a rider also sets byte 4 bit 7, but always has a vehicle).
    constexpr bool isSpecial() const {
        return (bytes[4] & 0x80) != 0 && (bytes[7] & 0xC0) == 0;
    }
    // Animated (non-special) NPC: byte 8 carries a nonzero animation-frame field.
    constexpr bool isAnimated() const {
        return !isSpecial() && (bytes[8] & 0xE0) != 0;
    }

    // --- fields common to every variant --------------------------------------
    // Tile position of the NPC (bytes 4/5; the high bits carry other fields).
    constexpr std::uint8_t posX() const { return bytes[4] & 0x7F; }
    constexpr std::uint8_t posY() const { return bytes[5] & 0x3F; }
    // Movement speed (byte 5 bits 6-7); only SLOWER..FAST occur (2-bit field).
    constexpr ObjectSpeed speed() const {
        return static_cast<ObjectSpeed>((bytes[5] >> 6) & 0x03);
    }
    // Sprite graphics set / actor index (byte 6).
    constexpr MapSpriteGfx gfx() const {
        return static_cast<MapSpriteGfx>(bytes[6]);
    }
    // Palette (byte 2 bits 2-4).
    constexpr MapSpritePal pal() const {
        return static_cast<MapSpritePal>((bytes[2] >> 2) & 0x07);
    }
    // Autonomous movement behaviour (byte 7 bits 0-3).
    constexpr NpcMovement movement() const {
        return static_cast<NpcMovement>(bytes[7] & 0x0F);
    }
    // Sprite-vs-sprite draw priority (byte 7 bits 4-5).
    constexpr NpcSpritePriority spritePriority() const {
        return static_cast<NpcSpritePriority>(bytes[7] & 0x30);
    }
    // Sprite-vs-background layer priority (byte 8 bits 3-4).
    constexpr NpcLayerPriority layerPriority() const {
        return static_cast<NpcLayerPriority>(bytes[8] & 0x18);
    }
    // Whether the NPC scrolls with BG2 rather than BG1 (byte 2 bit 5).
    constexpr bool scrollsWithBg2() const { return (bytes[2] & 0x20) != 0; }
    // The gating event switch (byte 2 bits 6-7 + byte 3), rebiased to its
    // $0300-based id; 0 means the NPC is not gated by a switch.
    constexpr std::uint16_t switchId() const {
        const std::uint16_t stored = static_cast<std::uint16_t>(
            (bytes[3] << 2) | ((bytes[2] >> 6) & 0x03));
        return stored == 0 ? 0 : static_cast<std::uint16_t>(stored + kSwitchBias);
    }

    // --- normal / animated fields --------------------------------------------
    // The event script the NPC runs when activated (bytes 0-1 + byte 2 bits 0-1).
    // Meaningful only when !isSpecial().
    constexpr EventScriptRef eventRef() const {
        return EventScriptRef{{bytes[0], bytes[1],
                               static_cast<std::uint8_t>(bytes[2] & 0x03)}};
    }
    // Whether the NPC turns to face the player when activated (byte 8 bit 2).
    // Meaningful on normal / animated records (special uses the bit for 32x32).
    constexpr NpcReact react() const {
        return static_cast<NpcReact>(bytes[8] & 0x04);
    }

    // --- normal-only fields --------------------------------------------------
    // Facing direction (byte 8 bits 0-1). Normal records only.
    constexpr EventDir dir() const {
        return static_cast<EventDir>(bytes[8] & 0x03);
    }
    // Vehicle the NPC rides (byte 7 bits 6-7). Normal records only.
    constexpr EventVehicle vehicle() const {
        return static_cast<EventVehicle>((bytes[7] & 0xC0) >> 1);
    }
    // Whether a ridden vehicle shows its rider (byte 4 bit 7). Normal records
    // only — special records set the bit unconditionally.
    constexpr bool showRider() const { return (bytes[4] & 0x80) != 0; }

    // --- animated / special animation fields ---------------------------------
    // Frame-cycle mode (byte 8 bits 0-1). Animated / special records.
    constexpr NpcAnimType animType() const {
        return static_cast<NpcAnimType>(bytes[8] & 0x03);
    }
    // Animation frame mode (byte 8 bits 5-7). Animated / special records.
    constexpr NpcAnimFrame animFrame() const {
        return static_cast<NpcAnimFrame>(bytes[8] & 0xE0);
    }
    // Animation frame rate (byte 7 bits 6-7). Animated (non-special) records.
    constexpr NpcAnimSpeed animSpeed() const {
        return static_cast<NpcAnimSpeed>(bytes[7] & 0xC0);
    }

    // --- special-only fields -------------------------------------------------
    // VRAM tile position of the special graphic (byte 0 bits 0-3 / 4-6).
    constexpr std::uint8_t vramX() const { return bytes[0] & 0x0F; }
    constexpr std::uint8_t vramY() const { return (bytes[0] >> 4) & 0x07; }
    // Horizontal flip (byte 0 bit 7).
    constexpr bool hFlip() const { return (bytes[0] & 0x80) != 0; }
    // 32x32 sprite (byte 8 bit 2, in the react slot).
    constexpr bool is32x32() const { return (bytes[8] & 0x04) != 0; }
    // Master-object reference: which object this slave follows and how it is
    // offset (byte 1 + byte 2 bits 0-1).
    constexpr std::uint8_t masterId() const { return bytes[1] & 0x1F; }
    constexpr std::uint8_t masterOffset() const { return (bytes[1] >> 5) & 0x07; }
    constexpr NpcMasterOffsetDir masterDir() const {
        return static_cast<NpcMasterOffsetDir>(bytes[2] & 0x01);
    }
    constexpr bool isSlave() const { return (bytes[2] & 0x02) != 0; }

    // --- builders ------------------------------------------------------------
    // Each builder names the properties a record overrides; the defaults match
    // the source's reset_npc_prop, so an omitted field packs its default byte.

    struct Pos {
        std::uint8_t x = 0;
        std::uint8_t y = 0;
    };
    struct Master {
        std::uint8_t id = 0;
        std::uint8_t offset = 0;                        // 0-7, stored << 5
        NpcMasterOffsetDir dir = NpcMasterOffsetDir::RIGHT;
        bool isSlave = false;  // set_npc_master marks a slave; 105 records clear
    };

    struct NormalFields {
        Pos pos{};
        std::uint16_t switchId = 0;
        EventScriptRef event = kEventReturnScript;
        MapSpriteGfx gfx = MapSpriteGfx::TERRA;
        MapSpritePal pal = MapSpritePal::EDGAR_SABIN_CELES;
        ObjectSpeed speed = ObjectSpeed::NORMAL;
        NpcMovement movement = NpcMovement::NONE;
        NpcSpritePriority spritePriority = NpcSpritePriority::NORMAL;
        NpcLayerPriority layerPriority = NpcLayerPriority::DEFAULT;
        NpcScroll scroll = NpcScroll::BG1;
        EventDir dir = EventDir::DOWN;
        NpcReact react = NpcReact::FACE_PLAYER;
        EventVehicle vehicle = EventVehicle::NONE;
        bool showRider = false;
    };
    struct AnimatedFields {
        Pos pos{};
        std::uint16_t switchId = 0;
        EventScriptRef event = kEventReturnScript;
        MapSpriteGfx gfx = MapSpriteGfx::TERRA;
        MapSpritePal pal = MapSpritePal::EDGAR_SABIN_CELES;
        ObjectSpeed speed = ObjectSpeed::NORMAL;
        NpcMovement movement = NpcMovement::NONE;
        NpcSpritePriority spritePriority = NpcSpritePriority::NORMAL;
        NpcLayerPriority layerPriority = NpcLayerPriority::DEFAULT;
        NpcScroll scroll = NpcScroll::BG1;
        NpcReact react = NpcReact::FACE_PLAYER;
        NpcAnimType animType = NpcAnimType::ONE_FRAME;
        NpcAnimFrame animFrame = NpcAnimFrame::DEFAULT;
        NpcAnimSpeed animSpeed = NpcAnimSpeed::FASTEST;
    };
    struct SpecialFields {
        Pos pos{};
        std::uint16_t switchId = 0;
        MapSpriteGfx gfx = MapSpriteGfx::TERRA;
        MapSpritePal pal = MapSpritePal::EDGAR_SABIN_CELES;
        ObjectSpeed speed = ObjectSpeed::NORMAL;
        NpcMovement movement = NpcMovement::NONE;
        NpcSpritePriority spritePriority = NpcSpritePriority::NORMAL;
        NpcLayerPriority layerPriority = NpcLayerPriority::DEFAULT;
        NpcScroll scroll = NpcScroll::BG1;
        NpcAnimType animType = NpcAnimType::ONE_FRAME;
        NpcAnimFrame animFrame = NpcAnimFrame::DEFAULT;
        Pos vramPos{};
        bool hFlip = false;
        bool is32x32 = false;
        Master master{};
    };

    // Shared byte-2 common bits (palette, switch low, BG2 scroll) and byte 3.
    static constexpr std::uint16_t storedSwitch(std::uint16_t id) {
        return id >= kSwitchBias ? static_cast<std::uint16_t>(id - kSwitchBias)
                                 : 0;
    }

    static constexpr NpcProperties npc(const NormalFields& f) {
        const std::uint16_t sw = storedSwitch(f.switchId);
        NpcProperties r{};
        r.bytes[0] = f.event.bytes[0];
        r.bytes[1] = f.event.bytes[1];
        r.bytes[2] = static_cast<std::uint8_t>(
            (f.event.bytes[2] & 0x03) |
            (static_cast<std::uint8_t>(f.pal) << 2) | ((sw & 3) << 6) |
            static_cast<std::uint8_t>(f.scroll));
        r.bytes[3] = static_cast<std::uint8_t>(sw >> 2);
        r.bytes[4] = static_cast<std::uint8_t>(f.pos.x | (f.showRider ? 0x80 : 0));
        r.bytes[5] = static_cast<std::uint8_t>(
            f.pos.y | (static_cast<std::uint8_t>(f.speed) << 6));
        r.bytes[6] = static_cast<std::uint8_t>(f.gfx);
        r.bytes[7] = static_cast<std::uint8_t>(
            ((static_cast<std::uint8_t>(f.vehicle) << 1) & 0xC0) |
            static_cast<std::uint8_t>(f.spritePriority) |
            static_cast<std::uint8_t>(f.movement));
        r.bytes[8] = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(f.dir) |
            static_cast<std::uint8_t>(f.react) |
            static_cast<std::uint8_t>(f.layerPriority));
        return r;
    }

    static constexpr NpcProperties animated(const AnimatedFields& f) {
        const std::uint16_t sw = storedSwitch(f.switchId);
        NpcProperties r{};
        r.bytes[0] = f.event.bytes[0];
        r.bytes[1] = f.event.bytes[1];
        r.bytes[2] = static_cast<std::uint8_t>(
            (f.event.bytes[2] & 0x03) |
            (static_cast<std::uint8_t>(f.pal) << 2) | ((sw & 3) << 6) |
            static_cast<std::uint8_t>(f.scroll));
        r.bytes[3] = static_cast<std::uint8_t>(sw >> 2);
        r.bytes[4] = f.pos.x;
        r.bytes[5] = static_cast<std::uint8_t>(
            f.pos.y | (static_cast<std::uint8_t>(f.speed) << 6));
        r.bytes[6] = static_cast<std::uint8_t>(f.gfx);
        r.bytes[7] = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(f.animSpeed) |
            static_cast<std::uint8_t>(f.spritePriority) |
            static_cast<std::uint8_t>(f.movement));
        r.bytes[8] = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(f.animType) |
            static_cast<std::uint8_t>(f.react) |
            static_cast<std::uint8_t>(f.layerPriority) |
            static_cast<std::uint8_t>(f.animFrame));
        return r;
    }

    static constexpr NpcProperties special(const SpecialFields& f) {
        const std::uint16_t sw = storedSwitch(f.switchId);
        NpcProperties r{};
        r.bytes[0] = static_cast<std::uint8_t>(
            (f.vramPos.x | (f.vramPos.y << 4)) | (f.hFlip ? 0x80 : 0));
        r.bytes[1] = static_cast<std::uint8_t>(
            f.master.id | (f.master.offset << 5));
        r.bytes[2] = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(f.master.dir) |
            (f.master.isSlave ? 0x02 : 0) |
            (static_cast<std::uint8_t>(f.pal) << 2) | ((sw & 3) << 6) |
            static_cast<std::uint8_t>(f.scroll));
        r.bytes[3] = static_cast<std::uint8_t>(sw >> 2);
        r.bytes[4] = static_cast<std::uint8_t>(f.pos.x | 0x80);
        r.bytes[5] = static_cast<std::uint8_t>(
            f.pos.y | (static_cast<std::uint8_t>(f.speed) << 6));
        r.bytes[6] = static_cast<std::uint8_t>(f.gfx);
        r.bytes[7] = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(f.spritePriority) |
            static_cast<std::uint8_t>(f.movement));
        r.bytes[8] = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(f.animType) | (f.is32x32 ? 0x04 : 0) |
            static_cast<std::uint8_t>(f.layerPriority) |
            static_cast<std::uint8_t>(f.animFrame));
        return r;
    }
};

static_assert(sizeof(NpcProperties) == 9,
              "NpcProperties must be byte-identical to a 9-byte ROM record");
static_assert(alignof(NpcProperties) == 1,
              "NpcProperties must be alignment-1 to stay packed in the array");

// --- counts + accessors ------------------------------------------------------

inline constexpr std::size_t kNpcRecordCount = 2193;
inline constexpr std::size_t kNpcMapSlots = 416;

// The NPCs on a map (0-415). Maps with no NPCs (including world maps 0-2 and the
// empty slot 415) return an empty span. mapIndex must be in range.
std::span<const NpcProperties> npcsForMap(std::uint16_t mapIndex);

// The flat record array + the per-map offset table (416 map slots + 1 end entry).
std::span<const NpcProperties> npcRecords();
std::span<const MapTriggerOffsetEntry> npcOffsets();

}  // namespace ostinato

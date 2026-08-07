// The attack-side special-effect index: byte +9 of an attack-properties
// record, naming an entry in the battle engine's special-effect dispatch
// space. The attack setup copies the record into the spell-mode block and
// doubles this byte in place as a jump-table index
// (battle_main.asm:6857-6875); any value >= $80 is zeroed there before
// dispatch (battle_main.asm:6870-6872), which is how the $FF sentinel reads
// as no-effect. Two parallel 88-entry jump tables consume the index:
// AttackerEffectTbl (battle_main.asm:10938, dispatched by DoAttackerEffect,
// :10118-10125) and TargetEffectTbl (:10014, dispatched by DoTargetEffect,
// :9087-9093); unfilled slots point at bare-rts handlers.
//
// One dispatch space, four feeders: the attack record's byte indexes it
// directly; weapon special effects enter as $00-$0F from the item record's
// special-effect high nibble (battle_main.asm:6954-6957 —
// WeaponSpecialEffect in ostinato/item_effects.h); consumable item-use
// effects enter offset by $48 into $49-$4E (:7024-7028 — ItemUseEffect); and
// the battle command handlers inject pre-doubled immediates for $50-$57
// (possess :3852, gp rain :4022, steal :3363/:8038, control :4110, leap
// :4124, sketch :3304, debilitator :7153, air anchor :7160).
//
// The enumerators name exactly the values the EN attack corpus carries; each
// name comes from the value's handler header in the jump tables, cited per
// enumerator (attacker-side handlers marked "attacker", target-side
// "target"). The emitter hard-errors on any attack byte outside this set, so
// a corpus divergence (e.g. a future JP rip) surfaces at emit time instead
// of silently.
#pragma once

#include <cstdint>

namespace ostinato {

enum class AttackSpecialEffect : std::uint8_t {
    // Carried by Pummel; no handler on either table (dead at dispatch — a
    // zero index lands on the unfilled slot-0 bare rts).
    PUMMEL                  = 0x00,
    SCAN                    = 0x10,  // target battle_main.asm:9710
    // battle_main.asm:10279 (attacker). The handler header (:10277) says
    // "scan", but the body (:10279-10283) loads the attacker's max HP into
    // the golem-block pool cell, and the value's sole carrier is Golem (Scan
    // itself is $10, target-side) — the header is a disassembly annotation
    // mislabel.
    GOLEM                   = 0x11,
    METAMORPH               = 0x12,  // target :9383; carried by Ragnarok
    PALIDOR                 = 0x13,  // target :9210, attacker :10761 ("sonic
                                     // dive")
    MANTRA                  = 0x15,  // attacker :10835
    SPIRALER                = 0x16,  // attacker :10802
    TAPIR                   = 0x17,  // target :9780
    WARP                    = 0x18,  // attacker :10461 (handler shared with
                                     // the warp-stone item effect, $4D)
    EXPLODER                = 0x19,  // attacker :10397, target :9695
    BLOW_FISH               = 0x1A,  // attacker :10600
    PEARL_WIND              = 0x1B,  // attacker :10264
    REFLECT_LORE            = 0x1C,  // attacker :10903; the target slot
                                     // (:9760) is a bare rts
    PEARL_LORE              = 0x1D,  // attacker :10522 ("l? pearl")
    STEP_MINE               = 0x1E,  // attacker :10150
    DISCHORD                = 0x1F,  // target :9268
    PEP_UP                  = 0x20,  // target :9795
    RIPPLER                 = 0x21,  // target :9661
    STONE                   = 0x22,  // target :9195
    DISABLE_COUNTERATTACK   = 0x23,  // target :9750; carried by X-Zone,
                                     // Odin, Raiden, Cleave, Snare, Xfer
    // Carried by Crusader; no handler on either table (dead at dispatch).
    CRUSADER                = 0x24,
    MISSES_FLOATING_TARGETS = 0x25,  // target :9559; carried by Quake,
                                     // Terrato, Magnitude8, and the other
                                     // ground-strike attacks
    WALLCHANGE              = 0x26,  // target :9289
    ESCAPE                  = 0x27,  // attacker :10542, target :9255 (target
                                     // handler shared across $27/$38/$4B)
    MIND_BLAST              = 0x28,  // attacker :10645, target :9608
    N_CROSS                 = 0x29,  // attacker :10659
    FLARE_STAR              = 0x2A,  // attacker :10613
    R_POLARITY              = 0x2B,  // target :9278
    LAUNCHER                = 0x2C,  // attacker :10437
    LOVE_TOKEN              = 0x2D,  // target :9886
    SEIZE                   = 0x2E,  // target :9810
    TARGETTING              = 0x2F,  // target :9971
    SUPLEX                  = 0x30,  // attacker :10876, target :9725
    FORCEFIELD              = 0x31,  // attacker :10571
    QUADRA_SLAM_SLICE       = 0x32,  // attacker :10588; carried by Quadra
                                     // Slam and Quadra Slice
    BABABREATH              = 0x33,  // attacker :10477, target :9246
    CHARM                   = 0x34,  // target :9765
    DOOM                    = 0x35,  // target :9911
    EMPOWERER               = 0x36,  // attacker :10784
    OVERCAST                = 0x37,  // target :9863
    SNEEZE                  = 0x38,  // target :9255 (shared handler)
    ENGULF                  = 0x39,  // target :9236
    ZINGER                  = 0x3A,  // target :9873
    EVIL_TOOT               = 0x3B,  // target :9628
    // Carried by Retort; no handler on either table (dead at dispatch). The
    // live Retort mechanism is the $3E4C.0 state flag the command handler
    // sets (battle_main.asm:3910-3911), not this byte.
    RETORT                  = 0x3C,
    REVENGE                 = 0x3D,  // attacker :10748
    PHANTASM                = 0x3E,  // target :9941
    STUNNER                 = 0x3F,  // target :9952
    FALLEN_ONE              = 0x40,  // target :9983
    QUICK                   = 0x43,  // attacker :10920
    DISCARD                 = 0x44,  // attacker :10818, target :9837
    // Carried by Clear; no handler on either table (dead at dispatch).
    CLEAR                   = 0x45,
    // No special effect. The dispatch setup zeroes any value >= $80 before
    // indexing (battle_main.asm:6870-6872), and the effect-is-zero gate at
    // :5845-5846 reads a stored $00 and the zeroed $FF identically — the
    // table stores the raw byte either way, so the two stay distinct in the
    // data.
    NONE                    = 0xFF,
};

static_assert(static_cast<std::uint8_t>(AttackSpecialEffect::NONE) == 0xFF,
              "the no-effect sentinel must stay the ROM's $FF byte");

}  // namespace ostinato

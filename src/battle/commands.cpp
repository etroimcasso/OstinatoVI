#include "battle/commands.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "battle/actions.h"
#include "battle/battle_start.h"
#include "battle/item_attack.h"
#include "battle/party_setup.h"
#include "battle/targeting.h"
#include "data/attack_properties.h"
#include "data/battle_tables.h"
#include "data/monster_properties.h"
#include "data/monster_special_anim.h"
#include "game/battle_switches.h"
#include "ostinato/attack_flags.h"
#include "ostinato/character_id.h"
#include "ostinato/character_prop_id.h"
#include "ostinato/item_effects.h"
#include "ostinato/status_id.h"
#include "ostinato/target_flags.h"
#include "ostinato/weapon_flags.h"

namespace ostinato::battle {

namespace {

// With an Offering, Fight makes eight attacks rather than two.
constexpr int kFightAttacksAfterTheFirst = 1;
constexpr int kOfferingAttacksAfterTheFirst = 7;

// An Imp monster without its Imp critical hits this hard.
constexpr std::uint8_t kImpMonsterPower = 1;

constexpr int kStatMax = 255;

// A desperation attack comes one time in sixteen: a draw whose low four bits are clear.
constexpr std::uint8_t kDesperationBits = 0x0F;

// A Jump's damage: half again as much, or twice as much with a spear in either hand.
constexpr int kJumpMultiplier = 1;
constexpr int kSpearMultiplier = 2;

// A Throw doubles the thrown weapon's damage.
constexpr int kThrowMultiplier = 2;

// A Dragon Horn's jump comes down twice, three times on a draw below the first of these and four
// times on a draw below the second.
constexpr std::uint8_t kDragonHornThirdLanding = 0x40;
constexpr std::uint8_t kDragonHornFourthLanding = 0x10;

// The menu numbers the eight blitzes, Pummel first.
constexpr int kBlitzes = 8;

// Umaro's four attacks, in the order of the rows of weights that choose between them.
enum class UmaroAttack : std::uint8_t {
    Fight,
    Tackle,
    Storm,
    Throw,
};

std::uint8_t raw(AttackId id) {
    return static_cast<std::uint8_t>(id);
}

ItemId asItem(AttackId attackOrItem) {
    return static_cast<ItemId>(raw(attackOrItem));
}

BattleCommandId asCommand(SpecialAnimation animation) {
    return static_cast<BattleCommandId>(animation);
}

bool wears(const Battler& battler, ItemId relic) {
    return std::find(battler.relics.begin(), battler.relics.end(), relic) != battler.relics.end();
}

// `set` with `slot` flipped: added if it is not in it, taken out if it is.
BattlerSet toggled(BattlerSet set, BattlerSlot slot) {
    if (set.has(slot)) {
        set.remove(slot);
    } else {
        set.add(slot);
    }
    return set;
}

// The run of a second command inside the first: the attack becomes `next`, made as the command it
// belongs to, by `maker`.
void runNestedCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                      BattlerSlot maker, AttackId next) {
    attack.attack = next;
    attack.command = commandForAttack(next);
    runCommand(state, hooks, script, attack, maker);
}

// --- spells ----------------------------------------------------------------------------------

// A spell or skill, named by `name`: its record loaded and aimed, the caster's magic power (or
// vigor, for a physical skill) and level behind it. Slot's spells cost no MP.
void castSpell(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
               BattlerSlot actor, AttackNameKind name) {
    attack.scratch.nameKind = name;
    initTargeting(attack, attack.command, attack.attack);
    initAttacker(state, attack, actor);
    if (attack.command == BattleCommandId::SLOT) {
        attack.record.mpCost = 0;
    }
    runAttack(state, hooks, script, attack, actor);
}

// Magic and X-Magic. Terra's spell, while the story asks for it, also starts the scene in which
// Locke and Edgar see her use magic — once.
void magicCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                  BattlerSlot actor) {
    if (actor.isCharacter() && state.at(actor).actor == CharacterPropId::TERRA &&
        state.switches.isSet(game::kTerraMagicSceneSwitch)) {
        state.switches.clear(game::kTerraMagicSceneSwitch);
        addImmediateAction(state, BattleEventAction{.event = kBattleEventTerraMagicScene});
    }
    castSpell(state, hooks, script, attack, actor, AttackNameKind::Attack);
}

// A desperation attack, or Umaro's Storm: cast as a spell under its own name.
void castAsSpell(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor, AttackId spell) {
    attack.attack = spell;
    attack.command = BattleCommandId::MAGIC;
    castSpell(state, hooks, script, attack, actor, AttackNameKind::Attack);
}

// Slot: Joker Doom under its own name, an esper summoned, Lagomorph with its message, any other
// outcome cast as Magic. The swirl plays.
void slotCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor) {
    attack.castingSwirlSuppressed = false;
    if (attack.attack == AttackId::L5_DOOM) {
        castSpell(state, hooks, script, attack, actor, AttackNameKind::JokerDoom);
        return;
    }
    if (attack.attack < AttackId::FIRE_SKEAN) {
        castSpell(state, hooks, script, attack, actor, AttackNameKind::Esper);
        return;
    }
    if (attack.attack == AttackId::LAGOMORPH) {
        attack.scratch.message = kMessageMuguMugu;
    }
    magicCommand(state, hooks, script, attack, actor);
}

// --- Fight -----------------------------------------------------------------------------------

// A party member's desperation attack, if this Fight turns into one: late enough in the battle,
// Near Fatal, neither seeing double nor confused, neither a zombie nor vanished, aiming at a
// monster, on a one-in-sixteen draw, and only once a battle. Gau, Umaro and the guests have none.
std::optional<AttackId> desperationAttackFor(BattleState& state, const AttackInFlight& attack, BattlerSlot actor) {
    const Battler& battler = state.at(actor);
    if (state.turnCounter < kTimeUpdatesBeforeDesperation || !battler.status.has(StatusId::NEAR_FATAL) ||
        battler.status.has(StatusId::IMAGE) || battler.status.has(StatusId::CONFUSE) ||
        battler.status.has(StatusId::ZOMBIE) || battler.status.has(StatusId::INVISIBLE) ||
        attack.targets.monstersOnly().empty()) {
        return std::nullopt;
    }
    if ((state.rng.next() & kDesperationBits) != 0) {
        return std::nullopt;
    }
    if (state.desperationUsed.has(actor)) {
        return std::nullopt;
    }
    // Marked as used even for a party member who turns out to have none.
    state.desperationUsed.add(actor);
    if (!battler.actor) {
        return std::nullopt;
    }
    const CharacterPropId actorId = *battler.actor;
    if (actorId != CharacterPropId::GOGO && actorId >= CharacterPropId::GAU) {
        return std::nullopt;
    }
    return desperationAttack(static_cast<CharacterId>(static_cast<std::uint8_t>(actorId)));
}

// Umaro's own attacks start from the plain hit with the power of both his hands, his level, and his
// vigor behind it.
void loadUmaroAttack(const BattleState& state, AttackInFlight& attack, BattlerSlot umaro) {
    const Battler& battler = state.at(umaro);
    plainCommandAttack(attack, attack.queuedCommand);
    attack.record.power = static_cast<std::uint8_t>(
        std::min(kStatMax, battler.weaponHand.battlePower + battler.shieldHand.battlePower));
    attack.attackerLevel = battler.level;
    attack.attackerStat = battler.vigor;
}

void umaroTackle(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot umaro) {
    loadUmaroAttack(state, attack, umaro);
    attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
    attack.canBeCritical = true;
    attack.command = asCommand(SpecialAnimation::UmaroTackle);
    runAttack(state, hooks, script, attack, umaro);
}

// Umaro throws an ally at the targets. The highest-placed ally asleep or confused is thrown if
// there is one; otherwise one of the others at random, never one in hiding. With no one to throw,
// he tackles.
void umaroThrow(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                BattlerSlot umaro) {
    loadUmaroAttack(state, attack, umaro);
    BattlerSet candidates = toggled(state.alive.charactersOnly(), umaro);
    for (int position = kPartySlots - 1; position >= 0; --position) {
        const BattlerSlot slot = BattlerSlot::character(position);
        if (!candidates.has(slot)) {
            continue;
        }
        const Battler& ally = state.at(slot);
        if (ally.status.has(StatusId::HIDE)) {
            candidates.remove(slot);
        } else if (ally.status.has(StatusId::SLEEP) || ally.status.has(StatusId::CONFUSE)) {
            candidates = BattlerSet::of(slot);
        }
    }
    const std::optional<BattlerSlot> thrown = state.rng.pickFrom(candidates);
    if (!thrown) {
        umaroTackle(state, hooks, script, attack, umaro);
        return;
    }
    Battler& ally = state.at(*thrown);
    const bool mog = ally.actor == CharacterPropId::MOG;
    if (mog) {
        attack.automaticCritical = true;
    }
    // The thrown ally's own power joins Umaro's, one more for Mog and any ally after him.
    const bool fromMogOn = ally.actor && *ally.actor >= CharacterPropId::MOG;
    const int allyPower =
        std::min(kStatMax, ally.weaponHand.battlePower + ally.shieldHand.battlePower + (fromMogOn ? 1 : 0));
    attack.record.power = static_cast<std::uint8_t>(std::min(kStatMax, allyPower + attack.record.power));
    attack.command = asCommand(SpecialAnimation::UmaroThrow);
    attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
    attack.canBeCritical = true;
    attack.rules.excludeAttacker = true;
    // Being thrown wakes the ally and brings it to its senses.
    if (ally.status.has(StatusId::SLEEP)) ally.statusesToClear.set(StatusId::SLEEP);
    if (ally.status.has(StatusId::CONFUSE)) ally.statusesToClear.set(StatusId::CONFUSE);
    runAttack(state, hooks, script, attack, umaro);
}

// Umaro picks one of his attacks by the rows of weights: a Fight or a tackle always, his Storm with
// a Blizzard Orb, and throwing an ally with a Rage Ring while another party member is alive.
void umaroAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot umaro) {
    const Battler& battler = state.at(umaro);
    std::array<bool, 4> offered{true, true, false, false};
    std::size_t row = 0;
    if (wears(battler, ItemId::RAGE_RING) && !toggled(state.alive.charactersOnly(), umaro).empty()) {
        offered[static_cast<std::size_t>(UmaroAttack::Throw)] = true;
        row += 1;
    }
    if (wears(battler, ItemId::BLIZZARD_ORB)) {
        offered[static_cast<std::size_t>(UmaroAttack::Storm)] = true;
        row += 2;
    }
    const std::optional<std::size_t> picked = pickWeighted(state.rng, kRandomBitRates[row].weights, offered);
    switch (static_cast<UmaroAttack>(picked.value_or(0))) {
    case UmaroAttack::Fight:
        fightAttack(state, hooks, script, attack, umaro);
        break;
    case UmaroAttack::Tackle:
        umaroTackle(state, hooks, script, attack, umaro);
        break;
    case UmaroAttack::Storm:
        attack.scratch.randomizeTargets = true;
        castAsSpell(state, hooks, script, attack, umaro, AttackId::STORM);
        break;
    case UmaroAttack::Throw:
        umaroThrow(state, hooks, script, attack, umaro);
        break;
    }
}

// Capture, and Fight past its desperation check: Umaro picks his own attack.
void captureCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                    BattlerSlot actor) {
    if (actor.isCharacter() && state.at(actor).actor == CharacterPropId::UMARO) {
        umaroAttack(state, hooks, script, attack, actor);
        return;
    }
    fightAttack(state, hooks, script, attack, actor);
}

void fightCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                  BattlerSlot actor) {
    if (actor.isCharacter()) {
        if (const std::optional<AttackId> desperation = desperationAttackFor(state, attack, actor)) {
            attack.castingSwirlSuppressed = false;
            castAsSpell(state, hooks, script, attack, actor, *desperation);
            return;
        }
    }
    captureCommand(state, hooks, script, attack, actor);
}

// --- items -----------------------------------------------------------------------------------

// Item, Throw and Tools. The item is named and aimed. One that casts a spell casts it — as Magic,
// for Item — free, unreflected and undodgeable, never at the fallen. Any other is the attack
// itself (itemAttack()); unless it raises the dead or turns its target to stone or a zombie, it
// reaches only the living and is not re-aimed when they are gone. An item used up leaves the
// user's hand; one that is not goes back into the inventory after the action.
void itemCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor) {
    const BattleCommandId command = attack.command;
    if (command == BattleCommandId::ITEM) {
        attack.scratch.damageModification = false;
        attack.vanishApplies = false;
    } else if (command == BattleCommandId::THROW) {
        attack.extraMultiplier = kThrowMultiplier;
        attack.ignoreDefenseStopsMultiplier = false;
    }
    attack.scratch.nameKind = AttackNameKind::Item;
    const ItemId item = asItem(attack.queuedAttack);
    const bool itemIsTheAttack = initTargeting(attack, command, attack.queuedAttack);
    Battler& battler = state.at(actor);
    if (!attack.scratch.itemKept) {
        battler.itemInHand.reset();
    }
    state.holdingItem.add(actor);

    if (itemIsTheAttack) {
        const ItemUse use = command == BattleCommandId::ITEM ? ItemUse::Consumed : ItemUse::ThrownOrWorked;
        itemAttack(state, attack, actor, item, use);
        if (!attack.record.statuses.has(StatusId::DEAD) && !attack.record.statuses.has(StatusId::PETRIFY) &&
            !attack.record.statuses.has(StatusId::ZOMBIE)) {
            attack.targets = attack.targets & (state.alive | state.aliveEnemyCharacters);
            attack.emptyTargetsReaim = false;
        }
        runAttack(state, hooks, script, attack, actor);
        return;
    }

    if (attack.scratch.lastSpell) {
        attack.attack = *attack.scratch.lastSpell;
    }
    if (command == BattleCommandId::ITEM) {
        attack.command = BattleCommandId::MAGIC;
    }
    attack.extraMultiplier = 0;
    initAttacker(state, attack, actor);
    attack.record.flags1.set(AttackFlag1::IGNORE_REFLECT);
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    attack.rules.mayTargetTheFallen = false;
    attack.record.mpCost = 0;
    runAttack(state, hooks, script, attack, actor);
}

// --- the commands that act on the user -------------------------------------------------------

// Morph and Revert: Morph is put on, or taken off, by an attack on the user. Revert without Morph
// only puts the party member back to standing.
void morphCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                  BattlerSlot actor, bool revert) {
    if (revert && !state.at(actor).status.has(StatusId::MORPH)) {
        script.add(PoseReset{.battler = actor});
        return;
    }
    plainCommandAttack(attack, attack.queuedCommand);
    attack.record.statuses.set(StatusId::MORPH);
    if (revert) {
        attack.record.flags2.set(AttackFlag2::REMOVE_STATUS);
    }
    runSelfAttack(state, hooks, script, attack, actor);
}

// Row, Runic and Def change something about the user and show it with an attack on the user.
void selfCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor) {
    Battler& battler = state.at(actor);
    switch (attack.command) {
    case BattleCommandId::ROW:
        battler.backRow = !battler.backRow;
        break;
    case BattleCommandId::RUNIC:
        battler.triggers.runic = true;
        break;
    case BattleCommandId::DEF:
        battler.defending = true;
        break;
    default:
        break;
    }
    plainCommandAttack(attack, attack.queuedCommand);
    runSelfAttack(state, hooks, script, attack, actor);
}

// A dancer dances: the dance it chose, or the last one chosen. A dance that matches the
// battlefield is danced; otherwise, on a coin, the battlefield changes to the dance's and it is
// danced, or the dancer stumbles — no more dancing, the "stumbled" message, and only its own
// stumble shown.
void danceCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                  BattlerSlot actor) {
    Battler& battler = state.at(actor);
    battler.status.set(StatusId::DANCE);
    attack.newBackground.reset();
    if (!battler.dance) {
        battler.dance = state.lastDance;
    }
    // Every Dance has a dance: the player's choice, or the one chosen for a dancer acting on its
    // own.
    if (!battler.dance) {
        return;
    }
    const DanceId dance = *battler.dance;
    if (backgroundDance(state.background) == dance) {
        magicCommand(state, hooks, script, attack, actor);
        return;
    }
    if (state.rng.coin()) {
        const BattleBackgroundId background = danceBackground(dance);
        attack.newBackground = background;
        state.background = background;
        magicCommand(state, hooks, script, attack, actor);
        return;
    }
    battler.status.clear(StatusId::DANCE);
    attack.scratch.message = kMessageStumbled;
    attack.command = asCommand(SpecialAnimation::DanceStumble);
    plainCommandAttack(attack, attack.command);
    runSelfAttack(state, hooks, script, attack, actor);
}

// --- the commands that act on a target -------------------------------------------------------

// Steal, Sketch, Control, Leap and GP Rain: the plain hit carrying the command's effect.
void effectCommand(AttackInFlight& attack, CommandEffect effect) {
    plainCommandAttack(attack, attack.queuedCommand);
    attack.effect = SpecialEffect::of(effect);
}

// A party member controlling a monster has it make the attack picked for it; otherwise Control
// tries to take hold of the target.
void controlCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                    BattlerSlot actor) {
    if (state.at(actor).status.has(StatusId::CHANT)) {
        script.add(PoseReset{.battler = actor});
        for (int index = 0; index < kBattlerSlots; ++index) {
            const BattlerSlot monster = BattlerSlot::fromIndex(index);
            if (state.at(monster).controlledBy == actor) {
                runNestedCommand(state, hooks, script, attack, monster, attack.attack);
                return;
            }
        }
        return;
    }
    effectCommand(attack, CommandEffect::Control);
    attack.record.traits.clear(AttackTrait::PHYSICAL);
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    runAttack(state, hooks, script, attack, actor);
}

// Sketch: the sketch is drawn, and if it caught its monster the sketcher makes the attack it caught,
// as the monster would, aimed at random and named again.
void sketchCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                   BattlerSlot actor) {
    effectCommand(attack, CommandEffect::Sketch);
    runAttack(state, hooks, script, attack, actor);
    const std::optional<SketchDrawn> sketch = std::exchange(attack.scratch.sketch, std::nullopt);
    if (!sketch) {
        return;
    }
    attack.scratch.sketcher = actor;
    Battler& sketcher = state.at(actor);
    const Battler& monster = state.at(sketch->monster);
    sketcher.specialAttackAnimation = monster.specialAttackAnimation;
    sketcher.specialAttack = monster.specialAttack;
    attack.scratch.randomizeTargets = true;
    attack.nameShown = false;
    runNestedCommand(state, hooks, script, attack, sketch->monster, sketch->attack);
}

// Rage: the party member rages as the monster it chose — or, choosing none, as the last party
// member to rage did — and makes the attack chosen for it.
void rageCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor) {
    Battler& battler = state.at(actor);
    if (!battler.rage) {
        battler.rage = state.at(state.lastRager.value_or(BattlerSlot::character(0))).rage;
    }
    state.lastRager = actor;
    battler.status.set(StatusId::RAGE);
    // A Rage always follows one that chose a monster: the player's choice, or a Rage this one mimics.
    if (!battler.rage) {
        return;
    }
    setRage(state, actor);
    applyDeathAndPoisonImmunity(battler);
    runNestedCommand(state, hooks, script, attack, actor, attack.attack);
    refreshStatusImmunity(state, actor);
}

// Bushido. Retort is readied by its first use, which shows only the stance and does no harm, and
// answered by its second, which swirls and is named. Every other technique is named and made.
void bushidoCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                    BattlerSlot actor) {
    initTargeting(attack, attack.command, attack.attack);
    initAttacker(state, attack, actor);
    if (attack.attack == AttackId::RETORT) {
        BattlerTriggers& triggers = state.at(actor).triggers;
        triggers.retort = !triggers.retort;
        if (triggers.retort) {
            attack.showsAttack = false;
            attack.record.power = 0;
            attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
            attack.record.traits.clear(AttackTrait::PHYSICAL);
            runAttack(state, hooks, script, attack, actor);
            return;
        }
        attack.castingSwirlSuppressed = false;
    }
    attack.scratch.nameKind = AttackNameKind::Bushido;
    runAttack(state, hooks, script, attack, actor);
}

// Blitz. Buttons that made no blitz hit no one: Pummel's record, no name, and the "incorrect
// blitz input" message.
void blitzCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                  BattlerSlot actor) {
    const int blitz = raw(attack.attack) - raw(AttackId::PUMMEL);
    if (blitz < 0 || blitz >= kBlitzes) {
        attack.hitsNoOne = true;
        attack.scratch.message = kMessageBlitzFailed;
        attack.attack = AttackId::PUMMEL;
    } else {
        attack.scratch.nameKind = AttackNameKind::Blitz;
    }
    initTargeting(attack, attack.command, attack.attack);
    initAttacker(state, attack, actor);
    runAttack(state, hooks, script, attack, actor);
}

// Jump: the jumper comes down with one hand's weapon — the one with power, or either on a coin —
// undodgeable, its row ignored, for half again the damage, or twice with a spear in either hand. A
// Dragon Horn brings it down two to four times at random targets. The jumper is no longer hidden.
void jumpCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor) {
    Battler& battler = state.at(actor);
    initTargeting(attack, attack.command, attack.attack);
    bool offHand = false;
    if (battler.shieldHand.battlePower != 0) {
        offHand = battler.weaponHand.battlePower == 0 || state.rng.coin();
    }
    loadWeaponAttack(state, attack, actor, offHand);
    attack.record.flags2 = AttackFlags2::of(AttackFlag2::UNDODGEABLE);
    attack.rowIgnored = true;
    attack.extraMultiplier = kJumpMultiplier;
    if (isSpear(battler.weaponHand.item) || isSpear(battler.shieldHand.item)) {
        attack.extraMultiplier = kSpearMultiplier;
    }
    if (battler.relicEffects2.has(RelicEffect2::CONTINUOUS_JUMP)) {
        attack.scratch.dragonHorn = true;
        const std::uint8_t draw = state.rng.next();
        ++attack.scratch.attacksLeft;
        if (draw < kDragonHornThirdLanding) {
            ++attack.scratch.attacksLeft;
        }
        if (draw < kDragonHornFourthLanding) {
            ++attack.scratch.attacksLeft;
        }
    }
    battler.status.clear(StatusId::HIDE);
    runAttack(state, hooks, script, attack, actor);
}

// The Roulette command: Lore's Roulette, aimed at no one, and the roulette cursor that picks its
// target.
void rouletteCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                     BattlerSlot actor) {
    attack.command = BattleCommandId::LORE;
    castSpell(state, hooks, script, attack, actor, AttackNameKind::Attack);
    script.add(AttackAnimation{.what = SpecialAnimation::RouletteCursor});
}

}  // namespace

StatusSet itemStatuses(const ItemProperties& properties) {
    return StatusSet{{properties.hitRateOrDefense, properties.elementsAbsorbed.bits, properties.elementsNullified.bits,
                      properties.elementsWeak.bits}};
}

std::uint8_t attackerLevel(const BattleState& state, const AttackInFlight& attack, BattlerSlot attacker) {
    return state.at(attack.scratch.sketcher.value_or(attacker)).level;
}

void initAttacker(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker) {
    const Battler& battler = state.at(attacker);
    attack.attackerStat = attack.record.traits.has(AttackTrait::PHYSICAL) ? battler.vigor : battler.magicPower;
    attack.scratch.weaponSpellcast = {};
    attack.attackerLevel = attackerLevel(state, attack, attacker);
}

void loadWeaponAttack(const BattleState& state, AttackInFlight& attack, BattlerSlot attacker, bool offHand) {
    const Battler& battler = state.at(attacker);
    attack.attackerStat = battler.vigor;
    attack.attackerLevel = attackerLevel(state, attack, attacker);
    if (battler.relicEffects3.has(RelicEffect3::HIT_100_IGNORE_MBLOCK)) {
        attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    }
    if (attack.attack == AttackId::SPECIAL && !battler.status.has(StatusId::IMP)) {
        attack.scratch.nameKind = AttackNameKind::MonsterSpecialAttack;
    }

    const Hand& hand = offHand ? battler.shieldHand : battler.weaponHand;
    attack.offHand = offHand;
    attack.record.power = hand.battlePower;
    attack.twoHanded = hand.weaponFlags.has(WeaponFlags::TWO_HAND);
    attack.rowIgnored = hand.weaponFlags.has(WeaponFlags::BACK_ROW);
    attack.automaticCritical = false;
    attack.record.element = hand.element;
    attack.record.hitRate = hand.hitRate;
    attack.scratch.weaponSpellcast = hand.spellCast;
    attack.effect = SpecialEffect::of(hand.blockEffect.weaponEffect());
    attack.secondary = static_cast<std::uint8_t>(static_cast<std::uint8_t>(hand.item) + 1);

    if (battler.relicEffects4.has(RelicEffect4::FIGHT_TO_X_FIGHT)) {
        attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
        attack.rules.randomTarget = true;
        attack.canBeCritical = false;
        attack.scratch.weaponSpellcast = {};
    }
    if (attack.record.power != 0 && attacker.isMonster() && battler.status.has(StatusId::IMP) &&
        !battler.impCritical) {
        attack.record.power = kImpMonsterPower;
    }
}

void plainCommandAttack(AttackInFlight& attack, BattleCommandId command) {
    initTargeting(attack, command, attack.attack);
    attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
    attack.effect = {};
    attack.attackerLevel = 0;
    attack.attackerStat = 0;
}

BattleCommandId commandForAttack(AttackId attack) {
    for (auto entry = kAiCommandsForAttack.rbegin(); entry != kAiCommandsForAttack.rend(); ++entry) {
        if (attack >= entry->attack) {
            return entry->command;
        }
    }
    return BattleCommandId::MAGIC;
}

bool isSpear(ItemId item) {
    return item >= ItemId::MITHRIL_PIKE && item <= ItemId::IMP_HALBERD;
}

void fightAttack(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                 BattlerSlot actor) {
    const bool offering = state.at(actor).relicEffects4.has(RelicEffect4::FIGHT_TO_X_FIGHT);
    attack.scratch.attacksLeft = offering ? kOfferingAttacksAfterTheFirst : kFightAttacksAfterTheFirst;
    attack.targets = removeAbsentTargets(state, attack.targets);
    rememberTargets(attack);
    attack.canBeCritical = true;
    attack.scratch.fightCommand = attack.command;
    runAttack(state, hooks, script, attack, actor);
}

void setRage(BattleState& state, BattlerSlot slot) {
    Battler& battler = state.at(slot);
    const MonsterId rage = *battler.rage;
    battler.specialAttackAnimation = monsterSpecialAnim(rage);
    const ItemId look = getMonsterProperties(rage).attackGraphic;
    battler.weaponHand.item = look;
    battler.shieldHand.item = look;
    battler.statusesToSet = loadMonsterTraits(battler, rage);
}

void runCommand(BattleState& state, AttackHooks& hooks, BattleScriptBuilder& script, AttackInFlight& attack,
                BattlerSlot actor) {
    switch (attack.command) {
    case BattleCommandId::FIGHT:
        fightCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::CAPTURE:
        captureCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::ITEM:
    case BattleCommandId::THROW:
    case BattleCommandId::TOOLS:
        itemCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::MAGIC:
    case BattleCommandId::X_MAGIC:
        magicCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::LORE:
    case BattleCommandId::MAGITEK:
        castSpell(state, hooks, script, attack, actor, AttackNameKind::Attack);
        break;
    case BattleCommandId::SUMMON:
        castSpell(state, hooks, script, attack, actor, AttackNameKind::Esper);
        break;
    case BattleCommandId::HEALTH:
        attack.attack = AttackId::CURE_2;
        castSpell(state, hooks, script, attack, actor, AttackNameKind::Command);
        break;
    case BattleCommandId::SHOCK:
        attack.attack = AttackId::SHOCK;
        castSpell(state, hooks, script, attack, actor, AttackNameKind::Command);
        break;
    case BattleCommandId::SLOT:
        slotCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::DANCE:
        danceCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::MORPH:
        morphCommand(state, hooks, script, attack, actor, false);
        break;
    case BattleCommandId::REVERT:
        morphCommand(state, hooks, script, attack, actor, true);
        break;
    case BattleCommandId::ROW:
    case BattleCommandId::RUNIC:
    case BattleCommandId::DEF:
        selfCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::STEAL:
        effectCommand(attack, CommandEffect::Steal);
        runAttack(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::SKETCH:
        sketchCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::CONTROL:
        controlCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::LEAP:
        effectCommand(attack, CommandEffect::Leap);
        attack.record.traits.clear(AttackTrait::PHYSICAL);
        attack.targeting = Targeting::of(TargetFlags::ENEMY);
        runAttack(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::GP_RAIN:
        effectCommand(attack, CommandEffect::GpRain);
        ++attack.record.power;
        attack.record.traits.set(AttackTrait::IGNORE_DEFENSE);
        attack.record.traits.set(AttackTrait::NO_DAMAGE_SPLIT);
        attack.scratch.damageModification = false;
        if (actor.isMonster()) {
            attack.scratch.nameKind = AttackNameKind::Command;
        }
        runAttack(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::POSSESS:
        initTargeting(attack, attack.command, attack.attack);
        attack.record.flags2.set(AttackFlag2::UNDODGEABLE);
        attack.effect = SpecialEffect::of(CommandEffect::Possess);
        runAttack(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::RAGE:
        rageCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::BUSHIDO:
        bushidoCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::BLITZ:
        blitzCommand(state, hooks, script, attack, actor);
        break;
    case BattleCommandId::JUMP:
        jumpCommand(state, hooks, script, attack, actor);
        break;
    case kRouletteCommand:
        rouletteCommand(state, hooks, script, attack, actor);
        break;
    default:
        break;
    }
}

}  // namespace ostinato::battle

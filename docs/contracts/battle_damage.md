# Battle damage — behavioral contract

Behavioral specification for the numbers an attack does: the base damage of a spell and of a blow,
the attacker's relic bonus on it, and the damage multiplier. All citations point into the pinned
reference tree `original-src/` (everything8215/ff6 @ `1ea47b5`); line numbers are into
`src/battle/battle_main.asm` unless another file is named. This document is the test authority for
`tests/test_damage_formulas.cpp` and `tests/test_damage_oracle.cpp`.

The attack frame (`docs/contracts/battle_commands.md`) calls these at three of its `AttackHooks`
points; this contract starts at those calls. The damage to one target, applying it and showing the
numerals, and the reflected attack are outside it for now.

Every claim here is also checked against the cartridge itself: the routines named below are run on
the cartridge, hosted on the engine's SNES machine, over the same inputs the port is given
(`tools/damage-trace/damage_oracle.h`, § The oracle).

## Sources

| Behavior | Source | Port surface |
|---|---|---|
| A spell's base damage | `CalcMagicDmg` `:7186-7210` | `magicDamage()` |
| A blow's base damage | `CalcDmg` `:7217-7285` | `physicalDamage()` |
| Which of the two | `CalcDmg` `:7224-7226` | `baseDamage()` |
| The relic bonus | `RelicDmgEffect` `:2060-2094` | `relicBonus()` |
| The multiplier | `ApplyDmgMult` `:8827-8849` | `applyMultiplier()`, `scaleDamage()` |
| The arithmetic | `MultAB` `:11801-11809`, `Mult24` `:11841-11858`, `LsrA` `:2150-2158` | expressions in `damage.cpp` |

## Where the frame calls them

`ExecAttack` works one attack out in this order (`:8092-8097`, `:8329`):

1. `CalcDmg`, then `RelicDmgEffect`, on the attack's record copy at `$11a0` — the power `$11a6`, the
   attacker's stat `$11ae` and level `$11af` — writing the damage word `$11b0`. The port:
   `baseDamage(state, attack, attacker)` sets `attack.damage`. The attacker is the battler the attack
   is worked out for: the sketcher, when a Sketch makes a monster's attack (`:8067-8069`).
2. The frame halves `$11b0` for a split and for a back-row attacker (`docs/contracts/battle_commands.md`,
   the attack frame, step 5).
3. `ApplyDmgMult` on `$11b0` with the steps in `$bc`. The port: `scaleDamage(attack)` multiplies
   `attack.damage` by `attack.multiplier`'s steps.

Between 2 and 3 the frame adds a step to `$bc` for each target hit in the back (`:8349-8351`). The
same routine runs again inside the damage to one target, which this contract does not yet cover.

**The widths.** `$11b0` is a 16-bit word and `$bc` a byte; every stat cell is a byte. The arithmetic
below is exact in whole numbers except where a rule names a 16-bit cut or a stop at 65,535. The port
writes the arithmetic in `int`; a cut appears in it only where this contract says one is reachable,
and a stop is the named constant `kDamageWordMax`.

## A spell — `CalcMagicDmg :7186-7210`

With *p* the power `$11a6`, *m* the attacker's magic stat `$11ae` (`$3b41`, the magic power times
one and a half as a byte, `InitAttacker :6842-6847`) and *l* the level `$11af`:

- base = *p* when *l* = 0, else 4·*p* (`cmp #$01; bcc` — the carry is "level ≥ 1", `:7190-7196`).
- damage = (base + ((*p* · *m* · *l*) >> 5)) **mod 65,536**: `MultAB` gives *p*·*m* (16 bits),
  `Mult24` by *l* gives the 24-bit product, `LsrA` with A = 4 shifts it right five times and hands
  back the low 16 bits, and the add is 16-bit (`:7198-7209`).
- It is a function of the three numbers alone, so the port's `magicDamage(power, magicStat, level)`
  takes no battle state; the field menu's spell use calls the same routine (`MenuMagicEffect
  :11769-11785`).

**The 16-bit cut is reachable.** At level 99 the sum passes 65,535 for every attack of power 84 or
more at a high enough stat: Ultima (150) at a magic stat of 140 — a magic power of 94 — comes to
65,568 and is kept as 32; at 139 it is 65,104. Goner (220), a monster's, wraps from a stat of 95 (3
damage). Atomic Ray (80) and everything weaker never reach it (`docs/Bugs.md`, "A strong enough
spell does almost nothing at level 99"). `magicDamage` keeps the low 16 bits.

| Case | Result | Test |
|---|---|---|
| (4, 0, 1) | 16 | `FourTimesThePowerAtLevelOneWithNoStat` |
| (50, 100, 0) | 50 | `ThePowerAloneAtLevelZero` |
| (10, 64, 10) | 240 | `TheProductIsDividedByThirtyTwo` |
| (255, 255, 255) | 60,435 — the shifted product's low 16 bits, 59,415, plus 1,020 | `TheDividedProductKeepsSixteenBits` |
| (150, 140, 99) | 32; (150, 139, 99) is 65,104 | `WrapsPastSixtyFiveThousand` |

## A blow — `CalcDmg :7224-7285`

With *p*, *v* the attacker's vigor stat `$11ae` (`$3b2c`, twice the vigor as a byte for a party
member; 56 to 63 for a monster), *l*, and the attacker's slot in X:

1. base = *p* for a party member, 4·*p* for a monster (`cpx #$08`, `:7234-7237`).
2. With the hand held in both hands (`$b3.6` clear — the port's `attack.twoHanded`), base ←
   (((base >> 1) + base) >> 1) + base: seven quarters of it in three integer steps (`:7238-7245`).
3. attack = base + *v*, 16-bit: the low byte's carry goes into the high byte (`adc $11ae; xba;
   adc #$00; xba`, `:7248-7251`), and the carry into the first add is clear on every path.
4. *t* = (attack · *l*) **mod 65,536**: `Mult24` leaves the 24-bit product in `$e8-$ea`, and
   `:7253-7259` reassembles its low two bytes and drops the third.
5. damage = (*t* · *l*) >> 8 (`Mult24`, 16 bits).
6. For a party member only (`:7262-7283`): damage ← ((2·*p* + damage) >> 1) + damage; then with
   `RelicEffect4::FIGHT_TO_X_FIGHT` (`$3c58.0`, an Offering) damage >>= 1; then with
   `RelicEffect4::EQUIP_2_WEAPONS` (`$3c58.4`, read as bit 3 after the word is shifted once:
   `lda $3c58,x; lsr; ...; bit #$0008`) damage ← damage − (damage >> 2). The bit stays set only while
   a Genji Glove is worn with one weapon (`docs/contracts/battle_core.md`).

**The 16-bit cut at step 4 is reachable.** For a monster: the Guardian at Vector, power 250 and level
71, has 4·250 + 56..63 = 1,056..1,063, and times 71 that is 74,976..75,473, kept as 9,440..9,937; the
blow comes to 2,618..2,756 where 20,795..20,933 was meant. For a party member: a weapon of power 255
in both hands (446) with a vigor stat of 255 is 701, and 701 × 94 = 65,894 passes 65,535; at level 99
the blow comes to 2,494 where 40,506 was meant (`docs/Bugs.md`, "A strong enough blow loses most
of its damage at a high level"). `physicalDamage` keeps the low 16 bits of the first product.

**No other cut is reachable.** Step 5's result is at most 65,535 × 255 / 256 = 65,279. Step 6 runs
for a party member, whose level is 1 to 99 (`kMaxLevel`, `src/data/level_up.h`), so its damage so far
is at most 65,535 × 99 / 256 = 25,343 and the sum at most (510 + 25,343) / 2 + 25,343 = 38,269. The
port writes steps 5 and 6 in `int`.

| Case | Result | Test |
|---|---|---|
| party member, p 100, v 50, l 10 | 58 → 187 | `APartyMembersBlow` |
| monster, p 100, v 50, l 10 | 175 | `AMonstersBlowCountsItsPowerFourTimes` |
| party member, p 200, v 100, l 10 | 375 (225 if the carry were lost) | `TheVigorCarriesIntoTheHighByte` |
| both hands, p 100, v 50, l 10 | 175 + 50 → 87 → 230 | `AWeaponInBothHandsCountsSevenQuartersOfItsPower` |
| Offering | 93 | `AnOfferingHalvesIt` |
| Genji Glove, one weapon | 141 | `AGenjiGloveWithOneWeaponTakesAQuarterOff` |
| both relic bits | 70 | `AnOfferingAndAGenjiGloveTogether` |
| a monster with those bits set | 175 | `AMonsterIgnoresThePartyMembersRelicBits` |
| both hands, p 255, v 255, l 99 / 98 | 2,494 / 2,070 | `TheFirstProductByTheLevelKeepsSixteenBits` |

## The relic bonus — `RelicDmgEffect :2060-2094`

Runs on `$11b0` after `CalcDmg`, for an attack that does not restore (`$11a4.0`,
`AttackFlag2::RESTORE_HP_MP`, clear; `:2062-2064`):

- The path: an attack on MP (`$11a3.7`, `AttackFlag1::AFFECT_MP`) takes the magic path whatever its
  trait (`:2065-2066`); otherwise a physical attack (`$11a2.0`) takes the physical path.
- Physical: `RelicEffect1::RAISE_FIGHT_DAMAGE` (`$3c44.0`) → damage += damage >> 2 (`:2075-2083`).
- Magic: `RelicEffect1::RAISE_MAGIC_DAMAGE` (`$3c44.1`, two items that raise magic damage) →
  damage += damage >> 1 (`:2072-2073`, `:2084-2088`); else `RelicEffect3::RAISE_MAGIC_DAMAGE`
  (`$3c45.1`, one of them) → damage += damage >> 2. The word at `$3c44` holds relic effects 1 in its
  low byte and relic effects 3 in its high byte (`InitCharBattle :6782-6793`); the magic path reads
  the high byte and tests its second bit (`xba; lsr; lsr`, `:2074-2076`).
- The sum stops at 65,535 (`bcc; clr_a; dec`, `:2088-2090`).

| Case | Result | Test |
|---|---|---|
| blow, raise fight damage, 400 / 401 | 500 / 501 | `ABlowGainsAQuarterFromRaiseFightDamage` |
| blow, both magic bits | 400 | `ABlowGainsNothingFromTheMagicBits` |
| spell, one item / two items / two items on 3 | 500 / 600 / 4 | `ASpellGainsHalfFromTwoItemsAndAQuarterFromOne` |
| spell, raise fight damage | 400 | `ASpellGainsNothingFromRaiseFightDamage` |
| blow on MP, raise fight damage / two magic items | 400 / 600 | `AnAttackOnMpTakesTheMagicPathWhateverItsTrait` |
| restores | 400 | `AnAttackThatRestoresGainsNothing` |
| 60,000 / 52,428 / 52,427 with raise fight damage | 65,535 / 65,535 / 65,533 | `StopsAtSixtyFiveThousandFiveHundredAndThirtyFive` |

## The multiplier — `ApplyDmgMult :8827-8849`

Called with the 16-bit accumulator holding the damage and `$bc` holding the steps:

- `$bc` = 0: the damage is returned unchanged and nothing is written (`:8830`).
- The multiplier is ignored when `$b3.4` is set and the attack ignores defense
  (`AttackTrait::IGNORE_DEFENSE`, `$11a2.5`): the 16-bit `lda $b2; asl; and $11a1; asl3` carries out
  bit 13, which is both together (`:8832-8837`). The damage is returned unchanged and **`$bc` is
  kept**. `$b3.4` is set when a command starts and a Throw clears it (`attack.ignoreDefenseStopsMultiplier`).
- Otherwise the damage becomes damage + steps · (damage >> 1), the half taken once from the damage
  as it came in, stopping at 65,535 (`:8838-8846`), and `$bc` is set to 0 (`:8847`).

The port: `applyMultiplier(damage, steps)` is the arithmetic; `scaleDamage(attack)` applies it to
`attack.damage` and zeroes `attack.multiplier` on the applied path only.

| Case | Result | Test |
|---|---|---|
| 100 with 0, 1, 2, 3 steps | 100, 150, 200, 250; the steps 0 after | `EachStepAddsHalfTheDamage` |
| 3 with 3 steps | 6 | `HalfIsRoundedDownOnce` |
| 40,000 with 1, 2, 3 | 60,000, 65,535, 65,535 | `StopsAtSixtyFiveThousandFiveHundredAndThirtyFive` |
| 0 steps | unchanged, no write | `NoStepsLeavesTheDamageAndWritesNothing` |
| 2 steps | 200, the steps used up | `ScaleDamageUsesTheStepsUp` |
| ignores defense, the stop set | 100, the steps kept | `AnAttackThatIgnoresDefenseIsNotMultipliedAndKeepsItsSteps` |
| ignores defense, a Throw | 200 | `AThrowMultipliesAnAttackThatIgnoresDefense` |

## The oracle

`tools/damage-trace/damage_oracle.h` runs the four routines on the cartridge. The cartridge is
hosted on the engine's SNES machine and started from a tick of the run loop as far as `Reset`
(`field/reset.asm`, `$C0:0019`), the first instruction after its `clc; xce`: the processor is in
native mode and nothing of the game has run. Each routine is bound at its address and called with the
registers the frame enters it with — X the attacker's slot times two, the status byte `$34` (both
widths narrow) or `$14` (the accumulator wide, `ApplyDmgMult`), the data bank `$7E` and the direct
page `$0000` — over cells written from a `DamageScenario`: the record at `$11a0`, the stat, level and
damage, the flags byte `$b3` with its two bits, the steps `$bc`, and each battler's relic words at
`$3c44` and `$3c58`. Watches on `$11b0` and `$bc` record the routine's writes.

Every case in the tables above asserts three ways — the port's value, the value worked out here, and
the cartridge's — and `tests/test_damage_formulas.cpp` sweeps 2,000 scenarios drawn from a fixed
seed through each routine, within the game's own ranges (a party member's level 1 to 99), asserting
the port against the cartridge. `ostinato-damage-trace --check` runs the same sweep from the command
line (`tools/damage-trace/README.md`).

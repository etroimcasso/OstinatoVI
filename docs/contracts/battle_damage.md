# Battle damage — behavioral contract

Behavioral specification for the numbers an attack does: the base damage of a spell and of a blow,
the attacker's relic bonus on it, the damage multiplier, the damage to each target the attack hits,
that damage taken from HP or MP and shown as numerals, and a spell bounced off Reflect. All
citations point into the pinned reference tree `original-src/` (everything8215/ff6 @ `1ea47b5`); line
numbers are into `src/battle/battle_main.asm` unless another file is named, and `btlgfx:` names a
line of `src/btlgfx/btlgfx_main.asm`. This document is the test authority for
`tests/test_damage_formulas.cpp`, `tests/test_target_damage.cpp`, `tests/test_apply_damage.cpp`,
`tests/test_reflected_attack.cpp` and `tests/test_damage_oracle.cpp`.

The attack frame (`docs/contracts/battle_commands.md`) calls these at its `AttackHooks` damage
points and, for a spell that bounced off Reflect, runs the bounce itself; this contract starts at
those calls. The battle's own hooks are `BattleHooks` (`src/battle/battle_hooks.h`), one call each
into `src/battle/damage.h`.

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
| The damage to one target | `CalcTargetDmg` `:1809-1882` | `targetDamage()` |
| Its parts | `CalcDmgMod` `:1961-2039`, `CalcDmgRatio` `:2100-2138`, `FixMPDmg` `:2166-2177`, `UltimaEffect` `:2248-2281`, `CalcMaxDmg` `:1891-1953`, `ZombieEffect` `:2220-2242`, `FixDrainDmg` `:2183-2214`, `_c2362f` `:8641-8652` | the steps of `targetDamage()` |
| Taking the damage | `ApplyDmg` `:2947-2969`, `ApplyDmgHP` `:2979-3000`, `ApplyDmgMP` `:3005-3033`, `_c21390` `:3041-3051`, `_c213a7` `:3060-3082` | `applyDamage()` |
| Showing it | `_c262ef` `:16193-16274`, `ShowDmgNumerals` `:16280-16295`, `ShowDmgNumeralsMulti` `:16301-16319` | `applyDamage()`, `DamageNumbersShown` |
| A spell bounced off Reflect, and a Launcher's or Super Ball's hits | `CalcReflectDmg` `:8377-8456`; the readers `GfxCmd_09` `btlgfx:25033-25243`, `GfxCmd_15` `btlgfx:25249-25367` | `resolveReflectedAttack()` (`src/battle/attack.h`), `ReflectedAttackShown`, `ScatteredHitsShown` |
| The arithmetic | `MultAB` `:11801-11809`, `Div` `:11818-11833`, `Mult24` `:11841-11858`, `MultDmg` `:2045-2054`, `CalcRatio` `:2144-2147`, `LsrA` `:2150-2158` | expressions in `damage.cpp` |

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

4. For each target hit that the special effect did not miss, highest slot first, `CalcTargetDmg`
   (`:8362`) with X the attacker and Y the target. The port: `targetDamage(state, attack, attacker,
   target, script)` adds to `attack.scratch.ledger`.
5. `_c262ef` (`:8365`), taking every battler's entries and showing them. The port:
   `applyDamage(state, attack, attacker, script)`.
6. Back in `ExecAttack`, for a spell (`$11a2.0` clear) with someone in `$a6`, `CalcReflectDmg`
   (`:8100-8105`). The port: `resolveReflectedAttack(state, hooks, script, attack, attacker)`, called by
   the frame itself.

Before each target in 4 the frame adds a step to `$bc` when that target is hit in the back
(`:8349-8351`); the same multiplier routine runs again inside the damage to that target, and uses
the steps up there (§ The damage to one target, step 1).

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

## The damage to one target — `CalcTargetDmg :1809-1882`

Entered with X the attacker's slot times two and Y the target's, the accumulator 16-bit and the
index registers 8-bit (`CalcAttackEffect` is `.a16 .i8`, `:8147-8148`). An attack of power 0
(`$11a6`) returns at once and writes nothing (`:1812-1813`): no ledger entry, no numeral.

### 1. The damage

An attack with `AttackFlag2::FRACTIONAL_DAMAGE` (`$11a4.7`) takes the fraction, any other the
modified damage (`:1814-1818`). Both leave the target's damage in `$f0`.

**The fraction — `CalcDmgRatio :2100-2138`.**

- *taken* is the ledger's taken entry for the target, 0 when it is empty. It is read from the HP side
  (`$33d0,y`) before `FixMPDmg` moves the index registers, so an attack on MP counts it too
  (`:2105-2109`).
- The pool is HP, or MP for an attack on MP (`$11a3.7`; `FixMPDmg :2166-2177` moves both index
  registers to the MP cells).
- The base is the pool's maximum for an Item (`$b5` = 1) or an attack with
  `AttackTrait::RESURRECTION_TARGET` (`$11a2.2`, the carry of `lsr3`), else the pool less *taken*,
  0 when that is negative (`:2112-2127`).
- damage = (base · power) >> 4 (`CalcRatio`), and 0 becomes 1 (`:2128-2132`).

The original keeps the result's low 16 bits, which a base times a power of 1,048,576 or more would
pass. All 17 attacks and all 8 items that do fractional damage have a power of 16 or less, and a
pool is at most 65,535, so the product is at most 1,048,560: the cut is never reached and the port
writes it in `int`. The sweep draws a fractional attack's power from 0 to 16 for the same reason;
with any power, the cut is the only place the port and the cartridge part.

**The modified damage — `CalcDmgMod :1961-2039`**, from `$11b0`. With damage modification off
(`$3414` = 0; the port's `attack.scratch.damageModification`) the damage is `$11b0` as it is and
nothing below runs, the multiplier included (`:1967-1968`). The cell is only ever 0 or `$FF`
(`stz` at each command that turns it off, `lda #$ff; sta $3414` at `:8128-8129`), so this test for zero and
the drain cap's test of its top bit (`:2188-2189`) agree.

1. The variance: one draw (`Rand`, `$be`), with its top three bits set — 224 to 255, though the
   comment at `:1970` says 240 — and `MultDmg` (`:2045-2054`): damage ← ((damage · factor) >> 8) + 1.
2. A *blow* is an attack that is physical (`$11a2.0`) and not on MP (`$11a3.7` clear): the carry
   `:1973-1977` leaves. An attack that ignores defense (`AttackTrait::IGNORE_DEFENSE`, `$11a2.5`)
   skips steps 2 to 4 entirely (`bne @0d22`, `:1978-1980`): defense, Safe and Shell, Defend, the
   back row and Morph all do nothing to it.
3. Defense: a blow is cut by the target's defense (`$3bb8`), anything else by its magic defense
   (`$3bb9`). A defense of 255 gives a factor of 0 and leaves 1 (`inc; beq`). Any other gives a
   factor of 255 − defense, except while a Golem or Interceptor blocks (`$3a82` and `$3a83` not both
   negative), when every target's defense counts as 192: a factor of 63 (`:1985-1996`). Then
   `MultDmg` with the factor.
4. Safe (`StatusId::PROTECT`, status 3 bit 6) against a blow, Shell (status 3 bit 5) against
   anything else: `MultDmg` with 170 (`:1998-2007`). A blow is halved for a target that is
   Defending (`$3aa1.1`) and again for one in the back row (`$3aa1.5`); anything else is halved for a
   Morphed target (status 4 bit 3) (`:2008-2024`).
5. Unless the attack restores (`$11a4.0`), a party member's attack on a party member is halved
   (`:2025-2033`).
6. The multiplier on `$f0` (`:2034-2036`), as § The multiplier: the steps the frame added since
   `scaleDamage` are applied and used up, or kept when the multiplier is ignored.

The widths: `MultDmg` comes to at most 65,535 · 255 / 256 + 1 = 65,281, the halvings only lower
it, and the multiplier stops at 65,535; nothing in the modified damage wraps.

### 2. Who gains — `:1819-1843`

*heal* starts as the attack's `AttackFlag2::RESTORE_HP_MP` (`$11a4.0`):

- A petrified target (status 1 bit 6) takes 0 with *heal* clear, whatever the attack, and the
  elements are skipped (`:1820-1822`, `@0bfa`); the Atma Weapon's step still runs.
- With `AttackTrait::INVERT_ON_UNDEAD` (`$11a2.3`): an undead target (`$3c95.7`) hit by an attack
  that puts Dead or Zombie on (`$11aa & $82`) gets nothing written at all, and nothing past this
  runs (`:1828-1832`); any other attack on an undead target has *heal* cleared, so a cure hurts it.
  For an undead target, or one in Zombie (status 1 bit 1, tested only when the target is not
  undead, `:1835-1837`), a drain (`$11a4.1`) turns *heal* over: the drain runs backward.

### 3. The elements — `:1844-1875`

Only for an attack with elements (`$11a1`), and only the first of these that applies:

1. Every element of the attack is one Forcefield has nullified (`$3ec8`; `not; and; beq`): 0.
2. The target absorbs one of them (`$3bcc`): *heal* turns over.
3. It nullifies one (`$3bcd`): 0.
4. It halves one (`$3be1`): halved.
5. It is weak to one (`$3be0`): doubled, unless the damage is 32,768 or more (`lda $f1; bmi`), so
   the doubling never passes 65,535. The cap of § 7 hides the gate except through the Atma
   Weapon's step.

### 4. The Atma Weapon — `UltimaEffect :2248-2281`

For an attack whose special effect is the Atma Weapon's: `$11a9` = 4, the doubled index 2, which
only `WeaponSpecialEffect::ATMA_WEAPON` has (the attacks' own effects start at `$10`).

- strength = ((HP_hi + 1) · level) / (maxHP_hi + 1), with HP_hi and maxHP_hi the high bytes of the
  attacker's HP and maximum HP (`$3bf5`, `$3c1d`) and level its own (`$3b18`).
- damage ← ((damage · strength) >> 6, low 16 bits) + 1, 16-bit (`Mult24`, `LsrA` with A = 5).
- From 501 the animation's second value `$b7` becomes 91, from 1001 92, and the attack's animation
  is written again where it went in the battle script queue (`_c235bb :8539`: `_c235d4` copies
  `$b4-$b7`, `_c235be` writes them at `$3a71`) — the beam's length. Below 501 nothing is rewritten.
  The port sets `attack.secondary` and calls `BattleScriptBuilder::replaceAnimation` with
  `animationOf(attack)`.

The widths: the strength is stored as a byte (`sta $e8` with the accumulator 8-bit), and HP_hi + 1
and maxHP_hi + 1 are 8-bit `inc`/`inx`. The Atma Weapon is a party member's, whose HP is at most its
maximum of at most 9,999, so both are at most 40 and the strength at most the level; the port writes
them in `int`. **The 16-bit cut is reachable**: at level 99 and full HP any damage of 42,367 or more
wraps, and 45,000 does 4,074 (`docs/Bugs.md`, "A hard enough Atma Weapon hit does almost
nothing"). The port keeps the low 16 bits.

### 5. What a blow does besides — `CalcMaxDmg :1891-1910`

For a physical attack (`$11a2.0`, the trait alone) that no Golem or Interceptor blocked
(`$3a82 & $3a83` negative):

- From an attacker in Zombie (status 1 bit 1), `ZombieEffect :2220-2242`: one draw; below 16 Poison
  is put in the target's statuses to set, else below 32 Blind (`$3dd4,y`).
- The target's Sleep and Muddle (status 2 bits 7 and 5), when it has them and the attack does not
  put them on itself (`$11ab`), go into its statuses to clear (`$3dfd,y`).
- The target's controller link ends (`$32b9,y |= $80`, the controller's slot kept): `Battler::controlEnding`
  is set. An ending link holds no one, and once the action is over `RemoveControlTarget
  :1151-1169` lets the controller go through that slot (`releaseEndedControl()`).

### 6. Drains — `CalcMaxDmg :1911-1924`, `FixDrainDmg :2183-2214`

For a drain (`$11a4.1`), on HP or on MP for an attack on MP (`FixMPDmg`):

- While damage modification is on: the damage stops at the pool the loser has — the target's, or
  the attacker's when *heal* is set — and, while the drain is capped, at what the gainer is missing
  (its maximum less its current). The cap's flag is the top bit of `$b2`: the 16-bit `lda $b1` at
  `:2201` tests bit 15. Seize clears it (`:13403`); it is the port's `attack.drainCapped`. HP and MP
  never exceed their maximum — every writer stops there — so what the gainer is missing is never
  negative, and the port writes it in `int`.
- `_c2362f` with X the target and Y the attacker: unless the attacker is already waiting to answer
  someone (`$32e0` bit 7), its `$32e0` becomes the target with the waiting bit taken from the carry.
  The carry is the attack's physical bit from the `lsr` at `:1893` — set for a blow (blocked or not),
  clear for a spell — except that a Zombie attacker's draw leaves it clear when the draw was below 32
  (`ZombieEffect`'s compares). The port: `Battler::lastAttacker` and `Battler::retaliationPending`.
  The loop's counter check answers a waiting battler (`CheckRetal :12534-12563`), so a monster whose
  special attack drains answers its own drain (`docs/Bugs.md`, "A monster that drains with its
  special attack answers its own drain").
- The attacker's ledger entry is written with the sense turned over (`sec` into `@0c76`): a drain
  that hurts the target heals the attacker by the same amount, and one that runs backward hurts it.

### 7. The ledger — `@0c76 :1929-1953`

*heal* writes the healed entry (`$33e4`), else the taken entry (`$33d0`). An empty entry (`$ffff`)
counts as 0; the damage is added to what is there, so a second hit on the same battler in one
attack adds to it; and the sum stops at 9,999, also when the 16-bit add carries. The port:
`DamageLedger`'s optional entries on `AttackScratch` and `kDamageCap`.

| Case | Result | Test |
|---|---|---|
| power 0 | nothing written | `PowerZero_WritesNothing` |
| modification off | 1,000 as it is | `NoModification_TakesTheDamageAsItIs` |
| variance 224 / 255 on 1,000 | 876 / 997 | `Variance_FromTwoHundredTwentyFourToTwoHundredFiftyFiveSixths` |
| a blow, defense 100 | 604 | `Defense_ABlowIsCutByDefense` |
| a spell / a blow on MP, magic defense 50 | 799 / 799 | `Defense_ASpellOrAnAttackOnMpIsCutByMagicDefense` |
| defense 255, blocked or not | 1 | `Defense_TwoHundredFiftyFiveLeavesOne` |
| a Golem or Interceptor block, defense 10 | 246 | `Defense_WhileAGolemOrInterceptorBlocksEveryTargetHasOneHundredNinetyTwo` |
| Safe on a blow / Shell on a blow / Shell on a spell / Safe on a spell | 661 / 994 / 661 / 994 | `SafeAndShell_SafeAgainstABlowShellAgainstASpell` |
| Defending / back row / both / a spell | 497 / 497 / 248 / 994 | `Stance_DefendingAndTheBackRowEachHalveABlow` |
| Morph on a spell / on a blow | 497 / 994 | `Stance_MorphHalvesASpell` |
| ignores defense, with every defense up | 997 | `IgnoreDefense_SkipsDefenseSafeShellStanceAndMorph` |
| party member on party member / restoring / a monster's | 498 / 997 healed / 997 | `FriendlyFire_APartyMembersAttackOnAPartyMemberIsHalved` |
| modification off, two steps | 1,000, the steps kept | `Modification_OffSkipsFriendlyFireAndKeepsTheSteps` |
| two steps | 1,988, the steps used up | `Multiplier_TheStepsAreAppliedAndUsedUp` |
| ignores defense, two steps | 997, the steps kept | `Multiplier_AnAttackThatIgnoresDefenseKeepsItsSteps` |
| two targets hit in the back | one step each | `BackHits_EachTargetUsesUpTheStepAddedForIt` |
| the same, modification off | one, then two | `BackHits_TheStepCarriesWhenTheDamageIsNotModified` |
| 8/16 of 1,000 left, 200 already taken | 600 | `Fractional_OfWhatTheTargetHasLeft` |
| 8/16 of the maximum 2,000, Item / revive | 1,200 | `Fractional_OfTheMaximumForAnItemOrARevive` |
| 8/16 of MP 100 / with 200 taken | 50 / 201 | `Fractional_OfMpForAnAttackOnMp` |
| 1/16 of 1 | 1 | `Fractional_ZeroDoesOne` |
| fractional with modification on and two steps | 600, no draw, the steps kept | `Fractional_IgnoresTheVarianceTheDefensesAndTheMultiplier` |
| a cure on a petrified target | 0 taken | `Petrified_TakesZeroEvenFromACure` |
| undead, an attack that kills or zombifies | nothing, no draw | `Undead_AnAttackThatKillsOrZombifiesDoesNothingAtAll` |
| undead, a cure with / without the trait | 1,000 taken / healed | `Undead_ACureHurtsIt` |
| undead, a drain | the target healed, the drainer hurt | `Undead_ADrainRunsBackward` |
| Zombie, a drain with / without the trait | backward / forward | `Zombie_ADrainOnAZombieRunsBackwardWithTheTrait` |
| Forcefield over every element / over one of two | 0 / 1,000 | `Forcefield_AnAttackWhoseEveryElementIsNullifiedDoesZero` |
| absorbed with nullified and weak / a cure absorbed | 1,000 healed / taken | `Elements_AbsorbingHealsAndComesFirst` |
| nullified / halved over weak / weak / one of two shared | 0 / 500 / 2,000 / 2,000 | `Elements_NullifyingHalvingAndWeakness` |
| Atma, level 50, 4,999 of 9,999 HP | 391, no rewrite | `Atma_ScaledByTheLevelAndTheHpLeft` |
| Atma at strength 64: 499 / 500 / 999 / 1,000 | 500 / 501 with 91 / 1,000 with 91 / 1,001 with 92 | `Atma_TheBeamLengthensFromFiveHundredOneAndAThousandOne` |
| Atma on a petrified target | 1 | `Atma_APetrifiedTargetTakesOne` |
| Atma at level 99, 45,000 | 4,074 with 92 | `Atma_TheDamageWrapsPastSixtyFiveThousand` |
| Atma at strength 1, weak: 32,767 / 32,768 | 1,024 / 513 | `Atma_WeaknessDoublesOnlyBelowThirtyTwoThousandSevenHundredSixtyEight` |
| a Zombie's blow, draws 4 / 31 / 182 | Poison / Blind / nothing to set | `Zombie_AZombiesBlowPoisonsOrBlindsOneTimeInSixteenEach` |
| a blocked blow | no draw, no wake, still controlled | `Blow_ABlockedBlowDoesNothingBesides` |
| a blow on a sleeping, muddled, controlled target / that puts Sleep on / a spell | both cleared and freed / Muddle cleared / nothing | `Blow_WakesClearsMuddleAndFreesFromControl` |
| a drain of 997 on 300 HP | 300 each way | `Drain_NoMoreThanTheTargetHas` |
| the drainer missing 200 / uncapped | 200 / 997 | `Drain_NoMoreThanTheDrainerIsMissingUnlessUncapped` |
| modification off | 1,000 | `Drain_NoCapWithoutDamageModification` |
| on MP, 50 MP | 50 | `Drain_OnMpCapsByMp` |
| backward: 100 missing / the drainer's 400 HP | 100 / 400 | `Drain_BackwardCapsByTheDrainerAndTheTargetsRoom` |
| the drainer's mark after a spell / a blow / already waiting | the target / the target, waiting / kept | `Drain_TheDrainerWaitsToAnswerAfterABlowNotASpell` |
| a Zombie drainer's blow, draws 31 / 182 | the target / the target, waiting | `Drain_AZombieDrainersBlowWaitsOnlyWhenItsDrawGaveNothing` |
| 5,000 + 1,000 / 9,500 + 1,000 / 9,999 + 60,000 / healed 9,500 + 1,000 | 6,000 / 9,999 / 9,999 / 9,999 | `Ledger_AddsToWhatIsThereAndStopsAtNineThousandNineHundredNinetyNine` |

## Taking the damage — `ApplyDmg :2947-2969`

Run by the showing routine (§ Showing it) for each battler with an entry, with X the battler who made
the attack and Y the battler, the accumulator 16-bit.

1. **The loss and the gain** (`_c213a7 :3060-3082`). The loss is the battler's taken entry, 0 when
   it is empty. A battler in the invincible set (`$3018,y & $3a3c`, `state.invincible`) has the entry
   itself set to 0, so it also shows 0. While a Golem or Interceptor blocks, the loss is 0 for every
   battler: `lda $3a81; and $3a82` is a 16-bit read whose high byte is the two block bytes ANDed, and
   `bmi` passes only while both are empty (`:3072-3075`). The gain is the healed entry, 0 when empty.
   The net is the gain less the loss; the routine's comment calls its carry "set = damage was taken",
   but the `sbc` leaves the carry **clear** when the loss is greater.
2. **The pool.** `ApplyDmg`'s `lda $11a2` runs with the accumulator wide, so its `bmi` tests bit 15:
   `$11a3.7`, `AttackFlag1::AFFECT_MP` (`:2952-2953`). An attack on MP changes MP, anything else HP.
3. **A net of 0** changes nothing and marks nothing (`beq _133b`).
4. **A gain** adds to the pool and stops at the maximum, also when the add carries past 65,535
   (`bcs`, `:2984-2990`, `:3009-3015`).
5. **A loss** comes off the pool. Anything left: the battler stays. Nothing or less (`beq` and the
   borrow, `:2998-3000`): `_c21390` sets HP to 0, cancels the weapon's spell (`$3a89`,
   `AttackScratch::weaponSpellcast`) and, unless the battler is a Zombie (status 1 bit 1), sets it to
   die (`$3dd4`, `Battler::statusesToSet`). On the MP path MP is set to 0 and only a battler that
   dies at 0 MP (`$3c95.0`, `Battler::diesAtZeroMp`) goes on to `_c21390`, whose HP then becomes 0
   too (`:3023-3030`).
6. **An MP change** either way, a gain or a loss, marks the battler's castable spells to be worked
   out again (`$3204.7`, `pendingUpdates.enabledMagic`, `:3032-3033`).
7. **The carry out** is "lost anything": set for every loss, the battler left standing or not (`bcs
   _133c` returns it set for a survivor, `:3031 sec` on the MP path), clear for a gain or a net of 0.
   The comment at `:2945` calls it "target died"; the code is the contract.
8. **On a loss** (`:2957-2966`): `_c2362f` with the carry set marks the battler as attacked by the
   attacker, waiting to answer it, unless it is already waiting to answer someone (`$32e0` bit 7;
   `Battler::lastAttacker`, `retaliationPending`). Then, unless the attacker is the battler itself:
   `$327c,y` ← the attacker (`Battler::lastDamagedBy`, which AI condition `$05` reads), and the
   battler's bit comes off `$3419` — an 8-bit `trb` of the low byte of `$3018,y`, which holds only the
   party members' bits, so a party member becomes one a Black Belt may counter for
   (`state.scratch.blackBeltEligible`) and a monster changes nothing.

The widths: the entries are at most 9,999, so the net is within ±9,999; the gain's carry is the stop
at the maximum and the loss's borrow is the stop at 0. The port writes the arithmetic in `int`.

## Showing it — `_c262ef :16193-16274`

For each battler, last slot first (`ldy #$12 … dey2`):

1. Both entries empty: on to step 4 (`:16201-16205`).
2. The battler leaves the missed set (`$3a5a`, `AttackScratch::missed`) and its damage is taken
   (§ Taking the damage). If it is the battler the Golem blocked for (`$3a82`, compared with Y) and
   its taken entry is not empty, the Golem's HP (`$3a36`, `state.golemHp`) loses the entry, stopping
   at 0 (`:16209-16219`) — the blocked hit is shown over the battler, taken from nobody, and taken
   from the Golem.
3. A healed entry gets the gain mark (bit 15, `ora #$8000`). With the taken entry empty, the gain
   moves into it and the healed entry empties, so the gain shows in the first pass; with both, both
   stay (`:16220-16235`).
4. A battler still in the missed set — one with no entries — gets the "Miss" mark (`$4000`) as its
   taken entry (`:16236-16240`).
5. A taken entry that is not empty is counted (`:16241-16244`).

Then the numerals: with fewer than five counted, one command `$0b` per battler with a taken entry,
last slot first, b1 the battler and b2-b3 the word (`ShowDmgNumerals`); with five or more, one command
`$03` and the whole ledger copied to the graphics buffer at `$2bce` + 20 · `$3a34`
(`ShowDmgNumeralsMulti`). `cpy #5` sets the carry for five or more; `ShowDmgNumerals` returns at once
with it set, and otherwise leaves it clear (the last `lsr` of an even slot index), so exactly one of
the two runs. If any battler both lost and gained, every healed entry is copied into the taken entry
and shown again the same way, counted afresh (`:16251-16264`). Last, every entry is emptied.

The port's `applyDamage()` shows each pass as one `DamageNumbersShown`, its numbers in the order the
commands go in, each `DamageNumber` the battler, the low 14 bits as the amount, the miss mark and the
gain mark; `wait` is `DamageNumberWait::UntilGone` from `kNumeralsShownTogether` (5) on and
`CarryOn` below it (the reader's two timings, `docs/contracts/battle_presentation.md`). A battler that
both lost and gained shows its loss in the first and its gain in the second. Nothing is shown when no
battler has a numeral.

| Case | Result | Test |
|---|---|---|
| a loss of 120 | 880 HP, the numeral, the ledger empty | `Loss_ComesOffHpAndTheLedgerEmpties` |
| a gain of 300 at 1,900 of 2,000 | 2,000, a gain numeral | `Gain_StopsAtTheMaximum` |
| a gain of 9,999 at 65,000 of 65,535 | 65,535 | `GainPastSixtyFiveThousand_StopsAtTheMaximum` |
| a loss of 50 on 50 HP | 0, set to die, the weapon's spell canceled | `LossToExactlyZero_SetsTheBattlerToDieAndCancelsTheWeaponSpell` |
| 9,999 on 30 HP | 0, set to die, 9,999 shown | `LossPastZero_StopsAtZero` |
| 50 on 51 HP | 1, the weapon's spell kept | `LossLeavingOneHp_KeepsTheWeaponSpell` |
| a Zombie brought to 0 | 0, not set to die, the spell canceled | `Zombie_AtZeroHpIsNotSetToDie` |
| lose 100 gain 30 / lose 30 gain 100 | 930 / 1,070; both shown, the gains second | `LossAndGain_TheDifferenceIsTakenAndBothAreShownTheGainSecond` |
| an attack on MP | MP changes, the spells marked, HP untouched | `AttackOnMp_ChangesMpAndMarksTheSpellsToBeWorkedOut` |
| MP to 0, dying at 0 MP or not | 0 MP and 0 HP, set to die / 0 MP, remembered | `MpToZero_KillsOnlyABattlerThatDiesAtZeroMpAndTakesItsHpToo` |
| invincible, 500 | untouched, 0 shown | `Invincible_LosesNothingAndShowsZero` |
| a Golem block, 300 on the blocked ally | nobody loses HP, a gain still counts, the Golem 1,000 → 700 | `GolemBlock_NobodyLosesAnythingAndTheGolemTakesTheBlockedHit` |
| the Golem at 100 | 0 | `GolemHp_StopsAtZero` |
| an Interceptor block | nobody loses HP, the Golem untouched | `InterceptorBlock_NobodyLosesAnythingAndTheGolemIsUntouched` |
| a loss on a battler not waiting / already waiting | marked waiting / kept; both remember the attacker | `Loss_MarksTheAttackerToBeAnsweredAndRemembersIt` |
| a party member loses, one gains, a monster loses | only the first may be countered for | `PartyMemberThatLosesAnything_MayBeCounteredForByABlackBelt` |
| the attacker's own loss | marked waiting on itself, not remembered | `LossByItsOwnAttack_IsMarkedButNotRemembered` |
| a gain / a loss of 0 | nobody marked; both shown | `GainOrZero_MarksNoOne` |
| missed with and without an entry | "Miss" only over the one without | `Miss_ShownOnlyOverABattlerWithNoEntry` |
| no entries, no misses | nothing shown | `NoEntryAndNoMiss_ShowsNothing` |
| four / five numerals | one at a time / all at once | `FourNumerals_OneAtATime_FiveAllAtOnce` |
| five losses, one of them also a gain | all at once, then the gain alone | `SecondPass_WaitsByItsOwnCount` |

## The reflected attack — `CalcReflectDmg :8377-8456`

Called after the attack for a spell (`$11a2.0` clear) with anyone in `$a6`
(`AttackAnimationParams::reflectedOff`). The set is the battlers the spell bounced off — or, for a
Launcher or Super Ball (attacker effects `$2c` and `$4a`), the attacker itself: `AttackerEffect_2c
:10439-10445` stores `$3018,y`, and `DoAttackerEffect :10123` has set Y to the attacker with `txy`.
Those effects put the hit count in `$3405` (8 for a Launcher, 1 to 4 for a Super Ball; the port's
`BattleState::extraHits` is the hits still to come, `$3405` + 1). The cell is the battle's, not the
command's: `InitBattle :6063-6067` sets it to `$ff` (none to come) as a battle starts, and the
per-command refill `ExecCmd :3103-3109` covers `$3410-$341f` only.

1. **Fresh** (`:8380-8390`): the params `$a0-$af` cleared (`ClearGfxParams`), the missed set
   cleared, the record's flags 1 gain `IGNORE_REFLECT` and `RETARGET_IF_INVALID` (`$11a3 |= $22`),
   the targeting byte becomes `$40` (one target, enemy first), the aiming rules gain random target
   and reflected (`$ba |= $50`), and the script's b2 is the attack (`$3a2a` ← `$b6`).
2. **The command** (`:8391-8398`): with hits to come (`$3405` not negative) the reflected rule comes
   off again and the command is `$15`; otherwise `$09`. The per-slot bytes `$a0-$a9` are set to `$ff`
   and the damage is **halved** (`lsr $11b0`).
3. **Each battler *r* in the set, last slot first** (`:8406-8445`): `ChooseTarget` with *r* as the
   attacker. No target: on to the next *r*. The target *t* is the highest slot of the result
   (`BitToTargetID :13438-13452`), which is always its only battler: random target re-aims and the
   single-target byte picks one. Its byte: `$a0` + *r* ← *t*, or with hits to come `$3405` is
   decremented and `$a0` + its old value ← *t*. Then `CheckHit` (*r*, *t*); on a hit, `_c235e3`
   records the hit **by *r*** — *t*'s last hit and its mark to answer name the battler the spell
   bounced off — then the status pass and the special effect, and unless the effect missed, *r*
   joins `$ae` and `CalcTargetDmg` (*r*, *t*) works out the damage, *r* its attacker. With hits to
   come the loop repeats on the same *r* while `$3405` is not negative.
4. **Without the reflected rule** (a Launcher's or Super Ball's hits, `:8448-8454`): `$ae` ← the
   attacker's bit and the party's block bytes `$aa-$ad` ← 0.
5. **Taken and shown**: `_c262ef` with X the attacker who made the attack — the caster — then the
   params copied to the script's buffer (`CopyGfxParamsToBuf`). A battler that loses HP to a bounced
   spell is therefore marked to answer the battler it bounced off (step 3) and remembers the caster
   as the one who damaged it (§ Taking the damage, step 8).

**The aim** (`ChooseTarget :14543-14603`, ported at `docs/contracts/battle_commands.md` § Aiming).
With the reflected rule the re-aim goes to the side opposite *r* and nothing else turns it
(`Retarget :14661-14663`); then, for a spell aimed at the other side only (`$ba.1`) and an *r* not
Confused, on tails it goes to the caster if the caster is among the targets (`CheckTargetsBattleType
:14719-14742`; the caster is the attacker of the params block before this one, `$2c5f` + `$3a32`).
A Launcher's hits are aimed as any random-target attack from the attacker.

**A target lost.** When `ChooseTarget` finds no one for a Launcher's or Super Ball's next hit, the
loop moves on with `$3405` left as it is, and nothing else in the battle program writes it: the count
stays for the rest of the battle (`BattleState::extraHits`). While it stands, every attack — any
battler's, any command's — stops after its attacker effect (`CalcAttackEffect :8276-8279`), before
the hit check: nothing it is aimed at is checked, shown or worked out, and it bounces off no one.
Another Launcher or Super Ball stores a new count and its own bit in `$a6`, so it comes here and
makes its hits. A bounce that comes here with a count standing takes the `$15` form whoever it
bounced off, each hit taking the count's byte until the count runs out and a later battler's hit its
own byte (step 3).

**What the script shows.** The command's 16-byte block, as its readers take it:

- `$09` (`GfxCmd_09`): bytes 0-9 by the slot of the battler the spell bounced off, the battler it
  went on to (`$ff` none; below 4 a party member). The bounces the reader draws are one per battler
  with a byte, from it to its target. Bytes 10-13 are the party's blocks (`btlgfx:25119-25127` reads a
  block of 3, the shield's, for a party member the spell bounced off). Bytes 14-15 are `$ae`, the
  bounces that hit. b2 is the attack whose animation plays (`InitAnimProp`).
  The port: `ReflectedAttackShown{attack, bouncedTo, bouncesThatHit, blocks}`.
- `$15` (`GfxCmd_15`): bytes 0-9 one per hit, the hit's target, the first hit at the highest byte;
  bytes 14-15 the attacker the hits fly from (`_c1aaad`, `btlgfx:25395-25405`). Bytes 10-13 are read
  for a party target's block, and this form has set them to 0. The port:
  `ScatteredHitsShown{attack, attacker, targets}`, the targets in hit order, missed hits included.
- b1 and b3 are left over from the last command written; neither reader reads them.

**The blocks.** A Golem or Interceptor steps in only for Fight or Capture (`CheckHit :5840-5867`),
and a bounce is always a spell, so neither blocks a bounce. (The block routine `_c223bf` stores the
battler's slot times two plus one in `$3a82`/`$3a83` when the attack is not physical, `:6024-6038`;
that path is never taken here.)

| Case | Result | Test |
|---|---|---|
| the hero's Fire bounced off a foe, only the ally standing | 500 to the ally, shown `4>1`, the foe's bounce hit | `Bounce_IsHalvedAndGoesToTheOtherSideFromTheBattlerItBouncedOff` |
| the same | the ally's last hit by the foe and waiting to answer it; damaged by the hero | `Bounce_TheBattlerItBouncedOffIsTheAttackerOfRecordTheCasterTheDamager` |
| bounced off both foes | `4>1 5>1`, 1,000 to the ally | `TwoBounces_LastSlotFirstAndTheirDamageAddsUp` |
| a hit rate of 0 | "Miss" over the ally, no bounce hit | `BounceThatMisses_ShowsMissAndHitsNoOne` |
| the same, the ally with a shield's block | the block shown for the ally | `BounceAPartyMemberBlocks_ShowsTheBlock` |
| no one on the other side | the command with no bounce, nothing shown | `NoOneToBounceTo_ShowsNoBounceAndDoesNothing` |
| aimed at the other side only, every random-number position | the hero or the ally, the hero more often, the port and the cartridge alike | `BounceAimedAtTheOtherSideOnly_GoesBackToTheCasterOnHalfTheDraws` |
| three hits from the hero, one foe | `4 4 4`, 1,500, no hits left | `ScatteredHits_EachOnARandomFoeTheirDamageAddingUp` |
| eight hits, no foe | nothing, the count kept by the battle | `ScatteredHitsWithNoOneToHit_KeepTheirCount` |
| two hits with a hit rate of 0 | `4 4`, no damage | `ScatteredHits_MissedOnesStillFly` |
| the hero's Fire bounced off a foe with two hits standing, only the ally standing | the `$15` form, `1 1`, 1,000 to the ally, no hits left | `HitsLeftStanding_MakeASpellsBounceScattered` |
| bounced off a foe, the ally away from the field, counterattacks off | no one reached, nothing shown | `ABattlerAwayFromTheField_IsNotReachedWhenCounterattacksAreOff` |
| the same after an attack aimed at its own attacker (`$341b` clear) | 500 to the ally, shown `4>1` | `ABattlerAwayFromTheField_IsReachedAfterASelfAimedAttack` |
| the special effect misses the target | no damage, no bounce hit | `ABounceTheSpecialEffectMisses_DoesNoDamageAndDoesNotCountAsAHit` |

## The oracle

`tools/damage-trace/damage_oracle.h` runs the seven routines on the cartridge. The cartridge is
hosted on the engine's SNES machine and started from a tick of the run loop as far as `Reset`
(`field/reset.asm`, `$C0:0019`), the first instruction after its `clc; xce`: the processor is in
native mode and nothing of the game has run. Each routine is bound at its address and called with the
registers the frame enters it with — X the attacker's slot times two (and Y the target's for
`CalcTargetDmg`), the status byte `$34` (both widths narrow) or `$14` (the accumulator wide and the
index registers narrow, `ApplyDmgMult` and `CalcTargetDmg`), the data bank `$7E` and the direct page
`$0000` — over cells written from a `DamageScenario`: the record at `$11a0`, the stat, level and
damage, the flags byte `$b3` with its two bits, the steps `$bc`, and each battler's relic words at
`$3c44` and `$3c58`. Watches on `$11b0` and `$bc` record the routine's writes.

For `CalcTargetDmg` the oracle also writes the special effect `$11a9` (its index doubled), the drain
cap's bit in `$b2`, the animation bytes `$b4-$b7`, the command `$b5`, the random-number position
`$be`, damage modification `$3414`, the blocks `$3a82`/`$3a83`, Forcefield `$3ec8`, an empty place
in the script queue (`$3a71`, `$3a72`, `$2d6e`), and every battler's level, defenses, HP and MP and
their maximums, elements, monster flags `$3c95`, stance `$3aa1`, statuses, statuses to set and to
clear, controller and last-attacker links and ledger entries. It reads back a `TargetDamageResult`
— the ledger, the statuses to set and to clear, the controller links, the last-attacker marks, the
steps left, `$b7`, whether the queue's place was rewritten and with what second value, and `$be` —
and `portTargetDamage()` builds the same from the port; `differences()` names every cell where they
part.

For the showing routine it also writes every battler's bit in `$3018`, the missed set `$3a5a`, the
invincible set `$3a3c`, the Golem's HP `$3a36`, the weapon's spellcast `$3a89`, the Black Belt cell
`$3419`, each battler's update flags `$3204` and last damager `$327c`, and an empty script queue and
buffer count; it reads back an `ApplyDamageResult` — every battler's HP, MP, statuses to set,
last-attacker mark, last damager and update flag, the Black Belt set, the missed set, the Golem's HP,
the spellcast, the ledger, and the numerals decoded from the queue (`$0b` commands in a run down the
slots, `$03` commands from the buffer). For `CalcReflectDmg` it adds the aiming cells (`$b8`, `$ba`,
`$3a46`, `$3a4e`, `$3a40`, the battle type `$201f`, the pincer sides `$2eac`/`$2ead`, each battler's
charm `$3395` and presence `$3aa0`), the reach check's cells (written so that what they leave between
them is the port's reachable set, with `$341b` from `AttackScratch::awayFromTheFieldUnreachable`), the hit check's
(`$3ec9`, `$341c`, `$3a54`, and each battler's stamina, evade and magic block, block graphics,
Retort and instant-death protection), the hit record's (`$3a7c`, `$3410`, `$3411`, each battler's
last hits), `$3405`, and the params block before the bounce's; it reads back a
`ReflectedAttackResult` — everything the showing routine leaves, each battler's statuses to clear and
last hitter, the bounce as the script shows it, the damage, whether the attack can still be critical,
the hits to come, the blocks and `$be`. The routine runs the cartridge's own status and special-effect
passes, which the port's hooks leave out, so its scenarios are spells those passes change nothing
for: no statuses or special effect, no Vanished or Frozen target, no miss for a status the target is
immune to.

**Replaying a game.** `DamageTrace` puts the same escapes and watches on any machine: the window of
`ostinato-damage-trace` records a game as it is played, and the oracle records its own calls. Each
traced call is rebuilt from the cells the routine read; its attacker is the params block's attacker
(`$a1`, written by `InitGfxParams :14428-14434` before the per-target pass and the showing routine),
its target the battler whose cells it touched other than the attacker. A bounce's calls are told by
their return sites (`$C2:3509`, `$C2:3528`): with the reflected rule set (`$ba.4`), the battler it
bounced off is the lowest slot of the params block's bytes that is set and its byte the target
(`:8414-8427`). Without it — hits were still to come as the bounce began — the hit is made from the
lowest battler in the bounces that hit (bytes 14-15, `$ae`, which takes that battler's bit just
before the call, `:8438-8439`, the loop running last slot first): for a Launcher's or Super Ball's
own hits, the attacker. Its target is in the byte one past the hits now to come; with none to come
it is in byte 0 (the count's last hit) or the battler's own byte (a later bounce), told apart by
whose cells the call touched, and with no cells touched but the battler's, its own byte. The
showing routine after a bounce is the attacker of the params block before this one (`$2c6e` +
`$3a32` − 15). A blow's base damage is a party member's when it
reads that member's relic word (`$3c58,x`), else a monster's, and a spell's does not depend on who
cast it. `tests/test_trace_replay.cpp` replays 200 of the oracle's own calls of each kind and every
recording under `tests/fixtures/damage-trace/`.

Every case in the tables above asserts three ways — the port's value, the value worked out here, and
the cartridge's — and the per-target cases compare every other cell of the two results as well.
`tests/test_damage_formulas.cpp` sweeps 2,000 scenarios drawn from a fixed seed through each of the
first four routines, within the game's own ranges (a party member's level 1 to 99), and
`tests/test_target_damage.cpp` sweeps 500 through `CalcTargetDmg` (a party member's HP and MP at
most 9,999 and 999 and never above the maximum, the Atma Weapon only for a party member, a
fractional attack's power at most 16), asserting the port against the cartridge.
`tests/test_apply_damage.cpp` sweeps 500 through the showing routine and
`tests/test_reflected_attack.cpp` 500 through `CalcReflectDmg` (a spell's own command, the blocks
equipment can show, counterattacks and `$341b` each drawn, and the hits to come drawn apart from the
battlers the spell bounced off).
`ostinato-damage-trace --check` runs the first four routines' sweep from the command line
(`tools/damage-trace/README.md`).

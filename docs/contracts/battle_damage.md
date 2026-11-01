# Battle damage — behavioral contract

Behavioral specification for the numbers an attack does: the base damage of a spell and of a blow,
the attacker's relic bonus on it, the damage multiplier, and the damage to each target the attack
hits. All citations point into the pinned reference tree `original-src/` (everything8215/ff6 @
`1ea47b5`); line numbers are into `src/battle/battle_main.asm` unless another file is named. This
document is the test authority for `tests/test_damage_formulas.cpp`, `tests/test_target_damage.cpp`
and `tests/test_damage_oracle.cpp`.

The attack frame (`docs/contracts/battle_commands.md`) calls these at four of its `AttackHooks`
points; this contract starts at those calls. Taking the damage and showing the numerals, and the
reflected attack, are not part of it.

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
- The target's controller link is broken (`$32b9,y |= $80`): `Battler::controlledBy` is cleared.

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

## The oracle

`tools/damage-trace/damage_oracle.h` runs the five routines on the cartridge. The cartridge is
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

Every case in the tables above asserts three ways — the port's value, the value worked out here, and
the cartridge's — and the per-target cases compare every other cell of the two results as well.
`tests/test_damage_formulas.cpp` sweeps 2,000 scenarios drawn from a fixed seed through each of the
first four routines, within the game's own ranges (a party member's level 1 to 99), and
`tests/test_target_damage.cpp` sweeps 500 through `CalcTargetDmg` (a party member's HP and MP at
most 9,999 and 999 and never above the maximum, the Atma Weapon only for a party member, a
fractional attack's power at most 16), asserting the port against the cartridge.
`ostinato-damage-trace --check` runs the first four routines' sweep from the command line
(`tools/damage-trace/README.md`).

# Trig tables

The lookup tables battle graphics use for angles: an arctangent grid that turns
a direction into an angle, and two sine tables that turn an angle back into a
distance.

## The surface

```cpp
#include "data/trig_tables.h"

std::uint8_t angle = ostinato::arcTangent(/*y=*/1, /*x=*/2);   // 18
std::int16_t s16   = ostinato::sine16(64);                     // 32767
std::int8_t  s8    = ostinato::sine8(192);                     // -127
```

Angles are in **256ths of a turn**: 0 points along the x axis, 64 is a quarter
turn, 128 a half. Because an angle fits in a `std::uint8_t`, adding past 255
wraps round the circle.

| Function | Returns |
|---|---|
| `arcTangent(y, x)` | The angle of the direction (x, y), 0..64. `x` and `y` are 0..31. |
| `sine16(angle)` | The sine of `angle`, scaled to ±32767. |
| `sine8(angle)` | The sine of `angle`, scaled to ±127. |

`arcTangentTable()`, `sine16Table()` and `sine8Table()` return the whole tables
for iteration. Arctangent entries carry `.y`, `.x` and `.angle` and run row by
row; sine entries carry `.angle` and `.value`.

## Using the arctangent grid

The grid covers one quadrant at a coarse resolution. To find the angle between
two points, take the distance along each axis as a positive number, scale each
from 0..255 down to 0..31 (divide by 8), look the pair up, then place the
result in the right quadrant from the signs of the two distances. A distance of
zero along x gives a quarter turn, including when both distances are zero.

## How exact the values are

The tables hold the game's own values, which follow a formula closely but not
exactly:

- **Arctangent** — `arctan(y / x) * 128 / π`, rounded to the nearest whole
  number or one below it. 323 of the 1,024 entries are the lower value.
- **16-bit sine** — `sin * 32767` rounded, with 13 entries one lower and 18 one
  higher.
- **8-bit sine** — `sin * 127` rounded, exactly. It is also exactly
  antisymmetric: `sine8(256 - a) == -sine8(a)`.

Code that needs to match the game's output must use these tables rather than
computing the functions.

## Changing a value

Edit the row in the matching file under `src/data/generated/`:

```cpp
{ .y =  1, .x =  2, .angle = 18 },   // arc_tangent_data.inc
{ .angle =  64, .value =  32767 },   // sine16_data.inc
```

Then update the same entry in `tests/fixtures/trig_tables_expected.h`, which
holds the raw bytes. `ostinato-vi-misc-tests` compares every entry against it,
and also checks each table against its formula with the counts above — a
changed value that moves an entry further from the formula, or changes how
many entries sit off it, is reported too.

## Gotchas

- **`arcTangent` takes `y` first.** The grid is stored row by row, so the
  vertical distance picks the row.
- **The arctangent covers one quadrant only.** It never returns more than a
  quarter turn; the caller works out the quadrant.
- **Don't substitute `std::sin` or `std::atan2`.** They agree most of the time,
  and the entries where they don't are the game's.
- **The two sine tables are not scaled copies of each other.** Their rounding
  differs; pick the one the effect you're writing uses.

## Where to change things

| Change | File |
|---|---|
| An arctangent entry | `src/data/generated/arc_tangent_data.inc` (+ the fixture) |
| A sine entry | `src/data/generated/sine16_data.inc` or `sine8_data.inc` (+ the fixture) |
| The entry types or accessors | `src/data/trig_tables.h`, `src/data/trig_tables.cpp` |

## See also

- `src/data/world_map.h` — the overworld carries its own separate sine table,
  measured in degrees rather than 256ths of a turn

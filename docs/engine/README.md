# Engine documentation

Guide for working with this port's C++ surfaces — looking things up, changing game
content, and building on top of the systems the port ships. Each area is documented
in its own directory; start with the index below.

These pages describe the surface as it exists: what a type holds, what an accessor
returns, where the backing data lives, and what to edit to change behavior. They are
written for someone modifying or extending the port, not for someone auditing it
against the original game — the reverse-derived behavioral specs live separately in
`docs/contracts/`.

## Sections

| Section | Covers |
|---|---|
| [data-layer/](data-layer/README.md) | The static game content the port ships with — the game-domain enums, character base stats, the RNG table, attack properties, magic-point awards, dances, espers, and natural magic. Compile-time `constexpr` data plus pure lookup functions. |

Further sections are added as the corresponding systems land.

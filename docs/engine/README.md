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
| [data-layer/](data-layer/README.md) | The static game content the port ships with — the game-domain enums, character base stats, the RNG table, attack properties, magic-point awards, dances, espers, natural magic, item properties, shops, colosseum wagers, monster properties and their satellite tables, battle formations, and the field-encounter tables. Compile-time `constexpr` data plus pure lookup functions. |
| [assets/](assets/README.md) | The content the port does not ship — the script, the graphics, and the tables that come out of a player's own cartridge. How a cartridge is recognized, kept, and read. |
| [text/](text/README.md) | The field dialog text engine — the state machine that types a message one glyph per frame, breaks its lines, waits on the player between pages, runs the multiple-choice cursor and shows the map title — the dialog window it types into, and the dialog font it is drawn in, read from the cartridge. The engine and the window are logic only; it records what is on screen for the rendering components to draw. |
| [battle/](battle/README.md) | The battle core — battle time (the ATB gauges, the advance waits, the status timers), the lines battlers wait in, the battle loop that takes turns and answers attacks, the player's commands, what happens after every action, and the battle's end — a battle's start from a formation and the party (the party, the monsters, the battle type, the starting gauges), what the game keeps when it ends, and `BattleSim`, a whole battle from a frame tick. Logic only: it records requests for the presentation and hands what each command does to an `ActionExecutor`. |
| [rendering/](rendering/README.md) | Putting the game on screen — components as functions that return engine sprites, tiles, regions and layers; `SpriteLayer`, `TileLayer` and the options they pass through; the depth table and key names; the dialog's text, choice arrow, page wipe and map title; the dialog box, its shading, its opening squash and its closing crop. |

Further sections are added as the corresponding systems land.

# Engine — Text

Guide for working with the text the game puts on screen in the field: how a dialogue message
is typed, paced and paged, how its choices are picked, and how the map title is shown. Read
this if you want to change how dialogue behaves, drive a message from an event, or draw one.

## What this area is

The dialogue engine is the logic between a decoded message and the picture. It takes the
token stream the text corpus produces (see [data-layer/text-corpus.md](../data-layer/text-corpus.md)),
and once per frame it advances by one glyph or one control code, keeping track of where
every glyph went, what the engine is waiting for, and where the choice cursor is. It holds
no drawing code and includes no rendering type. The rendering components
([rendering/components.md](../rendering/components.md)) read its state and turn it into
sprites, drawn from the dialogue font the player's cartridge holds.

The dialogue *window* — the box the text sits in, its opening and closing — is a separate
system and is not covered here.

## Index

| File | Covers |
|---|---|
| [dialogue.md](dialogue.md) | The engine's state and its per-frame tick; starting a message; the control codes and what each costs; the English word wrap; the name, item, spell and gold splices; multiple choice; the page wipe; the map title; what the rendering reads and how it is drawn; the constants in `layout.h`. |
| [font.md](font.md) | The dialogue font read from the cartridge — the width table, the glyph artwork and the choice arrow (`DialogueFont`, `FontMetrics`, `CursorArt`) — and the glyph and arrow sheets built from it. |

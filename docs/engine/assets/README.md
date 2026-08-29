# Engine — Assets

Guide for working with the content the port does not ship: the words, the pictures, and
the tables that live in a Final Fantasy VI cartridge. Read this if you want to know how
a player's cartridge gets in, where it is kept, or how to reach a part of it the game
does not read yet.

## What this area is

Nothing Final Fantasy VI says or draws is compiled into this program. A player supplies
a cartridge; it is copied into their own files once, and every launch reads what it
needs straight out of that copy. There is no intermediate step — nothing is decoded to
files on the way — so a developer, a CI runner, and a player all exercise the same two
pieces of code.

The static tables the port *does* ship — stats, formulas, encounter rates, everything
under `src/data/` — are a different thing entirely and are covered by
[data-layer/](../data-layer/README.md). The dividing line is what the content **is**:
mechanics and the numbers encoding them are compiled in; authored expression comes out
of the cartridge.

## Index

| File | Covers |
|---|---|
| [rom-ingestion.md](rom-ingestion.md) | The whole route — recognising a cartridge, keeping it, and reading families out of it — plus the region table that says where each family lives and the identity table that says which cartridges are accepted. |

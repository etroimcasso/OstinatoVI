# Game text — the runtime corpus and its codecs

Final Fantasy VI's text — character and item names, spell descriptions, battle
messages, and the field dialogue — is **copyrighted expression**, so unlike the
rest of the data layer it is never compiled into the binary. It ships as raw
byte `.dat` files that the game reads from disk at runtime and decodes itself.

This layer is two things:

- **`TextCorpus`** — loads the `.dat` files from a language directory and hands
  out one record at a time as a span of raw bytes, keyed by the game's own id
  spaces (`ItemId`, `MonsterId`, …) where a class maps cleanly onto one, or by
  decimal index otherwise.
- **The codecs** — turn a record's raw bytes into a token stream: runs of glyph
  indices interleaved with typed control tokens (newline, pause, name-splice,
  …). This is the *structural* decode. It identifies glyphs and commands; it
  does **not** map glyph indices to readable characters or apply any
  presentation. Turning glyph indices into on-screen text (the font, word
  wrapping, what a pause actually does) is a later presentation layer.

## Public surface

```cpp
#include "data/text_corpus.h"
#include "data/text_codec.h"

using namespace ostinato;

// Load every text-class .dat present under a language directory.
TextCorpus corpus = TextCorpus::loadFromDirectory("assets/text/en");

// Fixed-length name records — id-keyed where the class matches an id space.
std::span<const std::uint8_t> name = corpus.itemName(ItemId{5});
std::span<const std::uint8_t> mon  = corpus.monsterName(MonsterId{112});
std::span<const std::uint8_t> ch   = corpus.charName(0);   // decimal index

// Variable-length records — located by the class's offset table.
std::span<const std::uint8_t> msg  = corpus.attackMessage(AttackId{9});
std::span<const std::uint8_t> line = corpus.dialogue(1203);  // combined dlg1+dlg2
std::span<const std::uint8_t> desc = corpus.itemDescription(ItemId{5});

// Decode a record into tokens.
auto tokens = tokenizeDialogue(line, corpus.dte());   // field dialogue
auto btoks  = tokenizeBattle(msg);                    // battle text
auto glyphs = decodeMenuDescription(desc);            // plain glyph bytes
```

`has(TextClass)` reports whether a class loaded; `rawBytes(TextClass)` returns a
class's whole file (mainly for the variable-length classes and tests).

## Loading and the I/O seam

`loadFromDirectory` reads each class's `<stem>.dat` from the directory through a
**single filesystem function** (`readFile` in `text_corpus.cpp`); everything
above it works on `std::span<const std::uint8_t>` over buffers the corpus owns.
There is also a buffer constructor that takes the bytes directly, indexed by
`TextClass`, with no file I/O — so the corpus can later be fed by an engine
byte-asset provider by changing that one seam and nothing else.

A class whose file is absent is simply not loaded (`has()` is false). That is
normal: some classes exist in only one language, and the Japanese corpus is not
present in the current rip. A **fixed**-length class whose file size is not
`recordCount × recordSize` is a hard error (a corrupt corpus).

Files are named by the class stem with no language suffix — `char_name.dat`,
`item_desc.dat` — and the language is the directory (`assets/text/en`,
`assets/text/jp`). `tools/populate_text_assets.sh` copies the rip's
suffixed files (`char_name_en.dat`) into that layout for development.

## Text classes and their structure

Every class is one `TextClass` enumerator (`include/ostinato/text_class.h`), and
its layout comes from a small metadata table
(`textClassMetadata()`, `src/data/text_metadata.h`):

```cpp
struct TextClassMetadata {
    TextClass id;
    std::string_view fileStem;   // "<stem>.dat"
    TextClassKind kind;          // FIXED or POINTER
    std::uint16_t recordCount;
    std::uint8_t recordSize;     // fixed width; 0 for POINTER
};
```

- **FIXED** classes are padded records of a constant width (the name tables and
  the DTE table). Record `i` is the byte range `[i×recordSize, (i+1)×recordSize)`
  — no terminator, no escapes.
- **POINTER** classes are variable-length records located by an **offset table**
  (`src/data/text_offsets.h`): record `i` is `[offset[i], offset[i+1])`, and the
  last record runs to the end of the file.

### Dialogue offsets

Field dialogue is split into two files (`dlg1`, `dlg2`) in the rip, but the
accessor exposes them as **one** stream: `dialogue(index)` takes an index
`0…3083` over the concatenation (dlg1's 1574 records, then dlg2's 1510), and
`dialogueOffsets()` is a single array of offsets into that combined stream. The
original ROM's 16-bit-pointer-plus-bank-increment addressing does not appear in
the surface.

### Shared-pointer records

A few records share a pointer with the next record — the offset table has two
equal consecutive offsets, so the earlier record slices to **zero length**. This
is preserved verbatim (the ROM's pointer table really does alias them); the
game, following the pointer, reads the shared string for both. The dialogue
`_0`/`_1` pair at offset 0 is the canonical example.

## Codecs

Three tokenizers cover the corpus, selected by class family:

| Codec | Classes | Bytes |
|---|---|---|
| `tokenizeDialogue` | field dialogue, map titles | glyphs, controls `< 0x20`, DTE pairs `≥ 0x80` |
| `tokenizeBattle` | battle dialogue, attack messages, monster dialogue | glyphs, controls `< 0x20`, no DTE |
| `decodeMenuDescription` | item / magic / lore / … descriptions | plain glyph bytes to the `0x00` terminator |

A tokenizer returns a `std::vector` of tokens — each token is either a
`GlyphRun` (a run of glyph indices) or a control token. A control byte the
grammar does not define makes the tokenizer **throw** `std::runtime_error`.

```cpp
struct GlyphRun { std::vector<std::uint8_t> glyphs; };  // glyph indices, not chars
struct FieldControl  { FieldTextCommand  command; std::uint8_t operand; };
struct BattleControl { BattleTextCommand command; std::uint8_t operand; };

using FieldTextToken  = std::variant<GlyphRun, FieldControl>;
using BattleTextToken = std::variant<GlyphRun, BattleControl>;
```

Control commands are named enums (`include/ostinato/field_text_command.h`,
`battle_text_command.h`) whose values are the code bytes and whose names follow
the game's own escape table — `NEWLINE`, `WAIT_FRAMES`, `PAGE`, `TAB`, `CHOICE`,
`ITEM_NAME`, `ATTACK_NAME`, and so on. A command that carries an argument reports
it in the token's `operand`:

- **Field:** `WAIT_FRAMES`, `TAB`, and `KEY_TIMED` each read one operand byte;
  `CHARACTER_NAME` (codes `0x02…0x0f`) carries the member in the code itself
  (operand = code − `0x02`). Every other field command has no operand.
- **Battle:** `CHARACTER_NAME`, `WAIT_FRAMES`, `COMMAND_NAME`, `ITEM_NAME`,
  `ATTACK_NAME`, and `VAR_STRING` each read one operand byte.

The operand is a raw `std::uint8_t` — resolving it (which member, which item)
belongs to the presentation layer.

### DTE and MTE

Field dialogue is compressed with **DTE** (dual-tile encoding): a byte `≥ 0x80`
expands to a pair of glyph bytes via the `DteTable` loaded from `dte_tbl.dat`
(`corpus.dte()`), and the tokenizer expands DTE codes into the glyph run inline.
The Japanese path additionally expands **MTE** (multi-tile) codes through an
`MteTable`; the Japanese glyph ranges (direct, multi-tile, name-splice, wide
space) are handled by the same `tokenizeDialogue` when passed
`TextLanguage::JP`.

## Japanese

The Japanese metadata, offsets, tokenizer branches, and MTE decode are all
built, but the current rip is a U-ROM: it carries no Japanese text bytes, so the
Japanese corpus cannot be validated yet. The Japanese-corpus tests skip visibly
until a Japanese ROM is available; the code path is present, not stubbed.

## Backing data / where to change

The `.dat` files are the game content — you do not edit them here; they come
from a ROM extraction. What this layer owns and what a `tools/asm_parser` run
regenerates:

- `src/data/generated/text_metadata_data.inc` — the per-class structure (counts,
  sizes, kinds).
- `src/data/generated/text_offsets_data.inc` — the pointer-class offset arrays.
- Their fixtures under `tests/fixtures/` (`text_metadata_expected.h`,
  `text_offsets_expected.h`, `text_menu_desc_expected.h`).

`tools/asm_parser/parse_text_meta.py` emits all four from the rip. It reads the
record counts and offsets straight from the disassembly's include files (never
hand-typed) and hard-errors on any structural surprise. The named types — the
`TextClass` enumerators, the command enums, the codec grammar — are the
hand-written port surface.

To decode a record to on-screen text you also need a glyph-index-to-character
map; that is a presentation concern and is not part of this layer. A test-only
copy of that map, emitted from the disassembly's char tables, lives in
`tests/fixtures/text_menu_desc_expected.h` and is used only to cross-check the
decode.

## What's tested

- `tests/test_text_corpus.cpp` — the metadata table against its fixture; the
  loader (both the filesystem seam and the buffer path) on synthetic inputs; and
  every fixed-length name class round-tripped byte-for-byte against the raw rip,
  with the id-keyed and decimal-index accessors and DTE expansion checked
  against the raw pairs.
- `tests/test_text_offsets.cpp` — every offset array against the independent
  fixture, and every variable-length accessor returning exactly the
  fixture-defined slice of the raw `.dat` (dialogue over the combined stream).
- `tests/test_text_codec.cpp` — every dialogue, battle, and description record
  tokenized without error (a proof that the grammar covers the whole corpus);
  byte-exact round-trip of the DTE-free families; the DTE, operand, and
  unknown-byte rules pinned with synthetic records; and the description records
  decoded and cross-checked against the upstream reference text for every record
  whose glyphs are unambiguous.

The real-corpus tests locate the rip through the `FF6_TEXT_DIR` environment
variable (defaulting to the in-tree rip path) and skip visibly when it is
absent.

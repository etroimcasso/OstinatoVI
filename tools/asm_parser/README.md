# Data parsers

Python scripts that read the game's tables from the upstream disassembly and write them out as the
port's C++ data. Each `parse_<name>.py` writes:

- the generated row files under `src/data/generated/`;
- any enum headers under `include/ostinato/`;
- the matching test fixtures under `tests/fixtures/`.

They are run by hand when a table changes. The build doesn't run them, and neither the build nor the
game needs them.

Python 3, standard library only.

## What they read

- **The disassembly:** cloned at `original-src/`, at the commit given in the top-level
  [README](../../README.md). Part of its data is produced by its own `make rip` step, run against your
  cartridge.
- **A cartridge:** parsers that check their output against the assembled game need a US cartridge
  image. They find it through `FF6_VANILLA_ROM`, or a single `.smc` under `original-src/vanilla/`.

## Running one

Run from the repository root:

```sh
python3 tools/asm_parser/parse_char_prop.py --source-root original-src
python3 tools/asm_parser/parse_char_prop.py --source-root original-src --check-only
```

| Option | Effect |
|---|---|
| `--source-root <dir>` | where the disassembly is; each parser finds its own files under it |
| `--check-only` | read and check everything, write nothing |
| `--inc-out`, `--fixture-out`, … | where to write each output. The defaults are the files already in the repository |

Each parser's `--help` lists its own inputs and outputs. A parser that meets something it doesn't
expect stops with `PARSE ERROR:` and the file and line, and exits 2.

## Tests

Every parser has a `test_parse_<name>.py` beside it. From the repository root:

```sh
python3 -m unittest discover -s tools/asm_parser -p 'test_parse_*.py'          # all of them
python3 -m unittest discover -s tools/asm_parser -p 'test_parse_char_prop.py'  # one parser's
```

Run them through `discover -s tools/asm_parser` as shown: the tests import their parser and
`common.py` from that directory. The tests that read the real disassembly or the cartridge are
skipped when those aren't present.

`common.py` holds what the parsers share: the assembler-syntax reader, the symbol tables, the
cartridge reader and the output writers.

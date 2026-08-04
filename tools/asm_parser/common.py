#!/usr/bin/env python3
"""Shared ca65 parsing helpers for the Ostinato VI port-time asm parsers.

Python 3 standard library only (no third-party deps) so it runs unchanged on a
bare dev machine and on the CI runner. Targets Python 3.9+ (no match/case, no
runtime PEP-604 unions).

This module knows how to read the small, regular subset of ca65 grammar the
FF6 disassembly uses for constant/enumeration definitions:

  * file-scope symbol assignments      NAME = <single-term-expr>
  * enumeration blocks                 .enum NAME ... .endenum
  * enum members                       NAME            (auto-increment)
                                       NAME = <term>   (explicit / alias / bit)
  * line/inline comments (';')
  * conditional directives             .if / .ifdef / .ifndef / .else / .endif
  * macro definitions                  .macro ... .endmacro   (skipped wholesale)

It deliberately does NOT implement a general expression evaluator: every value
in an *emitted* enum is a single term (an integer literal or a symbol, possibly
scoped with '::'). Anything richer is either (a) inside a skipped enum, where
the body is never evaluated, or (b) a grammar the parser is not allowed to guess
at — it raises ParseError with a file:line citation so the executor escalates
rather than silently accepting a deviation. That hard-error posture is the
whole point: a parser that quietly accepts unexpected input produces wrong bytes
without anyone noticing.
"""

from __future__ import annotations

import re


class ParseError(Exception):
    """Raised on any deviation from the expected ca65 grammar.

    Always carries a source path + 1-based line number so a failure points at
    the exact contract line to inspect.
    """

    def __init__(self, path, lineno, message):
        self.path = path
        self.lineno = lineno
        self.message = message
        super().__init__("{}:{}: {}".format(path, lineno, message))


# --- lexical helpers -------------------------------------------------------

# A ca65 identifier. Enum member / symbol names are SCREAMING_SNAKE in this
# corpus but the pattern is the general ca65 identifier so the parser stays
# honest about what it accepts.
_IDENT = r"[A-Za-z_][A-Za-z0-9_]*"

_RE_IDENT = re.compile(r"^{}$".format(_IDENT))
# A scoped reference: ENUM::MEMBER (ca65 scope-resolution operator).
_RE_SCOPED = re.compile(r"^({})::({})$".format(_IDENT, _IDENT))


def strip_comment(line):
    """Split a raw source line into (code, comment).

    ca65 comments start with ';' and run to end of line. There are no string
    literals in this corpus that could contain a ';', so a plain split is safe;
    if that ever changes the caller's grammar asserts will trip loudly.
    Returns (code_without_trailing_ws, comment_text_without_semicolon_or_None).
    """
    idx = line.find(";")
    if idx < 0:
        return line.rstrip(), None
    return line[:idx].rstrip(), line[idx + 1:].strip()


def parse_int_literal(token):
    """Parse a single ca65 integer literal, or return None if not a literal.

      $hex   -> hexadecimal
      %bin   -> binary
      dddd   -> decimal
    """
    if not token:
        return None
    if token.startswith("$"):
        body = token[1:]
        if body and all(c in "0123456789abcdefABCDEF" for c in body):
            return int(body, 16)
        return None
    if token.startswith("%"):
        body = token[1:]
        if body and all(c in "01" for c in body):
            return int(body, 2)
        return None
    if token.isdigit():
        return int(token, 10)
    return None


def is_version_variant_condition(cond_text):
    """Classify an .if/.ifdef/.ifndef condition as version-variant or benign.

    Version-variant = the emitted value would differ between the supported ROMs
    (J 1.0 / US 1.0 / US 1.1). Those axes are the config symbols LANG_EN,
    ROM_VERSION, LANG_EN_REV1, and DEBUG. The plain include guard
    (.ifndef CONST_INC and similar *_INC guards) is benign.

    Emitted values must be version-independent; the walker uses this to
    hard-error if an .enum ever opens while such a condition is active.
    """
    tokens = re.findall(_IDENT, cond_text)
    version_axes = {"LANG_EN", "ROM_VERSION", "LANG_EN_REV1", "DEBUG"}
    return any(tok in version_axes for tok in tokens)


# --- parsed data model -----------------------------------------------------

class EnumMember(object):
    """One enumerator: its name, resolved integer value, and how it was written.

    rhs_kind records the source form so the emitter can preserve upstream aliases
    faithfully:
      'bare'       NAME                (auto-increment)
      'literal'    NAME = $NN / %.. / N (a literal)
      'same_alias' NAME = OTHER_MEMBER  (alias to an earlier member of this enum)
      'cross_ref'  NAME = SCOPE::MEMBER (value shared with another enum)
    rhs_symbol holds the referenced symbol text for 'same_alias' / 'cross_ref'.
    """

    def __init__(self, name, value, rhs_kind, rhs_symbol=None):
        self.name = name
        self.value = value
        self.rhs_kind = rhs_kind
        self.rhs_symbol = rhs_symbol


class EnumDef(object):
    """A parsed .enum block."""

    def __init__(self, name, src_line):
        self.name = name
        self.src_line = src_line
        self.skipped = False  # True if the body was recognized-and-skipped
        self.members = []  # list[EnumMember], in source order
        self._by_name = {}  # member name -> value, for same-enum alias resolution

    def add(self, member):
        self.members.append(member)
        self._by_name[member.name] = member.value

    def value_of(self, member_name):
        return self._by_name.get(member_name)

    @property
    def max_value(self):
        return max((m.value for m in self.members), default=0)


class ParsedConstants(object):
    """The whole-file result: file-scope symbols plus every enum, in order."""

    def __init__(self):
        self.globals = {}          # symbol name -> int (benign scope only)
        self.enums = []            # list[EnumDef], in source order
        self._enum_by_name = {}    # upstream enum name -> EnumDef

    def add_enum(self, enum):
        self.enums.append(enum)
        self._enum_by_name[enum.name] = enum

    def enum(self, name):
        return self._enum_by_name.get(name)

    def resolve_scoped(self, scope, member):
        """Resolve SCOPE::MEMBER against already-parsed enums. None if unknown."""
        target = self._enum_by_name.get(scope)
        if target is None:
            return None
        return target.value_of(member)


# --- the walker ------------------------------------------------------------

class _Conditional(object):
    __slots__ = ("version_variant",)

    def __init__(self, version_variant):
        self.version_variant = version_variant


def _resolve_term(token, path, lineno, parsed, current_enum):
    """Resolve a single value term to an int.

    Accepts: integer literal, a bare symbol (same-enum member, then file-scope
    global), or SCOPE::MEMBER. Raises ParseError on anything else or on an
    unresolved symbol — the executor escalates rather than guessing.
    Returns (value:int, rhs_kind:str, rhs_symbol:str|None).
    """
    lit = parse_int_literal(token)
    if lit is not None:
        return lit, "literal", None

    scoped = _RE_SCOPED.match(token)
    if scoped:
        scope, member = scoped.group(1), scoped.group(2)
        val = parsed.resolve_scoped(scope, member)
        if val is None:
            raise ParseError(path, lineno,
                             "unresolved scoped reference '{}'".format(token))
        return val, "cross_ref", token

    if _RE_IDENT.match(token):
        # same-enum alias takes precedence over a file-scope symbol of the same
        # spelling (matches ca65 scope lookup order).
        if current_enum is not None:
            same = current_enum.value_of(token)
            if same is not None:
                return same, "same_alias", token
        if token in parsed.globals:
            return parsed.globals[token], "literal", None
        raise ParseError(path, lineno, "unknown symbol '{}'".format(token))

    raise ParseError(path, lineno,
                     "unsupported value expression '{}' (single term expected)"
                     .format(token))


def parse_ca65_constants(path, skip_body_enums=None):
    """Parse a ca65 constants/enum file into a ParsedConstants.

    Structurally enforces that emitted enum values are version-independent: an
    .enum opening while a version-variant conditional (LANG_EN / ROM_VERSION /
    ...) is active is a hard error, because that would make an emitted enum
    value version-dependent.

    skip_body_enums: an optional set of enum names whose *bodies* are recognized
    and consumed without member-grammar evaluation (e.g. the combined-status
    views STATUS12/23/34/14, which use '<<' and '::' expressions the port does
    not emit). Their EnumDef is still registered — with .skipped = True and no
    members — so a caller-side coverage check can assert every enum in the file
    is accounted for.
    """
    skip_body_enums = set(skip_body_enums or ())
    with open(path, "r", encoding="utf-8") as fh:
        raw_lines = fh.readlines()

    parsed = ParsedConstants()
    cond_stack = []            # list[_Conditional]
    macro_depth = 0            # inside .macro ... .endmacro
    current_enum = None        # EnumDef while inside .enum ... .endenum
    current_enum_skip = False  # body of current_enum is being skipped
    enum_counter = 0           # ca65 auto-increment counter within current enum

    def version_variant_active():
        return any(c.version_variant for c in cond_stack)

    for idx, raw in enumerate(raw_lines):
        lineno = idx + 1
        code, comment = strip_comment(raw)
        if not code:
            continue
        stripped = code.strip()

        # --- macro bodies: skip wholesale ---
        low = stripped.lower()
        if low.startswith(".macro"):
            macro_depth += 1
            continue
        if low.startswith(".endmacro"):
            if macro_depth == 0:
                raise ParseError(path, lineno, ".endmacro without .macro")
            macro_depth -= 1
            continue
        if macro_depth > 0:
            continue

        # --- conditional directives ---
        if low.startswith(".if"):  # .if / .ifdef / .ifndef
            cond_text = stripped.split(None, 1)
            cond_arg = cond_text[1] if len(cond_text) > 1 else ""
            cond_stack.append(_Conditional(is_version_variant_condition(cond_arg)))
            continue
        if low.startswith(".else") or low.startswith(".elseif"):
            # An .else keeps the same version-variance classification.
            continue
        if low.startswith(".endif"):
            if not cond_stack:
                raise ParseError(path, lineno, ".endif without .if")
            cond_stack.pop()
            continue

        # --- enum blocks ---
        if low.startswith(".enum"):
            if current_enum is not None:
                raise ParseError(path, lineno, "nested .enum not supported")
            if version_variant_active():
                raise ParseError(
                    path, lineno,
                    "VERSION-VARIANT VIOLATION: .enum opened inside a "
                    "version-variant conditional — an emitted enum value would "
                    "be version-dependent; escalate, never guess.")
            parts = stripped.split(None, 1)
            if len(parts) != 2 or not _RE_IDENT.match(parts[1].strip()):
                raise ParseError(path, lineno, "malformed .enum directive")
            current_enum = EnumDef(parts[1].strip(), lineno)
            current_enum_skip = current_enum.name in skip_body_enums
            enum_counter = 0
            continue
        if low.startswith(".endenum"):
            if current_enum is None:
                raise ParseError(path, lineno, ".endenum without .enum")
            current_enum.skipped = current_enum_skip
            parsed.add_enum(current_enum)
            current_enum = None
            current_enum_skip = False
            continue

        # --- inside an enum: member lines ---
        if current_enum is not None:
            if current_enum_skip:
                continue  # recognized-and-skipped body; do not evaluate
            _parse_enum_member(stripped, comment, path, lineno,
                               parsed, current_enum, enum_counter)
            # enum_counter is advanced by the helper via return value
            enum_counter = current_enum._next_counter
            continue

        # --- file scope: symbol assignments and ignorable directives ---
        if low.startswith(".list") or low.startswith(".define") \
                or low.startswith(".export") or low.startswith(".import") \
                or low.startswith(".global") or low.startswith(".setcpu") \
                or low.startswith(".p816") or low.startswith(".a") \
                or low.startswith(".i") or low.startswith(".segment") \
                or low.startswith(".include"):
            continue
        if stripped.startswith("def_config"):
            # macro invocation defining a config axis; not needed for emission.
            continue

        # NAME = <single-term-expr>
        assign = _split_assignment(stripped)
        if assign is not None:
            name, rhs = assign
            if not _RE_IDENT.match(name):
                raise ParseError(path, lineno,
                                 "malformed symbol name '{}'".format(name))
            # Only record symbols defined in benign scope. Version-variant
            # globals (e.g. LANG_EN_REV1) are intentionally omitted; if an
            # emitted enum ever references one, resolution hard-errors — which
            # is the correct escalation.
            if not version_variant_active():
                value, _kind, _sym = _resolve_term(rhs, path, lineno,
                                                    parsed, None)
                parsed.globals[name] = value
            continue

        raise ParseError(path, lineno,
                         "unrecognized file-scope line: '{}'".format(stripped))

    if cond_stack:
        raise ParseError(path, len(raw_lines), "unterminated .if (missing .endif)")
    if macro_depth:
        raise ParseError(path, len(raw_lines),
                         "unterminated .macro (missing .endmacro)")
    if current_enum is not None:
        raise ParseError(path, len(raw_lines),
                         "unterminated .enum (missing .endenum)")
    return parsed


def _split_assignment(stripped):
    """Return (name, rhs) for a 'NAME = RHS' line, else None.

    Rejects ca65 scope-resolution '::' and comparison operators so it never
    mistakes a directive for an assignment.
    """
    # Must contain a single '=' that is not part of '==', '!=', '<=', '>='.
    if "=" not in stripped:
        return None
    # Reject relational operators outright (none appear at file scope, but be
    # explicit rather than guess).
    if re.search(r"[=!<>]=", stripped):
        return None
    name, rhs = stripped.split("=", 1)
    name = name.strip()
    rhs = rhs.strip()
    if not name or not rhs:
        return None
    if " " in name:  # 'def_config X, 0' etc. are not assignments
        return None
    return name, rhs


def _parse_enum_member(stripped, comment, path, lineno,
                       parsed, current_enum, counter):
    """Parse one enum member line and append it to current_enum.

    ca65 .enum counter semantics:
      * bare member        value = counter; counter += 1
      * member = <term>    value = eval(term); counter = value + 1

    If the trailing comment documents a value ('= N' or a bare number), the
    computed value is asserted equal to it — the upstream author's own comments
    become an extra structural check for free.
    Stashes the post-line counter on current_enum._next_counter.
    """
    assign = _split_assignment(stripped)
    if assign is None:
        # bare member: the whole line must be a lone identifier
        name = stripped
        if not _RE_IDENT.match(name):
            raise ParseError(path, lineno,
                             "malformed enum member '{}'".format(name))
        value = counter
        member = EnumMember(name, value, "bare")
        next_counter = value + 1
    else:
        name, rhs = assign
        if not _RE_IDENT.match(name):
            raise ParseError(path, lineno,
                             "malformed enum member name '{}'".format(name))
        value, rhs_kind, rhs_symbol = _resolve_term(
            rhs, path, lineno, parsed, current_enum)
        member = EnumMember(name, value, rhs_kind, rhs_symbol)
        next_counter = value + 1

    _assert_doc_value(comment, value, path, lineno, name)
    current_enum.add(member)
    current_enum._next_counter = next_counter


def _assert_doc_value(comment, value, path, lineno, name):
    """If the inline comment documents a value, assert it matches computed."""
    if not comment:
        return
    # Forms seen: '= $00  ; 0', '= 5', or a bare number after the code comment.
    m = re.match(r"^=\s*(\S+)", comment)
    if m:
        doc = parse_int_literal(m.group(1))
        if doc is not None and doc != value:
            raise ParseError(
                path, lineno,
                "member '{}' computed value {} disagrees with documented "
                "value {} (counter/grammar bug)".format(name, value, doc))

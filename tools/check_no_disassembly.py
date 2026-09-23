#!/usr/bin/env python3
"""Pre-commit guard: the repositories must never contain disassembly.

Usage:
    python3 tools/check_no_disassembly.py            # same as --staged
    python3 tools/check_no_disassembly.py --staged   # added lines of the index
    python3 tools/check_no_disassembly.py --all      # every tracked file
    python3 tools/check_no_disassembly.py PATH...    # whole given files
    python3 tools/check_no_disassembly.py --self-test
  Option (with --staged, --all or PATH...):
    --include-asm-bodies   also report the decomp's CodeWarrior asm function
                           bodies and inline asm under src/ (skipped by
                           default, see below)

Allowed: original ADDRESSES, descriptions in words, C-like expressions over
named fields/lanes ("x lane = t; w = x"), and a single mnemonic named in running
prose to explain a rule ("a vsqrt is used here").  Not allowed:
reproduced instruction streams, i.e. "mnemonic operands" lines, listing blocks,
pasted objdump/splat output and dumped hex.

Checks (each finding prints path:line: [check] reason):
  a  an (optional address +) mnemonic with two or more operands, at least one of
     them a register (GPR, FPR, VU vf/vi, ACC) or an offset(base) operand; a
     non-English mnemonic with any comma-separated operand list ("<mn> 1,
     +0x0A"), a bare FPR list ("<fpu-op> fN,fM,fK"), a mnemonic whose
     register destination is followed by an expression over registers
     ("<vu-mn> vfA = vfB + vfC", "<mn> vfA -> vfB, vfC", "= mem[<gpr>]"), and a
     markdown table row whose mnemonic cell is followed by an operand cell
  b  a comment whose text is mostly "mnemonic operands" (one operand is enough);
     a quoted or backticked span holding a mnemonic plus any operand ("`<mn>
     <reg>`", "'<mn> <imm>'", "`<mn>/<mn> <imm>`"); a (non-English) mnemonic
     followed by one register or offset(base) operand in prose, whatever
     follows it ("<mn> vfN = p * q" names the original's register); three or
     more mnemonic names written in order with '/' or ';' between them (two
     when one has a register operand; three joined by commas when one has a
     register operand)
  c  three or more consecutive instruction-looking lines (a listing block), or
     one inline-asm statement/block holding three or more instructions, or the
     marker on two or more instruction lines within 5 lines of each other
  d  hex blobs: 64+ contiguous hex digits, long runs of space-separated hex
     bytes / 32-bit words / 64- and 128-bit groups, and 8+ comma-separated
     0x-prefixed 32-bit words in comments/prose (the pinned boot-ELF SHA-256 and
     SHA-1 are allowed)
  e  lines over 400 characters that are mostly hex/numbers/commas
Integer instruction words in oracle code are not findings by themselves; a
mnemonic comment next to them is.  Upper- and mixed-case listings and Ghidra's
delay-slot prefix ("_" before the mnemonic) are folded before checking.  A
C-like expression after a mnemonic is prose only when the mnemonic has no
operand at all ("the <vu-mn> (ACC = a * b)").  Allowed: a single mnemonic NAME
in prose, a pair of names ("<mn>/<mn>"), an upper-case (ISA-manual) list of
op names without operands, and invented placeholders such as "<reg>"/"<sym>".  Hash-family scripts that decode instruction words
(interpreters/oracles: ">> 26", "opcode", def cop1/special/regimm) may label
handlers with bare opcode names ("# <mn> / <mn> / <mn>").

Escape hatch: the marker  no-disasm-ok  on a line suppresses (a) and (b) on
that line only.  It is for a single, isolated, deliberate instruction mention
in prose: a marked line holding two or more instructions is still reported,
marked lines still count toward (c) listing blocks, two or more marked
instruction lines within 5 lines of each other are reported as (c) even with
blank lines between them, the marker never hides an asm function body, and
it does not exempt hex dumps (d)/(e).

Decomp repo (the root holds tools/decomp/build.py): the CodeWarrior asm
function bodies under src/ (from an "asm <type> name(...) {" line to its
closing brace), inline asm inside C functions under src/ (__asm__(...)
statements and asm { } blocks), INCLUDE_ASM/INCLUDE_RODATA macro lines and
splat-generated .s/.inc files under src/ are compiled code the user decided to
keep; they are skipped unless --include-asm-bodies is given.  Comments outside
those bodies (NEARMISS headers, notes) are always scanned.

Exit status: 0 clean, 1 findings, 2 usage.  Dependency-free (standard library
only); the same file lives in both repos.
"""

import os
import re
import subprocess
import sys

ALLOW_MARKER = "no-disasm-ok"
PINNED_HEX = {
    # Target identity of SCUS-97112 (boot ELF), pinned in CLAUDE.md.
    "ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a",
    "2cca045edce7db2af2c865bd80b46f79661608f0",
}
C_FAMILY = {".c", ".h", ".m", ".mm", ".cpp", ".hpp", ".js", ".ts", ".inc",
            ".lds", ".lcf"}
HASH_FAMILY = {".py", ".sh", ".yaml", ".yml", ".toml"}
# Scanned as plain text: every line is judged as prose.
PROSE_FAMILY = {".md", ".txt", ".json", ".csv", ".s", ".S", ".cfg",
                ".html", ".htm", ".xml", ".ini"}
SCAN_EXTS = C_FAMILY | HASH_FAMILY | PROSE_FAMILY
# Decomp-only: splat output that may sit under src/ (asm includes).
SPLAT_INCLUDE_EXTS = {".s", ".S", ".inc"}

# --------------------------------------------------------------------------
# Instruction vocabulary
# --------------------------------------------------------------------------
_GPR = (r"zero|at|v[01]|a[0-3]|t[0-9]|s[0-8]|k[01]|gp|sp|fp|ra")
_MIPS_MN = r"""
    lb lbu lh lhu lw lwu lwl lwr ld ldl ldr lq lwc1 ldc1 lqc2 ll
    sb sh sw swl swr sd sdl sdr sq swc1 sdc1 sqc2 sc
    add addu addi addiu dadd daddu daddi daddiu sub subu dsub dsubu
    and andi or ori xor xori nor lui slt slti sltu sltiu
    sll srl sra sllv srlv srav dsll dsrl dsra dsll32 dsrl32 dsra32 dsllv dsrlv dsrav
    mult multu div divu mult1 multu1 div1 divu1 madd maddu madd1 maddu1
    mfhi mflo mthi mtlo mfhi1 mflo1 mthi1 mtlo1 mfsa mtsa mtsab mtsah
    beq bne beqz bnez blez bgtz bltz bgez bltzal bgezal
    beql bnel beqzl bnezl blezl bgtzl bltzl bgezl bal
    j jal jr jalr b
    mfc0 mtc0 mfc1 mtc1 cfc1 ctc1 qmfc2 qmtc2 cfc2 ctc2 bc0f bc0t
    bc1f bc1t bc1fl bc1tl bc2f bc2t bc2fl bc2tl
    movn movz move li la negu not
    nop syscall break sync eret ei di teq tne tge tlt cache pref
    plzcw pmfhi pmflo pmthi pmtlo pextlw pextuw pextlh pextuh pextlb pextub
    pcpyld pcpyud pcpyh paddw paddh paddb psubw psubh psubb pand por pxor pnor
    pmaxw pmaxh pminw pminh psllh psrlh psrah psllw psrlw psraw pmultw pmaddw
    pinth pinteh pexew pexeh prot3w pcgtw pceqw ppacw ppach ppacb qfsrv
    """.split()
_FPU_MN = r"""
    add sub mul div abs neg mov sqrt rsqrt max min madd msub adda suba mula
    madda msuba cvt\.s\.w cvt\.w\.s c\.eq c\.lt c\.le c\.f
    """.split()
_VU_OPS = (r"add|sub|mul|madd|msub|max|mini|opmula|opmsub|abs|"
           r"ftoi(?:0|4|12|15)|itof(?:0|4|12|15)")
_VU_MACRO = (
    r"v(?:(?:" + _VU_OPS + r")a?(?:(?:bc)?[xyzw]|q|i)?"
    r"|clipw|div|sqrt|rsqrt|move|mr32|nop|wait|iadd|iaddi|isub|iand|ior|lqi|sqi|lqd|sqd"
    r"|ilwr|iswr|mfir|mtir|rnext|rget|rinit|rxor|callms|callmsr)"
    r"(?:\.[xyzw]{1,4})?")
# Micro-mode VU spellings only count with a dest suffix ("mula.xyzw").
_VU_MICRO = (r"(?:(?:(?:" + _VU_OPS + r")a?(?:(?:bc)?[xyzw]|q|i)?|clipw)\.[xyzw]{1,4}"
             # micro-mode ops that carry no dest suffix
             r"|(?:ftoi|itof)(?:0|4|12|15)|ib(?:eq|ne|gtz|ltz|lez|gez)|iaddiu|isubiu"
             r"|xgkick|xitop|xtop|fc(?:and|or|eq|set|get)|fm(?:and|or|eq)|fs(?:and|or|eq|set)"
             r"|e(?:sadd|rsadd|leng|rleng|atan|exp|sin|sqrt|rsqrt)|waitp|mfp)")
_MNEMONIC = (
    r"(?:" + _VU_MACRO + r"|" + _VU_MICRO + r"|"
    r"(?:" + "|".join(_FPU_MN) + r")\.s|"
    r"(?:" + "|".join(sorted(_MIPS_MN, key=len, reverse=True)) + r")"
    r"(?:\.(?:ni|i|p))?)")
# "~" is excluded so the self-test samples (written with "~" for spaces)
# never read as chains in this file's own source.
MNEMONIC_RE = re.compile(r"(?<![\w.$%'\"~-])(" + _MNEMONIC + r")(?![\w.])")
MNEMONIC_FULL = re.compile(_MNEMONIC + r"\Z")

_VF = r"vf(?:[0-2]?[0-9]|3[01])(?:[xyzw]{1,4}|\.[xyzw]{1,4})?"
_VI = r"vi(?:[0-2]?[0-9]|3[01])"
_FR = r"f(?:[12]?[0-9]|3[01])"
_STRONG_REG = (r"(?:\$(?:" + _GPR + r"|" + _FR + r"|" + _VF + r"|" + _VI +
               r"|\d{1,2})|" + _VF + r"|" + _VI + r"|ACC)")
_REG = (r"(?:" + _STRONG_REG + r"|\$?(?:" + _GPR + r")|" + _FR + r")")
_REG_RANGE = r"(?:" + _REG + r"(?:\.\.|-)" + _REG + r")"
_IMM = (r"(?:[-+]?0[xX][0-9A-Fa-f]+|[-+]?\d+(?:\.\d+)?|%(?:hi|lo|gp_rel)\([^()]*\)"
        r"|[DL]_[0-9A-Fa-f]{6,8}|func_[0-9A-Fa-f]{8}|jtbl_\w+|\.L\w+"
        r"|<[\w +.]+>|%\d)")
_MEM = r"(?:(?:" + _IMM + r")?\(\s*(?:" + _REG + r")\s*\))"
_OPERAND = (r"(?:" + _MEM + r"|" + _REG_RANGE + r"|" + _REG + r"|" + _IMM +
            r"|[QIRP])(?![\w(-])")
# Operand lists separated by commas (listing style) or, for a bare trailing
# offset, by spaces ("<store-mn> zero <off>").
INSN_RE = re.compile(
    r"(?<![\w.$%'\"-])(?P<mn>" + _MNEMONIC + r")[ \t]+"
    r"(?P<ops>" + _OPERAND + r"(?:[ \t]*,[ \t]*" + _OPERAND + r")*)")
REG_FULL = re.compile(r"(?:" + _REG + r"|" + _REG_RANGE + r")\Z")
STRONG_REG_FULL = re.compile(r"(?:" + _STRONG_REG + r"|" + _REG_RANGE +
                             r"|\$?(?:" + "|".join(
                                 g for g in _GPR.split("|") if g != "at") +
                             r")|" + _FR + r")\Z")
MEM_FULL = re.compile(_MEM + r"\Z")
IMM_FULL = re.compile(r"(?:" + _IMM + r"|[QIRP])\Z")
SYM_FULL = re.compile(r"(?:[DL]_[0-9A-Fa-f]{6,8}|func_[0-9A-Fa-f]{8}|jtbl_\w+"
                      r"|\.L\w+|%(?:hi|lo|gp_rel)\([^()]*\))\Z")
SPLIT_OPS = re.compile(r"[ \t]*,[ \t]*")
_LOOSE_TOK = r"[\w.$%+-]+(?:\([\w.$%+-]*\))?"
LOOSE_RE = re.compile(
    r"(?<![\w.$%'\"-])(?P<mn>" + _MNEMONIC + r")[ \t]+"
    r"(?P<ops>" + _LOOSE_TOK + r"(?:[ \t]*,[ \t]*" + _LOOSE_TOK + r")+)")
STRONG_ONLY_FULL = re.compile(_STRONG_REG + r"\Z")
LOOSE_MEM_FULL = re.compile(r"-?0x[0-9A-Fa-f]+\([\w$]+\)\Z")
# Mnemonics that are also everyday words; they never count on their own.
ENGLISH_MN = {"and", "or", "not", "b", "j", "move", "add", "sub", "div", "li",
              "la", "sync", "di", "ei", "cache", "pref", "mult", "max", "min",
              "abs", "neg", "mov", "madd", "msub", "nor", "xor", "bal", "nop",
              "break"}

ZERO_OP = re.compile(r"(?:nop|vnop|syscall|eret|sync(?:\.[lp])?|ei|di|vwaitq)\Z")
ADDR_PREFIX = re.compile(
    r"^\s*(?:[-*>|#/]+\s*)?(?:0x)?[0-9A-Fa-f]{6,8}\s*:?\s*"
    r"(?:[0-9A-Fa-f]{8}\s+)?")
# objdump lines (address, colon, raw word, mnemonic) and splat's per-line
# comment carrying ROM offset, vram and raw word.
OBJDUMP_RE = re.compile(r"^\s*[0-9a-f]{5,8}:\s+[0-9a-f]{8}\s+[a-z]")
SPLAT_RE = re.compile(r"/\*\s*[0-9A-F]{6}\s+[0-9A-F]{8}\s+[0-9A-F]{8}\s*\*/")

HEX_BLOB = re.compile(r"(?<![0-9A-Fa-f])[0-9A-Fa-f]{64,}(?![0-9A-Fa-f])")
HEX_BYTES = re.compile(r"(?<![\w])(?:[0-9A-Fa-f]{2}[ \t]){31,}[0-9A-Fa-f]{2}(?![\w])")
HEX_WORDS = re.compile(r"(?<![\w])(?:[0-9A-Fa-f]{8}[ \t]+){7,}[0-9A-Fa-f]{8}(?![\w])")
# 64-bit / 128-bit groups (doubleword and quadword dumps).
HEX_DWORDS = re.compile(r"(?<![\w])(?:[0-9A-Fa-f]{16}[ \t]+){3,}[0-9A-Fa-f]{16}(?![\w])")
HEX_QWORDS = re.compile(r"(?<![\w])(?:[0-9A-Fa-f]{32}[ \t]+){1,}[0-9A-Fa-f]{32}(?![\w])")
WORD_TOKEN = re.compile(r"[A-Za-z_$%.][\w.$%]*(?:\([^()]*\))?|-?0x[0-9A-Fa-f]+|-?\d+")
ASM_START = re.compile(r"\b(?:__asm__|__asm|asm)\b(?:\s+(?:__volatile__|volatile))?\s*\(")
ASM_BLOCK = re.compile(r"\b(?:__asm__|__asm|asm)\b(?:\s+(?:__volatile__|volatile))?\s*\{")
STRING_RE = re.compile(r'"(?:[^"\\]|\\.)*"')


# --------------------------------------------------------------------------
# Line analysis
# --------------------------------------------------------------------------
EXPR_TAIL = re.compile(r"[ \t]*(?:=(?!=)|[-+*/<>|&^]=|<-|->|\?)")
# The mnemonic sits right after an address ("0x00ABC000:", "00ABC000 <raw word>").
ADDR_BEFORE = re.compile(
    r"(?:^|[^\w.$])(?:0x)?(?=[0-9A-Fa-f]*\d)[0-9A-Fa-f]{5,8}[ \t]*:?[ \t]*"
    r"(?:[0-9A-Fa-f]{8}[ \t]+)?[`*]?\Z")
_GPR_OR_FPR_FULL = re.compile(r"\$?(?:" + _GPR + r"|" + _FR + r")\Z")


_TAIL_TOKEN = re.compile(r"\$?[A-Za-z_]\w*(?:\.[xyzw]{1,4})?")
_TAIL_REG_FULL = re.compile(r"(?:\$\w+|" + _VF + r"|" + _VI + r"|" + _FR + r"|"
                            + "|".join(g for g in _GPR.split("|") if g != "at")
                            + r")\Z")


def expr_tail(text, m):
    """The C-like expression after a mnemonic's destination ("= a * b",
    "-> vfN, vfM"), up to the end of the clause; None when there is none."""
    t = EXPR_TAIL.match(text, m.end())
    if not t:
        return None
    end = len(text)
    for stop in (";", "*/", "`"):
        k = text.find(stop, t.end())
        if 0 <= k < end:
            end = k
    return text[t.end():end]


def tail_registers(tail):
    """Register names in an expression tail (vf/vi, $-registers, GPR/FPR)."""
    return [w for w in _TAIL_TOKEN.findall(tail or "") if _TAIL_REG_FULL.match(w)]


def _expr_tail_exempt(text, m, ops):
    """An expression form ("<vu-mn> ACC = a * b"): the mnemonic names one
    destination and a C-like expression over NAMED values follows.  Never for
    an addressed line, two or more operands, a $-register, a GPR/FPR or an
    offset(base) destination, or a tail that names registers ("= vfN + vfM",
    "-> vfN, vfM", "= mem[a0]"): those are listing lines however they end."""
    tail = expr_tail(text, m)
    if tail is None:
        return False
    if ADDR_BEFORE.search(text, 0, m.start()):
        return False
    if len(ops) != 1:
        return False
    o = ops[0]
    if o.startswith("$") or MEM_FULL.match(o) or _GPR_OR_FPR_FULL.match(o):
        return False
    return not tail_registers(tail)


def _insn_matches(text):
    """INSN_RE matches, minus expression forms ("vopmula.xyz ACC = a * b")."""
    for m in INSN_RE.finditer(text):
        if _expr_tail_exempt(text, m, _ops_of(m)):
            continue
        yield m


def _ops_of(match):
    return [o for o in SPLIT_OPS.split(match.group("ops").strip()) if o]


def _has_reg(ops):
    if all(o == "zero" for o in ops):
        return False  # both operands zero: the ISA's spelling of b; not a listing
    if len(ops) == 1 and ops[0] in ("at", "zero"):
        return False  # "<call-mn> at <address>", "and zero give 0": prose
    return any(REG_FULL.match(o) or MEM_FULL.match(o) for o in ops)


def insn_strong(text):
    """(a): a mnemonic with 2+ operands, one of them a register/mem operand.
    The loose form also accepts named placeholders next to a VU/$ register or
    a hex offset(base) ("<vu-mn> res, vfN, n.z", "<store-mn> <imm>, <off>(q)")."""
    for m in _insn_matches(text):
        ops = _ops_of(m)
        if len(ops) >= 2 and _has_reg(ops):
            return m.group(0).strip()
    for m in LOOSE_RE.finditer(text):
        ops = [o for o in SPLIT_OPS.split(m.group("ops").strip()) if o]
        if m.group("mn") in ENGLISH_MN or _expr_tail_exempt(text, m, ops):
            continue
        if len(ops) >= 2 and any(STRONG_ONLY_FULL.match(o) or LOOSE_MEM_FULL.match(o)
                                 for o in ops):
            return m.group(0).strip()
    return None


# Any token holding an upper-case letter ("LW", "Lw", "lW", "$T0", "VF4.xyz"):
# folded only when its lower-case form is a mnemonic or a register.
_CAPS_TOKEN = re.compile(r"(?<![A-Za-z0-9$%._])\$?(?=[\w.]*[A-Z])[A-Za-z][A-Za-z0-9]*"
                         r"(?:\.[A-Za-z]+)*(?![\w])")
_GHIDRA_DS = re.compile(r"(?<![\w.$])_(?=(?:" + _MNEMONIC + r")(?:[ \t`*/;]|\Z))")


def _fold_token(m):
    tok = m.group(0)
    if tok == "ACC" or len(tok.lstrip("$")) < 2:
        return tok  # ACC and the one-letter Q/I/R/P operands keep their case
    low = tok.lower()
    bare = low.lstrip("$")
    if MNEMONIC_FULL.match(bare) or REG_FULL.match(low) or REG_FULL.match(bare):
        return low
    return tok


def normalize(text):
    """Fold listing spellings the vocabulary is written for: upper-case
    mnemonic and register tokens, and Ghidra's delay-slot prefix (an
    underscore glued to the mnemonic).  Other text is left alone."""
    if not text:
        return text
    t = _CAPS_TOKEN.sub(_fold_token, text) if re.search(r"[A-Z]", text) else text
    return _GHIDRA_DS.sub(" ", t) if "_" in t else t


def insn_weak(text):
    """Instruction-looking: mnemonic + register/mem/symbol operand, a lone
    zero-operand mnemonic, or an objdump/splat line."""
    if OBJDUMP_RE.search(text) or SPLAT_RE.search(text):
        return True
    # Expression forms count here too: one of them alone is prose, three in a
    # row are a listing.
    for m in INSN_RE.finditer(text):
        ops = _ops_of(m)
        first = ops[0]
        if len(ops) == 1 and first in ("zero", "at"):
            continue  # "`sq zero` stores" in prose
        if (STRONG_REG_FULL.match(first) or MEM_FULL.match(first)
                or SYM_FULL.match(first) or (len(ops) >= 2 and _has_reg(ops))):
            return True
    body = ADDR_PREFIX.sub("", text, count=1) if ADDR_PREFIX.match(text) else text
    body = body.strip().strip("`*/#;").strip()
    return bool(ZERO_OP.match(body))


def _token_is_insn(tok):
    t = tok.rstrip(",;:)")
    t = t.lstrip("(")
    if not t:
        return True
    if t == "at" or t in ENGLISH_MN or t.startswith("func_"):
        return False  # English words ("<call-mn> at <address>", "sub"), helper names
    return bool(MNEMONIC_FULL.match(t) or REG_FULL.match(t) or MEM_FULL.match(t)
                or IMM_FULL.match(t) or re.match(r"\.?[xyzw]{1,4}\Z", t)
                or re.match(r"(?:0x)?[0-9A-Fa-f]{6,8}:?\Z", t)
                or re.match(_REG_RANGE + r"\Z", t))


def comment_mostly_insn(text):
    """(b): comment text dominated by 'mnemonic operand...'."""
    t = text.strip()
    if not t:
        return None
    # Needs a mnemonic immediately followed by a register-ish operand.
    hit = None
    for m in _insn_matches(t):
        ops = _ops_of(m)
        if m.group("mn") in ENGLISH_MN or all(o == "zero" for o in ops):
            continue
        if STRONG_REG_FULL.match(ops[0]) or MEM_FULL.match(ops[0]) or _has_reg(ops):
            hit = m.group(0).strip()
            break
    if not hit:
        return None
    toks = WORD_TOKEN.findall(t)
    if not toks:
        return None
    good = sum(1 for tok in toks if _token_is_insn(tok))
    n = len(toks)
    if good >= 2 and (good * 2 > n or (n <= 6 and good * 2 >= n)):
        return hit
    return None


def _mnemonic_stats(text):
    toks = WORD_TOKEN.findall(text)
    good = sum(1 for tok in toks if _token_is_insn(tok))
    mns = [tok.rstrip(",;:)").lstrip("(") for tok in toks]
    mns = [t for t in mns if MNEMONIC_FULL.match(t) and t not in ENGLISH_MN]
    return len(toks), good, mns


ASSIGN_RE = re.compile(r"(?:^|[^=!<>+\-*/])=(?!=)|[-+*/]=")


def comment_mnemonic_sequence(text, ext):
    """(b): a comment that is a sequence of mnemonics ("lhu/addiu/sh",
    "vmul.xyz; vaddx.y / vaddz.y; vrsqrt; vmulq.y"), or one instruction with
    its operand and nothing else ("<load-mn> <address>").  Skipped for C-like
    expressions ("= ACC + p*q") and for oracle scripts, which name the
    opcodes they implement ("# movz / movn")."""
    if ext in HASH_FAMILY:
        return None
    n, good, mns = _mnemonic_stats(text)
    toks = [t.strip(",;:()") for t in WORD_TOKEN.findall(text)]
    operands = [i for i in range(1, len(toks))
                if MNEMONIC_FULL.match(toks[i - 1])
                and (re.match(r"-?(?:0x[0-9A-Fa-f]+|\d+)\Z", toks[i])
                     or REG_FULL.match(toks[i]))]
    # An assignment ("= ACC + p*q") makes it a C-like expression, unless the
    # text still names three mnemonics, or two with a register operand.
    if ASSIGN_RE.search(text) and not (
            len(mns) >= 3 or (len(mns) >= 2 and any(
                STRONG_REG_FULL.match(toks[i]) for i in operands))):
        return None
    if good * 4 >= n * 3 and (len(mns) >= 3 or (len(mns) == 2 and operands)):
        return " ".join(mns[:4])
    if mns and n >= 2 and good == n:
        for m in _insn_matches(text):
            if m.group("mn") not in ENGLISH_MN:
                return m.group(0).strip()
    return None


def comment_bare_mnemonic(text, ext):
    """A comment that is only an instruction name ("vaddz.y"): not a finding by
    itself, but it counts toward a (c) listing block (not in oracle scripts,
    whose opcode handlers are labelled that way)."""
    if ext in HASH_FAMILY or ASSIGN_RE.search(text):
        return False
    n, good, mns = _mnemonic_stats(text)
    return bool(mns) and good * 4 >= n * 3


# --------------------------------------------------------------------------
# Shorter instruction forms (one operand, quoted, chained, table cells)
# --------------------------------------------------------------------------
_PLAIN_IMM_FULL = re.compile(r"[-+]?(?:0x[0-9A-Fa-f]+|\d+)\Z")
QUOTED_SPAN = re.compile(r"`([^`\n]{1,80})`"
                         r"|(?<![\w'])'([^'\n]{1,60})'(?![\w'])"
                         r"|\"([^\"\n]{1,60})\"")
_SPAN_SPLIT = re.compile(r"[\s,/;|]+")
_FPR_BARE = re.compile(
    r"(?<![\w.$%'\"-])(?:add|sub|mul|div|mov|neg|abs|sqrt|rsqrt|max|min|madd|msub"
    r"|adda|suba|mula|madda|msuba)[ \t]+\$?f\d{1,2}(?:[ \t]*,[ \t]*\$?f\d{1,2})+"
    r"(?![\w(])")
HEX_WORD_LIST = re.compile(r"(?<![\w])(?:0[xX][0-9A-Fa-f]{8}[ \t]*,[ \t]*){7,}"
                           r"0[xX][0-9A-Fa-f]{8}(?![\w])")


def _operand_token(t):
    t = t.strip("`*[]")
    if t.count("(") != t.count(")") or t.startswith("("):
        t = t.strip("()")
    if t.startswith("<") and not re.search(r"\d", t):
        return False  # an invented placeholder ("<reg>", "<sym>"), not an operand
    return bool(t) and bool(REG_FULL.match(t) or MEM_FULL.match(t) or IMM_FULL.match(t)
                            or SYM_FULL.match(t))


def quoted_instruction(seg):
    """A backticked or quoted span that is an instruction: a mnemonic plus at
    least one operand ("`<mn> <reg>`", "'<mn> <reg>,<off>(<base>)'",
    "`<mn> <imm>`", "`<mn>/<mn> <imm>`")."""
    for m in QUOTED_SPAN.finditer(seg):
        body = normalize((m.group(1) or m.group(2) or m.group(3) or "").strip())
        toks = [t for t in _SPAN_SPLIT.split(body) if t]
        if len(toks) < 2 or len(toks) > 8:
            continue
        first = toks[0].strip("*")
        if not MNEMONIC_FULL.match(first):
            continue
        if toks[1] == "at" and len(toks) > 2 and re.match(
                r"(?:0x)?[0-9A-Fa-f]{5,8}\Z", toks[2]):
            continue  # "<call-mn> at <address>" in words
        rest = toks[1:]
        ops = [t for t in rest if _operand_token(t)]
        if not ops:
            continue
        other = [t for t in rest if not _operand_token(t)
                 and not MNEMONIC_FULL.match(t.strip("*"))]
        if first in ENGLISH_MN:
            # "`li 3`" yes; "`b = a + 1`", "`add 4 bytes`" no.
            if other:
                continue
        elif len(other) * 2 > len(rest):
            continue
        return body
    return None


def single_operand_insn(seg):
    """Unquoted prose: a (non-English) mnemonic followed by exactly one
    operand that is a register ($-register, vf/vi, ACC, a GPR other than
    zero/at, an FPR) or an offset(base), e.g. "<branch-mn> <gpr> when ...".
    An expression after it does not help ("<vu-mn> vfN = p * q" names the
    original's register); write "the <vu-mn> (x = p * q)" instead."""
    for m in INSN_RE.finditer(seg):
        ops = _ops_of(m)
        if len(ops) != 1 or m.group("mn") in ENGLISH_MN:
            continue
        o = ops[0]
        if o in ("zero", "at"):
            continue
        if not (o.startswith("$") or STRONG_REG_FULL.match(o) or MEM_FULL.match(o)):
            continue
        return m.group(0).strip()
    return None


def register_expression(seg):
    """"<mn> <dest-reg> = <expr naming registers>" / "<mn> <reg> -> <regs>":
    a listing line written as an assignment."""
    for m in INSN_RE.finditer(seg):
        ops = _ops_of(m)
        if len(ops) != 1 or m.group("mn") in ENGLISH_MN:
            continue
        tail = expr_tail(seg, m)
        if tail is None or not tail_registers(tail):
            continue
        o = ops[0]
        if STRONG_REG_FULL.match(o) or o == "ACC" or MEM_FULL.match(o):
            return (m.group(0) + seg[m.end():m.end() + len(tail) + 4]).strip()
    return None


def operand_list_insn(seg):
    """A non-English mnemonic with two or more comma-separated operands even
    when none is a register ("<store-mn> 1, +0x0A", "<load-mn> 0x10, 0x14"),
    and bare FPU spellings with FPR operand lists ("<fpu-op> fN,fM,fK")."""
    m = _FPR_BARE.search(seg)
    if m:
        return m.group(0)
    for m in INSN_RE.finditer(seg):
        ops = _ops_of(m)
        if len(ops) < 2 or m.group("mn") in ENGLISH_MN:
            continue
        if _expr_tail_exempt(seg, m, ops):
            continue
        if all(re.match(r"\d{1,2}\Z", o) for o in ops):
            continue  # "<mn> 1, 2 and 3" in running prose
        if all(o == "zero" for o in ops):
            continue  # both operands zero: the ISA's spelling of b
        return m.group(0).strip()
    return None


_CHAIN_SEP = re.compile(r"[/;,]")
_CHAIN_FILLER = re.compile(r"[\s`*'\"()=+\-<>|&^~!?:\[\]]+")


def mnemonic_chain(seg, names_ok=False):
    """Mnemonics written out in order: three or more (non-English) joined by
    '/' or ';' ("<mn>/<mn>/<mn>", "<mn> <reg>; <mn>; <mn> <reg>"), two with a
    register operand, or three or more joined by commas when one of them has a
    register operand ("<mn> vfN = a * b, <mn>, <mn>").  The text between two
    links may only hold operands, lanes or short placeholders.  names_ok: an
    instruction decoder/interpreter script, whose handlers are labelled with
    the opcode names they implement; only chains with register operands count."""
    hits = [m for m in MNEMONIC_RE.finditer(seg)]
    if len(hits) < 2:
        return None
    best = None
    run = [hits[0]]
    seps = set()
    regs = False

    def gap_info(a, b):
        gap = seg[a.end():b.start()]
        sep = set(_CHAIN_SEP.findall(gap))
        words = [w for w in _CHAIN_FILLER.split(_CHAIN_SEP.sub(" ", gap)) if w]
        ok = bool(sep) and len(words) <= 5 and all(
            _operand_token(w) or re.match(r"\.?[xyzw]{1,4}\Z", w)
            or re.match(r"[A-Za-z]\w{0,2}(?:\.[xyzw]{1,4})?\Z", w) for w in words)
        has_reg = any(STRONG_REG_FULL.match(w.strip("`*")) and w not in ("ACC", "zero")
                      for w in words)
        return ok, sep, has_reg

    def judge(run, seps, regs):
        names = [h.group(1) for h in run if h.group(1) not in ENGLISH_MN]
        if not names or (names_ok and not regs):
            return None
        if seps <= {"/", ";"}:
            if len(names) >= 3 or (len(names) >= 2 and regs):
                return "/".join(h.group(1) for h in run)
        elif len(names) >= 3 and regs:
            return "/".join(h.group(1) for h in run)
        return None

    def trailing_reg(h):
        after = seg[h.end():h.end() + 24]
        mm = re.match(r"[ \t`*]+(\$?\w+)(?![\w.-])", after)
        return bool(mm and STRONG_REG_FULL.match(mm.group(1))
                    and mm.group(1) not in ("ACC", "zero", "at"))

    regs = trailing_reg(hits[0])
    for h in hits[1:]:
        ok, sep, has_reg = gap_info(run[-1], h)
        if ok:
            run.append(h)
            seps |= sep
            regs = regs or has_reg or trailing_reg(h)
            continue
        best = best or judge(run, seps, regs)
        run, seps, regs = [h], set(), trailing_reg(h)
    return best or judge(run, seps, regs)


def table_row_variant(line):
    """A markdown table row whose cell is a bare mnemonic: rejoin the
    following cells as its operand list ("| addr | <mn> | <ops> |")."""
    s = line.strip()
    if not s.startswith("|"):
        return None
    cells = [c.strip().strip("`*") for c in s.strip("|").split("|")]
    for i, c in enumerate(cells):
        if not MNEMONIC_FULL.match(normalize(c)) or i + 1 >= len(cells):
            continue
        ops = [o for o in SPLIT_OPS.split(normalize(cells[i + 1])) if o]
        if ops and all(_operand_token(o) for o in ops):
            return normalize(c) + " " + ", ".join(ops)
    return None


def short_forms(seg, ext, decoder=False):
    """(a)/(b) forms the listing checks above do not see.  Returns
    (check, reason) or None.  decoder: see mnemonic_chain(names_ok)."""
    if not seg or not seg.strip():
        return None
    nseg = normalize(seg)
    for text in {seg, nseg}:
        h = register_expression(text)
        if h:
            return ("a", "mnemonic with a register expression: %r" % h[:60])
        h = operand_list_insn(text)
        if h:
            return ("a", "mnemonic with operands: %r" % h[:60])
        h = quoted_instruction(text)
        if h:
            return ("b", "quoted instruction: %r" % h[:60])
        h = single_operand_insn(text)
        if h:
            return ("b", "mnemonic with a register operand: %r" % h[:60])
    # Bare-name chains count in the listing spelling (lower case); an
    # upper-case catalogue of ISA op names ("<MN> / <MN> / <MN>") only counts
    # with a register operand.
    h = mnemonic_chain(seg, names_ok=decoder) or (
        mnemonic_chain(nseg, names_ok=True) if nseg != seg else None)
    if h:
        return ("b", "mnemonic sequence: %r" % h[:60])
    m = HEX_WORD_LIST.search(seg)
    if m:
        words = re.findall(r"0[xX]([0-9A-Fa-f]{8})", m.group(0))
        if sum(w.startswith("00") for w in words) * 2 < len(words):
            return ("d", "list of %d 32-bit words" % len(words))
    return None


def comment_segments(lines, ext):
    """Per line, the comment/prose text to judge with check (b)."""
    out = []
    in_block = False
    for line in lines:
        segs = []
        if ext in PROSE_FAMILY:
            segs.append(line)
        elif ext in C_FAMILY:
            i = 0
            n = len(line)
            while i < n:
                if in_block:
                    j = line.find("*/", i)
                    if j < 0:
                        segs.append(line[i:])
                        i = n
                    else:
                        segs.append(line[i:j])
                        in_block = False
                        i = j + 2
                    continue
                m = re.compile(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'|/\*|//').search(line, i)
                if not m:
                    break
                if m.group(0) == "/*":
                    in_block = True
                    i = m.end()
                elif m.group(0) == "//":
                    segs.append(line[m.end():])
                    break
                else:
                    i = m.end()
        else:  # hash-comment languages; triple-quoted blocks are prose
            if in_block:
                segs.append(line)
                if line.count(in_block) % 2 == 1:
                    in_block = False
                out.append(segs)
                continue
            for q in ('"""', "'''"):
                if line.count(q) % 2 == 1:
                    in_block = q
                    segs.append(line)
                    break
            m = re.compile(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'|#').search
            i = 0
            while True:
                mm = m(line, i)
                if not mm:
                    break
                if mm.group(0) == "#":
                    segs.append(line[mm.end():])
                    break
                # Docstring/str contents are prose too.
                segs.append(mm.group(0)[1:-1])
                i = mm.end()
        out.append(segs)
    return out


def asm_exempt_ranges(lines, ext):
    """C-family: inline-asm string literals.  A statement with one or two
    instructions (a sync/ei/mfc0 intrinsic) is exempt from (a)/(b); three or
    more is reported as (c).  Returns (per-line blanked text, findings)."""
    blanked = list(lines)
    findings = []
    if ext not in C_FAMILY:
        return blanked, findings
    # CodeWarrior statement blocks, e.g. the two-instruction SDK syscall stubs.
    i = 0
    while i < len(lines):
        m = ASM_BLOCK.search(lines[i])
        if not m or ASM_FUNC_RE.match(lines[i]):
            i += 1
            continue
        depth = 0
        insns = 0
        j = i
        col = m.end() - 1
        done = False
        while j < len(lines) and not done and j - i < 200:
            line = blanked[j]
            k = col if j == i else 0
            start_k = k
            while k < len(line):
                if line[k] == "{":
                    depth += 1
                elif line[k] == "}":
                    depth -= 1
                    if depth == 0:
                        done = True
                        break
                k += 1
            body = line[start_k + (1 if j == i else 0):k]
            insns += len([p for p in body.split(";")
                          if MNEMONIC_RE.match(p.strip() + " ")
                          and p.strip() != "nop"])
            blanked[j] = line[:start_k + (1 if j == i else 0)] + " " * len(body) + line[k:]
            j += 1
        if insns >= 3:
            findings.append((i + 1, "c", "inline asm block with %d instructions" % insns))
        i = max(j, i + 1)
    i = 0
    while i < len(lines):
        m = ASM_START.search(lines[i])
        if not m:
            i += 1
            continue
        depth = 0
        start = i
        insns = 0
        j = i
        col = m.end() - 1
        done = False
        while j < len(lines) and not done and j - start < 40:
            line = lines[j]
            k = col if j == start else 0
            parts = []
            while k < len(line):
                ch = line[k]
                if ch == '"':
                    sm = STRING_RE.match(line, k)
                    if not sm:
                        break
                    body = sm.group(0)[1:-1]
                    insns += len([p for p in re.split(r"\\n|;|\\t\s*\\n", body)
                                  if re.search(r"[a-z]", p) and
                                  MNEMONIC_RE.search(p.replace("\\t", " ")) and
                                  p.replace("\\t", " ").strip() != "nop"])
                    parts.append((k, sm.end()))
                    k = sm.end()
                    continue
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        done = True
                        break
                k += 1
            if parts:
                s = blanked[j]
                for a, b in reversed(parts):
                    s = s[:a] + '"' + " " * (b - a - 2) + '"' + s[b:]
                blanked[j] = s
            j += 1
        if insns >= 3:
            findings.append((start + 1, "c",
                             "inline asm statement with %d instructions" % insns))
        i = max(j, i + 1)
    return blanked, findings


DIGEST_WORD = re.compile(r"sha-?(?:1|256)|\bhash|digest|\bmd5|checksum", re.I)
URL_RE = re.compile(r"https?://\S+")


def is_digest(lines, idx, m):
    """A lone SHA-1/SHA-256 value labelled as a digest (same or previous line)
    or embedded in a URL is a fingerprint, not content."""
    if len(m.group(0)) not in (40, 64):
        return False
    raw = lines[idx]
    if any(u.start() <= m.start() < u.end() for u in URL_RE.finditer(raw)):
        return True
    context = raw + (lines[idx - 1] if idx else "")
    return bool(DIGEST_WORD.search(context))


def mostly_numeric(line):
    s = line.strip()
    if not s:
        return False
    numeric = sum(1 for ch in s if ch in "0123456789abcdefABCDEFxX,.-+[]{}() \t:;\"'")
    letters = len(re.findall(r"[g-wyzG-WYZ]", s))
    return numeric >= 0.9 * len(s) and letters < 0.03 * len(s)


ASM_FUNC_RE = re.compile(r"^\s*(?:static\s+)?asm\s+(?!volatile\b|__volatile__\b)"
                         r"[A-Za-z_][\w\s\*]*\(")


def asm_function_blocks(lines, ext):
    """C-family: CodeWarrior 'asm void f(...) { ... }' bodies.  Each one is a
    listing by construction; report it once as (c) instead of per line."""
    blocks = []
    if ext not in C_FAMILY:
        return blocks
    i = 0
    while i < len(lines):
        if not ASM_FUNC_RE.match(lines[i]) or lines[i].rstrip().endswith(";"):
            i += 1
            continue
        depth = 0
        opened = False
        j = i
        while j < len(lines):
            depth += lines[j].count("{") - lines[j].count("}")
            opened = opened or "{" in lines[j]
            if opened and depth <= 0:
                break
            j += 1
        body = lines[i + 1:j]
        n = sum(1 for b in body if insn_weak(b) or re.match(r"\s*\.word\s", b))
        if opened:
            blocks.append((i, min(j, len(lines) - 1), n))
        i = j + 1
    return blocks


def _judge(line, segs, ext, folded=False, decoder=False):
    """Checks (a)/(b) on one line and its comment segments.  Returns
    (finding or None, instruction-like?).  On the folded (upper-case /
    Ghidra) variant only operand-bearing forms count: a catalogue of
    upper-case opcode NAMES in prose is not a listing."""
    hit = insn_strong(line)
    if not hit:
        for seg in segs:
            hit = insn_strong(seg)
            if hit:
                break
    if hit:
        return ("a", "mnemonic with operands: %r" % hit[:60]), True
    for seg in segs:
        h = comment_mostly_insn(seg)
        if h:
            return ("b", "comment is mnemonic+operands: %r" % h[:60]), True
        h = None if folded else comment_mnemonic_sequence(seg, ext)
        if h:
            return ("b", "comment is a mnemonic sequence: %r" % h[:60]), True
    if OBJDUMP_RE.search(line) or SPLAT_RE.search(line):
        return ("a", "objdump/splat listing line"), True
    if not folded:
        for seg in segs:
            h = short_forms(seg, ext, decoder)
            if h:
                return h, h[0] != "d"
        if ext in PROSE_FAMILY:
            row = table_row_variant(line)
            if row:
                found, _ = _judge(row, [row], ext, folded=True)
                h = found or short_forms(row, ext, decoder)
                if h:
                    return (h[0], "table row: " + h[1]), True
    weak = (insn_weak(line) or any(insn_weak(s) for s in segs)
            or (not folded and any(comment_bare_mnemonic(s, ext) for s in segs)))
    return None, weak


# Hash-family scripts that decode instruction words (oracles/interpreters):
# their handlers are labelled with the opcode names they implement.
DECODER_RE = re.compile(r">>\s*26\b|\bopcode\b|def\s+(?:cop1|special|regimm)\b")
MARKER_WINDOW = 5


def insn_count(text):
    """Instructions with operands on one line (for the marker rules)."""
    n = 0
    for m in INSN_RE.finditer(normalize(text)):
        ops = _ops_of(m)
        if m.group("mn") in ENGLISH_MN and not _has_reg(ops):
            continue
        if _has_reg(ops) or any(IMM_FULL.match(o) for o in ops):
            n += 1
    return n


INCLUDE_ASM_RE = re.compile(r"^\s*(?:#\s*include\s*[\"<][^\">]*\.(?:s|S|inc)[\">]"
                            r"|(?:INCLUDE_ASM|INCLUDE_RODATA|GLOBAL_ASM)\s*\()")


def scan_text(path, text, skip_asm_bodies=False):
    """Return a list of (line_no, check, reason, span_end) findings.
    skip_asm_bodies: the decomp's kept CodeWarrior asm function bodies and
    splat/INCLUDE_ASM includes are not reported (the caller decides when)."""
    ext = os.path.splitext(path)[1]
    if ext not in SCAN_EXTS:
        return []
    if skip_asm_bodies and ext in SPLAT_INCLUDE_EXTS:
        return []
    lines = text.split("\n")
    code_lines, findings = asm_exempt_ranges(lines, ext)
    # Inline asm statements/blocks inside C functions are compiled code the
    # user decided to keep, like the asm function bodies (decomp src/ only).
    findings = [] if skip_asm_bodies else [(ln, c, r, ln) for ln, c, r in findings]
    decoder = ext in HASH_FAMILY and bool(DECODER_RE.search(text))
    marked_insn = []
    segs = comment_segments(code_lines, ext)
    insn_line = [False] * len(lines)
    in_asm_func = [False] * len(lines)
    for a, b, n in asm_function_blocks(lines, ext):
        # A no-disasm-ok marker never hides an asm function body.
        if not skip_asm_bodies:
            findings.append((a + 1, "c", "asm function body with %d instruction lines"
                             % n, b + 1))
        for k in range(a + 1 if skip_asm_bodies else a, b + 1):
            in_asm_func[k] = True
    if skip_asm_bodies:
        for idx, raw in enumerate(lines):
            if INCLUDE_ASM_RE.match(raw):
                in_asm_func[idx] = True
    for idx, raw in enumerate(lines):
        if in_asm_func[idx]:
            continue
        line = code_lines[idx]
        ln = idx + 1
        marked = ALLOW_MARKER in raw
        found, weak = _judge(line, segs[idx], ext, decoder=decoder)
        if not found:
            nline = normalize(line)
            nsegs = [normalize(s) for s in segs[idx]]
            if nline != line or nsegs != segs[idx]:
                found, weak2 = _judge(nline, nsegs, ext, folded=True, decoder=decoder)
                weak = weak or weak2
        # A marked line is exempt from (a)/(b) but still counts toward (c);
        # it may carry one instruction mention, never two.
        insn_line[idx] = bool(found) or weak
        if found and marked and found[0] != "d":
            n = max(insn_count(line), max([insn_count(sg) for sg in segs[idx]] or [0]))
            if n >= 2:
                findings.append((ln, "a", "%s line holds %d instructions"
                                 % (ALLOW_MARKER, n), ln))
            marked_insn.append(idx)
        if found and (not marked or found[0] == "d"):
            findings.append((ln, found[0], found[1], ln))
        for m in HEX_BLOB.finditer(raw):
            if m.group(0).lower() in PINNED_HEX or is_digest(lines, idx, m):
                continue
            findings.append((ln, "d", "hex blob of %d digits" % len(m.group(0)), ln))
        for rx, what in ((HEX_BYTES, "hex byte dump"), (HEX_WORDS, "hex word dump"),
                         (HEX_DWORDS, "hex doubleword dump"),
                         (HEX_QWORDS, "hex quadword dump")):
            m = rx.search(raw)
            if m:
                words = m.group(0).split()
                if what == "hex word dump" and sum(w.startswith("00") for w in words) * 2 >= len(words):
                    continue  # a list of addresses, not code words
                findings.append((ln, "d", "%s (%d groups)" % (what, len(words)), ln))
        if len(raw) > 400 and mostly_numeric(raw):
            findings.append((ln, "e", "%d-char line of mostly hex/numbers" % len(raw), ln))
    # (c) marker abuse: two or more marked instruction lines close together
    # (blank or prose lines in between do not break the group).
    k = 0
    while k < len(marked_insn):
        j = k
        while j + 1 < len(marked_insn) and marked_insn[j + 1] - marked_insn[j] <= MARKER_WINDOW:
            j += 1
        if j > k:
            findings.append((marked_insn[k] + 1, "c", "%s on %d nearby instruction lines"
                             % (ALLOW_MARKER, j - k + 1), marked_insn[j] + 1))
        k = j + 1
    # (c) listing blocks
    idx = 0
    while idx < len(lines):
        if insn_line[idx]:
            j = idx
            while j + 1 < len(lines) and insn_line[j + 1]:
                j += 1
            if j - idx + 1 >= 3:
                findings.append((idx + 1, "c", "listing block of %d instruction-like lines"
                                 % (j - idx + 1), j + 1))
            idx = j + 1
        else:
            idx += 1
    findings.sort(key=lambda f: (f[0], f[1]))
    return findings


# --------------------------------------------------------------------------
# Git plumbing
# --------------------------------------------------------------------------
def _git(args, cwd):
    return subprocess.run(["git"] + args, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True).stdout


def repo_root():
    try:
        return _fsdecode(_git(["rev-parse", "--show-toplevel"], os.getcwd())).rstrip("\n")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return os.getcwd()


def _fsdecode(data):
    return data.decode("utf-8", "surrogateescape")


def staged_added_lines(root):
    """{path: set of added line numbers} for the index.  Names come from
    'git diff --name-only -z', so spaces, tabs, quotes and non-ASCII names are
    taken verbatim (no "+++ b/" header parsing, no C-quoting)."""
    names = _fsdecode(_git(["diff", "--cached", "--name-only", "-z",
                            "--diff-filter=ACMR"], root)).split("\0")
    added = {}
    for path in names:
        if not path:
            continue
        out = _fsdecode(_git(["--literal-pathspecs", "diff", "--cached", "-U0",
                              "--no-color", "--no-ext-diff", "--diff-filter=ACMR",
                              "--", path], root))
        lines = set()
        for line in out.split("\n"):
            m = re.match(r"@@ -\S+ \+(\d+)(?:,(\d+))? @@", line)
            if m:
                start, count = int(m.group(1)), int(m.group(2) or "1")
                lines.update(range(start, start + count))
        added[path] = lines
    return added


def _decode(data):
    if b"\0" in data[:8192]:
        return None
    return data.decode("utf-8", "replace")


def is_decomp_root(root):
    return os.path.isfile(os.path.join(root, "tools", "decomp", "build.py"))


def _skip_asm(root, relpath, include_asm_bodies):
    """Decomp repo, src/ only, unless --include-asm-bodies."""
    if include_asm_bodies or not is_decomp_root(root):
        return False
    rel = relpath.replace(os.sep, "/")
    return rel.startswith("src/")


def run_staged(root, include_asm_bodies=False):
    results = []
    for path, lines in sorted(staged_added_lines(root).items()):
        if not lines or os.path.splitext(path)[1] not in SCAN_EXTS:
            continue
        text = _decode(_git(["show", ":0:" + path], root))
        if text is None:
            continue
        skip = _skip_asm(root, path, include_asm_bodies)
        for ln, check, reason, end in scan_text(path, text, skip):
            if any(n in lines for n in range(ln, end + 1)):
                results.append((path, ln, check, reason, end))
    return results


def run_files(root, paths, include_asm_bodies=False, base=None):
    """Scan whole files.  Relative paths resolve against base (default root);
    the src/ asm-body rule uses the path relative to root."""
    results = []
    base = base or root
    for path in paths:
        full = path if os.path.isabs(path) else os.path.join(base, path)
        if os.path.splitext(path)[1] not in SCAN_EXTS or not os.path.isfile(full):
            continue
        with open(full, "rb") as fh:
            text = _decode(fh.read())
        if text is None:
            continue
        rel = os.path.relpath(os.path.abspath(full), root)
        skip = _skip_asm(root, rel, include_asm_bodies)
        for ln, check, reason, end in scan_text(path, text, skip):
            results.append((path, ln, check, reason, end))
    return results


def run_all(root, include_asm_bodies=False):
    files = _fsdecode(_git(["ls-files", "-z"], root)).split("\0")
    return run_files(root, [f for f in files if f], include_asm_bodies)


# --------------------------------------------------------------------------
# Self-test.  Every sample is SYNTHETIC: invented addresses (0x00ABCxxx),
# invented register/operand combinations and invented symbol names; nothing
# is taken from the original program or its listings.  Samples are written
# with "~" for spaces so this file's own source never looks like a listing to
# the --all scan.
# --------------------------------------------------------------------------
def _s(text):
    return text.replace("~", " ")


_ASM_FUNC_SAMPLE = ("//~kept~asm~body~below\n"
                    "asm~void~func_00ABC300(void)~{\n~~~~daddiu~~$k0,~$k1,~0x2B\n"
                    "~~~~.word~0x1234abcd\n~~~~jr~$ra\n~~~~nop\n}\n")


_INLINE_ASM_SAMPLE = ("int~g(int~k)~{\n~~~~int~r;\n"
                      "~~~~__asm__~__volatile__(\"daddu~$12,~$13,~$14\\n\\tdsll~$12,~$12,~5\\n\\tor~%0,~$12,~$0\"\n"
                      "~~~~~~~~:~\"=r\"(r)~::~\"$12\");\n~~~~return~r;\n}\n")
_ASM_BLOCK_SAMPLE = ("void~h(void)~{\n~~~~asm~{\n~~~~~~~~addiu~$k1,~$zero,~9\n~~~~spin:\n"
                     "~~~~~~~~addiu~$k1,~$k1,~-1\n~~~~~~~~bnez~$k1,~spin\n~~~~~~~~nop\n"
                     "~~~~}\n}\n")


def _self_test_staged():
    """--staged must see files whose names hold spaces, tabs or non-ASCII."""
    import shutil
    import tempfile
    if shutil.which("git") is None:
        print("self-test: git not found, staged-name test skipped")
        return 0
    fails = 0
    tmp = tempfile.mkdtemp(prefix="nodisasm-selftest-")
    try:
        _git(["-c", "init.defaultBranch=main", "init", "-q"], tmp)
        names = ["plain.md", "with space.md", "café über.md", "tab\there.md",
                 'quo"te.md', "1:colon.md"]
        for name in names:
            with open(os.path.join(tmp, name), "w", encoding="utf-8") as fh:
                fh.write("intro\n" + _s("~~~~lw~t7,~0x5A4(s6)") + "\n")
        _git(["add", "--"] + names, tmp)
        got = {p for p, _, c, _, _ in run_staged(tmp) if c == "a"}
        for name in names:
            if name not in got:
                fails += 1
                print("FAIL staged: %r not reported (got %r)" % (name, sorted(got)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return fails


def self_test():
    marker = ALLOW_MARKER
    cases = [
        # (path, text, expected set of checks found anywhere in the text;
    #  "a|b" means either check)
        ("x.c", "~~~~blend(v9,~q,~48);~/*~lqc2~vf27,~D_00ABC5E8~*/", {"a"}),
        ("x.c", "~~~~vu(st,~P,~R);~/*~vmuly.xz~vf27,~vf13,~vf22y~*/", {"a"}),
        ("x.c", "~~~~m~=~n;~/*~lw~t7,0x5A4(s6)~*/", {"a"}),
        ("x.c", "/*~msub.xyz~ACC,~vf19,~vf11~*/", {"a"}),
        ("x.c", "~~~~r[1][2]~=~two;~~/*~vaddz.y~vf21~*/", {"b"}),
        ("x.c", "~~~~z~=~min(z);~/*~vminiy.z~vf14y~*/", {"b"}),
        ("x.c", "~~~~clear(r,~0,~16);~/*~sw~zero~0x5C/0x58/0x54/0x50~*/", {"b"}),
        ("x.c", "/*~00ABC4F0:~vadd.xyz~vf23,~vf2,~vf2;~vsub.w~vf23,~vf23,~vf1;~vmr32~x4;", {"a"}),
        ("x.py", "~~~~0x4AB2C3D4,~~#~vmul.xyz~vf23,~vf2,~vf2", {"a"}),
        ("x.md", "0x00ABC0A4:~~sh~~~~~$t5,~-21468($gp)~~~~~~#~spill", {"a"}),
        ("x.md", "~~abc4f0:\t4ab2c3d4~\tvmul.xyz\tvf23,vf2,vf2", {"a"}),
        ("x.c", "/*~0BC4F0~00ABC4F0~4AB2C3D4~*/~~vmul.xyz~$vf23,~$vf2,~$vf2", {"a"}),
        ("x.md", "0x00ABC100:~~lui~~~~$t6,~0x00AB\n0x00ABC104:~~jalr~~~$t6\n"
                 "0x00ABC108:~~sh~~~~~$t5,~-21472($gp)", {"a", "c"}),
        ("x.md", "a~jal~func_00ABCDE0\nthen~jr~$ra\nnop", {"c"}),
        ("x.c", "static~const~char~k[]~=~\"" + "ab" * 40 + "\";", {"d"}),
        ("x.txt", " ".join(["5c"] * 40), {"d"}),
        ("x.json", "[" + ",".join(["67890"] * 90) + "]", {"e"}),
        ("x.c", "~*~~~vmsubz.xyz~~res,~vf19,~n.z~*/", {"a"}),
        ("x.c", "~~~~q[5]~=~half;~/*~sw~0x3F000000,~0x7C(q)~*/", {"a"}),
        ("x.c", "~~~~o->k--;~~/*~lhu/addiu/sh~*/", {"b"}),
        ("x.c", "~~~~blend(s,~k,~20);~/*~lqc2~vf26~(z~lane~unused)~*/", {"b"}),
        ("x.md", "~~~`jalr~$t9;~nop`~verbatim.", {"b"}),
        ("x.c", "~~~~if~(!lt(a))~{~~/*~c.lt.s~+136,~0;~bc1f~*/", {"b"}),
        ("x.c", "~~~~*r~=~g[7];~~/*~lw~0x70001234~*/", {"b"}),
        ("x.c", "/*~00ABC200(dst,~src):~vmul.xyz~('vmul',9);~vaddx.y~/~vaddz.y;~vrsqrt;~vmulq.y;~*/", {"b"}),
        ("x.c", "~~~~a~=~f(a);~/*~vaddx.y~*/\n~~~~a~=~g(a);~/*~vaddz.y~*/\n~~~~a~=~h(a);~/*~vaddw.y~*/", {"c"}),
        ("x.h", "~~~~float~y~=~o[1];~/*~lq~vf17.y,~392(vi03)~*/", {"a"}),
        ("x.py", "~~~~o.plain(0x0007A2BF)~~#~dsrl32~$7,~$9,~10", {"a"}),
        ("x.py", "~~~~o.save(0xABC104,~0x01C0F809)~~#~jalr~t6", {"b"}),
        ("x.c", "asm~void~func_00ABC300(void)~{\n~~~~daddiu~~$k0,~$k1,~0x2B\n~~~~.word~0x1234abcd\n~~~~jr~$ra\n~~~~nop\n}", {"c"}),
        ("x.c", "void~g(void)~{\n~~~~asm~{~lh~$t2,~6($t4);~ori~$t2,~$t2,~8;~sh~$t2,~6($t4);~};\n}", {"c"}),
        ("x.c", "__asm__~volatile(\"daddu~$15,~$24,~$25\\n\\tdsll~$15,~$15,~7\\n\\tor~%0,~$15,~$0\");", {"c"}),
        # (2) an expression-looking tail does not rescue a listing line.
        ("x.md", "0x00ABC100:~~lui~~~~$t6,~0x00AB~->~hi\n0x00ABC104:~~ori~~~~$t6,~$t6,~0x10~->~lo\n"
                 "0x00ABC108:~~sh~~~~~$t5,~-21472($gp)~->~store", {"a", "c"}),
        ("x.c", "/*~lw~t7,~0x5A4(s6)~=~cached~*/", {"a"}),
        ("x.c", "/*~vmuly.xz~vf27,~vf13,~vf22y~?~*/", {"a"}),
        ("x.md", "jalr~$t6~->\nsh~$t5,~8($t6)~?\njr~$ra~<-", {"a", "c"}),
        ("x.md", "00ABC10C:~jr~ra~->~back\n00ABC110:~jalr~t9~->~call\n00ABC114:~mflo~t2~->~lo", {"c"}),
        ("x.md", "0x00ABC120:~vaddw.x~vf5~=~n\n0x00ABC124:~vmulq.y~vf6~=~n\n0x00ABC128:~vaddz.w~vf7~=~n", {"c"}),
        # (4) upper case, Ghidra delay-slot prefix, 64/128-bit dumps, extensions.
        ("x.c", "~~~~n~=~m;~/*~LW~T7,~0x5A4(S6)~*/", {"a"}),
        ("x.md", "~~~~VMULY.XZ~~VF27,~VF13,~VF22Y", {"a"}),
        ("x.md", "00ABC120:~~_sh~t5,~0x6C(s5)", {"a"}),
        ("x.md", "~~~~bnez~~t3,~0x00ABC200\n~~~~_nop\n~~~~jalr~t9", {"a", "c"}),
        ("x.txt", "3C1A00AB275AC0F0~8F5B0010AF5B0014~2D2EAF33E9C1B070~1BADB002CAFEF00D", {"d"}),
        ("x.txt", "0123456789ABCDEFFEDCBA9876543210~13579BDF02468ACE13579BDF02468ACE", {"d"}),
        ("x.toml", "note~=~\"lw~t7,~0x5A4(s6)\"", {"a"}),
        ("x.toml", "k~=~1~~#~sh~t5,~0x6C(s5)", {"a"}),
        ("x.cfg", "hint:~sh~t5,~0x6C(s5)", {"a"}),
        ("x.html", "<p>sh~t5,~0x6C(s5)</p>", {"a"}),
        ("x.ts", "//~lw~t7,~0x5A4(s6)", {"a"}),
        ("x.S", "~~~~lw~t7,~0x5A4(s6)", {"a"}),
        ("x.yml", "k:~1~~#~lw~t7,~0x5A4(s6)", {"a"}),
        # (5) the marker never hides a block or an asm body.
        ("x.md", "jr~$ra~~" + marker + "\nlw~t7,~0x5A4(s6)~~" + marker +
                 "\nsh~t5,~8(t6)~~" + marker, {"c"}),
        ("x.c", "asm~void~func_00ABC380(void)~{~//~" + marker +
                "\n~~~~daddiu~~$k0,~$k1,~0x2B\n~~~~jr~$ra\n~~~~nop\n}", {"c"}),
        ("x.md", "dump~" + marker + ":~" + " ".join(["5c"] * 40), {"d"}),
        # Marker abuse: marked lines apart by blank lines, two on one line.
        ("x.md", "lw~k1,~0x3E(k0)~~" + marker + "\n\naddiu~k1,~k1,~6~~" + marker +
                 "\n\nsw~k1,~0x1C(sp)~~" + marker, {"c"}),
        ("x.md", "lw~k1,~0x3E(k0);~addiu~k1,~k1,~6~~" + marker, {"a"}),
        # Round 2: register ranges and named-ACC operands, quoted single-operand
        # forms, chains, bare FPR lists, $vi, "+0x" offsets, table rows, mixed
        # case, 0x word lists.
        ("x.c", "/*~The~rows~are~fetched~first~(lqc2~vf28..vf31);~then~each~is~summed~*/", {"b"}),
        ("x.c", "//~msuba.s/msub.s~ACC~on~the~pair", {"b"}),
        ("x.md", "the~helper~clears~it~with~`lq~zero`~twice", {"b"}),
        ("x.md", "the~guard~is~`bltz~k1`~here", {"b"}),
        ("x.md", "spilled~with~'sd~k0,0x1F8(k1)'~first", {"b"}),
        ("x.md", "built~by~`lui~0x7E5A`~alone", {"b"}),
        ("x.md", "the~count~comes~from~`li~93`", {"b"}),
        ("x.md", "a~trap~via~`bnel/break~6`~on~failure", {"b"}),
        ("x.md", "a~trap~via~\"break~6\"~on~failure", {"b"}),
        ("x.md", "then~`bgez~5~/~j~out`~to~leave", {"b"}),
        ("x.c", "/*~vmulaz/vmaddaw/vmaddax/vmaddy~over~the~rows~*/", {"b"}),
        ("x.c", "/*~VMULAZ~vf3~/~VMADDAW~vf5~/~VMADDX~vf7~*/", {"a|b"}),
        ("x.c", "//~daddiu~k0;~lui;~ori;~sd~k1~on~entry", {"b"}),
        ("x.md", "packed~with~dsrl32/dsll32/xor/subu~per~value", {"b"}),
        ("x.c", "/*~the~target~has~sub~f7,f9,f7~here~*/", {"a"}),
        ("x.c", "/*~flags~via~ctc2~$vi27~*/", {"b"}),
        ("x.md", "stored~as~sh~7,~+0x1E~in~the~slot", {"a"}),
        ("x.md", "|~00ABC4A0~|~lhu~|~k1,~0x3E(k0)~|", {"a"}),
        ("x.md", "Lhu~k1,~0x3E(k0)~then~return", {"a"}),
        ("x.md", "lHU~~K1,~0X3E(K0)", {"a|b"}),
        ("x.md", "words:~" + ",~".join("0x%08X" % (0x5B1C2D3E + 0x01010101 * i) for i in range(8)), {"d"}),
        # VU listings written as expressions over registers.
        ("x.c", "/*~vadd.xyz~vf3~=~vf5~+~vf7~*/\n/*~vmul.xyz~vf9~=~vf3~*~vf3~*/\n"
                "/*~vsub.xyz~vf11~=~vf9~-~vf5~*/", {"a|b", "c"}),
        ("x.c", "/*~vadd.xyz~vf3~=~vf5~+~vf7;~vmul.xyz~vf9~=~vf3~*~vf3;~vsub.xyz~vf11~=~vf9~-~vf5~*/", {"a|b"}),
        ("x.c", "/*~lqc2~vf3~=~mem[k0]~*/\n/*~vmul.xyz~vf9~=~vf3~*~vf3~*/\n/*~sqc2~vf9~->~mem[k0]~*/", {"a|b", "c"}),
        ("x.c", "/*~vmulaw.xyzw~ACC~=~vf3~*~vf5.w~*/", {"a|b"}),
        ("x.c", "/*~vmul.xyz~vf3~->~vf5,~vf7~*/", {"a|b"}),
        # A register destination is an operand even before a named expression.
        ("x.c", "/*~00ABC260(dst,~p,~q):~vopmula.xyz~ACC~=~p.zxy~*~q.yzx~*/", {"b"}),
        ("x.c", "/*~vmuly.xz~vf27~=~p~*~q.y~*/", {"b"}),
        ("x.c", "/*~then~msubbcz.xw~ACC~=~ACC~-~g~*~h.z~*/", {"b"}),
        ("x.py", "#~then~ftoi12~vf13~packs~it", {"b"}),
        ("x.py", "#~the~ibne~vi05,~vi06~test~skips~ahead", {"a"}),
        ("x.c", "/*~00ABC7C0(p,~q):~vmul.xyz~vf9~=~p~*~q,~vaddw.x,~vaddz.x~*/", {"b"}),
    ]
    negatives = [
        ("x.c", "/*~00ABC4F8:~y~lane~=~u;~z~=~y~*/"),
        ("x.c", "/*~00ABC500..00ABC52C:~Horner~steps~over~five~terms,~y~lane~only~*/"),
        ("x.c", "/*~here~a~vrsqrt~is~used,~so~Q~is~rounded~*/"),
        ("x.c", "/*~00ABC260(dst,~p,~q):~the~vopmula~(ACC~=~p.zxy~*~q.yzx)~*/"),
        ("x.c", "/*~the~vmuly~writes~xz~=~p~*~q.y~*/"),
        ("x.c", "~~~~uint32_t~t3,~t4,~a2~=~0;"),
        ("x.c", "~~~~return~sub(a2,~a3);"),
        ("x.c", "~~~~__asm__~__volatile__~(\"mfc0~%0,~$15\"~:~\"=r\"(prid));"),
        ("x.c", "void~ShiftPairX(void)~{\n~~~~asm~{~daddu~$k0,~$k1,~$zero;~dsll~$k0,~$k0,~2;~};\n}"),
        ("x.c", "~~~~asm~volatile~(\"cfc2~%0,~$vi21\"~:~\"=r\"(bits));"),
        ("x.py", "WORDS~=~[0x4AB2C3D4,~0x4AB2C3E8,~0x4A0012FC]~~#~three~instruction~words"),
        ("x.py", "~~~~if~not~a2~and~t4:"),
        ("x.md", "Boot~ELF~" + max(PINNED_HEX, key=len)),
        ("x.md", "The~store~at~00ABC0A0~writes~the~pad~word;~a2~holds~the~slot."),
        ("x.md", "Use~`lh`/`sh`~pairs~only~when~the~field~is~aligned."),
        ("x.md", "addresses:~00ABC000~00ABC040~00ABC080~00ABC0C0~00ABC100~00ABC140~00ABC180~00ABC1C0"),
        ("x.c", "~~~~y~=~vmul(1,~c);~/*~vrsqrt;~then~Q~is~scaled~by~one~*/"),
        ("x.c", "static~const~Form~F_ZERO~=~{0};~/*~vmul*,~vmulbc*,~vmulq,~vadd~9,~vaddbc*,~vop*~*/"),
        ("x.c", "~~~~/*~vmr32.xyzw~fd,~fs:~fd~=~(fs.y,~fs.z,~fs.w,~fs.x),~a~rotate.~*/"),
        ("x.c", "/*~vclipw.xyz~v,~v.w:~bits~0/1~y~>~|w|~/~y~<~-|w|,~4/5~for~z~*/"),
        ("x.md", "Both~add~and~sub~truncate~here."),
        ("x.c", "/*~sb~0x20~at~+5,~sw~0~at~+8,~sh~len~at~+0~(bytes~+6~kept).~*/"),
        ("x.py", "~~~~\"\"\"lbu~zero-extends~and~lb~sign-extends,~at~odd~offsets.\"\"\""),
        ("x.h", "~*~~~~~~~~~~~~~~~~~~~~~~~~~~~~not~$f3,~is~where~the~helper~keeps~it"),
        ("x.c", "~~~~out8[5]~=~0.5f;~~/*~clip.z~=~y~*/"),
        ("x.h", "~~~~int16_t~spare;~/*~D_00ABC9C4~(lh/sh)~*/"),
        ("x.c", "~~~~~~~~if~(!(flags~&~2u))~continue;~/*~0xABC2F0~bnel~*/"),
        ("x.c", "~~~~~~~~~~~~return~-2;~/*~jal~at~0xABC31C~*/"),
        ("x.py", "~~~~~~~~if~word~&~63~in~(12,~13):~~#~movn~/~movz"),
        ("x.py", "~~~~'rsqrt.s':~70,~'neg.s':~71,\n~~~~'max.s':~72,~'min.s':~73,\n~~~~'msub.s':~74,~'adda.s':~75,"),
        ("x.py", "~~~~pass~~#~an~always-taken~beq~zero,~zero~branch"),
        ("x.md", "with~EE~SHA256\n`" + "0123456789abcdef" * 4 + "`."),
        ("x.h", "~~~~int16_t~delay;~~~~~/*~+0x2C:~lh/sh~countdown~*/"),
        ("x.md", "-~0x00ABC000..0x00ABFFFF:~12~sd-spill,~3~sq-spill"),
        ("x.c", "~~~~~~~~~~~~asm~{~nop;~ei;~nop;~}"),
        ("x.c", "~~~~cs.z~=~f[1]~+~f[2]~*~cs.z;~~/*~vmulay.z~/~vmaddz.z~*/"),
        ("x.c", "~~~~key~=~screen.w;~~/*~8~*~(clip.w~/~clip.z)~*/"),
        ("x.c", "//~~~~~~vadd,~func_00ABC6F0~vsub,~func_00ABC708~normalize,~func_00ABC720"),
        ("x.c", "/*~vmul.xyz~vf23,~vf2,~vf2~*/~/*~" + marker + "~*/"),
        ("x.md", "|~00ABC0A0~|~one-frame~step~breakpoint~|~" + "x" * 420 + "~|"),
        # New negatives: folding and tails must not create false positives.
        ("x.md", "Keep~the~LW/SW~pair~for~ALIGNED~fields."),
        ("x.c", "#define~SP_SLOTS~(OR_MASK~|~4)"),
        ("x.c", "~~~~int~_sw~=~pick(t5,~t6);"),
        ("x.md", "The~pad~handler~at~00ABC300~->~dispatch;~a2~=~port."),
        ("x.md", "NOTE:~ADD~A~FIELD~AND~OR~IT~IN."),
        ("x.md", "Hashes:~0123456789abcdef~fedcba9876543210"),
        ("x.md", "Measured~at~ADD.S~0xABC010,~SUB.S~0xABC020,~MUL.S~0xABC030,\n"
                 "DIV.S~0xABC040,~MADD.S~0xABC050,~NEG.S~0xABC060,\nC.EQ.S~0xABC070."),
        ("x.md", "-~Out~of~scope~here:~MIN.S,~NEG.S,~MSUBA.S,~CVT.W.S."),
        # Round 2 negatives.
        ("x.md", "lw~k1,~0x3E(k0)~~" + marker + "\n\n\nfirst\nsecond\nthird\n\n"
                 "sw~k1,~0x1C(sp)~~" + marker),
        ("x.md", "a~call~through~`jal~<sym>`~or~`jr~<reg>`"),
        ("x.md", "|~MSUBA.S~|~ACC~-~p~*~q~(made-up~row)~|~3~|"),
        ("x.md", "Both~halves~use~a~beq~zero,~zero~as~the~always-taken~form."),
        ("x.py", "major~=~raw~>>~26\nif~major~in~HANDLED:~~#~bgtz~/~blez~/~bne"),
        ("x.c", "//~it~uses~sltu/lwu~gp-rel~addressing"),
        ("x.md", "a~`jal~at~0xABC31C`~in~words"),
        ("x.md", "-~Upper~ops~(FTOI4~/~ITOF12~/~MADDBCX~/~CLIPW~/~...)."),
        ("x.c", "~~~~swap(&p,~&q);~/*~or~t7<->t8~swap~*/"),
        ("x.md", "Use~a~`vmaddz.y`~/~`vmulax.w`~pair~with~no~operands."),
    ]
    fails = 0
    for path, text, want in cases:
        got = {c for _, c, _, _ in scan_text(path, _s(text))}
        if not all(any(x in got for x in w.split("|")) for w in want):
            fails += 1
            print("FAIL positive %-6s want %s got %s: %s" % (path, sorted(want), sorted(got), _s(text)[:90]))
    for path, text in negatives:
        got = scan_text(path, _s(text))
        if got:
            fails += 1
            print("FAIL negative %-6s got %s: %s" % (path, [(c, r) for _, c, r, _ in got], _s(text)[:90]))
    # (6) decomp src/: kept asm function bodies and splat includes are skipped
    # unless asked for; comments outside the body are still scanned.
    extra = 0
    body = _s(_ASM_FUNC_SAMPLE)
    checks = [
        ("asm body skipped", scan_text("src/x.c", body, True), set()),
        ("asm body reported", {c for _, c, _, _ in scan_text("src/x.c", body, False)}, {"c"}),
        ("comment outside body scanned",
         {c for _, c, _, _ in scan_text("src/x.c", _s("//~lw~t7,~0x5A4(s6)\n") + body, True)}, {"a"}),
        ("NEARMISS header scanned",
         {c for _, c, _, _ in scan_text("src/x.c", _s("//~NEARMISS~func_00ABC300~sh~t5,~0x6C(s5)\n"
                                                    "int~f(void)~{~return~0;~}\n"), True)}, {"a"}),
        ("INCLUDE_ASM line skipped",
         scan_text("src/x.c", _s("INCLUDE_ASM(\"asm/nonmatchings/x\",~func_00ABC300);\n"
                                 "#include~\"asm/x/lw_t7_0x5A4.s\"\n"), True), set()),
        ("splat include file skipped",
         scan_text("src/x.s", _s("~~~~lw~t7,~0x5A4(s6)\n"), True), set()),
        ("inline asm statement skipped",
         scan_text("src/x.c", _s(_INLINE_ASM_SAMPLE), True), set()),
        ("inline asm statement reported",
         {c for _, c, _, _ in scan_text("src/x.c", _s(_INLINE_ASM_SAMPLE), False)}, {"c"}),
        ("inline asm block skipped",
         scan_text("src/x.c", _s(_ASM_BLOCK_SAMPLE), True), set()),
        ("inline asm block reported",
         {c for _, c, _, _ in scan_text("src/x.c", _s(_ASM_BLOCK_SAMPLE), False)}, {"c"}),
        ("comment next to inline asm scanned",
         {c for _, c, _, _ in scan_text("src/x.c", _s("//~lw~t7,~0x5A4(s6)\n" + _ASM_BLOCK_SAMPLE),
                                        True)}, {"a"}),
    ]
    for name, got, want in checks:
        extra += 1
        ok = (got == [] or got == set()) if not want else want <= got
        if not ok:
            fails += 1
            print("FAIL %s: got %r" % (name, got))
    # _skip_asm: only the decomp root, only src/, never with the option.
    tmp_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if is_decomp_root(tmp_root):
        for rel, inc, want in (("src/a.c", False, True), ("src/a.c", True, False),
                               ("docs/a.md", False, False)):
            extra += 1
            if _skip_asm(tmp_root, rel, inc) != want:
                fails += 1
                print("FAIL _skip_asm(%s, %s) != %s" % (rel, inc, want))
    extra += 1
    staged_fails = _self_test_staged()
    fails += 1 if staged_fails else 0
    total = len(cases) + len(negatives) + extra
    print("self-test: %d/%d passed (%d positive, %d negative, %d mode/staged)"
          % (total - fails, total, len(cases), len(negatives), extra))
    return 1 if fails else 0


def main(argv):
    args = argv[1:]
    if args and args[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    if args == ["--self-test"]:
        return self_test()
    include_asm = "--include-asm-bodies" in args
    args = [a for a in args if a != "--include-asm-bodies"]
    root = repo_root()
    if not args or args == ["--staged"]:
        results, what = run_staged(root, include_asm), "staged additions"
    elif args == ["--all"]:
        results, what = run_all(root, include_asm), "tracked files"
    elif any(a.startswith("--") for a in args):
        print(__doc__, file=sys.stderr)
        return 2
    else:
        results, what = run_files(root, args, include_asm, os.getcwd()), "given files"
    for path, ln, check, reason, end in results:
        span = "" if end == ln else "-%d" % end
        print("%s:%d%s: [%s] %s" % (path, ln, span, check, reason))
    sys.stdout.flush()
    if results:
        print("check_no_disassembly: %d finding(s) in %s. Rewrite as addresses + "
              "words/C-like expressions; '%s' only covers a single isolated "
              "prose mention." % (len(results), what, ALLOW_MARKER),
              file=sys.stderr)
        return 1
    print("check_no_disassembly: clean (%s)" % what)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

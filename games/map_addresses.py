#!/usr/bin/env python3
"""Find the base game's addresses in an expansion's executable.

    games/map_addresses.py <base game dir> <other game dir> ADDR [ADDR ...]

The three Heroes V executables are builds of one code base, so most functions
are byte-identical once their relocated dwords (absolute addresses) are
masked. A function address maps through such a match. A data address (a
global, a vtable) maps through the matched functions that name it: the
relocated operand at the same offset in the other function is its
counterpart, and the answer is the one most of them agree on.

Each game dir needs original/<exe> and its Ghidra listings (functions.tsv).
"""

import collections
import hashlib

import capstone
from capstone import x86_const as X
import struct
import sys
import tomllib
from pathlib import Path


class Image:
    def __init__(self, game_dir):
        cfg = tomllib.loads((game_dir / "game.toml").read_text())
        exe = game_dir / cfg["game"]["developer_exe"]
        listings = game_dir / cfg["translate"]["listings"]
        self.data = exe.read_bytes()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", d, opt + 28)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        first = opt + struct.unpack_from("<H", d, pe + 20)[0]
        self.sections = []
        for i in range(nsec):
            s = first + i * 40
            vs, va, rs, ra = struct.unpack_from("<IIII", d, s + 8)
            self.sections.append((va, max(vs, rs), ra, rs))
        reloc_rva, reloc_size = struct.unpack_from("<II", d, opt + 96 + 5 * 8)
        self.relocs = set()
        pos, end = self.off(reloc_rva), self.off(reloc_rva) + reloc_size
        while pos < end:
            page, size = struct.unpack_from("<II", d, pos)
            if size < 8:
                break
            for j in range((size - 8) // 2):
                entry = struct.unpack_from("<H", d, pos + 8 + 2 * j)[0]
                if entry >> 12 == 3:
                    self.relocs.add(self.base + page + (entry & 0xFFF))
            pos += size
        self.functions = {}
        for line in (listings / "functions.tsv").read_text().splitlines()[1:]:
            parts = line.split("\t")
            self.functions[int(parts[0], 16)] = int(parts[2])

    def off(self, rva):
        for va, size, ra, rs in self.sections:
            if va <= rva < va + size:
                return rva - va + ra
        return None

    def read(self, addr, n):
        o = self.off(addr - self.base)
        return self.data[o:o + n] if o is not None else b""

    def dword(self, addr):
        b = self.read(addr, 4)
        return struct.unpack("<I", b)[0] if len(b) == 4 else None

    _cs = None

    def signature(self, addr):
        """The function's bytes with relocated dwords and branch displacements
        (calls and jumps move with the code around them) zeroed."""
        n = self.functions[addr]
        body = bytearray(self.read(addr, n))
        if len(body) < n or n < 8:
            return None
        for i in range(n - 3):
            if addr + i in self.relocs:
                body[i:i + 4] = b"\0\0\0\0"
        if Image._cs is None:
            Image._cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            Image._cs.detail = True
        for insn in Image._cs.disasm(bytes(self.read(addr, n)), addr):
            if insn.group(X.X86_GRP_CALL) or insn.group(X.X86_GRP_JUMP) or insn.group(X.X86_GRP_BRANCH_RELATIVE):
                if insn.imm_offset and insn.imm_size:
                    o = insn.address - addr + insn.imm_offset
                    body[o:o + insn.imm_size] = b"\0" * insn.imm_size
        return hashlib.sha1(bytes(body) + n.to_bytes(4, "little")).digest()


def match_functions(a, b):
    """Base function -> other function, for signatures unique on both sides."""
    sa, sb = collections.defaultdict(list), collections.defaultdict(list)
    for f in a.functions:
        s = a.signature(f)
        if s:
            sa[s].append(f)
    for f in b.functions:
        s = b.signature(f)
        if s:
            sb[s].append(f)
    return {fa[0]: sb[s][0] for s, fa in sa.items() if len(fa) == 1 and len(sb.get(s, ())) == 1}


def branch_targets(image, addr):
    """(instruction index, target) of each relative call/jump in the function."""
    if Image._cs is None:
        image.signature(addr)
    out = []
    for i, insn in enumerate(Image._cs.disasm(bytes(image.read(addr, image.functions[addr])), addr)):
        if (insn.group(X.X86_GRP_CALL) or insn.group(X.X86_GRP_JUMP)) and insn.operands \
                and insn.operands[0].type == X.X86_OP_IMM:
            out.append((i, insn.operands[0].imm))
    return out


def map_by_calls(a, b, matches, target):
    """Votes for a function's counterpart: what the matched functions that call
    it call at the same instruction in the other executable."""
    votes = collections.Counter()
    for fa, fb in matches.items():
        ta = branch_targets(a, fa)
        if not any(t == target for _, t in ta):
            continue
        tb = dict(branch_targets(b, fb))
        for i, t in ta:
            if t == target and i in tb:
                votes[tb[i]] += 1
    return votes


def rtti_name(image, vtable):
    """The class name RTTI gives a vtable: [vtable-4] is its complete object
    locator, whose type descriptor (+12) holds the decorated name at +8."""
    col = image.dword(vtable - 4)
    td = image.dword(col + 12) if col else None
    if not td:
        return None, None
    raw = image.read(td + 8, 256).split(b"\0")[0]
    return raw.decode("latin-1"), image.dword(col + 4)


def map_vtable(a, b, target, matches=None):
    """The other executable's vtable for the same class and subobject: the one
    whose slots hold the matched counterparts of this vtable's functions
    (a subobject's offset may differ between the builds), with that offset."""
    name, offset = rtti_name(a, target)
    if not name or not name.startswith(".?A"):
        return None, name
    if matches is not None:
        best = None
        for vt, off in class_vtables(b, name):
            score = sum(1 for i in range(24)
                        if matches.get(a.dword(target + 4 * i)) == b.dword(vt + 4 * i))
            if score and (best is None or score > best[0]):
                best = (score, vt, off)
        if best:
            return best[1], "%s +%#x (was +%#x), %d slots agree" % (name, best[2], offset, best[0])
        # No slot agrees (the functions changed): the subobject at the same
        # place counting from the end, where a grown class keeps its order.
        mine, theirs = class_vtables(a, name), class_vtables(b, name)
        index = [v for v, _ in mine].index(target) - len(mine) if target in [v for v, _ in mine] else None
        if index is not None and -index <= len(theirs):
            vt, off = theirs[index]
            return vt, "%s +%#x (was +%#x), same place from the end" % (name, off, offset)
        return None, name
    needle = name.encode("latin-1") + b"\0"
    found = []
    for va, size, ra, rs in b.sections:
        chunk = b.data[ra:ra + rs]
        pos = chunk.find(needle)
        while pos >= 0:
            td = b.base + va + pos - 8
            # Complete object locators naming td, then vtables whose [-1] is one.
            for cva, csize, cra, crs in b.sections:
                cdata = b.data[cra:cra + crs]
                for j in range(0, len(cdata) - 20, 4):
                    if struct.unpack_from("<I", cdata, j + 12)[0] == td and \
                            struct.unpack_from("<I", cdata, j + 4)[0] == offset:
                        col = b.base + cva + j
                        packed = struct.pack("<I", col)
                        k = cdata.find(packed)
                        while k >= 0:
                            if k % 4 == 0:
                                found.append(b.base + cva + k + 4)
                            k = cdata.find(packed, k + 1)
            pos = chunk.find(needle, pos + 1)
    return (found[0] if len(found) == 1 else found or None), name


def class_vtables(b, name):
    """Every (vtable, subobject offset) RTTI gives class `name` in b."""
    needle = name.encode("latin-1") + b"\0"
    out = []
    for va, size, ra, rs in b.sections:
        chunk = b.data[ra:ra + rs]
        pos = chunk.find(needle)
        while pos >= 0:
            td = b.base + va + pos - 8
            for cva, csize, cra, crs in b.sections:
                cdata = b.data[cra:cra + crs]
                for j in range(0, len(cdata) - 20, 4):
                    if struct.unpack_from("<I", cdata, j + 12)[0] == td and \
                            struct.unpack_from("<I", cdata, j)[0] == 0:
                        col = b.base + cva + j
                        packed = struct.pack("<I", col)
                        k = cdata.find(packed)
                        while k >= 0:
                            if k % 4 == 0:
                                out.append((b.base + cva + k + 4, struct.unpack_from("<I", cdata, j + 4)[0]))
                            k = cdata.find(packed, k + 1)
            pos = chunk.find(needle, pos + 1)
    return out


def map_data(a, b, matches, target):
    """Votes for target's counterpart from relocated operands in matched functions."""
    votes = collections.Counter()
    for fa, fb in matches.items():
        n = a.functions[fa]
        for i in range(n - 3):
            p = fa + i
            if p in a.relocs:
                value = a.dword(p)
                if value is not None and target - 0x40 <= value <= target + 0x40:
                    other = b.dword(fb + i)
                    if other is not None:
                        votes[other + (target - value)] += 1
    return votes


def main():
    a, b = Image(Path(sys.argv[1])), Image(Path(sys.argv[2]))
    matches = match_functions(a, b)
    print("# %d of %d base functions matched uniquely" % (len(matches), len(a.functions)), file=sys.stderr)
    for text in sys.argv[3:]:
        addr = int(text, 16)
        if addr in a.functions:
            if addr in matches:
                print("%08x -> %08x  (function)" % (addr, matches[addr]))
            else:
                votes = map_by_calls(a, b, matches, addr).most_common(3)
                print("%08x -> %s  (function by its callers, votes %s)" % (
                    addr, "%08x" % votes[0][0] if votes else "?", ", ".join("%08x:%d" % v for v in votes)))
            continue
        vt, name = map_vtable(a, b, addr, matches)
        if name and name.startswith(".?A"):
            print("%08x -> %s  (vtable of %s)" % (addr, ("%08x" % vt) if isinstance(vt, int) else vt, name))
            continue
        votes = map_data(a, b, matches, addr)
        best = votes.most_common(3)
        print("%08x -> %s  (data, votes %s)" % (addr, "%08x" % best[0][0] if best else "?",
                                                ", ".join("%08x:%d" % v for v in best)))


if __name__ == "__main__":
    main()

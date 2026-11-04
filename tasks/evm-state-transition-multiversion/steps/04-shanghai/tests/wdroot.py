#!/usr/bin/env python3
"""wdroot.py -- EIP-4895 withdrawals root, computed from the EIP text.

EIP-4895: a `Withdrawal` is the RLP list `[index, validator_index, address,
amount]`, and

    withdrawals_root = compute_trie_root_from_indexed_data(withdrawals)

where `compute_trie_root_from_indexed_data` inserts each object into a
Merkle-Patricia trie keyed by its position in the list -- "constructed
identically to the transactions root", i.e. key = RLP(i).

Standard library only, apart from the vendored pure-Python keccak.

Nothing here reads a running go-ethereum.  `selftest()` re-derives the
`withdrawalsRoot` recorded in every withdrawals blockchain-test fixture header
from that fixture's own withdrawals list; that is what makes the roots this
module states for hand-authored lists trustworthy.
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from keccak import keccak256  # noqa: E402

EMPTY_ROOT = "0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"


# --------------------------------------------------------------------------
# RLP encoding
# --------------------------------------------------------------------------


def _len_header(n: int, offset: int) -> bytes:
    if n < 56:
        return bytes([offset + n])
    be = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([offset + 55 + len(be)]) + be


def rlp(item) -> bytes:
    if isinstance(item, (bytes, bytearray)):
        b = bytes(item)
        if len(b) == 1 and b[0] < 0x80:
            return b
        return _len_header(len(b), 0x80) + b
    if isinstance(item, int):
        return rlp(b"" if item == 0 else item.to_bytes((item.bit_length() + 7) // 8, "big"))
    if isinstance(item, (list, tuple)):
        payload = b"".join(rlp(x) for x in item)
        return _len_header(len(payload), 0xC0) + payload
    raise TypeError("cannot rlp-encode %r" % (type(item),))


# --------------------------------------------------------------------------
# hexary Merkle-Patricia trie, root only
# --------------------------------------------------------------------------


def _nibbles(b: bytes):
    out = []
    for x in b:
        out.append(x >> 4)
        out.append(x & 0x0F)
    return out


def _hp(nib, terminator: bool) -> bytes:
    flag = 2 if terminator else 0
    if len(nib) % 2:
        head = (flag + 1) * 16 + nib[0]
        rest = nib[1:]
    else:
        head = flag * 16
        rest = nib
    return bytes([head]) + bytes(rest[i] * 16 + rest[i + 1] for i in range(0, len(rest), 2))


def _ref(node):
    """A child is embedded when its own encoding is under 32 bytes, hashed
    otherwise -- the standard MPT reference rule."""
    enc = rlp(node)
    return node if len(enc) < 32 else keccak256(enc)


def _build(items, depth):
    """items: sorted [(nibbles, value)], all agreeing on nibbles[:depth]."""
    if len(items) == 1:
        nib, val = items[0]
        return [_hp(nib[depth:], True), val]

    # longest shared prefix past `depth` -> extension node
    end = depth
    while True:
        first = items[0][0]
        if end >= len(first):
            break
        c = first[end]
        if any(end >= len(n) or n[end] != c for n, _ in items):
            break
        end += 1
    if end > depth:
        return [_hp(items[0][0][depth:end], False), _ref(_build(items, end))]

    branch = [b""] * 17
    groups = {}
    for nib, val in items:
        if len(nib) == depth:
            branch[16] = val
        else:
            groups.setdefault(nib[depth], []).append((nib, val))
    for nib_val, grp in groups.items():
        branch[nib_val] = _ref(_build(grp, depth + 1))
    return branch


def trie_root(pairs) -> str:
    """pairs: [(key_bytes, value_bytes)] -> 0x-prefixed root hash."""
    if not pairs:
        return EMPTY_ROOT
    items = sorted((_nibbles(k), v) for k, v in pairs)
    return "0x" + keccak256(rlp(_build(items, 0))).hex()


# --------------------------------------------------------------------------
# EIP-4895
# --------------------------------------------------------------------------


def _qty(v) -> int:
    if isinstance(v, int):
        return v
    s = str(v).strip()
    return int(s, 16) if s.lower().startswith("0x") else int(s, 10)


def _addr(v) -> bytes:
    s = str(v).strip().lower()
    if s.startswith("0x"):
        s = s[2:]
    b = bytes.fromhex(s.rjust(40, "0"))
    if len(b) != 20:
        raise ValueError("not a 20-byte address: %r" % (v,))
    return b


def withdrawal_rlp(w) -> bytes:
    return rlp([_qty(w["index"]), _qty(w["validatorIndex"]),
                _addr(w["address"]), _qty(w["amount"])])


def withdrawals_root(withdrawals) -> str:
    """EIP-4895 `withdrawals_root` for a t8n `env.withdrawals` list."""
    return trie_root([(rlp(i), withdrawal_rlp(w))
                      for i, w in enumerate(withdrawals)])


# --------------------------------------------------------------------------
# self-test against the vendored fixtures (never against a running tool)
# --------------------------------------------------------------------------


def selftest(root_dir) -> int:
    ok = bad = 0
    for dirpath, _dirs, files in os.walk(root_dir):
        for fn in sorted(files):
            if not fn.endswith(".json"):
                continue
            try:
                doc = json.load(open(os.path.join(dirpath, fn)))
            except Exception:
                continue
            for _name, entry in (doc.items() if isinstance(doc, dict) else []):
                if not isinstance(entry, dict):
                    continue
                for blk in entry.get("blocks") or []:
                    hdr = blk.get("blockHeader") or {}
                    want = hdr.get("withdrawalsRoot")
                    rd = blk.get("rlp_decoded") or {}
                    wds = rd.get("withdrawals")
                    if wds is None:
                        wds = blk.get("withdrawals")
                    if want is None or wds is None:
                        continue
                    got = withdrawals_root(wds)
                    if got.lower() == str(want).lower():
                        ok += 1
                    else:
                        bad += 1
                        print("MISMATCH %s/%s  want=%s got=%s" % (dirpath, fn, want, got))
    print("wdroot selftest: %d match, %d mismatch" % (ok, bad))
    return 0 if bad == 0 and ok > 0 else 1


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--selftest":
        sys.exit(selftest(sys.argv[2]))
    # Accepts a bare withdrawals list or a whole t8n --input.env document, so
    # the roots frozen into probe/expect.json can be re-derived in place:
    #     python3 wdroot.py probe/env-many.json
    doc = json.load(open(sys.argv[1]))
    if isinstance(doc, dict):
        doc = doc.get("withdrawals") or []
    print(withdrawals_root(doc))

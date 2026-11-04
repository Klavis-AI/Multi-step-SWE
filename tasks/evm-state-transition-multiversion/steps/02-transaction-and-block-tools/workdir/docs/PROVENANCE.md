# Provenance of the EIP texts in this document set

Every `fork/<fork>/eip-<n>.md` file in this tree is an unmodified copy of the
corresponding `EIPS/eip-<n>.md` from the upstream EIPs repository. No file was
reformatted, truncated, or annotated; the `sha256` column below is the hash of
the file as shipped and is identical to the hash of the upstream blob.

The table covers the **9** EIP files this workspace ships, across
`fork/`: berlin, london. Later forks' EIP texts arrive with the
step that introduces them and are recorded in that step's copy of this file.

## Source

| field | value |
| --- | --- |
| repository | `https://github.com/ethereum/EIPs` |
| commit | `dbc6d457cbf90d5f9d55552f3fd91d58b88cc898` |
| commit date | `2026-09-18T04:41:31-06:00` |
| clone date (UTC) | `2026-09-20T07:18:57Z` |
| clone command | `git clone --depth 1 https://github.com/ethereum/EIPs` |
| repository licence | CC0-1.0, copied verbatim to `LICENSES/EIPS-LICENSE.md` |

## Licence status

At this commit the EIPs repository does **not** carry a `license:` key in EIP
front matter. EIP-1 instead requires every EIP to carry a copyright waiver in
its body: "All EIPs must be in the public domain. The copyright waiver MUST
link to the license file and use the following wording:
`Copyright and related rights waived via [CC0](../LICENSE.md).`"

The table therefore records two columns instead of one:

- `front-matter license` — the value of the YAML `license:` key, or `(absent)`.
  It is `(absent)` for all 9 files at this commit.
- `body waiver` — `CC0` when the body contains the EIP-1 mandated
  `## Copyright` waiver naming CC0, `(none)` when it contains no waiver.

8 of the 9 files declare CC0 in the body. **`fork/berlin/eip-155.md`
declares no licence at all** — it was created 2016-10-14, before EIP-1 made the
waiver mandatory, and its text has not been revised since. It is covered by the
repository-level CC0-1.0 `LICENSE.md` but carries no per-file statement. This
is an upstream property of the file, not an omission in this copy.

## Files

| fork | file | eip | status | front-matter license | body waiver | bytes | sha256 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| berlin | `fork/berlin/eip-155.md` | 155 | Final | (absent) | (none) | 3607 | `3c208813585d8fca67a92d908ece47402f87a18a14f1b20ee5ebcb3e702dcc9c` |
| berlin | `fork/berlin/eip-2565.md` | 2565 | Final | (absent) | CC0 | 6093 | `a3a75622403e775d4e6fa0b298545d0e93fe5f7d92ffcebe8404ee9570651d7d` |
| berlin | `fork/berlin/eip-2718.md` | 2718 | Final | (absent) | CC0 | 6825 | `876de14066ad67c9a1c0a87dc3bd18695325dcfc7d4b8326734de0bc174e4bf4` |
| berlin | `fork/berlin/eip-2929.md` | 2929 | Final | (absent) | CC0 | 17374 | `177b2040bf0376b1182eb5571c7fda7a41b6f594083fa816506d65cfcf2acc4a` |
| berlin | `fork/berlin/eip-2930.md` | 2930 | Final | (absent) | CC0 | 8313 | `691f06ecd80a1f5ec71f169553b808c837f68e06860ffac68dd44649e93b804b` |
| london | `fork/london/eip-1559.md` | 1559 | Final | (absent) | CC0 | 21053 | `4460ad966a22d8501e12d75165c2c784c61748d8128229f115ebac1bfccac59f` |
| london | `fork/london/eip-3198.md` | 3198 | Final | (absent) | CC0 | 2470 | `7cb24d49e35a2c2fd7a8cc1fae9df390cf6ff62dbe7410a044c9db34568524cc` |
| london | `fork/london/eip-3529.md` | 3529 | Final | (absent) | CC0 | 12429 | `0446476577d3ebfd307e4524b565d0868b7aa2f078911908e5383ebff1ad6f3b` |
| london | `fork/london/eip-3541.md` | 3541 | Final | (absent) | CC0 | 4900 | `dff418f1ed441352ee656eb5ba1d96c12acdefc788251e4d63149cfb442fba07` |

## Supporting files

| file | source | sha256 |
| --- | --- | --- |
| `LICENSES/EIPS-LICENSE.md` | upstream `LICENSE.md` | `a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499` |

## Verifying this record

```sh
# from this docs/ directory: every row must re-hash to the value above
awk -F'`' '/^\| [a-z]+ \| `fork\//{print $4"  "$2}' PROVENANCE.md | sha256sum -c
```

# Provenance of the EIP texts in this document set

Every `fork/<fork>/eip-<n>.md` file in this tree is an unmodified copy of the
corresponding `EIPS/eip-<n>.md` from the upstream EIPs repository. No file was
reformatted, truncated, or annotated; the `sha256` column below is the hash of
the file as shipped and is identical to the hash of the upstream blob.

The table covers the **29** EIP files this workspace ships, across
`fork/`: berlin, london, merge, shanghai, cancun, prague. Later forks' EIP texts arrive with the
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
  It is `(absent)` for all 29 files at this commit.
- `body waiver` — `CC0` when the body contains the EIP-1 mandated
  `## Copyright` waiver naming CC0, `(none)` when it contains no waiver.

28 of the 29 files declare CC0 in the body. **`fork/berlin/eip-155.md`
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
| merge | `fork/merge/eip-3675.md` | 3675 | Final | (absent) | CC0 | 22848 | `7345c394b8726383a667b3e5114fa0916410c95de3c1a031663a8e07cbe40e03` |
| merge | `fork/merge/eip-4399.md` | 4399 | Final | (absent) | CC0 | 12136 | `3cabb6092e244042e56e28b9ce3a2920d8244cf65299c5a99ed7c8e20aeccf60` |
| shanghai | `fork/shanghai/eip-3651.md` | 3651 | Final | (absent) | CC0 | 1828 | `dfb73b4e9122cb490dde61309a995defbe7428209f5cae20db1ef05d6ee759f4` |
| shanghai | `fork/shanghai/eip-3855.md` | 3855 | Final | (absent) | CC0 | 3490 | `07d51ed796b48c67e4b431125404de4fcc55dd5ec62b91ac70f4e46644e25055` |
| shanghai | `fork/shanghai/eip-3860.md` | 3860 | Final | (absent) | CC0 | 8442 | `c28b1da260cde50902ea7dcff35fb15b394131d2fa76ff35beaa797f487b44fc` |
| shanghai | `fork/shanghai/eip-4895.md` | 4895 | Final | (absent) | CC0 | 7950 | `f7766f0bd9bb14f76479c01e77388fd1a8823cb17c2919a317d51038a9e1be69` |
| cancun | `fork/cancun/eip-1153.md` | 1153 | Final | (absent) | CC0 | 16925 | `c0e7a2e793f3fa44f048b772a3fb4779199d29b6d240c97e3723ace1e46e1c40` |
| cancun | `fork/cancun/eip-4788.md` | 4788 | Final | (absent) | CC0 | 10962 | `491e6afa5695e0f9a5df1e0a2009cc928a5bd938aab8b39e6187b8127c7810d2` |
| cancun | `fork/cancun/eip-4844.md` | 4844 | Final | (absent) | CC0 | 24604 | `2772bdb675d90d89ebb4bf74269e48c8ea46745e78161c8574ffa78328b31885` |
| cancun | `fork/cancun/eip-5656.md` | 5656 | Final | (absent) | CC0 | 7124 | `c4d74510c645c13d8f3daace25f3bfa061e5f906bd088d14faf735327b6a4a0b` |
| cancun | `fork/cancun/eip-6780.md` | 6780 | Final | (absent) | CC0 | 5908 | `3941eed8379cc93abb7342c5b186bd883e8b1518b0025d94a49be068f8ea2101` |
| cancun | `fork/cancun/eip-7516.md` | 7516 | Final | (absent) | CC0 | 2888 | `91ec406457498fecbbbe27ec801963b2b8de1099fdd3ded7ba7950fad8b4e2f4` |
| prague | `fork/prague/eip-2537.md` | 2537 | Final | (absent) | CC0 | 23215 | `7071929b494198ebe39125c190f3442edb3c55192135b62ed2795072b87e9e53` |
| prague | `fork/prague/eip-2935.md` | 2935 | Final | (absent) | CC0 | 11019 | `3237f773d610420fb50438e690c9317e0f13244cb29f8f8dc357d179f1f89103` |
| prague | `fork/prague/eip-6110.md` | 6110 | Final | (absent) | CC0 | 14823 | `5e3d603bbf83dd7da977825475c2f0214704bc64a1e6426cbba6421344211962` |
| prague | `fork/prague/eip-7002.md` | 7002 | Final | (absent) | CC0 | 28503 | `33c151fb3589f3e09fa7f3dcf6fe897f982c7c0e95fc17baa00e14776986b1ee` |
| prague | `fork/prague/eip-7251.md` | 7251 | Final | (absent) | CC0 | 27722 | `2a29b8d474d92c04b077f7acdb40da0cb5ba3e2e78e1e8be94c862a77c58ea5a` |
| prague | `fork/prague/eip-7623.md` | 7623 | Final | (absent) | CC0 | 6342 | `b991940be17e3265684047d49b0f5758742c3873b88efaa9aa30e4229ea92c33` |
| prague | `fork/prague/eip-7685.md` | 7685 | Final | (absent) | CC0 | 5336 | `99b3272f275293fd5e9af5f45d673132a909dbf28816a7ea7804c247485622a2` |
| prague | `fork/prague/eip-7702.md` | 7702 | Final | (absent) | CC0 | 29313 | `3d46953e13ebc7ebe229971b301ede853aad6e29f4faf4db1eb9a2085accde1b` |

## Supporting files

| file | source | sha256 |
| --- | --- | --- |
| `LICENSES/EIPS-LICENSE.md` | upstream `LICENSE.md` | `a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499` |

## Verifying this record

```sh
# from this docs/ directory: every row must re-hash to the value above
awk -F'`' '/^\| [a-z]+ \| `fork\//{print $4"  "$2}' PROVENANCE.md | sha256sum -c
```

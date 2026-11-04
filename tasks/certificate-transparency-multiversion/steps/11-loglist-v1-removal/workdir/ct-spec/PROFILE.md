# PROFILE — step 11 of 13: log-list v1 removal (certificate-transparency-go v1.1.4)

## What this milestone adds over v1.1.3

Almost nothing. This is a withdrawal, and the only release in the series where
the interesting question is not *what* is removed but *what the survivors are
retyped to*.

**The removal.** The v1 log-list package goes. Flat schema, `Logs` and
`Operators` joined by integer ids, `final_sth` and `disqualified_at` doing the
work of a life-cycle — deleted, with the bundled list-signing public key and
with `findlog`, the command that only ever searched it. After this release the
import path must not resolve and must not be a build target.

**The migration, and the step it skips.** The v1 dependants move to **v3**, not
to v2. Four are named individually by the release — the `ctclient` command
tree, `ctutil`'s `LogInfo` entry points, `sctscan` and `ct_hammer` — and
`sctcheck` comes along with `ctutil`. In the same release every remaining v2
consumer is retyped onto v3 as well: `ctpolicy` and `submission`, whose
interfaces change shape with it — `LogsByGroup`, `NewDistributor` and
`LogClientBuilder` are all declared over the v3 types — and the two witness
tools, `feeder` and `config`, which simply swap parsers: their
`--log_list_url` already defaulted to a v3 document, so only what reads it
changes. Flag defaults follow the types elsewhere: the `--log_list` defaults
and the submission server's `--loglist_path` take the v3 constants.

**What survives with nobody left.** The v2 package is *not* removed here. It
keeps compiling and keeps its exported surface, and no code in the product
imports it any more. That is the state this milestone is supposed to end in.

**Everything else is housekeeping.** glog is replaced by klog across the
product: the exit statuses are unchanged, the logger's flag names are not, and
a program that never installs the logger's flag set ends up carrying no logging
flags at all. `tools.go` becomes `tools/tools.go`, a build-tagged package that
produces no binary. `migrillian` stops asking for a consistency proof when the
source root is size zero, because the empty tree is consistent with every root.
The YAML library the witness configuration tool marshals with moves up a major
version along with the rest of the tree, which changes the indentation of the
block sequences it writes — and that file is compared byte for byte, so the
change is not cosmetic here (CLI.md §5.1).

## Reading route through ct-spec/

1. **CLIENT-API.md** §4.4 — the removal and where each consumer lands. This is
   the milestone.
2. **CLIENT-API.md** §4.1 and §4.2 — the schema you are deleting, and the one
   you must *not* migrate to.
3. **CLIENT-API.md** §4.3 — the v3 schema, its types and its selection helpers.
4. **CLIENT-API.md** §2.1 — `ctutil`'s `LogInfo` entry points and the argument
   type that changes under them.
5. **CLIENT-API.md** §7.1 and §7.2 — `ctpolicy` and `submission` over v3.
6. **CLI.md** §1 and §4.5 — the inventory, one program shorter.
7. **CLI.md** §3.2, §4.4, §4.9 — the flag defaults that move with the package,
   and what `ctclient` does with a `--log_name` that matches no log or more
   than one.
8. **CLI.md** §4.10 and §5.1 — the witness programs, which read a log list too,
   and the exact YAML the configuration tool writes.
9. **CLI.md**, the flag-grammar and exit-status preamble — the logging swap.
10. **MERKLE.md** §3 — the empty-tree case.
11. **RELEASE-MAP.md** §11 and §14 — the milestone on one screen, and the
    directory listing you can check yourself.

## How you will know you are done

A program importing the v1 log-list path does not compile, and the compiler
names that path; the v1 command is not a build target; the v2 package still
compiles and nothing imports it; `ctutil`'s log-info constructors take v3
types; a Chrome and an Apple policy both satisfy the policy interface declared
over a v3 list; the distributor is built from a v3 list; the witness config
tool turns a v3 list into the exact expected bytes; everything else that
worked at v1.1.3 still works.

# Workspace handoff — step 13

You are continuing the workspace step 12 left behind. Nothing has
been reset, removed or fixed up for you.

## What is already there

The previous step left an HTTP/3 console debugger as a workspace member, a
`set_max_idle_timeout` on `Connection` as well as on `Config`, bounded tracking
of unknown transport parameters, additional header sections in HTTP/3, and an
`h3::Stats` with QPACK byte counters. All of it stays, and all of it now has to
keep working through a connection type that carries a type parameter.

The workspace gains four members. Whatever builds the workspace must build all
nine, and three of the four new ones are small libraries with no binary. The
root manifest is no longer only a member list: members inherit their version,
edition, licence and dependency versions from it, so a member whose manifest
says `workspace = true` for a dependency has no version of its own to read.

`apps` is unchanged, and its two binaries are still named `quiche-client` and
`quiche-server`. That matters more here than at any previous step: they are now
built in a workspace whose dependency graph is several times larger, and they
are the only thing in the package that proves the whole of it still works.

## What must keep working

This step is scored partly on milestones you already passed. The
inherited groups are:

* `reg-05-recvinfo-from` — step 05's `cap-recvinfo-from`, re-run unchanged: Carried as this step's plan-mandated re-check of a step-5 hot-path observation. `RecvInfo` is the descriptor every inbound datagram is delivered with, and it is on the receive path of every connection in the package. This step replaces the type that path lives on -- `Connection` becomes `Connection<F>` -- and adds a second, independent stack on top of it, so if either of those rebuilt the receive path, the address descriptor is where it shows first. Two halves: * **The type.** `RecvInfo { from, to }` values compare equal to themselves and unequal when either field differs. Step 05 asserted this over a single-field `RecvInfo { from }`; the type gained `to` at a later release, so the carried assertion is widened to both fields rather than dropped. That is the only adaptation, and it is an adaptation of the *same* observation. * **The hot path.** A 4 KiB `client_to_server` transfer, and then, over every `SendInfo` the exchange produced at both ends, `info.to` is the address the datagram was actually addressed to -- the server's address for client-side sends, the client's for server-side sends. **Deliberately clock-free.** Step 05's sibling observation `cap-sendinfo-at` read `SendInfo::at`, a wall-clock instant, and was removed from the gate at steps 07 and 11 under the determinism rule: a gate group is an unpriced veto, so anything in it that could read differently under load costs every candidate the whole step. It stays removed here, and nothing in this case reads a clock, a duration or a timer.
* `reg-10-bbr2` — step 10's `cap-bbr2`, narrowed to its deterministic half: Carried because this step adds a fifth entry to the very enum that group was about. The failure mode it guards is specific and likely: appending a variant is the moment a name table, a discriminant or an exhaustive match gets rewritten, and a rewrite that gets the *new* entry right while disturbing an old one would otherwise be scored only by the group that was looking at the new entry. * **Names.** `"bbr2"` maps to `Ok(())` and selects `BBR2` -- not the gcongestion variant, and not a default. `"BBR2"`, `"bbr2 "`, `"bbr3"` and `""` are each `Err(Error::CongestionControl)`, as they were at step 10: the lookup does not case-fold, does not trim, and does not fall through. `reno`, `cubic` and `bbr` still resolve. * **Discriminants.** `(Reno, CUBIC, BBR, BBR2)` as `u32` is still `(0, 1, 2, 3)`. The new variant is checked where it belongs, in `cap-gcongestion-bbr2`; what this group asserts is that adding it moved nothing. * **That it still carries data.** Both selection routes -- by name and by variant -- are handshaked and each carries a 256 KiB transfer that arrives byte-for-byte with its fin, and `stats().sent_bytes` is at least the payload length. The `sent_bytes` bound is one-sided on purpose. An exact figure depends on how the send path chunked the transfer and on retransmission, neither of which a gate group may assert; "at least what we handed it" is true of every correct implementation and false of one that dropped the data.
* `reg-08-probe-and-migrate` — step 08's `cap-probe-and-migrate`, narrowed to its deterministic half: The end-to-end connection-migration path, run between the candidate's own `quiche-server` and `quiche-client`. The client is given `--no-verify` and `--perform-migration` and asked for a 512 KiB file. It must exit 0 within the group's deadline and write the file out byte-for-byte. That is the whole assertion. Carried because migration exercises path validation, connection-ID retirement and the address descriptors on both the send and receive paths at once, and because this step rebuilds the type those paths hang off. It is also the only gate group other than `gate-endtoend` that runs the two real binaries, so it is what would notice a workspace that builds but whose `apps` member no longer links. **The socket-sampling leg is deliberately absent.** Step 10's copy of this group counted locally bound UDP ports and required at least two at once, to witness the migration rather than merely its result. That leg was removed upstream under the determinism rule -- the sample is a race against the client's own socket lifetime -- and it stays removed. What survives is the one-sided, deterministic half: a migrating client that completes its transfer. The case takes the harness's T1 serial guard.

They are re-runs, not new work. They are here because this step's
change plausibly breaks them.


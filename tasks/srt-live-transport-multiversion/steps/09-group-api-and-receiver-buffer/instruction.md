# Step 9 — The socket group API with a minimum link stability timeout, over a rewritten receiver buffer

## 1. Implement this milestone

Add the socket group API in its settled shape, and rebuild the receiver buffer
underneath it. The previous milestone already carried a live stream over a
group of member links and added the main/backup mode; this one fixes the API
that programs write against and replaces the machinery that decides when a
received packet becomes readable.

The group API:

* `srt_create_group` takes a group type and returns a group id that the rest of
  the API accepts wherever a socket id is accepted — `srt_connect_group`,
  `srt_sendmsg2`/`srt_recvmsg2`, `srt_getsockflag`/`srt_setsockflag`,
  `srt_getsockstate`, `srt_bstats` and `srt_epoll_add_usock`. That relation does
  not run the other way: `srt_connect_group` takes a group id and only a group
  id, and refuses a plain socket id;
* the group type enumeration names exactly three things, undefined, broadcast
  and backup, so its end marker is 3. The balancing and multicast types the
  previous milestone reserved are withdrawn, and nothing is renumbered around
  them;
* `srt_include`, `srt_exclude` and `srt_group_configure` are withdrawn with
  them. A member link joins a group only by being named in the
  `SRT_SOCKGROUPCONFIG` array handed to `srt_connect_group`, and its private
  settings travel in that array; `srt_create_config`, `srt_config_add` and
  `srt_delete_config` are unchanged. `srt_prepare_endpoint` and
  `SRT_SOCKGROUPCONFIG` are part of this milestone's own surface — make sure
  both are declared and working in the header you install, whatever your
  inherited tree says about them;
* a listener with group connection enabled returns a group id from
  `srt_accept`, not a member socket id.

The minimum link stability timeout, `SRTO_GROUPMINSTABLETIMEO`:

* takes over the position and the meaning of the previous milestone's stability
  timeout under a new name and in milliseconds rather than microseconds. The
  old name is not a second option — both spellings refer to one position —
  and whether you keep it declared as an alias enumerator is not measured
  either way;
* defaults to 60 ms, refuses anything below 60, and refuses anything strictly
  above the peer idle timeout in force — a value equal to it is accepted;
* is readable as well as settable, on a plain socket and on a group;
* is a pre-connect option that the socket layer now enforces rather than merely
  documents, so a listening socket refuses it.

The receiver buffer is rewritten. A packet becomes readable when its
time-stamp-based delivery time arrives, and the buffer itself — not the
acknowledgement path — is what decides that. The receiver's statistics are
produced from the new buffer's counters, and §5 of `TEST_INTERFACE.md` records
the three places where the counters this release actually produces and the
counters `srt-docs/` describes do not agree. Where §5 is silent, `srt-docs/`
remains the authority for every name, default and documented constant, and the
drafts remain the authority for the wire format.

## 2. What you inherit

Milestone 08 established socket groups carrying a live stream over member
links, the main/backup group mode with its stability timeout, declarative
per-member configuration objects, the full socket-option restriction matrix,
strict encryption with key-material state reporting, packet filtering, TSBPD
with too-late packet drop, and selective-repeat recovery over a lossy channel.
This milestone rewrites the receiver buffer under all of it, so all of it must
still work: a dropped burst is still retransmitted and delivered complete, a
message is still held until the negotiated latency has elapsed and dropped when
it is later than that, and encryption still carries the payload intact at both
key lengths while a mismatched passphrase is still refused at connect time.

`/app/WORKSPACE_HANDOFF.md` states it precisely, including what this milestone
is required **not** to break.

## 3. How this step is checked

`/app/TEST_INTERFACE.md` lists every group by name. This step has 4 **gate**
groups that check the milestones you inherited are still intact, and 10
**behaviour** groups covering this milestone's own work. All of them are in
scope.

The gate must hold: break something an earlier milestone established and this
step is not satisfied, however much of the new work you finished. Preserving
what you inherited and implementing nothing new does not satisfy this step
either. Every behaviour group is required; a group you do not reach is a gap in
the step.

One thing qualifies that, and it is worth knowing before you plan the work: the
drivers are built as one test binary per driver *family*, and every requirement in
a family depends on the family compiling — a family that does not compile
affects every group in it, not only the group whose capability is missing. That
is why section 5 of `TEST_INTERFACE.md` marks every public name
it lists either **required to compile** or optional. A name marked required is
one a driver references without a fallback guard, so a tree that does not
declare it affects the whole family; a name marked optional is guarded, and
missing it affects only the group that exercises it. Which names fall on which
side is measured from the drivers each time this document is generated, so a
section 5 that marks nothing as required means this milestone introduces no
unguarded name -- not that the distinction does not apply.

Everything is measured through the installed public interface: `/app` is
built with

```
cmake -S /app -B <build> -DCMAKE_BUILD_TYPE=Release -DENABLE_APPS=OFF -DENABLE_BONDING=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local && make -C <build> -j4 \
  && make -C <build> install DESTDIR=<dest>
```

and then separate programs are compiled against `<dest>/usr/local`, including
only `<srt/srt.h>`, and linked with `-lsrt` together with the system thread and
dynamic-loader libraries -- nothing else of yours. If that build or that install does not
produce `include/srt/srt.h` and `lib/libsrt.so*`, no group can run.

`/app/PROFILE.md` §2 lists what is out of scope for the whole task, §5 lists
the values the specifications leave open and what this task does about them,
and §6 states the determinism requirement.

## 4. Documentation

- `/app/srt-spec/draft-sharabayko-srt-01.txt` — the normative SRT wire specification.
- `/app/srt-spec/draft-gg-udt-03.txt` — the UDT specification the core derives from.
- `/app/srt-docs/` — upstream API documentation pinned to this milestone; see
  `srt-docs/PROVENANCE.md` for which release each file comes from.

## 5. Time

Run `/app/timer.sh` to see how much of this milestone's budget is left.

## 6. Constraints

This environment has no network access, and everything the build needs is already
installed. The upstream project's source, its releases, mirrors and package
registries are unreachable, so a copy cannot be fetched by any route.

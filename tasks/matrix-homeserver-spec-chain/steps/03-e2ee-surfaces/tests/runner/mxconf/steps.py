"""Per-step field exclusions.

A recorded response may carry a field that this step must not be graded on:

* the pinned reference implements a later proposal than the specification
  release this step targets, or
* the field belongs to a module a later step introduces, so requiring it here
  would leak that step, or
* the specification release marks it optional and this step's instruction does
  not require it.

Each entry is a dotted suffix: `unsigned.membership` excludes an event's
`unsigned.membership` without touching an `m.room.member` content's
`membership`.  Every exclusion is written as the *last step it applies to*, so
the step whose release first publishes the field starts requiring it and no
later step can silently keep letting it through.
"""

# The specification release each step targets.  The comments below refer to it;
# nothing computes with it.
RELEASE = {1: "v1.1", 2: "v1.2", 3: "v1.3", 4: "v1.6",
           5: "v1.9", 6: "v1.11", 7: "v1.13", 8: "v1.15"}

LAST = 8

# field -> (last step it is excluded at, why)
_FIELDS: list[tuple[str, int, str]] = [
    # ---- the reference runs ahead of the release --------------------------
    ("m.topic", 7,
     "MSC3765 rich topics; the specification adds `m.topic` in release v1.15."),
    ("unsigned.membership", 5,
     "MSC4115; the specification adds `unsigned.membership` in release v1.11."),

    # ---- the reference emits what no release ever defines ------------------
    ("unsigned.prev_sender", LAST,
     "`UnsignedData` never declares it, in any release."),
    ("unsigned.replaces_state", LAST,
     "As above: the reference's own, in every release."),
    ("historical", LAST,
     "The reference puts a `historical` key in its default power levels that no "
     "release's m.room.power_levels schema defines."),
    ("m.call.invite", LAST,
     "Likewise a default the reference raises to 50 and no schema names."),
    ("summary", LAST,
     "The room summary is optional in every release these steps target and no "
     "step's instruction asks for it."),
    ("unsigned.age", LAST,
     "`age` is optional in every release: a server that sends only "
     "`origin_server_ts` is conforming."),

    # ---- a later step's module --------------------------------------------
    ("device_one_time_keys_count", 2,
     "The end-to-end-encryption module: step 3 introduces it, and requiring it "
     "earlier would leak that step."),
    ("device_unused_fallback_key_types", 2, "As above."),
    ("device_lists", 2, "As above."),
    ("unread_thread_notifications", 3,
     "Threaded notification counts arrive with release v1.4, at step 4; before "
     "that a server counts per room only."),
]

EXCLUDED: dict[int, list[str]] = {
    step: [name for name, last, _ in _FIELDS if step <= last]
    for step in range(1, LAST + 1)
}


def excluded(step: int) -> list[str]:
    return list(EXCLUDED.get(step, []))


# Event types a derived list must leave out at this step.
#
# `m.room.guest_access`: the `POST /createRoom` preset table says `public_chat`
# sets `guest_access: forbidden`, but the pinned reference homeserver creates no
# such event for that preset (it relies on the default).  Oracle and
# specification disagree, in every release, so the event is not scored: a
# candidate that follows the table and one that follows the reference both pass.
EXCLUDED_STATE_TYPES: dict[int, set[str]] = {
    step: {"m.room.guest_access"} for step in range(1, LAST + 1)
}


def keep_types(step: int, types):
    """Drop the event types this step does not score from a derived list."""
    drop = EXCLUDED_STATE_TYPES.get(step, set())
    return [t for t in types if t not in drop]


# Headers the pinned reference sets that the spec release this step targets does
# not define.
_HEADERS: list[tuple[str, int, str]] = [
    ("cross-origin-resource-policy", 3,
     "MSC3828 adds Cross-Origin-Resource-Policy to the media repository in "
     "release v1.4, at step 4."),
    ("content-disposition", 6,
     "Content-Disposition on media downloads is specified from release v1.12 "
     "(MSC2701/MSC2702), at step 7; before that the header is the server's own "
     "choice."),
]

EXCLUDED_HEADERS: dict[int, set[str]] = {
    step: {name for name, last, _ in _HEADERS if step <= last}
    for step in range(1, LAST + 1)
}


def excluded_headers(step: int) -> set[str]:
    return set(EXCLUDED_HEADERS.get(step, set()))

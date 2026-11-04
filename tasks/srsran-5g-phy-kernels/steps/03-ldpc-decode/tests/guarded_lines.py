#!/usr/bin/env python3
"""Print the lines of a file that are unreachable in a build without AVX-512.

An AVX-512 block behind a positive AVX-512 guard -- `#if defined(__AVX512F__)`,
`#ifdef HAVE_AVX512` -- is dead in a configuration that targets AVX2, so the
envelope check does not judge it. Only what the pinned configuration would
actually compile is judged.

Polarity matters and is not a formality, in both directions and in both
spellings. `#if !defined(__AVX512F__)` and `#ifndef __AVX512F__` are TRUE on
this host, so their bodies are LIVE and must be judged, and the `#else` arms of
an AVX-512 guard are live too, for the same reason. Treating any line that
merely mentions AVX-512 as dead would let a candidate hide a threading pragma
inside `#ifndef __AVX512F__` and have the scan skip it -- and it did, until the
`#ifndef` spelling was taught the same polarity as `!defined`.

The guard's own condition line is reported as dead whenever it names an
AVX-512 macro, in either polarity: a condition is a compile-time test, not a
construct. `#ifndef __AVX512F__` is what upstream itself writes to *require*
AVX-512 for a block, and flagging the test itself would fail a candidate for
the spelling; the block under it is judged by the polarity above.

Nesting-aware.
"""
import sys

TRIGGERS = ("__AVX512", "HAVE_AVX512")


def negated(cond):
    """Drop `!defined(X)` and `!X` terms, so what remains is what the guard
    requires to be *present* for its body to be compiled."""
    out = []
    i = 0
    while i < len(cond):
        if cond[i] == "!":
            j = i + 1
            if cond.startswith("defined", j):
                j += len("defined")
                while j < len(cond) and cond[j] in " \t":
                    j += 1
                if j < len(cond) and cond[j] == "(":
                    depth = 0
                    while j < len(cond):
                        if cond[j] == "(":
                            depth += 1
                        elif cond[j] == ")":
                            depth -= 1
                            if depth == 0:
                                j += 1
                                break
                        j += 1
            else:
                while j < len(cond) and (cond[j].isalnum() or cond[j] == "_"):
                    j += 1
            i = j
            continue
        out.append(cond[i])
        i += 1
    return "".join(out)


def classify(st):
    """(mentions, consequent_dead) for an `#if`-family line.

    `mentions` is True when the condition names an AVX-512 macro in either
    polarity -- that is what makes the condition line itself unjudged.
    `consequent_dead` is True only when the body compiles only if AVX-512 is
    present, which is what makes the body dead here.
    """
    if st.startswith("#ifndef"):
        cond = st[len("#ifndef"):]
        # The body requires the macro to be ABSENT: live in this build.
        return any(t in cond for t in TRIGGERS), False
    if st.startswith("#ifdef"):
        cond = st[len("#ifdef"):]
        hit = any(t in cond for t in TRIGGERS)
        return hit, hit
    cond = st[len("#if"):] if st.startswith("#if") else st
    return any(t in cond for t in TRIGGERS), any(t in negated(cond) for t in TRIGGERS)


def main(path):
    try:
        lines = open(path, errors="replace").read().split("\n")
    except OSError:
        return
    # Stack of (dead_arm, condition_mentions_avx512, seen_else). A region is
    # dead while its innermost guard's current arm is the AVX-512 one.
    stack = []
    dead = False
    for line in lines:
        st = line.lstrip()
        is_if = st.startswith("#if ") or st.startswith("#ifdef ") or st.startswith("#ifndef")
        if is_if:
            mentions, arm_dead = classify(st)
            stack.append([arm_dead, mentions, False])
            dead = any(f[0] for f in stack)
            if dead or mentions:
                print(line)
            continue
        if st.startswith("#else") and stack:
            frame = stack[-1]
            if frame[1]:                    # this guard is an AVX-512 one
                frame[0] = not frame[0]     # the alternative arm flips polarity
            frame[2] = True
            dead = any(f[0] for f in stack)
            if dead:
                print(line)
            continue
        if st.startswith("#elif") and stack:
            frame = stack[-1]
            mentions, arm_dead = classify("#if" + st[len("#elif"):])
            if mentions:
                # An elif arm that itself names AVX-512 follows its own
                # polarity; one that does not is live whenever the guard is
                # one AVX-512 can satisfy, and judged (conservatively) otherwise.
                frame[0] = arm_dead
            elif frame[1]:
                frame[0] = False
            dead = any(f[0] for f in stack)
            if dead or mentions:
                print(line)
            continue
        if dead:
            print(line)
        if st.startswith("#endif") and stack:
            stack.pop()
            dead = any(f[0] for f in stack)


if __name__ == "__main__":
    main(sys.argv[1])

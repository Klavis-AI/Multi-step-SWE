#!/usr/bin/env bash
# Shared scoring engine for every step of pcre2-regex-engine-multiversion.
#
# reward = gate x score
#   gate  in {0,1}: proof of life plus one narrow deterministic case per
#         inherited milestone. Earns nothing; vetoes everything.
#   score in [0,1]: the weighted fraction of the SCORED groups that pass.
#         Weights are carried in manifest.tsv and sum to 1.0 over scored
#         groups only.
#
# report() in helpers.sh then writes the binary step reward to reward.json:
# 1 only when gate x score is exactly 1.
#
# Every group is a single pass/fail assertion made at the process boundary:
# run the candidate's pcre2test over an input file and diff its output against
# the expected file byte for byte. There is no partial credit inside a group.
set -u

PC_BUILD_TIMEOUT="${PC_BUILD_TIMEOUT:-900}"
PC_CASE_TIMEOUT="${PC_CASE_TIMEOUT:-120}"
PC_WORK=/tmp/pcre2-verify
PC_LOG=/logs/verifier
# Cases run here rather than in /tests/corpus. The serialization test -- number
# 19 at 10.20, renumbered 20 from 10.22 on -- writes its `#save` output into the
# working directory, and /tests/corpus is root-owned and not writable by the
# score user, so those cases would exit 1 there. The fixtures are symlinked
# in, so a case still reads the real file by name and still resolves
# `#loadtables ./testbtables` against its working directory, while anything it
# writes lands in scratch that is thrown away.
PC_CWD="$PC_WORK/cwd"

pc_gate_total=0
pc_gate_passed=0
pc_gate_json=""
pc_score_json=""
pc_score_accum=0
pc_weight_accum=0
pc_card=""

pc_say() { printf '%s\n' "$*" >&2; pc_card="${pc_card}$*
"; }

# Detail strings are snippets of the candidate's own pcre2test output, so they
# routinely carry bytes that are not valid UTF-8 -- a diff of a UTF-16 test
# transcript, a malformed-UTF error case, a byte from a binary subject. Emitted
# raw they make reward_details.json undecodable, which matters precisely when
# it is most needed: it is the file that says whether the gate passed and
# whether every scored group was zero. High bytes are dropped rather than
# escaped because this is a diagnostic snippet, not the comparison -- the
# comparison already happened, byte for byte, against the expected file.
pc_json_escape() {
    printf '%s' "${1-}" \
        | LC_ALL=C tr -d '\000-\037\177-\377' \
        | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

# pc_prepare_cwd -- a writable working directory that looks like /tests/corpus.
#
# Symlinks, not copies: the corpus runs to tens of megabytes per step and is
# read identically by every case, while the only thing a case ever writes is a
# handful of serialized-pattern files. The directory is owned by the score user
# and mode 0755, which keeps it clear of both fs.protected_regular and
# fs.protected_symlinks -- in a sticky world-writable directory the kernel would
# refuse to let the score user follow root-owned symlinks.
pc_prepare_cwd() {
    local f
    rm -rf "$PC_CWD"
    mkdir -p "$PC_CWD"
    for f in /tests/corpus/*; do
        [ -e "$f" ] || continue
        ln -s "$f" "$PC_CWD/${f##*/}"
    done
    chown -h "$SCORE_UID:$SCORE_GID" "$PC_CWD" "$PC_CWD"/* 2>/dev/null || true
    chmod 0755 "$PC_CWD"
}

# pc_reset_cwd -- drop what the last case wrote, keep the symlinked fixtures.
#
# Without this a `#save` file survives into the next case, and a later `#load`
# could read a file the current case never wrote: the same input would then
# grade differently depending on what ran before it.
pc_reset_cwd() {
    find "$PC_CWD" -maxdepth 1 ! -type l ! -path "$PC_CWD" -delete 2>/dev/null || true
}

# pc_prepare_work -- create the scratch tree the whole run writes into.
#
# Owned by the score user and not world-writable-sticky. A root-owned 1777
# directory falls under fs.protected_regular (2 on this kernel, and on the
# image's host): root may not open for write a file it does not own in a sticky
# world-writable directory it does not own. The verifier does exactly that --
# pcre2test creates the output file as uid 65534, then the shell reopens it to
# append the `-error` appendix -- so in such a directory every test 2 case would
# fail with an empty stderr and the misleading detail "appendix exited 1".
# Owning the directory takes it out of that rule entirely; both users can
# write, no capability needed.
pc_prepare_work() {
    mkdir -p "$PC_WORK"
    chown "$SCORE_UID:$SCORE_GID" "$PC_WORK" 2>/dev/null || true
    chmod 0755 "$PC_WORK"
    pc_prepare_cwd
}

# ---------------------------------------------------------------- proof of life
#
# Builds the candidate tree with the documented interface -- `make` at /app
# producing build/pcre2test -- as an unprivileged user, and confirms the binary
# reports all three code-unit widths. Returns non-zero when the tree does not
# build, in which case the gate is 0 and no group is attempted: a tree that does
# not build cannot be distinguished group by group, and pretending otherwise
# would report a partial score for an empty result.
pc_build() {
    pc_prepare_work
    give_app_to_unprivileged
    pc_say "=== proof of life: make at /app ==="
    # run_unprivileged FIRST, then timeout. run_unprivileged is a shell
    # function; `timeout` execs a program and cannot see one, so the other
    # order fails with 127 for every single invocation -- build included --
    # and the whole package scores 0 for reasons that look like the candidate.
    if ! run_unprivileged timeout "$PC_BUILD_TIMEOUT" \
            make -C /app -j"$(nproc)" > "$PC_WORK/build.log" 2>&1; then
        pc_say "FAIL build: \`make\` at /app exited non-zero (tail below)"
        tail -n 40 "$PC_WORK/build.log" >&2 || true
        cp "$PC_WORK/build.log" "$PC_LOG/build.log" 2>/dev/null || true
        return 1
    fi
    cp "$PC_WORK/build.log" "$PC_LOG/build.log" 2>/dev/null || true
    if [ ! -x /app/build/pcre2test ]; then
        pc_say "FAIL build: /app/build/pcre2test missing or not executable after \`make\`"
        return 1
    fi
    if ! run_unprivileged timeout 60 /app/build/pcre2test -C > "$PC_WORK/config.txt" 2>&1; then
        pc_say "FAIL build: \`pcre2test -C\` exited non-zero"
        return 1
    fi
    local w missing=0
    for w in 8 16 32; do
        grep -qE "^ *${w}-bit +(support|library)" "$PC_WORK/config.txt" \
            || grep -qE "${w}-bit" "$PC_WORK/config.txt" || missing=1
    done
    if [ "$missing" -ne 0 ]; then
        pc_say "FAIL build: pcre2test -C does not report all three code-unit widths"
        sed -n '1,40p' "$PC_WORK/config.txt" >&2 || true
        return 1
    fi
    cp "$PC_WORK/config.txt" "$PC_LOG/pcre2test-C.txt" 2>/dev/null || true
    # JIT must be reported unavailable (PROFILE.md). Accept any wording that
    # says so -- 10.20's JIT-less branch prints "No just-in-time compiler
    # support", later releases print "Just-in-time compiler support: no" --
    # and fail only on a -C that positively claims JIT is available.
    if grep -qi "just-in-time" "$PC_WORK/config.txt" && \
       ! grep -i "just-in-time" "$PC_WORK/config.txt" | \
         grep -qiE "(^|[^a-z])(no|not|none|unavailable|disabled|without)([^a-z]|$)"; then
        pc_say "FAIL build: pcre2test -C reports JIT support (build must be JIT-free)"
        return 1
    fi
    pc_say "PASS build: make succeeded, pcre2test reports 8-, 16- and 32-bit, no JIT"
    return 0
}

# pc_run_case <width> <mode> <input> <expected> [append-args...]
#
# One graded assertion. `mode` is "std" or "dfa". Both files are read from
# /tests/corpus. The candidate binary runs unprivileged with a wall-clock
# ceiling so a runaway matcher costs one case, not the whole verifier.
#
# The working directory is $PC_CWD -- a writable mirror of /tests/corpus -- and
# not /tests/corpus itself. It has to look like the corpus because some inputs
# carry a `#loadtables testbtables` directive that pcre2test resolves against
# the current directory (from 10.39 on, running test 2 anywhere else exits 1),
# and it has to be writable because the serialization test writes its `#save`
# output there.
#
# `append-args`, when present, is a second pcre2test invocation whose output is
# appended to the first. From step 02 on the corpus uses this for test 2
# (`pcre2test -q -<w> -error <list> >> out`, as upstream's own RunTest does),
# and there the shipped expected file includes that appendix, so a case that
# omits it can never match. No step-01 case has a fifth column, and 10.20's
# pcre2test has no such option.
pc_run_case() {
    local width="$1" mode="$2" input="$3" expected="$4"; shift 4
    local modeflag=() out rc
    [ "$mode" = "dfa" ] && modeflag=(-dfa)
    out="$PC_WORK/out.$$"
    rm -f "$out"
    pc_reset_cwd
    # Status is captured from the subshell directly, never from `if ! cmd`:
    # `!` inverts the status before `$?` is read, so the failure branch of an
    # `if !` always sees 0 and every diagnostic would read "exited 0". The
    # number matters here -- 124 is the case timeout firing, and a signal death
    # is 128+n -- because it is the only thing distinguishing a hang from a
    # crash when this shows up in a log.
    ( cd "$PC_CWD" && run_unprivileged timeout "$PC_CASE_TIMEOUT" \
            /app/build/pcre2test -q "-$width" "${modeflag[@]}" \
            "$input" "$out" ) < /dev/null > "$PC_WORK/case.log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
        pc_case_detail="pcre2test exited $rc: $(head -c 200 "$PC_WORK/case.log" | tr '\n' ' ')"
        return 1
    fi
    if [ "$#" -gt 0 ]; then
        ( cd "$PC_CWD" && run_unprivileged timeout "$PC_CASE_TIMEOUT" \
                /app/build/pcre2test -q "-$width" "${modeflag[@]}" "$@" \
             ) < /dev/null >> "$out" 2>> "$PC_WORK/case.log"
        rc=$?
        if [ "$rc" -ne 0 ]; then
            pc_case_detail="pcre2test appendix exited $rc: $(head -c 200 "$PC_WORK/case.log" | tr '\n' ' ')"
            return 1
        fi
    fi
    # Normalise both sides identically before comparing. The normaliser only
    # deletes regions that expose the internal compiled representation, so it
    # cannot rescue a wrong answer -- see /tests/normalize.awk.
    awk -f /tests/normalize.awk < "/tests/corpus/$expected" > "$PC_WORK/exp.norm" 2>/dev/null
    awk -f /tests/normalize.awk < "$out" > "$PC_WORK/act.norm" 2>/dev/null
    if ! diff -u "$PC_WORK/exp.norm" "$PC_WORK/act.norm" > "$PC_WORK/case.diff" 2>&1; then
        pc_case_detail="output differs from expected: $(sed -n '3,6p' "$PC_WORK/case.diff" | tr '\n' ' ' | head -c 240)"
        return 1
    fi
    pc_case_detail=""
    return 0
}

# pc_run_group <kind> <name> <tier> <weight> <casefile>
#
# A group passes only if every one of its cases passes AND at least one case
# ran. Cases are lines of
# <width> TAB <mode> TAB <input> TAB <expected> [TAB <appendix args>].
#
# The case count is not bookkeeping. Without it a group whose case file is
# empty, is all comments, or does not exist at all reports PASS having asserted
# nothing -- the loop simply never executes and `ok` is still its initial 1 --
# so a typo in a manifest's fifth column would hand a candidate the group's
# whole weight for free. A group that ran no cases is a defect in the package,
# and the package should fail loudly on itself rather than quietly pay out.
pc_run_group() {
    local kind="$1" name="$2" tier="$3" weight="$4" casefile="$5"
    local ok=1 first_fail="" width mode input expected extra
    local ncases=0
    pc_case_detail=""
    if [ ! -f "/tests/cases/$casefile" ]; then
        ok=0; first_fail="no case file /tests/cases/$casefile"
    fi
    while IFS=$'\t' read -r width mode input expected extra; do
        [ -z "${width:-}" ] && continue
        case "$width" in \#*) continue;; esac
        ncases=$((ncases + 1))
        # shellcheck disable=SC2086
        if ! pc_run_case "$width" "$mode" "$input" "$expected" ${extra:-}; then
            ok=0
            [ -z "$first_fail" ] && first_fail="[-$width${mode:+/$mode} $input] $pc_case_detail"
            break
        fi
    done < "/tests/cases/$casefile"

    if [ "$ok" -eq 1 ] && [ "$ncases" -eq 0 ]; then
        ok=0; first_fail="group ran 0 cases (empty or all-comment case file)"
    fi

    if [ "$kind" = "gate" ]; then
        pc_gate_total=$((pc_gate_total + 1))
        [ "$ok" -eq 1 ] && pc_gate_passed=$((pc_gate_passed + 1))
        pc_gate_json="${pc_gate_json}${pc_gate_json:+,}{\"name\":\"$(pc_json_escape "$name")\",\"passed\":$([ "$ok" -eq 1 ] && echo true || echo false),\"detail\":\"$(pc_json_escape "$first_fail")\"}"
        pc_say "$([ "$ok" -eq 1 ] && echo PASS || echo FAIL) gate  $name${first_fail:+  -- $first_fail}"
    else
        pc_weight_accum=$(awk -v a="$pc_weight_accum" -v w="$weight" 'BEGIN{printf "%.9f", a+w}')
        [ "$ok" -eq 1 ] && pc_score_accum=$(awk -v a="$pc_score_accum" -v w="$weight" 'BEGIN{printf "%.9f", a+w}')
        pc_score_json="${pc_score_json}${pc_score_json:+,}{\"name\":\"$(pc_json_escape "$name")\",\"tier\":\"$(pc_json_escape "$tier")\",\"weight\":$weight,\"passed\":$([ "$ok" -eq 1 ] && echo true || echo false),\"detail\":\"$(pc_json_escape "$first_fail")\"}"
        pc_say "$([ "$ok" -eq 1 ] && echo PASS || echo FAIL) $tier  $name (w=$weight)${first_fail:+  -- $first_fail}"
    fi
}

# pc_run_manifest -- run every group in /tests/manifest.tsv, gates first so a
# gate failure is visible at the top of the log.
pc_run_manifest() {
    local kind name tier weight casefile
    for kind in gate scored; do
        while IFS=$'\t' read -r k name tier weight casefile; do
            [ -z "${k:-}" ] && continue
            case "$k" in \#*) continue;; esac
            [ "$k" = "$kind" ] || continue
            pc_run_group "$k" "$name" "$tier" "$weight" "$casefile"
        done < /tests/manifest.tsv
    done
}

# pc_finish <gate-ok>
#
# gate-ok is 0 when proof of life failed. Writes reward.json,
# reward_details.json and a human-readable scorecard, then exits.
pc_finish() {
    local build_ok="$1" gate=1 score reward
    if [ "$build_ok" -ne 0 ] || [ "$pc_gate_passed" -ne "$pc_gate_total" ]; then
        gate=0
    fi
    score=$(awk -v s="$pc_score_accum" 'BEGIN{printf "%.6f", s}')
    reward=$(awk -v g="$gate" -v s="$pc_score_accum" 'BEGIN{printf "%.6f", g*s}')

    # The scored weights are the denominator of the score, so a manifest whose
    # weights do not sum to 1 reports a number that means nothing. When that
    # happens the package is broken and the run must say so rather than emit a
    # plausible-looking fraction. Only checked when the build succeeded: a
    # failed build runs no group, and 0 != 1 is then expected.
    if [ "$build_ok" -eq 0 ] && \
       ! awk -v w="$pc_weight_accum" 'BEGIN{exit !(w > 0.999999 && w < 1.000001)}'; then
        pc_say "VERIFIER ERROR: scored weights sum to $pc_weight_accum, not 1.0 -- manifest.tsv is malformed"
        printf '{"reward":0,"verifier_error":"weight_sum","weight_total":%s}\n' \
            "$pc_weight_accum" > "$PC_LOG/reward_details.json"
        printf '%s' "$pc_card" > "$PC_LOG/scorecard.txt"
        report 0
    fi

    pc_say "--- weights summed over scored groups: $pc_weight_accum (must be 1.000000000 when the gate ran)"
    pc_say "GATE=$gate ($pc_gate_passed/$pc_gate_total inherited groups)  SCORE=$score  REWARD=$reward"
    {
        printf '{"reward":%s,"gate":{"passed":%s,"build_ok":%s,"groups_passed":%s,"groups_total":%s,"groups":[%s]},' \
            "$reward" "$([ "$gate" -eq 1 ] && echo true || echo false)" \
            "$([ "$build_ok" -eq 0 ] && echo true || echo false)" \
            "$pc_gate_passed" "$pc_gate_total" "$pc_gate_json"
        printf '"score":%s,"weight_total":%s,"groups":[%s]}\n' \
            "$score" "$pc_weight_accum" "$pc_score_json"
    } > "$PC_LOG/reward_details.json"
    printf '%s' "$pc_card" > "$PC_LOG/scorecard.txt"
    report "$reward"
}

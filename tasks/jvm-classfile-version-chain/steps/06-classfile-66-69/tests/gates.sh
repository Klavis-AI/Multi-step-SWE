#!/usr/bin/env bash
# Instruction-count gates. callgrind counts executed instructions, so these are
# machine- and load-independent.
set -u
source /tests/helpers.sh
V=/app/jvm/bin/java
D=/tests/gates
[ -d "$D" ] || { echo "GATES 0 2"; exit 0; }
export LANG=C LC_ALL=C
# A measurement counts only for a run that did the program's work: the exit
# status and the stdout the reference JDK gives for that arm. The expectations
# sit outside $D, which is copied to the candidate, and the candidate's stdout
# lands in a root-only directory through a descriptor root opened.
EXP=/tests/gates-expected.txt
R=$(mktemp -d)

# The measured program is the candidate's launcher, so it runs as the candidate
# uid, exactly like every corpus case: run as root, it could write anything this
# scorer can. Each measurement gets a fresh copy of the gate classes as its
# working directory and $HOME, so nothing one run leaves behind -- a file, a
# .valgrindrc, a process -- reaches the next. valgrind writes its summary to a
# file there, which root reads only after every candidate process is gone.
ir() {
    local arm="$1" out g rc want got
    shift
    g=$(mktemp -d)
    cp -r "$D/." "$g/" && chown -R "$CAND_UID:$CAND_UID" "$g" || { rm -rf "$g"; return; }
    ( cd "$g" && timeout -k 10 300 setpriv --reuid="$CAND_UID" --regid="$CAND_UID" \
          --clear-groups --no-new-privs \
          env -i PATH=/usr/bin:/bin HOME="$g" LANG=C LC_ALL=C \
          valgrind --tool=callgrind --callgrind-out-file=/dev/null \
          --log-file="$g/vg.log" "$@" ) </dev/null >"$R/$arm.out" 2>/dev/null
    rc=$?
    sweep_candidate
    out=""
    want=$(awk -v a="$arm" '$1 == a { print $2 " " $3 }' "$EXP")
    got="$rc $(head -c 65536 "$R/$arm.out" | sha256sum | cut -d' ' -f1)"
    if [ -z "$want" ] || [ "$got" != "$want" ]; then
        echo "gate/$arm: not counted -- the run did not exit with the program's result (exit $rc)" >&2
        rm -rf "$g"
        echo ""
        return
    fi
    if [ -f "$g/vg.log" ] && [ ! -L "$g/vg.log" ]; then
        out=$(grep -oE 'refs: *[0-9,]+' "$g/vg.log" | tail -1)
    fi
    rm -rf "$g"
    echo "${out##*: }" | tr -d ', '
}

pass=0
total=2

# 1. A compiled tier: the same program must execute materially fewer
#    instructions than the same virtual machine run with -Xint.
a=$(ir jit-tier "$V" -cp . Bench)
b=$(ir jit-xint "$V" -Xint -cp . Bench)
echo "gate/jit: tier=$a interpreted=$b"
if [ -n "$a" ] && [ -n "$b" ] && [ "$b" -gt 0 ]; then
    if awk -v a="$a" -v b="$b" 'BEGIN { exit !(a <= 0.80 * b) }'; then
        echo "gate/jit: PASS"
        pass=$((pass + 1))
    else
        echo "gate/jit: FAIL (ratio $(awk -v a="$a" -v b="$b" 'BEGIN{printf "%.3f", a/b}'))"
    fi
else
    echo "gate/jit: FAIL (no measurement)"
fi

# 2. A generational collector: making the live set 150x larger must not make an
#    allocation-heavy program cost much more.
c=$(ir gc-biglive "$V" -Xmx32m -cp . GcLive 150000 1500000)
d=$(ir gc-small "$V" -Xmx32m -cp . GcLive 1000 1500000)
echo "gate/gc: biglive=$c smalllive=$d"
if [ -n "$c" ] && [ -n "$d" ] && [ "$d" -gt 0 ]; then
    if awk -v c="$c" -v d="$d" 'BEGIN { exit !(c <= 1.60 * d) }'; then
        echo "gate/gc: PASS"
        pass=$((pass + 1))
    else
        echo "gate/gc: FAIL (ratio $(awk -v c="$c" -v d="$d" 'BEGIN{printf "%.3f", c/d}'))"
    fi
else
    echo "gate/gc: FAIL (no measurement)"
fi

rm -rf "$R"
echo "GATES $pass $total"

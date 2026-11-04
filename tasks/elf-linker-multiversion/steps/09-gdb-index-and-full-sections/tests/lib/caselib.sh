# shellcheck shell=bash
# caselib.sh -- the shared harness every linker conformance case is written
# against. Sourced by a case script with:
#
#   WILD   absolute path to the linker under test
#   ARCH   x86_64 | aarch64 | riscv64 | loongarch64   (default x86_64)
#   cwd    a private, empty scratch directory
#
# A case passes by returning 0 and fails by calling die(). Every case builds
# its own inputs from source here, so the corpus has no prebuilt binary blobs
# and is byte-reproducible from the shipped text.

set -uo pipefail

: "${ARCH:=x86_64}"

# Resolve each tool to the first spelling that exists.  This matters more than
# it looks: every inspection helper below redirects stderr to /dev/null, so a
# missing tool would otherwise produce empty output and a CASE-FAIL that blames
# the linker for a hole in the image.  Missing tools must be loud.
_pick() {
  local v=$1; shift
  if [ -n "${!v:-}" ]; then return; fi
  local c
  for c in "$@"; do command -v "$c" >/dev/null 2>&1 && { printf -v "$v" '%s' "$c"; return; }; done
  echo "CASE-FAIL: none of [$*] is installed - the verifier image is missing a tool" >&2
  exit 91
}
_pick CLANG   clang-16 clang
_pick READELF llvm-readelf-16 llvm-readelf readelf
_pick OBJDUMP llvm-objdump-16 llvm-objdump objdump
_pick NM      llvm-nm-16 llvm-nm nm
_pick AR      llvm-ar-16 llvm-ar ar

# EMULATION is the -m value the linker is given. A cross link needs it: wild
# has no --target, and with no -m the machine is taken from the first input.
# x86-64 cases deliberately pass nothing, so they stay inside the option set
# the earliest checkpoint accepts.
case "$ARCH" in
  x86_64)      TRIPLE=x86_64-unknown-linux-gnu     ; RUNNER=""                       ; EMULATION="" ;;
  aarch64)     TRIPLE=aarch64-unknown-linux-gnu    ; RUNNER=qemu-aarch64-static      ; EMULATION=aarch64linux ;;
  riscv64)     TRIPLE=riscv64-unknown-linux-gnu    ; RUNNER=qemu-riscv64-static      ; EMULATION=elf64lriscv ;;
  loongarch64) TRIPLE=loongarch64-unknown-linux-gnu; RUNNER=qemu-loongarch64-static  ; EMULATION=elf64loongarch ;;
  *) echo "caselib: unknown ARCH=$ARCH" >&2; exit 90 ;;
esac

CFLAGS_FREE="-target $TRIPLE -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -O1"

die() { echo "CASE-FAIL: $*" >&2; exit 1; }
skip() { echo "CASE-SKIP: $*" >&2; exit 77; }

# ---------------------------------------------------------------- compilation

cfile() { # cfile <name.c> ; body on stdin
  cat > "$1"
}

cc_obj() { # cc_obj <out.o> <src.c> [extra flags...]
  local out=$1 src=$2; shift 2
  $CLANG $CFLAGS_FREE "$@" -c "$src" -o "$out" 2> cc.err \
    || die "compile $src failed: $(tail -3 cc.err | tr '\n' ' ')"
}

as_obj() { # as_obj <out.o> <src.s> [extra flags...]
  local out=$1 src=$2; shift 2
  $CLANG -target "$TRIPLE" "$@" -c "$src" -o "$out" 2> as.err \
    || die "assemble $src failed: $(tail -3 as.err | tr '\n' ' ')"
}

mkar() { # mkar <out.a> <member.o>...
  local out=$1; shift
  rm -f "$out"
  $AR crs "$out" "$@" 2> ar.err || die "ar $out failed: $(tail -2 ar.err | tr '\n' ' ')"
}

# ------------------------------------------------------------------ runtime

# The freestanding runtime every runnable case links against. It provides
# _start, calls int entry(void), exits with its return value, and exposes
# sys_write so cases can print without libc.
rt_src() {
  case "$ARCH" in
  x86_64) cat <<'EOF'
        .text
        .globl _start
_start: call    entry
        mov     %eax, %edi
        mov     $60, %eax
        syscall
        hlt
        .globl sys_write
sys_write:
        mov     $1, %eax
        syscall
        ret
        .section .note.GNU-stack,"",@progbits
EOF
  ;;
  aarch64) cat <<'EOF'
        .text
        .globl _start
_start: bl      entry
        mov     w8, #93
        svc     #0
        brk     #0
        .globl sys_write
sys_write:
        mov     x8, #64
        svc     #0
        ret
        .section .note.GNU-stack,"",@progbits
EOF
  ;;
  riscv64) cat <<'EOF'
        .text
        .globl _start
_start: call    entry
        li      a7, 93
        ecall
        unimp
        .globl sys_write
sys_write:
        li      a7, 64
        ecall
        ret
        .section .note.GNU-stack,"",@progbits
EOF
  ;;
  loongarch64) cat <<'EOF'
        .text
        .globl _start
_start: bl      entry
        li.w    $a7, 93
        syscall 0
        break   0
        .globl sys_write
sys_write:
        li.w    $a7, 64
        syscall 0
        jr      $ra
        .section .note.GNU-stack,"",@progbits
EOF
  ;;
  esac
}

# rt.h -- the C view of the runtime, plus print/putdec built on it.
rt_header() { cat <<'EOF'
long sys_write(long fd, const void *buf, long len);
static void print(const char *s) {
    long n = 0; while (s[n]) n++;
    sys_write(1, s, n);
}
static void putdec(long v) {
    char b[24]; int i = 23; b[i--] = 0;
    int neg = v < 0; unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (!u) b[i--] = '0';
    while (u) { b[i--] = (char)('0' + (u % 10)); u /= 10; }
    if (neg) b[i--] = '-';
    print(&b[i + 1]);
}
EOF
}

rt_obj() { # rt_obj <out.o>
  rt_src > __rt.s
  as_obj "$1" __rt.s
  rt_header > rt.h
}

# ------------------------------------------------------------------- linking

# link <out> <args...> -- run the linker under test. Records rc in LINK_RC, the
# two streams separately in LINK_STDOUT and LINK_ERR, and their concatenation
# (stdout first) in LINK_OUT; never dies, so negative cases can inspect any of
# them.
#
# The streams are captured apart because `00-compatibility-profile.md` §8
# requires diagnostics on **stderr** -- "stdout MUST NOT be used for errors" --
# and a `2>&1` capture cannot tell the two apart. LINK_OUT is the combined
# stream; a case that wants the routing rule asserts on LINK_STDOUT.
#
# The stderr spill is a DOT file on purpose. Cases that assert the exact
# contents of a directory they linked into (§9.1's "leave no other file behind")
# must not see the harness's own scratch file; `ls` without -a does not list it.
link() {
  local out=$1; shift
  local pre=()
  [ -n "$EMULATION" ] && pre=(-m "$EMULATION")
  LINK_STDOUT=$("$WILD" "${pre[@]}" "$@" -o "$out" 2>.link.err); LINK_RC=$?
  LINK_ERR=$(cat .link.err 2>/dev/null)
  if [ -n "$LINK_STDOUT" ] && [ -n "$LINK_ERR" ]; then
    LINK_OUT=$(printf '%s\n%s' "$LINK_STDOUT" "$LINK_ERR")
  else
    LINK_OUT=$LINK_STDOUT$LINK_ERR
  fi
  return 0
}

link_ok() { # link_ok <out> <args...>
  link "$@" || true
  [ "$LINK_RC" -eq 0 ] || die "link failed rc=$LINK_RC: $(printf '%s' "$LINK_OUT" | tail -3 | tr '\n' ' ')"
  [ -s "$1" ] || die "link produced no output at $1"
}

link_fails() { # link_fails <out> <args...> -- expect a diagnosed failure
  link "$@" || true
  [ "$LINK_RC" -ne 0 ] || die "link unexpectedly succeeded"
}

# ------------------------------------------------------------------ execution

run() { # run <exe> [args...] ; sets RUN_OUT, RUN_RC
  chmod +x "$1"
  if [ -n "$RUNNER" ]; then
    RUN_OUT=$(timeout 25 "$RUNNER" "$@" 2>&1); RUN_RC=$?
  else
    RUN_OUT=$(timeout 25 "./$1" "${@:2}" 2>&1); RUN_RC=$?
  fi
  return 0
}

run_expect() { # run_expect <exe> <expected-stdout> <expected-rc>
  run "$1" || true
  [ "$RUN_RC" = "$3" ] || die "$1: exit $RUN_RC, want $3 (output: $(printf '%s' "$RUN_OUT" | head -c 200))"
  [ "$RUN_OUT" = "$2" ] || die "$1: output '$(printf '%s' "$RUN_OUT" | head -c 200)', want '$2'"
}

# ------------------------------------------------------------------ ELF views

hdr()      { $READELF -h "$1" 2>/dev/null; }
sections() { $READELF -S --wide "$1" 2>/dev/null; }
segments() { $READELF -l --wide "$1" 2>/dev/null; }
dynamic()  { $READELF -d --wide "$1" 2>/dev/null; }
dynsyms()  { $READELF --dyn-syms --wide "$1" 2>/dev/null; }
symbols()  { $READELF -s --wide "$1" 2>/dev/null; }
relocs()   { $READELF -r --wide "$1" 2>/dev/null; }
notes()    { $READELF -n --wide "$1" 2>/dev/null; }
vernum()   { $READELF -V --wide "$1" 2>/dev/null; }

# NEVER pipe into `grep -q` (or anything else that exits before its input is
# consumed) in a status-bearing pipeline. `grep -q` exits at the first match and
# closes the pipe; the writer then dies of SIGPIPE, and under `pipefail` the
# pipeline reports 141 even though the assertion HELD -- intermittently, so
# identical runs can disagree. Every match below therefore captures its input
# first and feeds `grep` a herestring, which has no writer to kill.
has_section() { local t; t=$(sections "$1"); grep -qE "[[:space:]]$2[[:space:]]" <<<"$t"; }
has_segment() { local t; t=$(segments "$1"); grep -qE "^[[:space:]]*$2[[:space:]]" <<<"$t"; }
has_dyn_tag() { local t; t=$(dynamic  "$1"); grep -qF -- "($2)" <<<"$t"; }

# Same rule as above: `awk ... exit` also closes the pipe early, so piping into
# these three would print the right value and return 141. Harmless where the
# caller only wants the string, fatal the moment someone writes
# `sym_value x f || die ...`.
# Capture first, match a herestring.
# sym_value <file> <name> -> hex address of a symbol in the symbol table
sym_value() {
  local t; t=$(symbols "$1"); awk -v n="$2" '$8 == n { print $2; exit }' <<<"$t"
}
# sec_addr / sec_size match the section name EXACTLY, field by field, unlike
# has_section, which takes a regex. Passing '\.tdata' to sec_size silently
# returns the empty string rather than failing -- write .tdata, unescaped.
# sec_addr <file> <name> -> hex address of a section
sec_addr() {
  local t; t=$(sections "$1"); awk -v n="$2" '{ for (i = 1; i <= NF; i++) if ($i == n) { print $(i+2); exit } }' <<<"$t"
}
# sec_size <file> <name> -> hex size of a section
sec_size() {
  local t; t=$(sections "$1"); awk -v n="$2" '{ for (i = 1; i <= NF; i++) if ($i == n) { print $(i+4); exit } }' <<<"$t"
}

# EVERY counted assertion bumps ASSERTS_RUN, and a case that executes at least
# one of them ends with `asserts_at_least N`.
#
# Only want, want_has, want_not and want_seg_perm count. A case that verifies
# purely through run_expect, link_ok, link_fails or a bare `... || die` has no
# counted assertion and therefore carries no floor -- those helpers fail loudly
# by themselves, so the silent-skip hole described below cannot open in them.
#
# This closes a silent-skip hole. A bash arithmetic error in a WORD of a simple
# command -- classically `$(( 16#$(sym_value out sym) ))` where the symbol was
# collected, so the expansion is the invalid literal `16#` -- aborts that one
# command, writes to stderr, and CONTINUES. It does not fail the case, and
# `set -e` would not catch it either (the error is raised during word
# expansion, so no ERR trap fires). The assertion simply never runs, and a
# never-run assertion passes. Counting executed assertions turns that from
# invisible into a case failure.
#
# Capture-then-compare is still the right way to write those comparisons; the
# counter is a backstop.
ASSERTS_RUN=0
want()     { ASSERTS_RUN=$((ASSERTS_RUN + 1)); [ "$2" = "$3" ] || die "$1: got '$2', want '$3'"; }
want_has() { ASSERTS_RUN=$((ASSERTS_RUN + 1)); grep -qE -- "$3" <<<"$2" || die "$1: '$(printf '%s' "$2" | head -c 300)' lacks /$3/"; }
want_not() { ASSERTS_RUN=$((ASSERTS_RUN + 1)); grep -qE -- "$3" <<<"$2" && die "$1: unexpectedly matches /$3/"; return 0; }

# asserts_at_least <n> -- last line of a case. N is the number of assertions
# the case executes on the reference linker; it is a floor, not an equality, so
# a case whose count legitimately varies by tag declares its minimum.
# Assertions must therefore run in the case's own shell: a `want` inside `$( )`
# or on the right of a `|` increments a counter in a subshell and is lost here.
asserts_at_least() {
  [ "$ASSERTS_RUN" -ge "$1" ] || die "only $ASSERTS_RUN assertions executed, expected at least $1 -- an assertion was skipped, not passed (look for an arithmetic error in a \$(( )) word)"
}

# seg_perm_of <file> <section-name> -> permission letters of the PT_LOAD that
# contains the section, with spaces squeezed out: "R", "RE", "RW".
#
# Use this, never a search for "the writable segment". From 0.9.0 on the linker
# emits a SECOND writable PT_LOAD -- the RELRO one, FileSiz 0 -- so selecting a
# segment by its permission bits picks the wrong one, and "exactly three
# PT_LOADs" is simply false. Ask which segment covers the address; then check
# its flags.
#
# Arithmetic is done in bash, not awk: the image ships mawk, which has no
# strtonum().
seg_perm_of() {
  local addr a t type off vaddr paddr filesz memsz rest v m flags
  addr=$(sec_addr "$1" "$2")
  [ -n "$addr" ] || return 0
  a=$((16#$addr))
  t=$(segments "$1")
  while read -r type off vaddr paddr filesz memsz rest; do
    [ "$type" = LOAD ] || continue
    v=$((16#${vaddr#0x})); m=$((16#${memsz#0x}))
    if [ "$a" -ge "$v" ] && [ "$a" -lt "$((v + m))" ]; then
      flags=${rest% *}          # drop the trailing Align column
      printf '%s\n' "${flags// /}"
      return 0
    fi
  done <<<"$t"
}

# want_seg_perm <file> <section-name> <expected>  e.g. want_seg_perm out .text RE
want_seg_perm() {
  local got; ASSERTS_RUN=$((ASSERTS_RUN + 1)); got=$(seg_perm_of "$1" "$2")
  [ -n "$got" ] || die "no PT_LOAD covers $2 in $1"
  [ "$got" = "$3" ] || die "$2 lives in a $got segment, want $3"
}

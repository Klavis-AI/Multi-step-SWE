// Package vhelp is the benchmark-owned reporting protocol every scored group
// speaks. It imports nothing but the standard library, so it compiles whether
// or not the candidate's product does.
//
// A group prints one nonce-prefixed line per case and exactly one summary:
//
//	<nonce> CASE <id>: PASS
//	<nonce> CASE <id>: FAIL <why>
//	<nonce> SUMMARY executed=<n> failed=<m>
//
// The nonce comes from CT_VERIFY_NONCE, which test.sh regenerates for every
// run. Product code the group drives cannot forge a line it cannot guess the
// prefix of, so a candidate that prints "CASE x: PASS" on its own stdout earns
// nothing. test.sh additionally requires the CASE-line count to equal the
// summary's `executed`, and `executed` to equal the count declared in
// groups.txt, so a group that dies part way through fails rather than scoring
// what it managed.
package vhelp

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
)

// T accumulates one group's results.
type T struct {
	nonce    string
	executed int
	failed   int
	seen     map[string]bool
	out      *os.File
	cleanups []func()
}

// New starts a group report.
func New() *T {
	return &T{nonce: os.Getenv("CT_VERIFY_NONCE"), seen: map[string]bool{}, out: os.Stdout}
}

func (t *T) emit(format string, a ...interface{}) {
	fmt.Fprintf(t.out, "%s %s\n", t.nonce, fmt.Sprintf(format, a...))
}

// Case records one case. A nil error is a pass.
func (t *T) Case(id string, err error) {
	t.record(id, err == nil, func() string {
		if err == nil {
			return ""
		}
		return err.Error()
	})
}

// Check records one case from a boolean plus a reason used only when it fails.
func (t *T) Check(id string, ok bool, format string, a ...interface{}) {
	t.record(id, ok, func() string { return fmt.Sprintf(format, a...) })
}

// Equal records one case comparing two rendered values.
func (t *T) Equal(id string, got, want interface{}) {
	t.record(id, fmt.Sprint(got) == fmt.Sprint(want), func() string {
		return fmt.Sprintf("got %v, want %v", got, want)
	})
}

// EqualBytes records one case comparing two byte slices exactly.
func (t *T) EqualBytes(id string, got, want []byte) {
	same := len(got) == len(want)
	if same {
		for i := range got {
			if got[i] != want[i] {
				same = false
				break
			}
		}
	}
	t.record(id, same, func() string {
		return fmt.Sprintf("got %x (%d bytes), want %x (%d bytes)", got, len(got), want, len(want))
	})
}

func (t *T) record(id string, ok bool, why func() string) {
	// A duplicated case id would let one behaviour be counted twice and let a
	// missing one hide behind it, so it is a failure of the group.
	if t.seen[id] {
		t.executed++
		t.failed++
		t.emit("CASE %s: FAIL duplicate case id", id)
		return
	}
	t.seen[id] = true
	t.executed++
	if ok {
		t.emit("CASE %s: PASS", id)
		return
	}
	t.failed++
	t.emit("CASE %s: FAIL %s", id, strings.ReplaceAll(why(), "\n", " / "))
}

// Fatalf abandons the group. The summary it prints is deliberately honest
// about how little ran, which the executed-count assertion then rejects.
func (t *T) Fatalf(format string, a ...interface{}) {
	fmt.Fprintf(os.Stderr, "group aborted: %s\n", fmt.Sprintf(format, a...))
	t.Done()
}

// Cleanup registers a function to run just before the process exits. A driver
// cannot use `defer` for this: every exit path here goes through Done, and
// Done calls os.Exit, which does not run deferred functions. Anything that
// must be released - a spawned server, a listening socket - registers here
// instead.
func (t *T) Cleanup(f func()) { t.cleanups = append(t.cleanups, f) }

// Done prints the single summary line and exits 0. test.sh reads the summary,
// never the exit status alone.
//
// The summary is emitted BEFORE the cleanups run, deliberately. test.sh parses
// that line and nothing else; if a cleanup were to hang, emitting first means
// the group is still scored correctly when the 600s timeout kills it, whereas
// cleaning up first would lose the result. Cleanups run in reverse order of
// registration, like defer, and a panicking one cannot stop the others.
func (t *T) Done() {
	t.emit("SUMMARY executed=%d failed=%d", t.executed, t.failed)
	for i := len(t.cleanups) - 1; i >= 0; i-- {
		func(f func()) {
			defer func() { _ = recover() }()
			f()
		}(t.cleanups[i])
	}
	os.Exit(0)
}

// Fixture returns the absolute path of a pinned fixture.
func Fixture(rel ...string) string {
	return filepath.Join(append([]string{os.Getenv("CT_FIXTURES")}, rel...)...)
}

// ReadFixture reads a pinned fixture or dies; a missing fixture is a corpus
// fault, and the asset-integrity prerequisite should already have caught it.
func ReadFixture(rel ...string) []byte {
	b, err := os.ReadFile(Fixture(rel...))
	if err != nil {
		fmt.Fprintf(os.Stderr, "fixture: %v\n", err)
		os.Exit(3)
	}
	return b
}

// GroupFile returns the absolute path of a file shipped beside this group.
func GroupFile(rel ...string) string {
	return filepath.Join(append([]string{os.Getenv("CT_GROUP_DIR")}, rel...)...)
}

// CandidateBin returns the absolute path of a binary built from the
// candidate's own source tree.
func CandidateBin(name string) string {
	return filepath.Join(os.Getenv("CT_BIN"), name)
}

// Command builds a subprocess with CT_VERIFY_NONCE stripped from its
// environment.
//
// Use it for every candidate binary a group runs. The nonce is what makes a
// reported PASS unforgeable, and a driver that spawns a child with os/exec's
// default environment hands the candidate the one secret it would need: the
// child inherits the nonce, and anything it prints on an inherited stdout is
// then indistinguishable from the group's own report. Stripping the variable
// removes the secret; capturing the child's output (cmd.Output, or a
// bytes.Buffer) rather than letting it share this process's stdout removes
// the channel. Do both.
func Command(name string, arg ...string) *exec.Cmd {
	cmd := exec.Command(name, arg...)
	env := os.Environ()
	kept := env[:0]
	for _, kv := range env {
		if strings.HasPrefix(kv, "CT_VERIFY_NONCE=") {
			continue
		}
		kept = append(kept, kv)
	}
	cmd.Env = kept
	return cmd
}

// SortedKeys is a small convenience so drivers iterate maps deterministically.
func SortedKeys(m map[string]string) []string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}

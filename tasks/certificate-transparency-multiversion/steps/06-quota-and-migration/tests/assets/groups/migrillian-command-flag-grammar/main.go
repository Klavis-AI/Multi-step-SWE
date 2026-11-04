// Group migrillian-command-flag-grammar.
//
// migrillian is the new command this release ships: it copies entries out of
// a CT log into a Trillian pre-ordered log, in order. It is the reason for
// most of the rest of the milestone (the exported leaf builder, the client's
// get-entry-and-proof, the fetcher's continuous mode), and its configuration
// surface is its public contract.
//
// A migration tool cannot be run to completion offline -- every useful
// invocation dials a CT log and a Trillian server -- so it is graded at the
// boundary a command-line tool always has regardless: the flag grammar it
// publishes and its exit status. Every declared flag is compared by name,
// type and default value, taken from the binary's own usage text, and the
// last case pins the complete set of flags the command defines, so a tree
// that declares a differently named or extra option fails.
//
// The child is run through vhelp.Command (which strips CT_VERIFY_NONCE) with
// its output captured into a buffer, so nothing it writes can reach this
// driver's stdout.
//
// SCOPE-NET / SCOPE-TIME: --help parses flags and returns; no connection is
// attempted and no clock is read.
//
// The migrillian command does not exist at v1.0.17, so there is no binary to
// run and every case fails against the previous tree.
package main

import (
	"bytes"
	"os/exec"
	"sort"
	"strings"

	"ctverify/vhelp"
)

// flagSpec is one line of the flag package's usage dump, reduced to the three
// things that make up the command's contract with its operator.
type flagSpec struct {
	typ string // "string", "int", "duration", "bool"
	def string // the printed default, or "-" when the flag defaults to zero
}

func (s flagSpec) String() string { return s.typ + " default=" + s.def }

// Flags registered by libraries the command links in, not by migrillian.
// Three sources, all named by CLI.md: glog's seven, which every binary that
// links glog carries (CLI.md preamble, "The two logging flag sets"), and the
// two `flag`-package globals of CLI.md 2.1 --
// allow_verification_with_non_compliant_keys from the root package and
// by_range from trillian/ctfe. Neither global is declared by any command, and
// whether one reaches migrillian's usage depends only on whether that package
// is anywhere in the candidate's import graph -- which is not part of
// migrillian's flag contract, so both are subtracted whether present or not.
var libraryFlags = map[string]bool{
	"allow_verification_with_non_compliant_keys": true,
	"alsologtostderr":  true,
	"by_range":         true,
	"log_backtrace_at": true,
	"log_dir":          true,
	"logtostderr":      true,
	"stderrthreshold":  true,
	"v":                true,
	"vmodule":          true,
}

// parseUsage turns `<cmd> --help` output into the set of flags it declares.
// A declaration line is "  -name type"; the line after it is the description,
// which carries "(default X)" when the default is not the zero value. A bool
// flag prints no type at all, which is how it is recognised.
func parseUsage(usage string) map[string]flagSpec {
	out := map[string]flagSpec{}
	lines := strings.Split(usage, "\n")
	for i, line := range lines {
		if !strings.HasPrefix(line, "  -") {
			continue
		}
		fields := strings.Fields(strings.TrimPrefix(line, "  -"))
		if len(fields) == 0 {
			continue
		}
		spec := flagSpec{typ: "bool", def: "-"}
		if len(fields) > 1 {
			spec.typ = fields[1]
		}
		if i+1 < len(lines) {
			desc := strings.TrimSpace(lines[i+1])
			if at := strings.LastIndex(desc, "(default "); at >= 0 && strings.HasSuffix(desc, ")") {
				spec.def = desc[at+len("(default ") : len(desc)-1]
			}
		}
		out[fields[0]] = spec
	}
	return out
}

// render describes a named flag exactly, or says it is missing.
func render(flags map[string]flagSpec, name string) string {
	spec, ok := flags[name]
	if !ok {
		return name + " ABSENT"
	}
	return name + " " + spec.String()
}

func renderAll(flags map[string]flagSpec, names ...string) string {
	parts := make([]string, 0, len(names))
	for _, n := range names {
		parts = append(parts, render(flags, n))
	}
	return strings.Join(parts, "; ")
}

// run executes the candidate's migrillian with the given arguments, capturing
// everything it writes.
func run(args ...string) (string, int, error) {
	cmd := vhelp.Command(vhelp.CandidateBin("migrillian"), args...)
	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out
	err := cmd.Run()
	switch e := err.(type) {
	case nil:
		return out.String(), 0, nil
	case *exec.ExitError:
		return out.String(), e.ExitCode(), nil
	default:
		return out.String(), -1, err
	}
}

func main() {
	t := vhelp.New()

	ids := []string{
		"migrillian-prints-its-usage-and-exits-zero",
		"migrillian-declares-its-ct-source-flags",
		"migrillian-declares-its-trillian-sink-flags",
		"migrillian-declares-its-http-connection-flags",
		"migrillian-rejects-an-unknown-flag",
		"migrillian-declares-exactly-its-own-flags",
	}

	usage, rc, err := run("--help")
	if err != nil {
		// No binary at all: every case fails, none is skipped.
		for _, id := range ids {
			t.Check(id, false, "cannot run migrillian: %v", err)
		}
		t.Done()
		return
	}

	// --- 1. the command exists and describes itself -------------------------
	firstLine := usage
	if nl := strings.IndexByte(usage, '\n'); nl >= 0 {
		firstLine = usage[:nl]
	}
	t.Equal(ids[0],
		strings.TrimSpace(strings.Join([]string{
			"rc=" + itoa(rc),
			"banner=" + boolStr(strings.HasPrefix(firstLine, "Usage of ")),
			"names-itself=" + boolStr(strings.HasSuffix(firstLine, "/migrillian:")),
		}, " ")),
		"rc=0 banner=true names-itself=true")

	flags := parseUsage(usage)

	// --- 2. where entries come from ----------------------------------------
	t.Equal(ids[1],
		renderAll(flags, "ct_log_uri", "pub_key", "ct_batch_size", "ct_fetchers",
			"start_index", "end_index", "mirror"),
		strings.Join([]string{
			`ct_log_uri string default="https://ct.googleapis.com/aviator"`,
			"pub_key string default=-",
			"ct_batch_size int default=512",
			"ct_fetchers int default=2",
			"start_index int default=-",
			"end_index int default=-",
			"mirror bool default=-",
		}, "; "))

	// --- 3. where they go ---------------------------------------------------
	t.Equal(ids[2],
		renderAll(flags, "trillian_uri", "log_id", "grpc_dial_timeout", "submitters", "submitter_batches"),
		strings.Join([]string{
			`trillian_uri string default="localhost:8090"`,
			"log_id int default=-",
			"grpc_dial_timeout duration default=5s",
			"submitters int default=2",
			"submitter_batches int default=5",
		}, "; "))

	// --- 4. and how the fetching side is pooled -----------------------------
	t.Equal(ids[3],
		renderAll(flags, "max_idle_conns", "max_idle_conns_per_host"),
		"max_idle_conns int default=100; max_idle_conns_per_host int default=10")

	// --- 5. an option it does not define is refused, with the flag package's
	//        own exit status, not ignored ------------------------------------
	badOut, badRC, err := run("--harbor_not_a_flag")
	if err != nil {
		t.Check(ids[4], false, "cannot run migrillian: %v", err)
	} else {
		t.Equal(ids[4],
			"rc="+itoa(badRC)+" usage-reprinted="+boolStr(strings.Contains(badOut, "Usage of ")),
			"rc=2 usage-reprinted=true")
	}

	// --- 6. the complete set of options the command owns --------------------
	// Library-registered flags (glog, and the two flag.CommandLine globals of
	// CLI.md 2.1) are subtracted; what is left must be exactly migrillian's
	// own fourteen of CLI.md 4.7.
	var own []string
	for name := range flags {
		if !libraryFlags[name] {
			own = append(own, name)
		}
	}
	sort.Strings(own)
	t.Equal(ids[5], strings.Join(own, " "),
		"ct_batch_size ct_fetchers ct_log_uri end_index grpc_dial_timeout log_id "+
			"max_idle_conns max_idle_conns_per_host mirror pub_key start_index "+
			"submitter_batches submitters trillian_uri")

	t.Done()
}

func boolStr(b bool) string {
	if b {
		return "true"
	}
	return "false"
}

func itoa(n int) string {
	if n < 0 {
		return "-" + itoa(-n)
	}
	if n < 10 {
		return string(rune('0' + n))
	}
	return itoa(n/10) + string(rune('0'+n%10))
}

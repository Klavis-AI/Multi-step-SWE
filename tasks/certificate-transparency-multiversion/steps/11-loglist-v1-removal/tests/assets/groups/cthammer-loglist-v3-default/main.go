// Group cthammer-loglist-v3-default.
//
// trillian/integration/ct_hammer is the third command-line consumer of the log
// list that v1.1.4 migrates from loglist to loglist3. Its observable surface
// has three parts, all of them outside the process:
//
//   - the flag grammar it accepts: v1.1.4 registers klog's flags, so the
//     -one_output / -log_file / -skip_headers family exists and glog's does
//     not;
//   - the default value of --log_list, which moves from the unversioned
//     all_logs_list.json to the v3 one;
//   - how --src_log_name resolves: v1.1.4 searches the v3 list, so a prefix
//     that matches several v3 descriptions names all of them, and a name that
//     matches exactly one is the one case that is not refused at all.
//
// None of that survives the previous release. v1.1.3's ct_hammer parses the
// same bytes with loglist.NewFromJSON, and the v1 schema has no "operators"
// member, so it reports "No log with name like ..." for every name in the
// pinned list -- including names its own release's binary would have resolved.
//
// Nothing the group does can reach a real log: every hostname in the pinned
// list ends in .invalid. Two of the three resolution cases are refusals and
// stop before a client is built; the unique-match case is not refused, so the
// binary goes on to try to reach the log it resolved and fails there. That is
// why every run below is bounded at 60 seconds of wall clock and killed if it
// has not finished -- a resolver that stalls on .invalid must cost the case,
// not the group.
package main

import (
	"bytes"
	"encoding/pem"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	bin := vhelp.CandidateBin("ct_hammer")
	list := vhelp.GroupFile("bench-loglist3.json")
	scratch := os.Getenv("CT_SCRATCH")

	// The pinned source-log config. cthammer refuses to run at all without a
	// --log_config, and the file has to be a real LogConfigSet or the parse
	// error would mask everything downstream, so the group synthesises one
	// from the pinned log public key rather than shipping a second fixture.
	cfgPath := filepath.Join(scratch, "cthammer-log-config.textproto")
	if err := os.WriteFile(cfgPath, []byte(pinnedLogConfigTextproto(t)), 0600); err != nil {
		t.Fatalf("write log config: %v", err)
	}

	// run executes the binary with the given arguments and returns stdout and
	// stderr merged, which is where glog and klog both write their fatal
	// lines. Output is captured, never inherited: see vhelp.Command.
	//
	// Bounded at 60 seconds, the same wall-clock bound the shell drivers in
	// this step give every candidate binary they start. Only the PID this
	// function started is ever signalled. A run killed at the bound comes back
	// with whatever it had printed and a non-zero rc, so the case that started
	// it fails and the group still reports every case.
	run := func(args ...string) (string, int) {
		cmd := vhelp.Command(bin, args...)
		var buf bytes.Buffer
		cmd.Stdout, cmd.Stderr = &buf, &buf
		if err := cmd.Start(); err != nil {
			t.Fatalf("start ct_hammer: %v", err)
		}
		timer := time.AfterFunc(60*time.Second, func() { _ = cmd.Process.Kill() })
		err := cmd.Wait()
		timer.Stop()
		rc := 0
		if ee, ok := err.(interface{ ExitCode() int }); ok {
			rc = ee.ExitCode()
		} else if err != nil {
			rc = -1
		}
		return buf.String(), rc
	}

	// --- case 1: control. The binary runs and reports its usage.
	help, helpRC := run("--help")
	t.Check("control-cthammer-help-exits-zero", helpRC == 0 && strings.Contains(help, "-src_log_name"),
		"rc=%d, usage names -src_log_name: %v", helpRC, strings.Contains(help, "-src_log_name"))

	// --- case 2: the default log list is the v3 one. Both releases say the
	// default in the usage text, so this is read from the binary's own words,
	// not from a source file.
	t.Check("default-log-list-is-the-v3-all-logs-url",
		strings.Contains(help, "https://www.gstatic.com/ct/log_list/v3/all_logs_list.json"),
		"usage does not name the v3 all-logs URL")

	// --- case 3: the flag grammar is klog's, not glog's. The six names below
	// are the ones whose presence distinguishes a v1.1.4 binary: klog's set is
	// a strict superset of glog's here (CLI.md's logging-flag preamble), so
	// none of them exists in a glog build and all six exist in every klog one.
	flags := flagNames(help)
	wantKlogOnly := []string{"add_dir_header", "log_file", "log_file_max_size", "one_output", "skip_headers", "skip_log_headers"}
	missing := []string{}
	for _, w := range wantKlogOnly {
		if !flags[w] {
			missing = append(missing, w)
		}
	}
	t.Check("flag-grammar-is-klogs-not-glogs", len(missing) == 0,
		"klog-only flags missing from usage: %v", missing)

	// --- case 4: a prefix matching three v3 descriptions names all three,
	// in log-list order. This is the one observation that could not be made
	// at all without a v3 reader: the v1 reader finds no logs in this file.
	out, rc := run(
		"--log_list="+list,
		"--src_log_name=Bench",
		"--log_config="+cfgPath,
	)
	// What is graded is which v3 descriptions were matched and in what order,
	// not the sentence upstream wraps them in: that wording is upstream's own
	// and no document gives it.
	matched := matchedNames(out, []string{
		"Google Bench Usable Log", "Google Bench Readonly Log", "Google Bench Qualified Log",
		"Shop Retired Log", "Shop Pending Log", "Shop Usable Log",
	})
	wantMatched := "Google Bench Usable Log|Google Bench Readonly Log|Google Bench Qualified Log"
	t.Check("ambiguous-name-names-every-v3-match",
		rc != 0 && matched == wantMatched,
		"rc=%d, matched=%q want=%q", rc, matched, wantMatched)

	// --- case 5: the match count is exactly the three usable/readonly/
	// qualified logs under the "Google Bench" operator. The retired, pending
	// and Shop logs must not appear, and neither must the operator name.
	t.Check("match-count-is-three-and-excludes-other-logs",
		matched == wantMatched &&
			!strings.Contains(out, "Retired") &&
			!strings.Contains(out, "Pending") &&
			!strings.Contains(out, "Harbor Bench Shop"),
		"an excluded log or operator leaked into the match list: matched=%q", matched)

	// --- case 6: a name matching exactly one v3 log is the one case that is
	// *not* refused, and that is all this case grades. Resolution by name has
	// no other observable (TEST_INTERFACE section 6, "A refusal that names
	// what it matched"): what a tool prints once it has resolved a log is not
	// part of the contract, so nothing here reads it.
	out, _ = run(
		"--log_list="+list,
		"--src_log_name=Shop Usable",
		"--log_config="+cfgPath,
	)
	// Graded: the run produced neither identifier a refusal admits -- no log
	// description out of the pinned list, and not the list it was told to read
	// -- so it did not refuse this name. The previous release cannot reach
	// that state: its v1 reader finds no logs at all in a v3 document, so the
	// same command line refuses and names the list, exactly as case 7 does.
	unique := matchedNames(out, []string{
		"Google Bench Usable Log", "Google Bench Readonly Log", "Google Bench Qualified Log",
		"Shop Retired Log", "Shop Pending Log", "Shop Usable Log",
	})
	t.Check("unique-name-is-not-refused",
		unique == "" && !strings.Contains(out, "bench-loglist3.json"),
		"the unique v3 name was refused: matched=%q out=%q", unique, firstLines(out, 3))

	// --- case 7: a name matching nothing is refused, and the refusal quotes
	// the list path -- which proves it read the list rather than failing
	// before it got there.
	out, rc = run(
		"--log_list="+list,
		"--src_log_name=Nowhere",
		"--log_config="+cfgPath,
	)
	// Graded: it refused, it named the list it was told to read and the name
	// it was asked for, and it matched none of the list's logs. The refusal's
	// wording is not graded.
	t.Check("unknown-name-is-refused-after-reading-the-list",
		rc != 0 &&
			strings.Contains(out, "Nowhere") &&
			strings.Contains(out, "bench-loglist3.json") &&
			matchedNames(out, []string{
				"Google Bench Usable Log", "Shop Usable Log",
			}) == "",
		"rc=%d, refusal does not name the list and the name: %q", rc, firstLines(out, 3))

	// --- case 8: a v3 list that is not valid JSON is reported as such, which
	// is the v3 reader's own error string and not a JSON-decoding accident of
	// some other library.
	truncated := filepath.Join(scratch, "truncated-loglist.json")
	if err := os.WriteFile(truncated, []byte(benchLogList3JSON[:200]), 0600); err != nil {
		t.Fatalf("write truncated list: %v", err)
	}
	out, rc = run(
		"--log_list="+truncated,
		"--src_log_name=Bench",
		"--log_config="+cfgPath,
	)
	// Graded: a truncated list is refused outright rather than treated as an
	// empty list, and the refusal happens at parse time -- before any name is
	// looked up, so the name that *would* have matched is never mentioned.
	// The parser's wording is not graded.
	t.Check("malformed-v3-list-is-reported-by-the-v3-parser",
		rc != 0 &&
			!strings.Contains(out, "Google Bench Usable Log") &&
			!strings.Contains(out, "bench.invalid"),
		"rc=%d, truncated list was not refused at parse time: %q", rc, firstLines(out, 3))

	t.Done()
}

// matchedNames returns the candidates that appear in out, in the order they
// first appear, joined with "|". It lets a case assert which log descriptions
// a binary named without asserting the sentence it named them in.
func matchedNames(out string, candidates []string) string {
	type hit struct {
		at   int
		name string
	}
	hits := []hit{}
	for _, c := range candidates {
		if i := strings.Index(out, c); i >= 0 {
			hits = append(hits, hit{i, c})
		}
	}
	sort.Slice(hits, func(i, j int) bool { return hits[i].at < hits[j].at })
	names := make([]string, len(hits))
	for i, h := range hits {
		names[i] = h.name
	}
	return strings.Join(names, "|")
}

// firstLines is a diagnostic helper: the first n lines of out, one string.
func firstLines(out string, n int) string {
	lines := strings.Split(out, "\n")
	if len(lines) > n {
		lines = lines[:n]
	}
	return strings.Join(lines, " | ")
}

// flagNames extracts the long flag names from a Go flag package usage dump.
func flagNames(usage string) map[string]bool {
	names := map[string]bool{}
	for _, line := range strings.Split(usage, "\n") {
		line = strings.TrimRight(line, "\r")
		if !strings.HasPrefix(line, "  -") {
			continue
		}
		rest := strings.TrimPrefix(line, "  -")
		if rest == "" {
			continue
		}
		end := strings.IndexAny(rest, " \t")
		if end < 0 {
			names[rest] = true
			continue
		}
		names[rest[:end]] = true
	}
	return names
}

// pinnedLogConfigTextproto renders the minimal LogConfigSet cthammer accepts
// on --log_config: one log whose public key is the pinned minted log key. It
// is synthesised rather than shipped as a file so that the group has exactly
// one data file beside it, the pinned v3 log list.
func pinnedLogConfigTextproto(t *vhelp.T) string {
	pub := vhelp.ReadFixture("minted", "keys", "log-pubkey.pem")
	der := pemBody(t, pub)
	var b strings.Builder
	b.WriteString("config {\n  log_id: 1001\n  prefix: \"bench\"\n  public_key: { der: \"")
	for _, c := range der {
		fmt.Fprintf(&b, "\\x%02x", c)
	}
	b.WriteString("\" }\n}\n")
	return b.String()
}

// pemBody returns the DER inside the first PEM block of b.
func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("minted/keys/log-pubkey.pem is not PEM")
	}
	return blk.Bytes
}

// Group ctserver-quota-flag-grammar.
//
// The two switches that turn this release's quota work on and off are
// command-line flags of ct_server:
//
//	quotaRemote       = flag.Bool("quota_remote", true, ...)
//	quotaIntermediate = flag.Bool("quota_intermediate", true, ...)
//
// A command-line capability is graded by running the binary as a subprocess
// and looking at what it writes and what status it exits with, so that is what
// this group does. Two things are observed, and both matter:
//
//   - the usage text the binary prints for itself declares both flags, as
//     booleans, and says each defaults to true. The default is half the
//     milestone: quota is requested unless the operator opts out, not the
//     other way round.
//   - the flag parser actually accepts them. A usage banner can be wrong;
//     the exit status of a parse cannot. `--quota_remote=false
//     --quota_intermediate=false --help` must exit 0 (the flag package's
//     answer to --help), and the control `--quota_bogus=false --help` must
//     not, so "exit 0" is not something this binary does for any input.
//   - the whole non-glog flag set is exactly seventeen names, CLI.md §2.1's
//     totals row for this release. Naming two flags says they were added; the
//     exact set says nothing else was, and it is the only assertion here that
//     notices a package-scope `flag` global left behind anywhere in the ctfe
//     import graph. Two of the seventeen come from that graph rather than
//     from ct_server's own file, which is why the total is 17 and not 15.
//
// SCOPE-NET/SCOPE-TIME: --help parses flags and returns; no socket is opened,
// no config is read, no clock is consulted. The child's output is captured
// into this process's buffers and never reaches its stdout, and vhelp.Command
// strips CT_VERIFY_NONCE from the child's environment so a candidate binary
// cannot forge a result line.
//
// v1.0.17's ct_server declares neither flag, so its usage text names neither,
// its parser rejects both, and its non-glog set is the same list minus those
// two -- fifteen names.
package main

import (
	"bytes"
	"os/exec"
	"sort"
	"strings"

	"ctverify/vhelp"
)

// runServer runs ct_server with the given arguments and returns its combined
// output and its exit status. A status of -1 means the process could not be
// started or did not exit normally.
func runServer(args ...string) (string, int) {
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"), args...)
	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out
	err := cmd.Run()
	switch e := err.(type) {
	case nil:
		return out.String(), 0
	case *exec.ExitError:
		if code := e.ExitCode(); code >= 0 {
			return out.String(), code
		}
	}
	return out.String(), -1
}

// flagBlock finds one flag in `flag`-package usage text and returns the rest
// of its declaration line (empty for a bool, "duration"/"string"/... for the
// others) together with the indented description that follows it.
func flagBlock(usage, name string) (decl, body string, found bool) {
	lines := strings.Split(usage, "\n")
	for i, raw := range lines {
		line := strings.TrimSpace(raw)
		if !strings.HasPrefix(line, "-"+name) {
			continue
		}
		rest := strings.TrimPrefix(line, "-"+name)
		if rest != "" && !strings.HasPrefix(rest, " ") && !strings.HasPrefix(rest, "=") {
			continue // a longer flag name that merely starts with this one
		}
		var desc []string
		for _, next := range lines[i+1:] {
			if strings.TrimSpace(next) == "" || !strings.HasPrefix(next, "  ") ||
				strings.HasPrefix(strings.TrimSpace(next), "-") {
				break
			}
			desc = append(desc, strings.TrimSpace(next))
		}
		return strings.TrimSpace(rest), strings.Join(desc, " "), true
	}
	return "", "", false
}

// glogFlags are the seven the logging library registers. They are excluded
// because they belong to a vendored dependency, not to this milestone, and
// their set has moved between releases.
var glogFlags = map[string]bool{
	"alsologtostderr": true, "log_backtrace_at": true, "log_dir": true,
	"logtostderr": true, "stderrthreshold": true, "v": true, "vmodule": true,
}

// nonGlogFlagNames returns the sorted, de-duplicated names of every flag the
// usage text declares, less the glog set. A flag declaration is a line of the
// form "  -name" -- continuation lines are indented further and are skipped.
func nonGlogFlagNames(usage string) []string {
	seen := map[string]bool{}
	for _, line := range strings.Split(usage, "\n") {
		if !strings.HasPrefix(line, "  -") {
			continue
		}
		name := strings.TrimPrefix(line, "  -")
		if i := strings.IndexFunc(name, func(r rune) bool {
			return !(r == '_' || r == '.' || r >= '0' && r <= '9' ||
				r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z')
		}); i >= 0 {
			name = name[:i]
		}
		if name != "" && !glogFlags[name] {
			seen[name] = true
		}
	}
	out := make([]string, 0, len(seen))
	for n := range seen {
		out = append(out, n)
	}
	sort.Strings(out)
	return out
}

func main() {
	t := vhelp.New()

	usage, code := runServer("--help")
	if usage == "" {
		t.Fatalf("ct_server --help printed nothing (exit %d)", code)
	}

	// --- 1..2. both flags are declared, and declared as booleans ------------
	// The flag package prints no type token for a bool, which is how a bool is
	// told apart from a string flag in usage text.
	for _, f := range []struct{ id, name string }{
		{"quota-remote-declared-as-a-boolean-flag", "quota_remote"},
		{"quota-intermediate-declared-as-a-boolean-flag", "quota_intermediate"},
	} {
		decl, _, found := flagBlock(usage, f.name)
		t.Check(f.id, found && decl == "",
			"ct_server usage: -%s found=%v type token %q, want a declared bool", f.name, found, decl)
	}

	// --- 3..4. and both default to true -------------------------------------
	for _, f := range []struct{ id, name string }{
		{"quota-remote-defaults-to-true", "quota_remote"},
		{"quota-intermediate-defaults-to-true", "quota_intermediate"},
	} {
		_, body, found := flagBlock(usage, f.name)
		t.Check(f.id, found && strings.HasSuffix(body, "(default true)"),
			"ct_server usage: -%s description is %q, want it to end with (default true)", f.name, body)
	}

	// --- 5. the parser accepts them ------------------------------------------
	// --help makes flag.Parse print usage and exit 0, but only after every
	// earlier argument has parsed; an unknown one short-circuits that.
	_, code = runServer("--quota_remote=false", "--quota_intermediate=false", "--help")
	t.Equal("both-quota-flags-accepted-by-the-parser", code, 0)

	// --- 6. control: an unknown quota flag is not ----------------------------
	// Without this, case 5 would pass for a binary that exits 0 whatever it is
	// handed.
	_, code = runServer("--quota_bogus=false", "--help")
	t.Check("unknown-quota-flag-rejected-by-the-parser", code > 0,
		"ct_server --quota_bogus=false --help exited %d, want a non-zero parse failure", code)

	// --- 7. the whole declared set, not just the two flags above -------------
	// CLI.md §2.1's totals row puts ct_server at seventeen flags at this
	// release, counting every flag --help lists except the logging library's
	// own seven. Fifteen are declared in ct_server's own main.go; the other
	// two -- allow_verification_with_non_compliant_keys, from the root
	// package, and by_range, from ctfe -- are package-scope globals
	// registered on flag.CommandLine by packages in the ctfe import graph,
	// which is exactly why the exact set is worth asserting and a count of
	// the local declarations is not.
	want := []string{
		"allow_verification_with_non_compliant_keys",
		"by_range",
		"etcd_http_service",
		"etcd_metrics_service",
		"etcd_servers",
		"get_sth_interval",
		"http_endpoint",
		"log_config",
		"log_rpc_server",
		"max_get_entries",
		"metrics_endpoint",
		"quota_intermediate",
		"quota_remote",
		"rpc_deadline",
		"tracing",
		"tracing_percent",
		"tracing_project_id",
	}
	got := nonGlogFlagNames(usage)
	t.Check("declared-flag-set-is-exactly-seventeen",
		strings.Join(got, " ") == strings.Join(want, " "),
		"ct_server non-glog flags are [%s], want [%s]",
		strings.Join(got, " "), strings.Join(want, " "))

	t.Done()
}

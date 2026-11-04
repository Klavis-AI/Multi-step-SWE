// Group glog-to-klog-flag-migration.
//
// The same upstream commit that removes log list v1 also switches every
// command in the tree from github.com/golang/glog to k8s.io/klog/v2. klog's
// flag set is a strict superset of glog's (CLI.md section 1): all seven glog
// names survive, and klog adds six -- add_dir_header, log_file,
// log_file_max_size, one_output, skip_headers and skip_log_headers. Those six
// are what tell a v1.1.4 binary from a v1.1.3 one.
//
// The other half of the change is *when* the flags are registered. glog
// registers in its own package init(), so at v1.1.3 any binary with glog
// anywhere in its import graph carries the seven whether it asked for them or
// not. klog registers nothing until a main() calls klog.InitFlags(nil), so
// from v1.1.4 the logging flags are opt-in and upstream added that call to
// some main()s and not others. "Add klog everywhere" is therefore exactly as
// wrong as "add it nowhere", and which binaries opted in is asserted here.
//
// This is observed entirely from outside each process: a binary either
// accepts the six klog-only flags on its command line and prints them in its
// usage, or it does not. Twelve binaries are run below.
//
//   - Seven must carry the whole klog surface: certcheck, crlcheck, the
//     witness feeder, the witness itself, ct_server, the witness client and
//     migrillian (cases 1, 2).
//   - Four must NOT have gained it, as a negative control on the case above:
//     dumpscts, the submission hammer, preloader and scanlog, none of which
//     calls klog.InitFlags at v1.1.4 (cases 1, 3).
//   - chainfix is the twelfth. It parses no flags at all, so there is no set
//     for a logger to register into; it carries none and exits 2 (case 9).
//
// Three of the seven -- certcheck, crlcheck and the feeder -- have their
// ENTIRE flag set pinned, product flags included (cases 4 to 6), so gaining
// klog's names while losing a product flag fails. ct_server's whole set is
// pinned the same way in case 8. Case 7 reads the klog-only flags' rendered
// types rather than just their names.
//
// Every binary is run with --help and nothing else. Nothing here dials, and
// the group is fixture-free.
package main

import (
	"sort"
	"strings"

	"ctverify/vhelp"
)

// klogOnly are the flags klog registers and glog never had.
var klogOnly = []string{
	"add_dir_header",
	"log_file",
	"log_file_max_size",
	"one_output",
	"skip_headers",
	"skip_log_headers",
}

// klogSurface is the exact flag set of a v1.1.4 command that calls
// klog.InitFlags, parametrised by the command's own product flags. Keeping the
// product flags in the expectation is what stops this group from passing for a
// binary that gained klog's flags but lost its own.
var commands = []struct {
	name    string
	product []string
}{
	{"certcheck", []string{"check_eku", "check_name", "check_name_constraint", "check_path_len", "check_revocation", "check_time", "check_unknown_critical_exts", "intermediate", "root", "strict", "system_roots", "v", "validate", "verbose"}},
	{"crlcheck", []string{"ca", "cert", "strict"}},
	{"feeder", []string{"log_list_url", "poll", "witness_url"}},
	{"witness", []string{"config_file", "db_file", "listen", "private_key"}},
	{"ct_server", nil}, // asserted by count below, product flags vary by backend
	{"client", nil},
	{"migrillian", nil},
}

// noKlog are the four commands the v1.1.4 tree ships without a klog flag set.
var noKlog = []string{"dumpscts", "hammer", "preloader", "scanlog"}

func main() {
	t := vhelp.New()

	// help runs one candidate binary with --help and returns its usage text.
	help := func(name string) (string, int) {
		cmd := vhelp.Command(vhelp.CandidateBin(name), "--help")
		out, err := cmd.CombinedOutput()
		rc := 0
		if ee, ok := err.(interface{ ExitCode() int }); ok {
			rc = ee.ExitCode()
		} else if err != nil {
			rc = -1
		}
		return string(out), rc
	}

	names := make([]string, 0, len(commands))
	for _, c := range commands {
		names = append(names, c.name)
	}

	// --- case 1: control. Every migrated command still starts and prints a
	// usage listing.
	bad := []string{}
	all := make([]string, 0, len(commands)+len(noKlog))
	for _, c := range commands {
		all = append(all, c.name)
	}
	all = append(all, noKlog...)
	for _, n := range all {
		if _, rc := help(n); rc != 0 {
			bad = append(bad, n)
		}
	}
	t.Check("control-every-command-still-starts", len(bad) == 0,
		"--help did not exit 0 for: %v", bad)

	// --- case 2: every command that called klog.InitFlags at v1.1.4 accepts
	// the six klog-only flags, and its usage names them.
	missing := []string{}
	for _, n := range names {
		text, rc := help(n)
		if rc != 0 {
			missing = append(missing, n+"(rc)")
			continue
		}
		got := flagNames(text)
		for _, f := range klogOnly {
			if !got[f] {
				missing = append(missing, n+"."+f)
			}
		}
	}
	t.Check("klog-flag-surface-present-on-every-migrated-command", len(missing) == 0,
		"klog-only flags absent: %v", missing)

	// --- case 3: the four commands that never called klog.InitFlags are
	// unchanged. This is a negative control on the case above: it fails if a
	// candidate churns flags into binaries the release did not touch.
	leaked := []string{}
	for _, n := range noKlog {
		text, rc := help(n)
		if rc != 0 {
			leaked = append(leaked, n+"(rc)")
			continue
		}
		got := flagNames(text)
		for _, f := range klogOnly {
			if got[f] {
				leaked = append(leaked, n+"."+f)
			}
		}
	}
	t.Check("untouched-commands-did-not-gain-the-flag-set", len(leaked) == 0,
		"klog-only flags leaked into: %v", leaked)

	// --- cases 4 to 6: the exact flag set of three commands whose product
	// flags are pinned, so gaining klog's flags is not enough -- the binary
	// must have kept its own.
	exact := map[string][]string{
		"certcheck": {"check_eku", "check_name", "check_name_constraint", "check_path_len", "check_revocation", "check_time", "check_unknown_critical_exts", "intermediate", "root", "strict", "system_roots", "validate", "verbose"},
		"crlcheck":  {"ca", "cert", "strict"},
		"feeder":    {"log_list_url", "poll", "witness_url"},
	}
	for _, n := range []string{"certcheck", "crlcheck", "feeder"} {
		text, _ := help(n)
		got := flagNames(text)
		wantSet := map[string]bool{}
		for _, f := range append(append([]string{}, klogFlags()...), exact[n]...) {
			wantSet[f] = true
		}
		extra, absent := []string{}, []string{}
		for f := range got {
			if !wantSet[f] {
				extra = append(extra, f)
			}
		}
		for f := range wantSet {
			if !got[f] {
				absent = append(absent, f)
			}
		}
		sort.Strings(extra)
		sort.Strings(absent)
		t.Check("exact-flag-set-of-"+n, len(extra) == 0 && len(absent) == 0,
			"unexpected %v, missing %v", extra, absent)
	}

	// --- case 7: the klog-only flags are declared with the types klog gives
	// them, not merely present by name. The flag package prints a String flag
	// as "-name string" and a Bool flag bare, so the usage dump states each
	// flag's type without this case grading any of the libraries' prose --
	// which is upstream's and which no document gives.
	text, _ := help("feeder")
	t.Check("klog-only-flags-have-klogs-types",
		strings.Contains(text, "  -log_file string\n") &&
			strings.Contains(text, "  -log_file_max_size uint\n") &&
			strings.Contains(text, "  -one_output\n") &&
			strings.Contains(text, "  -add_dir_header\n") &&
			strings.Contains(text, "  -skip_headers\n") &&
			strings.Contains(text, "  -skip_log_headers\n"),
		"the klog-only flags are not declared with klog's own types")

	// --- case 8: ct_server's own flag set is exactly the nineteen CLI.md
	// section 2.1 gives for v1.1.4 -- eighteen declared in the command plus
	// align_getentries from the ctfe package -- and klog's thirteen, and
	// nothing else. This is TEST_INTERFACE requirement R17: the migration had
	// to leave the server's own command line alone while replacing the
	// logging library underneath it, and no other group in this step looks at
	// that set. It is a negative control in both directions: a candidate that
	// kept glog fails on the klog names, and one that rebuilt the server's
	// flags from a later release fails on the extras.
	ctServer := []string{
		"align_getentries", "etcd_http_service", "etcd_metrics_service",
		"etcd_servers", "get_sth_interval", "handler_prefix", "http_endpoint",
		"log_config", "log_rpc_server", "mask_internal_errors",
		"max_get_entries", "metrics_endpoint", "pkcs11_module_path",
		"quota_intermediate", "quota_remote", "rpc_deadline", "tracing",
		"tracing_percent", "tracing_project_id",
	}
	srvText, _ := help("ct_server")
	got := flagNames(srvText)
	wantSet := map[string]bool{}
	for _, f := range append(append([]string{}, klogFlags()...), ctServer...) {
		wantSet[f] = true
	}
	extra, absent := []string{}, []string{}
	for f := range got {
		if !wantSet[f] {
			extra = append(extra, f)
		}
	}
	for f := range wantSet {
		if !got[f] {
			absent = append(absent, f)
		}
	}
	sort.Strings(extra)
	sort.Strings(absent)
	t.Check("exact-flag-set-of-ct_server", len(extra) == 0 && len(absent) == 0,
		"unexpected %v, missing %v", extra, absent)

	// --- case 9: chainfix is on the same "carries no logging flag" row of
	// CLI.md section 1 as the four above, but for a different reason: it
	// imports no flag package at all and reads its three arguments straight
	// out of os.Args (CLI.md section 4.6), so there is no flag set for a
	// logger to register into and nothing for the swap to change. Given one
	// argument it runs off the end of that slice, which is the documented
	// behaviour -- no argument-count check, an index-out-of-range, exit 2.
	// What is graded is that status and the absence of every logging flag
	// name from its output; the panic text itself belongs to the Go runtime
	// and is not read.
	cfOut, cfErr := vhelp.Command(vhelp.CandidateBin("chainfix"), "--help").CombinedOutput()
	cfRC := 0
	if ee, ok := cfErr.(interface{ ExitCode() int }); ok {
		cfRC = ee.ExitCode()
	} else if cfErr != nil {
		cfRC = -1
	}
	cfGot := flagNames(string(cfOut))
	cfLeak := []string{}
	for _, f := range klogFlags() {
		if cfGot[f] {
			cfLeak = append(cfLeak, f)
		}
	}
	sort.Strings(cfLeak)
	t.Check("chainfix-parses-no-flags-at-all", cfRC == 2 && len(cfLeak) == 0,
		"rc=%d (want 2), logging flags present: %v", cfRC, cfLeak)

	t.Done()
}

// flagNames extracts long flag names from a Go flag usage dump, the same way
// the flag package prints them.
func flagNames(usage string) map[string]bool {
	names := map[string]bool{}
	for _, line := range strings.Split(usage, "\n") {
		line = strings.TrimRight(line, "\r")
		if !strings.HasPrefix(line, "  -") {
			continue
		}
		rest := strings.TrimPrefix(line, "  -")
		if rest == "" || strings.HasPrefix(rest, " ") {
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

// klogFlags is the full klog v2 flag set as registered by InitFlags(nil).
func klogFlags() []string {
	return []string{
		"add_dir_header", "alsologtostderr", "log_backtrace_at", "log_dir",
		"log_file", "log_file_max_size", "logtostderr", "one_output",
		"skip_headers", "skip_log_headers", "stderrthreshold", "v", "vmodule",
	}
}

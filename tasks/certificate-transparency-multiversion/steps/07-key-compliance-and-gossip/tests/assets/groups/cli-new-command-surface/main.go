// Group cli-new-command-surface.
//
// The command-line surface v1.0.21 adds, all of it read out of the binaries'
// own usage text and exit statuses -- the command's public interface -- and
// never out of a source file. Every case here moved out of build-clean, which
// is a gate group and so may assert only what the previous milestone already
// had:
//
//   - the command set grows from fourteen to sixteen: gossip/minimal ships
//     goshawk and gosmin, neither of which exists at v1.0.19;
//   - goshawk carries the five scanning flags and gosmin exactly one, with
//     the types and defaults CLI.md 4.10 documents, and both refuse to start
//     without --config;
//   - ct_server declares the new global --handler_prefix (CLI.md 2.1);
//   - migrillian's command line becomes multi-tenant: the eight single-log
//     flags (--ct_log_uri, --pub_key, --trillian_uri, --log_id,
//     --ct_batch_size, --start_index, --end_index, --mirror) give way to
//     --config plus the master-election flags, while the six process-wide
//     tuning flags stay (CLI.md 4.7);
//   - certcheck gains --system_roots (CLI.md 4.3).
//
// Where a flag's type and default are documented, they are read back out of
// the usage text too. Go's flag package prints the kind rather than the width
// (an Int64 flag renders as "int"), omits the type word entirely for a bool,
// and omits the "(default ...)" clause when the default is the type's zero
// value -- the conventions CLI.md's preamble states.
//
// Two cases assert flags that are NOT new -- migrillian's six tuning flags
// and the five inherited certcheck flags beside --system_roots. They are the
// controls that keep "the command line changed" from being satisfiable by a
// command that simply dropped most of its flags, and they cannot make this
// group payable to a no-op: at v1.0.19 goshawk and gosmin cannot be executed
// at all, ct_server has no --handler_prefix and certcheck no --system_roots,
// so the group as a whole fails against that tree.
//
// SCOPE-NET / SCOPE-TIME: no clock, no network, no host property. Each child
// is run with --help or with a deliberately incomplete command line; its
// output is captured into this process's own buffer and never shares its
// stdout, and CT_VERIFY_NONCE is stripped from the child's environment by
// vhelp.Command.
package main

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"sort"
	"strings"

	"ctverify/vhelp"
)

// The two commands gossip/minimal brings with it.
var newCommands = []string{"goshawk", "gosmin"}

// usageOf runs `<bin> --help` and returns everything it wrote.
func usageOf(name string) (string, error) {
	cmd := vhelp.Command(vhelp.CandidateBin(name), "--help")
	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out
	err := cmd.Run()
	if out.Len() == 0 && err != nil {
		return "", err
	}
	return out.String(), nil
}

// exitOf runs a command to completion and reports its exit status, or -1 if
// it could not be started at all.
func exitOf(name string, arg ...string) int {
	cmd := vhelp.Command(vhelp.CandidateBin(name), arg...)
	var sink bytes.Buffer
	cmd.Stdout = &sink
	cmd.Stderr = &sink
	err := cmd.Run()
	if err == nil {
		return 0
	}
	if ee, ok := err.(*exec.ExitError); ok {
		return ee.ExitCode()
	}
	return -1
}

// declares reports whether usage text declares exactly this flag name.
func declares(usage, flag string) bool {
	for _, line := range strings.Split(usage, "\n") {
		if !strings.HasPrefix(line, "  -"+flag) {
			continue
		}
		rest := strings.TrimPrefix(line, "  -"+flag)
		if rest == "" || strings.HasPrefix(rest, " ") {
			return true
		}
	}
	return false
}

// flagReport renders `name=true/false` for each flag, in the order given, so a
// failure names every flag that disagreed rather than just the first.
func flagReport(usage string, flags ...string) string {
	parts := make([]string, 0, len(flags))
	for _, f := range flags {
		parts = append(parts, fmt.Sprintf("%s=%v", f, declares(usage, f)))
	}
	return strings.Join(parts, " ")
}

// flagSpec renders one flag as "<name>=<type>/<default>" exactly as the
// command's own usage text declares it, so a type or a default that drifts is
// named. A bool prints no type word, so "bool" is substituted; a default that
// is the type's zero value is not printed at all, so "-" is substituted.
func flagSpec(usage, name string) string {
	lines := strings.Split(usage, "\n")
	for i, line := range lines {
		if !strings.HasPrefix(line, "  -"+name) {
			continue
		}
		rest := strings.TrimPrefix(line, "  -"+name)
		if rest != "" && !strings.HasPrefix(rest, " ") {
			continue
		}
		typ := strings.TrimSpace(rest)
		if typ == "" {
			typ = "bool"
		}
		def := "-"
		for j := i + 1; j < len(lines) && !strings.HasPrefix(lines[j], "  -"); j++ {
			k := strings.LastIndex(lines[j], "(default ")
			if k >= 0 && strings.HasSuffix(lines[j], ")") {
				def = lines[j][k+len("(default ") : len(lines[j])-1]
			}
		}
		return fmt.Sprintf("%s=%s/%s", name, typ, def)
	}
	return name + "=absent"
}

// specReport renders several flags in the order given.
func specReport(usage string, flags ...string) string {
	parts := make([]string, 0, len(flags))
	for _, f := range flags {
		parts = append(parts, flagSpec(usage, f))
	}
	return strings.Join(parts, " ")
}

// control renders one command's answer for a flag that no release has, or
// "no-usage" where the command printed nothing at all. An absent binary
// declares nothing, and reading that silence as "false" would let a tree
// without goshawk and gosmin satisfy the control case below while asserting
// nothing about it -- build-clean's equivalent control guards itself the same
// way, by requiring the usage text to be non-empty first.
func control(usage, flag string) string {
	if usage == "" {
		return "no-usage"
	}
	return fmt.Sprintf("%v", declares(usage, flag))
}

func wantReport(value bool, flags ...string) string {
	parts := make([]string, 0, len(flags))
	for _, f := range flags {
		parts = append(parts, fmt.Sprintf("%s=%v", f, value))
	}
	return strings.Join(parts, " ")
}

func main() {
	t := vhelp.New()

	// --- 1. the two new commands were built and are executable -------------
	var missing []string
	for _, name := range newCommands {
		info, err := os.Stat(vhelp.CandidateBin(name))
		switch {
		case err != nil:
			missing = append(missing, name+"(absent)")
		case info.IsDir():
			missing = append(missing, name+"(directory)")
		case info.Mode().Perm()&0111 == 0:
			missing = append(missing, name+"(not executable)")
		}
	}
	sort.Strings(missing)
	t.Check("gossip-commands-built-and-executable", len(missing) == 0,
		"unusable: %s", strings.Join(missing, " "))

	// --- 2. and each describes itself --------------------------------------
	usages := make(map[string]string, 5)
	var mute []string
	for _, name := range newCommands {
		usage, err := usageOf(name)
		if err != nil {
			mute = append(mute, name+"("+err.Error()+")")
			continue
		}
		usages[name] = usage
		if !strings.Contains(usage, "Usage of ") || !strings.Contains(usage, name) {
			mute = append(mute, name+"(no usage banner)")
		}
	}
	sort.Strings(mute)
	t.Check("gossip-commands-print-their-own-usage", len(mute) == 0,
		"silent or unrecognisable: %s", strings.Join(mute, " "))

	for _, name := range []string{"ct_server", "migrillian", "certcheck"} {
		u, err := usageOf(name)
		if err != nil {
			t.Fatalf("%s --help: %v", name, err)
		}
		usages[name] = u
	}

	// --- 3. goshawk is part of the build, with its scanning grammar --------
	hawk := usages["goshawk"]
	t.Equal("goshawk-declares-its-config-and-scan-flags",
		flagReport(hawk, "config", "batch_size", "num_workers", "parallel_fetch", "start_index"),
		wantReport(true, "config", "batch_size", "num_workers", "parallel_fetch", "start_index"))

	// --- 4. gosmin is too, and is configured only by --config --------------
	// The gossiper has exactly one flag of its own; asserting that the
	// scanner flags goshawk has are absent here keeps "declared" from being
	// the answer for every name.
	smin := usages["gosmin"]
	t.Equal("gosmin-is-configured-only-by-config",
		flagReport(smin, "config", "batch_size", "num_workers", "start_index"),
		"config=true batch_size=false num_workers=false start_index=false")

	// --- 5. and neither will start without one -----------------------------
	// Exit status only; the diagnostic text is the command's own and is not
	// graded. 1 is glog.Exitf's status and distinguishes a refused
	// configuration both from success (0) and from a rejected flag (2).
	t.Equal("gossip-commands-refuse-to-start-without-a-config",
		fmt.Sprintf("goshawk=%d gosmin=%d", exitOf("goshawk", "--logtostderr"), exitOf("gosmin", "--logtostderr")),
		"goshawk=1 gosmin=1")

	// --- 6. ct_server carries the new global path prefix -------------------
	// The behaviour of the flag is graded in ctserver-handler-prefix-paths;
	// here it is only required to be part of the command's grammar, next to
	// two flags it has had since v1.0.
	srv := usages["ct_server"]
	t.Equal("ct-server-declares-handler-prefix",
		flagReport(srv, "handler_prefix", "log_config", "http_endpoint"),
		wantReport(true, "handler_prefix", "log_config", "http_endpoint"))

	// --- 7. migrillian's command line becomes multi-tenant -----------------
	mig := usages["migrillian"]
	t.Equal("migrillian-command-line-is-multi-tenant",
		flagReport(mig, "config", "force_master", "etcd_servers", "lock_file_path", "metrics_endpoint",
			"ct_log_uri", "trillian_uri", "log_id", "pub_key", "mirror", "start_index", "end_index", "ct_batch_size"),
		"config=true force_master=true etcd_servers=true lock_file_path=true metrics_endpoint=true "+
			"ct_log_uri=false trillian_uri=false log_id=false pub_key=false "+
			"mirror=false start_index=false end_index=false ct_batch_size=false")

	// --- 8. and the six process-wide tuning flags are NOT part of that -----
	// The rework removes the per-log flags only. These six say nothing about
	// which log is being migrated, so they survive it untouched (CLI.md 4.7),
	// and asserting that keeps case 7 from being satisfied by a migrillian
	// that simply dropped most of its command line.
	t.Equal("migrillian-keeps-its-process-wide-tuning-flags",
		flagReport(mig, "max_idle_conns_per_host", "max_idle_conns", "grpc_dial_timeout",
			"ct_fetchers", "submitters", "submitter_batches"),
		wantReport(true, "max_idle_conns_per_host", "max_idle_conns", "grpc_dial_timeout",
			"ct_fetchers", "submitters", "submitter_batches"))

	// --- 9. certcheck gains one flag at this release -----------------------
	// The https:// positional that arrives with it is out of scope (it would
	// need a live site), so the flag set is all of certcheck that is graded.
	// The four flags beside it are ones certcheck has had since v1.0.5/v1.0.10
	// and must still declare, so "new flag present" is not the only thing
	// this can be satisfied by.
	cc := usages["certcheck"]
	t.Equal("certcheck-declares-system-roots",
		flagReport(cc, "system_roots", "root", "intermediate", "validate", "ignore_unknown_critical_exts"),
		wantReport(true, "system_roots", "root", "intermediate", "validate", "ignore_unknown_critical_exts"))

	// --- 10. the gossip flags carry the documented types and defaults ------
	// CLI.md 4.10 gives both for all six. Reading them back out of the usage
	// text is the only way a command line can be observed from outside.
	t.Equal("goshawk-flags-have-their-documented-types-and-defaults",
		specReport(hawk, "config", "batch_size", "num_workers", "parallel_fetch", "start_index"),
		"config=string/- batch_size=int/1000 num_workers=int/2 parallel_fetch=int/2 start_index=int/-")
	t.Equal("gosmin-config-flag-has-its-documented-type-and-default",
		specReport(smin, "config"), "config=string/-")

	// --- 11. control: a flag no command has is still absent ----------------
	// Guards every assertion above against a usage dump that answers yes to
	// everything. Each command must have printed a usage text for its "false"
	// to mean anything: a command that printed nothing reads "no-usage", so
	// the control cannot be satisfied by a binary that does not exist.
	t.Equal("unknown-flag-absent-from-every-usage",
		fmt.Sprintf("goshawk=%s gosmin=%s ct_server=%s migrillian=%s certcheck=%s",
			control(hawk, "harbor_not_a_flag"),
			control(smin, "harbor_not_a_flag"),
			control(srv, "harbor_not_a_flag"),
			control(mig, "harbor_not_a_flag"),
			control(cc, "harbor_not_a_flag")),
		"goshawk=false gosmin=false ct_server=false migrillian=false certcheck=false")

	t.Done()
}

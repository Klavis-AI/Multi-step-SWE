// Group cli-new-command-surface.
//
// The command-line surface v1.0.19 adds and changes, all of it read out of the
// binaries' own usage text -- the command's public interface -- and never out
// of a source file. Every case here moved out of build-clean, which is a gate
// group and so may assert only what the previous milestone already had:
//
//   - the command set grows from thirteen to fourteen: migrillian ships with
//     this release (CLI.md 1, TEST_INTERFACE 4) and does not exist at v1.0.17
//     at all;
//   - migrillian is really in the build rather than a stub: it runs, prints
//     its own usage, and declares the two endpoints that define it,
//     --ct_log_uri and --trillian_uri (its full grammar, with types and
//     defaults, is graded and priced by migrillian-command-flag-grammar);
//   - scanlog loses --quiet, which goes with FetcherOptions.Quiet (CLI.md
//     4.1); the library half of that removal is graded by
//     scanner-fetcher-continuous-and-sth-caching;
//   - the two public-log defaults move to https:// -- scanlog's aviator and
//     ctclient's rocketeer (CLI.md 4.1, 3.1).
//
// Nothing here is satisfiable by inherited work (gate rule 4). At v1.0.17
// there is no migrillian binary at all, scanlog still declares --quiet, and
// both log_uri defaults are http://, so every one of the four cases fails
// against that tree.
//
// Two of the cases carry their own control, so a usage dump that answered yes
// to everything could not pass them: the --quiet case asserts two flags
// scanlog still has alongside the one it lost, and the defaults case pins the
// printed default string exactly rather than testing for a substring. The
// wider "a flag no command declares is absent from every usage" control stays
// in build-clean, where it guards the inherited usage sweep.
//
// SCOPE-NET / SCOPE-TIME: no clock, no network, no host property. Each child
// is run with --help, which the flag package answers by printing usage and
// returning; the child's output is captured into this process's own buffer and
// never shares its stdout, and CT_VERIFY_NONCE is stripped from the child's
// environment by vhelp.Command. chainfix is excluded from the usage
// assertions: it reads os.Args positionally and registers no flags, so it has
// no usage text to print. It is still required to be present and executable.
package main

import (
	"bytes"
	"fmt"
	"os"
	"sort"
	"strings"

	"ctverify/vhelp"
)

// The full command set built from the candidate tree at this milestone:
// thirteen inherited from v1.0.17 plus migrillian.
var commands = []string{
	"certcheck",
	"chainfix",
	"crlcheck",
	"ct_hammer",
	"ct_server",
	"ctclient",
	"dumpscts",
	"findlog",
	"gossip_server",
	"migrillian",
	"preloader",
	"scanlog",
	"sctcheck",
	"sctscan",
}

// usageOf runs `<bin> --help` and returns everything it wrote.
func usageOf(name string) string {
	cmd := vhelp.Command(vhelp.CandidateBin(name), "--help")
	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out
	_ = cmd.Run()
	return out.String()
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

// defaultOf returns the printed default of a flag, or "" if it has none and
// "ABSENT" if the flag is not declared at all.
func defaultOf(usage, flag string) string {
	lines := strings.Split(usage, "\n")
	for i, line := range lines {
		if !strings.HasPrefix(line, "  -"+flag) {
			continue
		}
		rest := strings.TrimPrefix(line, "  -"+flag)
		if rest != "" && !strings.HasPrefix(rest, " ") {
			continue
		}
		if i+1 >= len(lines) {
			return ""
		}
		desc := strings.TrimSpace(lines[i+1])
		if at := strings.LastIndex(desc, "(default "); at >= 0 && strings.HasSuffix(desc, ")") {
			return desc[at+len("(default ") : len(desc)-1]
		}
		return ""
	}
	return "ABSENT"
}

func main() {
	t := vhelp.New()

	// --- 1. the command set is fourteen ------------------------------------
	// migrillian is the fourteenth; the other thirteen are asserted alongside
	// so "fourteen commands" means the set, not just the new name.
	var missing []string
	for _, name := range commands {
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
	t.Check("fourteen-commands-built-and-executable", len(missing) == 0,
		"unusable: %s", strings.Join(missing, " "))

	// --- 2. the new command is part of the build ---------------------------
	// Presence in the build with its two defining endpoints declared; the
	// grammar itself is graded in migrillian-command-flag-grammar.
	mig := usageOf("migrillian")
	t.Equal("migrillian-is-part-of-the-build",
		fmt.Sprintf("usage=%v ct=%v trillian=%v", mig != "",
			declares(mig, "ct_log_uri"), declares(mig, "trillian_uri")),
		"usage=true ct=true trillian=true")

	// --- 3. scanlog no longer offers --quiet -------------------------------
	// The flag went with FetcherOptions.Quiet. Two flags it still has are
	// asserted alongside, so an empty or broken usage dump cannot pass by
	// making everything "absent".
	scan := usageOf("scanlog")
	t.Equal("scanlog-no-longer-declares-quiet",
		fmt.Sprintf("quiet=%v end_index=%v print_chains=%v",
			declares(scan, "quiet"), declares(scan, "end_index"), declares(scan, "print_chains")),
		"quiet=false end_index=true print_chains=true")

	// --- 4. the public log defaults are https ------------------------------
	t.Equal("public-log-defaults-are-https",
		fmt.Sprintf("scanlog=%s ctclient=%s",
			defaultOf(scan, "log_uri"), defaultOf(usageOf("ctclient"), "log_uri")),
		`scanlog="https://ct.googleapis.com/aviator" ctclient="https://ct.googleapis.com/rocketeer"`)

	t.Done()
}

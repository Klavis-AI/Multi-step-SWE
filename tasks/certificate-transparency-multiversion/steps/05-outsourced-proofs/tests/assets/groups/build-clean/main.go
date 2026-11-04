// Group build-clean -- KIND gate (proof of life).
//
// The milestone's thirteen commands must all be present, executable, and
// self-describing. All thirteen are already in product scope at v1.0.13, so
// every assertion here holds against the step-04 tree: this group is proof of
// life and nothing more, which is exactly what a gate group may assert.
//
// What was here and is NOT any more (gate rule 4, "nothing in the gate may be
// satisfiable by the new capability alone"): two flags this release adds.
//
//   - ct_server's --tracing, --tracing_project_id and --tracing_percent
//     (commits 88f78d6c / fe719e00). Graded and priced by the scored group
//     ctserver-tracing-flags, which reads the same usage text and additionally
//     pins the declared type of each of the three.
//   - scanlog's --end_index (the bounded scan added with scanner/fetcher).
//     Graded and priced by the scored group scanlog-end-index, which drives
//     the flag end to end against a loopback log and also asserts that the
//     binary's own usage text declares it.
//
// Anchoring proof of life to either of them made this group an unpriced veto:
// it cannot pass against a step-04 tree, so the step's own no-op continuation
// reported a FAILED GATE instead of a zero score and measured nothing.
//
// SCOPE-TIME: no clock, no network. Each child is run with --help (and, in
// case 4, once more with an unrecognised flag), which the flag package answers
// by printing usage or a parse error and exiting; the child's output is
// captured, never shared with this process's stdout, and CT_VERIFY_NONCE is
// stripped from its environment by vhelp.Command. chainfix is excluded from
// the usage and exit-status assertions: it reads os.Args positionally and
// registers no flags at either milestone, so it has no usage text to print and
// the flag package never runs in it. It is still required to be present and
// executable.
package main

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"sort"
	"strings"

	"ctverify/vhelp"
)

// The full command set built from the candidate tree at this milestone. All
// thirteen already exist at v1.0.13.
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
	"preloader",
	"scanlog",
	"sctcheck",
	"sctscan",
}

// noFlagsCommand takes its arguments positionally and defines no flags, so it
// has no usage text; it is graded for presence only.
const noFlagsCommand = "chainfix"

// usageOf runs `<bin> --help` and returns everything it wrote. Only the text
// is graded here; the exit status is asserted separately by case 4, which
// pins it to the value CLI.md's preamble gives it (`-h`/`--help` yields
// flag.ErrHelp, which flag.CommandLine in its default ExitOnError mode turns
// into exit 0). The error is therefore returned rather than swallowed, but a
// child that printed usage and then failed is still graded on its text.
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

// exitStatus runs `<bin> <args...>` with its output discarded and returns the
// child's exit status, or -1 if the child could not be started at all.
func exitStatus(name string, args ...string) int {
	cmd := vhelp.Command(vhelp.CandidateBin(name), args...)
	cmd.Stdout = io.Discard
	cmd.Stderr = io.Discard
	err := cmd.Run()
	if err == nil {
		return 0
	}
	var exit *exec.ExitError
	if errors.As(err, &exit) {
		return exit.ExitCode()
	}
	return -1
}

// flagLine reports whether usage text declares exactly this flag name.
func flagLine(usage, flag string) bool {
	for _, line := range strings.Split(usage, "\n") {
		line = strings.TrimSpace(line)
		if !strings.HasPrefix(line, "-"+flag) {
			continue
		}
		rest := strings.TrimPrefix(line, "-"+flag)
		if rest == "" || strings.HasPrefix(rest, " ") || strings.HasPrefix(rest, "=") {
			return true
		}
	}
	return false
}

func main() {
	t := vhelp.New()

	// --- 1. every command was built and is executable ----------------------
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
	t.Check("thirteen-commands-built-and-executable", len(missing) == 0,
		"unusable: %s", strings.Join(missing, " "))

	// --- 2. and each one describes itself ----------------------------------
	usages := make(map[string]string, len(commands))
	var mute []string
	for _, name := range commands {
		if name == noFlagsCommand {
			continue
		}
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
	t.Check("every-flag-driven-command-prints-its-own-usage", len(mute) == 0,
		"silent or unrecognisable: %s", strings.Join(mute, " "))

	// --- 3. control: a flag no milestone has is not claimed ----------------
	// Guards case 2 against a usage dump that answers yes to everything, and
	// is the reason "prints its own usage" is evidence and not decoration.
	srv := usages["ct_server"]
	scan := usages["scanlog"]
	t.Check("unknown-flag-absent-from-both-usages",
		srv != "" && scan != "" && !flagLine(srv, "harbor_not_a_flag") && !flagLine(scan, "harbor_not_a_flag"),
		"usage text claims a flag that does not exist")

	// --- 4. control: the two exit statuses CLI.md's preamble fixes ---------
	// `-h`/`--help` makes the flag package return flag.ErrHelp, which
	// flag.CommandLine in its default ExitOnError mode turns into exit 0; an
	// unrecognised flag is a parse error and exits 2. Both are the flag
	// package's own behaviour rather than anything this release introduces, so
	// they hold against the step-04 tree too and this group stays proof of
	// life. chainfix is excluded for the same reason as in case 2: it registers
	// no flags, so neither status is defined for it.
	var badStatus []string
	for _, name := range commands {
		if name == noFlagsCommand {
			continue
		}
		if got := exitStatus(name, "--help"); got != 0 {
			badStatus = append(badStatus, fmt.Sprintf("%s --help exited %d, want 0", name, got))
		}
	}
	for _, name := range []string{"ct_server", "scanlog"} {
		if got := exitStatus(name, "--harbor_not_a_flag"); got != 2 {
			badStatus = append(badStatus, fmt.Sprintf("%s --harbor_not_a_flag exited %d, want 2", name, got))
		}
	}
	sort.Strings(badStatus)
	t.Check("help-exits-zero-and-an-unknown-flag-exits-two", len(badStatus) == 0,
		"%s", strings.Join(badStatus, "; "))

	t.Done()
}

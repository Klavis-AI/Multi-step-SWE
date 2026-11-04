// Group build-clean -- KIND gate (proof of life).
//
// The fourteen commands the tree already had at v1.0.19 must all be present,
// executable, and self-describing. Every one of them is in product scope at
// the previous milestone too, so every assertion here holds against the
// step-06 tree: this group is proof of life and nothing more, which is
// exactly what a gate group may assert.
//
// What was here and is NOT any more (gate rule 4, "nothing in the gate may be
// satisfiable by the new capability alone"). All of it is v1.0.21 command-line
// surface, none of it can hold against a step-06 tree, and all of it has moved
// -- case for case -- into the scored group cli-new-command-surface:
//
//   - the command set growing from fourteen to sixteen: goshawk and gosmin
//     arrive with gossip/minimal and do not exist at v1.0.19 at all, so
//     "sixteen commands built" is a statement about this release's new code.
//     cli-new-command-surface requires both binaries, their usage banners,
//     their flag grammars, their documented types and defaults, and that
//     neither starts without --config.
//   - ct_server's --handler_prefix (CLI.md 2.1). v1.0.19's ct_server does not
//     declare it; the flag's behaviour is graded by ctserver-handler-prefix-paths
//     and its declaration by cli-new-command-surface.
//   - migrillian's rework from eight per-log flags to --config plus the
//     master-election flags (CLI.md 4.7), together with the six process-wide
//     tuning flags that survive it -- the control that keeps the rework
//     assertion honest. The two belong together and moved together.
//   - certcheck's --system_roots (CLI.md 4.3), absent at v1.0.19.
//
// Anchored to any of those, this group was an unpriced veto: it could not pass
// against a step-06 tree, so the step's own no-op continuation reported a
// FAILED GATE instead of a zero score, and measured nothing at all.
//
// SCOPE-TIME: no clock, no network, no host property. Each child is run with
// --help, which the flag package answers by printing usage; the child's output
// is captured into this process's own buffer and never shares its stdout, and
// CT_VERIFY_NONCE is stripped from the child's environment by vhelp.Command.
// chainfix is excluded from the usage assertion: it reads os.Args positionally
// and registers no flags at either anchor, so it has no usage text to print.
// It is still required to be present and executable.
package main

import (
	"bytes"
	"os"
	"sort"
	"strings"

	"ctverify/vhelp"
)

// The command set the candidate tree already had at v1.0.19. goshawk and
// gosmin are deliberately absent: they are this release's, and are required
// by cli-new-command-surface instead.
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

// noFlagsCommand takes its arguments positionally and defines no flags, so it
// has no usage text; it is graded for presence only.
const noFlagsCommand = "chainfix"

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
	t.Check("fourteen-commands-built-and-executable", len(missing) == 0,
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
	// The three commands read back here exist at both anchors.
	srv := usages["ct_server"]
	mig := usages["migrillian"]
	cc := usages["certcheck"]
	t.Check("unknown-flag-absent-from-every-usage",
		srv != "" && mig != "" && cc != "" &&
			!declares(srv, "harbor_not_a_flag") &&
			!declares(mig, "harbor_not_a_flag") &&
			!declares(cc, "harbor_not_a_flag"),
		"usage text claims a flag that does not exist, or a command printed nothing")

	t.Done()
}

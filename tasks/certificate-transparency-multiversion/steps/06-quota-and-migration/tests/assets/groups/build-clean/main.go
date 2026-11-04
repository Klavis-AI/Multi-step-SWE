// Group build-clean -- KIND gate (proof of life).
//
// The thirteen commands the tree already had at v1.0.17 must all be present,
// executable, and self-describing. Every one of them is in product scope at
// the previous milestone too, so every assertion here holds against the
// step-05 tree: this group is proof of life and nothing more, which is exactly
// what a gate group may assert. A candidate that adds a binary is not
// penalised -- each command is looked up by name, and nothing here counts the
// directory.
//
// What was here and is NOT any more (gate rule 4, "nothing in the gate may be
// satisfiable by the new capability alone"). All of it is v1.0.19 command-line
// surface, none of it can hold against a step-05 tree, and all of it has moved
// -- case for case -- into the scored group cli-new-command-surface:
//
//   - the command set growing from thirteen to fourteen: migrillian arrives
//     with this release (CLI.md 1) and does not exist at v1.0.17 at all, so
//     "fourteen commands built" is a statement about this release's new code;
//   - migrillian being part of the build at all, with its two defining
//     endpoints declared;
//   - scanlog losing --quiet, which goes with FetcherOptions.Quiet (CLI.md
//     4.1); v1.0.17's scanlog declares it;
//   - the two public-log defaults moving to https:// (CLI.md 4.1, 3.1);
//     v1.0.17's scanlog and ctclient default to http://.
//
// Anchored to any of those, this group was an unpriced veto that could not
// pass against a step-05 tree -- which is why it had been re-tiered to scored,
// leaving the step with no group at all to answer "does this tree build and do
// its binaries run". A candidate shipping a tree that does not compile then
// failed gate-endtoend on a readiness timeout instead, and reward_details.json
// named a vetoing case that was true but useless.
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

// The command set the candidate tree already had at v1.0.17. migrillian is
// deliberately absent: it is this release's, and is required by
// cli-new-command-surface instead.
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

	// --- 1. every inherited command was built and is executable ------------
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
	// The three commands read back here exist at both anchors.
	srv := usages["ct_server"]
	scan := usages["scanlog"]
	cli := usages["ctclient"]
	t.Check("unknown-flag-absent-from-every-usage",
		srv != "" && scan != "" && cli != "" &&
			!declares(srv, "harbor_not_a_flag") &&
			!declares(scan, "harbor_not_a_flag") &&
			!declares(cli, "harbor_not_a_flag"),
		"usage text claims a flag that does not exist, or a command printed nothing")

	t.Done()
}

// Group ctclient-command-tree.
//
// At v1.1.3 client/ctclient/ctclient.go is deleted and the tool is rebuilt as
// a Cobra command tree in client/ctclient/cmd/: a root command with a Short
// description and five persistent flags, and seven leaf subcommands each with
// its own Use/Short/Aliases and its own flag set. This group reads that tree
// back out of the candidate's own binary through the only interface a user
// has -- the generated help text -- and compares every line it asserts, byte
// for byte, with the spelling the milestone fixes: the root header, the
// advertised subcommand set, and for each leaf its short description, its
// usage line, its declared aliases and its own flag block.
//
// At v1.1.2 ctclient is a single stdlib-flag main: flags come first, the
// command is a trailing positional drawn from {sth, upload, getroots,
// getentries, inclusion, consistency, bisect}, there is no help subcommand and
// no generated usage of this shape. `ctclient help <x>` is an unknown command
// there, and a bare `ctclient` is a fatal error rather than a help dump, so
// every case below fails.
package main

import (
	"regexp"
	"strings"

	"ctverify/vhelp"
)

// connFlags is the shared connection-flag fragment every leaf command splices
// into its Use string.
const connFlags = "{--log_uri uri | --log_name name [--log_list {file|uri}]} [--pub_key file]"

// The seven product subcommands, in the order Cobra sorts them.
//
// `aliases` is the line the framework prints under `Aliases:`, which is the
// command's own name followed by every alias CLI.md section 3.2 declares for
// it, in declaration order.
//
// `flags` is the command's own flag block, one entry per flag the same table
// gives it, with runs of whitespace collapsed to one space. Each line is what
// the flag library prints for that declaration: the flag's name, then its type
// (a Bool prints none, an Int64 prints `int`, a Uint64 prints `uint`, a
// Duration prints `duration`), then the description the flag-description table
// of section 3.2 gives it, then the default in parentheses when the default is
// not the type's zero value. The framework's own `-h, --help` line is filtered
// out before the comparison: it belongs to the framework, not to the
// milestone's declarations.
var commands = []struct {
	name    string
	short   string
	use     string
	aliases string
	flags   []string
}{
	{"bisect", "Find a log entry by timestamp",
		"bisect " + connFlags + " --timestamp=ts [--chain] [--text=false]",
		"bisect, find-timestamp",
		[]string{
			"--chain Display entire certificate chain",
			"--text Display certificates as text (default true)",
			"--timestamp int Timestamp to use for inclusion checking",
		}},
	{"get-consistency-proof", "Fetch and verify a consistency proof between two tree states",
		"get-consistency-proof " + connFlags + " --size=N --tree_hash=hash --prev_size=N --prev_hash=hash",
		"get-consistency-proof, getconsistencyproof, consistency-proof, consistency",
		[]string{
			"--prev_hash string Previous tree hash to check against (as hex string or base64)",
			"--prev_size uint Previous tree size to get consistency against",
			"--size uint Tree size to query at",
			"--tree_hash string Tree hash to check against (as hex string or base64)",
		}},
	{"get-entries", "Fetch a range of entries in the log",
		"get-entries " + connFlags + " --first=idx [--last=idx]",
		"get-entries, getentries, entries",
		[]string{
			"--chain Display entire certificate chain",
			"--first int First entry to get (default -1)",
			"--last int Last entry to get (default -1)",
			"--text Display certificates as text (default true)",
		}},
	{"get-inclusion-proof", "Fetch and verify the inclusion proof for an entry",
		"get-inclusion-proof " + connFlags + " {--leaf_hash=hash | --cert_chain=file} [--timestamp=ts] [--size=N]",
		"get-inclusion-proof, getinclusionproof, inclusion-proof, inclusion",
		[]string{
			"--cert_chain string Name of file containing certificate chain as concatenated PEM files",
			"--leaf_hash string Leaf hash to retrieve (as hex string or base64)",
			"--size uint Tree size to query at",
			"--timestamp int Timestamp to use for inclusion checking",
		}},
	{"get-roots", "Fetch the root certificates accepted by the log",
		"get-roots " + connFlags,
		"get-roots, getroots, roots",
		[]string{
			"--text Display certificates as text (default true)",
		}},
	{"get-sth", "Fetch the latest STH of the log",
		"get-sth " + connFlags,
		"get-sth, sth",
		nil},
	{"upload", "Submit a certificate (pre-)chain to the log",
		"upload " + connFlags + " --cert_chain=file [--log_mmd=dur]",
		"upload, add-chain",
		[]string{
			"--cert_chain string Name of file containing certificate chain as concatenated PEM files",
			"--log_mmd duration Log's maximum merge delay (default 24h0m0s)",
		}},
}

// rootPersistentFlags are the five flags the root command declares as
// persistent. On the root itself they are the root's own flags, so they are
// what its `Flags:` block must contain once the logging library's merged-in
// flags and the framework's `-h` are set aside.
var rootPersistentFlags = []string{
	`--log_list string Location of master log list (URL or filename) (default "https://www.gstatic.com/ct/log_list/all_logs_list.json")`,
	`--log_name string Name of log to retrieve information from --log_list for`,
	`--log_uri string CT log base URI (default "https://ct.googleapis.com/rocketeer")`,
	`--pub_key string Name of file containing log's public key`,
	`--skip_https_verify Skip verification of HTTPS transport connection`,
}

var spaces = regexp.MustCompile(`[ \t]+`)

func main() {
	t := vhelp.New()
	requireClient(t)

	// 1. A bare invocation is no longer an error: the root command has no Run,
	//    so Cobra prints the root help on stdout and exits 0.
	root := runClient()
	t.Check("bare-invocation-exit-status", root.code == 0,
		"exit=%d stderr=%s", root.code, oneLine(root.stderr))

	rootLines := strings.Split(root.stdout, "\n")
	head := ""
	if len(rootLines) >= 4 {
		head = strings.Join(rootLines[:4], "\n")
	}
	t.Equal("bare-invocation-header", head,
		"A command line client for Certificate Transparency logs\n\nUsage:\n  ctclient [command]")

	// 2. The set of product subcommands advertised in the root help. Cobra's
	//    own built-ins (help, completion) are not part of the milestone and are
	//    filtered out.
	got := listedCommands(root.stdout)
	want := make([]string, 0, len(commands))
	for _, c := range commands {
		want = append(want, c.name)
	}
	t.Equal("product-subcommand-set", strings.Join(got, ","), strings.Join(want, ","))

	// 3. `--help` reaches the same root help dump on stdout and exits 0. At
	//    v1.1.2 the flag package answers it with a "Usage of <path>:" block,
	//    and that transition is this milestone's own. It was asserted by the
	//    gate group build-clean until the 2026-09-21 tier audit, which moved
	//    it here: build-clean is proof of life and may only assert what the
	//    predecessor anchor already does (gate rule 3).
	hf := runClient("--help")
	hfLines := strings.Split(hf.stdout, "\n")
	hfHead := ""
	if len(hfLines) >= 4 {
		hfHead = strings.Join(hfLines[:4], "\n")
	}
	hfCmds := strings.Join(listedCommands(hf.stdout), ",")
	t.Check("help-flag-prints-the-command-tree",
		hf.code == 0 &&
			hfHead == "A command line client for Certificate Transparency logs\n\nUsage:\n  ctclient [command]" &&
			hfCmds == strings.Join(want, ","),
		"exit=%d head=%q commands=%q stderr=%s",
		hf.code, hfHead, hfCmds, oneLine(hf.stderr))

	// 4. The five connection flags are declared on the root, so they are the
	//    root's own flags and no leaf's.
	t.Equal("root-own-flag-block", flagBlock(root.stdout, "Flags:", ownFlagNames()),
		strings.Join(rootPersistentFlags, "\n"))

	// 5. Each leaf command answers `ctclient help <name>` with its own short
	//    description, its own fully-spelled usage line, its declared aliases
	//    and its own flag block.
	for _, c := range commands {
		h := runClient("help", c.name)
		lines := strings.Split(h.stdout, "\n")
		short := ""
		if len(lines) > 0 {
			short = lines[0]
		}
		t.Equal("short-"+c.name, short, c.short)
		t.Equal("usage-"+c.name, lineAfter(lines, "Usage:"), "  ctclient "+c.use+" [flags]")
		t.Equal("aliases-"+c.name,
			spaces.ReplaceAllString(strings.TrimSpace(lineAfter(lines, "Aliases:")), " "),
			c.aliases)
		t.Equal("flags-"+c.name, flagBlock(h.stdout, "Flags:", nil), strings.Join(c.flags, "\n"))
	}

	t.Done()
}

// ownFlagNames is the set of the five persistent flag names, used to pick them
// out of the root's `Flags:` block, which also carries the logging library's
// flags -- those belong to glog, not to this milestone, and step 09 already
// grades them.
func ownFlagNames() map[string]bool {
	m := map[string]bool{}
	for _, f := range rootPersistentFlags {
		m[strings.SplitN(f, " ", 2)[0]] = true
	}
	return m
}

// flagBlock extracts a help dump's flag section, with runs of whitespace
// collapsed so that the framework's column padding is not part of the
// assertion, and with its own `-h, --help` line dropped. When keep is non-nil
// only the named flags are returned; otherwise every remaining line is. An
// absent section returns a marker that cannot match any expectation, and an
// empty one returns the empty string, which is what a command with no flags of
// its own must produce.
func flagBlock(help, hdr string, keep map[string]bool) string {
	var out []string
	in, seen := false, false
	for _, ln := range strings.Split(help, "\n") {
		if strings.TrimSpace(ln) == hdr {
			in, seen = true, true
			continue
		}
		if !in {
			continue
		}
		if strings.TrimSpace(ln) == "" {
			break
		}
		norm := spaces.ReplaceAllString(strings.TrimSpace(ln), " ")
		name := strings.SplitN(norm, " ", 2)[0]
		if name == "-h," {
			continue
		}
		if keep != nil && !keep[name] {
			continue
		}
		out = append(out, norm)
	}
	if !seen {
		return "<no " + hdr + " section>"
	}
	return strings.Join(out, "\n")
}

// listedCommands returns the names in the "Available Commands:" block of a
// Cobra root help dump, minus Cobra's own built-ins.
func listedCommands(help string) []string {
	builtin := map[string]bool{"help": true, "completion": true}
	var out []string
	in := false
	for _, ln := range strings.Split(help, "\n") {
		if strings.TrimSpace(ln) == "Available Commands:" {
			in = true
			continue
		}
		if !in {
			continue
		}
		if strings.TrimSpace(ln) == "" {
			break
		}
		f := strings.Fields(ln)
		if len(f) == 0 || builtin[f[0]] {
			continue
		}
		out = append(out, f[0])
	}
	return out
}

// lineAfter returns the line following the first line equal to hdr, or a
// marker that can never match an expectation when the header is absent.
func lineAfter(lines []string, hdr string) string {
	for i, ln := range lines {
		if strings.TrimSpace(ln) == hdr && i+1 < len(lines) {
			return lines[i+1]
		}
	}
	return "<no " + hdr + " section>"
}

func oneLine(s string) string {
	return strings.ReplaceAll(strings.TrimSpace(s), "\n", " / ")
}

// Group ctclient-arg-grammar.
//
// The v1.1.3 rebuild does not just rename ctclient's commands, it replaces the
// argument grammar wholesale. The command word now comes first and its flags
// come after it; every old spelling survives only as a declared Cobra alias;
// the root's five connection flags become persistent flags that bind on either
// side of the command word; and an unknown command, an unknown flag or a
// surplus positional are each diagnosed with a specific message instead of
// being silently ignored. This group drives the candidate's own ctclient
// binary and asserts each of those properties from outside the process.
//
// At v1.1.2 the tool parses with the stdlib flag package: a flag written after
// the command word is a positional, `help` is not a command, the old spellings
// are the only spellings, and there are no Cobra diagnostics at all, so no
// case here can pass.
package main

import (
	"regexp"
	"strings"

	"ctverify/vhelp"
)

// Every alias the milestone declares, mapped to the Short description of the
// command it must resolve to.
var aliases = []struct{ alias, short string }{
	{"sth", "Fetch the latest STH of the log"},
	{"getroots", "Fetch the root certificates accepted by the log"},
	{"roots", "Fetch the root certificates accepted by the log"},
	{"getentries", "Fetch a range of entries in the log"},
	{"entries", "Fetch a range of entries in the log"},
	{"getinclusionproof", "Fetch and verify the inclusion proof for an entry"},
	{"inclusion-proof", "Fetch and verify the inclusion proof for an entry"},
	{"inclusion", "Fetch and verify the inclusion proof for an entry"},
	{"getconsistencyproof", "Fetch and verify a consistency proof between two tree states"},
	{"consistency-proof", "Fetch and verify a consistency proof between two tree states"},
	{"consistency", "Fetch and verify a consistency proof between two tree states"},
	{"find-timestamp", "Find a log entry by timestamp"},
	{"add-chain", "Submit a certificate (pre-)chain to the log"},
}

// persistentFlags are the five connection flags the root command owns; they
// must appear in every leaf command's "Global Flags:" block with these exact
// types, descriptions and defaults.
var persistentFlags = []string{
	`--log_list string Location of master log list (URL or filename) (default "https://www.gstatic.com/ct/log_list/all_logs_list.json")`,
	`--log_name string Name of log to retrieve information from --log_list for`,
	`--log_uri string CT log base URI (default "https://ct.googleapis.com/rocketeer")`,
	`--pub_key string Name of file containing log's public key`,
	`--skip_https_verify Skip verification of HTTPS transport connection`,
}

// leafCommands are the seven product subcommands by their canonical names.
var leafCommands = []string{
	"bisect", "get-consistency-proof", "get-entries", "get-inclusion-proof",
	"get-roots", "get-sth", "upload",
}

var spaces = regexp.MustCompile(`[ \t]+`)

func main() {
	t := vhelp.New()
	requireClient(t)

	// 1. Every declared alias resolves to its command.
	for _, a := range aliases {
		r := runClient("help", a.alias)
		first := ""
		if lines := strings.Split(r.stdout, "\n"); len(lines) > 0 {
			first = lines[0]
		}
		t.Equal("alias-"+a.alias, first, a.short)
	}

	// 2. A surplus positional is rejected by the per-command Args validator
	//    rather than ignored. CLI.md §3.2 fixes the status as well as the
	//    stream: the error goes through Fatal, so it is 255 and not 1, and it
	//    is diagnosed on stderr with nothing written to stdout. The framework
	//    writes that diagnosis in its own words -- §3.2 says so -- so its
	//    wording is not compared here.
	r := runClient("get-sth", "surplus")
	t.Check("surplus-positional-rejected",
		r.code == 255 && r.stdout == "" && strings.TrimSpace(r.stderr) != "",
		"exit=%d stdout=%q stderr=%s", r.code, r.stdout, oneLine(r.stderr))

	// 3. An unknown flag on a leaf command is a diagnosed error, not a
	//    positional argument. Only the flag this group supplied is looked for
	//    in the diagnosis; the sentence around it belongs to the framework.
	r = runClient("get-sth", "--nosuchflag")
	t.Check("unknown-flag-rejected",
		r.code == 255 && r.stdout == "" && strings.Contains(r.stderr, "--nosuchflag"),
		"exit=%d stderr=%s", r.code, oneLine(r.stderr))

	// 4. An unknown command word is refused and named back, rather than being
	//    taken as an argument to something else.
	r = runClient("nosuchcmd")
	t.Check("unknown-command-rejected",
		r.code == 255 && r.stdout == "" && strings.Contains(r.stderr, "nosuchcmd"),
		"exit=%d stderr=%s", r.code, oneLine(r.stderr))

	// 5. The connection flags are persistent: --pub_key, declared on the root,
	//    binds when written after the leaf command word, and it changes the
	//    outcome. The pinned STH below carries a syntactically valid but
	//    unverifiable signature, so supplying the log's real public key must
	//    turn a success into a fatal error.
	s := newStubLog()
	defer s.close()
	s.json("/ct/v1/get-sth", 200, sthJSON)
	r = runClient("get-sth", "--log_uri="+s.URL)
	t.Check("no-pub_key-skips-verification",
		r.code == 0 && strings.Join(s.requests(), "|") == "GET /ct/v1/get-sth",
		"exit=%d requests=%v stderr=%s", r.code, s.requests(), oneLine(r.stderr))

	s2 := newStubLog()
	defer s2.close()
	s2.json("/ct/v1/get-sth", 200, sthJSON)
	r = runClient("get-sth", "--log_uri="+s2.URL,
		"--pub_key="+vhelp.Fixture("minted/keys/log-pubkey.pem"))
	t.Check("pub_key-enables-verification",
		r.code != 0 && r.stdout == "" && strings.Join(s2.requests(), "|") == "GET /ct/v1/get-sth",
		"exit=%d stdout=%q requests=%v", r.code, r.stdout, s2.requests())

	// 6. ...and they are advertised as global flags of every leaf command,
	//    with the milestone's exact descriptions and defaults. Every one of
	//    the seven leaves is asked, because "persistent" is precisely the
	//    claim that none of them has to declare these itself.
	wantBlock := strings.Join(persistentFlags, "\n")
	gotBlocks := ""
	for _, leaf := range leafCommands {
		h := runClient("help", leaf)
		if b := globalFlagBlock(h.stdout); b != wantBlock {
			gotBlocks += leaf + ": " + strings.ReplaceAll(b, "\n", " / ") + "\n"
		}
	}
	t.Check("persistent-flag-block", gotBlocks == "",
		"leaves whose Global Flags block is not the five persistent flags:\n%s", gotBlocks)

	// 7. The logging library's own flags were merged into the new flag set by
	//    the package init(), so -v is a flag rather than an unknown one: it is
	//    accepted on either side of the command word and changes nothing about
	//    the request the tool makes. (At v1.1.4 the merge copies an empty set
	//    and this stops being true -- CLI.md §3.2.)
	s3 := newStubLog()
	defer s3.close()
	s3.json("/ct/v1/get-sth", 200, sthJSON)
	r = runClient("get-sth", "--log_uri="+s3.URL, "-v=1")
	t.Check("glog-flag-accepted-after-command",
		r.code == 0 && strings.Join(s3.requests(), "|") == "GET /ct/v1/get-sth",
		"exit=%d requests=%v stderr=%s", r.code, s3.requests(), oneLine(r.stderr))

	t.Done()
}

// sthJSON is a pinned, minimal tree head. The client is run without a public
// key, so no signature verification is attempted and the body need only parse.
// tree_head_signature is the TLS encoding of DigitallySigned{sha256, ecdsa,
// 0x3045} written out by hand so that this group depends on no product code.
const sthJSON = `{"tree_size":7,"timestamp":1500000000000,` +
	`"sha256_root_hash":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",` +
	`"tree_head_signature":"BAMAAjBF"}`

// globalFlagBlock extracts the milestone's own five flags from a help dump's
// "Global Flags:" section, with runs of whitespace collapsed so that Cobra's
// column padding is not part of the assertion.
func globalFlagBlock(help string) string {
	wanted := map[string]bool{
		"--log_list": true, "--log_name": true, "--log_uri": true,
		"--pub_key": true, "--skip_https_verify": true,
	}
	var out []string
	in := false
	for _, ln := range strings.Split(help, "\n") {
		if strings.TrimSpace(ln) == "Global Flags:" {
			in = true
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
		if wanted[name] {
			out = append(out, norm)
		}
	}
	if len(out) == 0 {
		return "<no Global Flags section>"
	}
	return strings.Join(out, "\n")
}

func oneLine(s string) string {
	return strings.ReplaceAll(strings.TrimSpace(s), "\n", " / ")
}

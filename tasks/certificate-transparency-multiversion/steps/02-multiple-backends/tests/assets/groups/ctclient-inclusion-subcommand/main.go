// Group ctclient-inclusion-subcommand.
//
// v1.0.5 gives ctclient the "inclusion" command, with the -leaf_hash and -size
// flags it needs: it fetches a get-proof-by-hash from the log, prints the
// audit path, and -- when no -size was given, so it had to fetch an STH of its
// own -- verifies the path against that STH's root hash with the by-hash
// verifier added in the same release. The release also adds the leaf hash to
// the output of "upload", which is what a user needs in order to ask for that
// proof later. CLI.md section 3.1 states all of that.
//
// SCOPE-DIAGNOSTIC: the only ctclient output CLI.md section 3.1 fixes verbatim
// at this release is the `Got STH` line, the `LeafHash: %x` line, the
// `Signature: Hash=%v Sign=%v Value=%x` rendering and the usage block's
// geometry (three spaces, then the command name padded to twelve columns).
// The "Inclusion proof for index ..." header, the verifier's failure wrapper
// and the missing-flag diagnostic are one implementation's wording and are not
// graded: those cases observe the exit status, whether a proof was printed,
// and whether the client went to the log for an STH.
//
// The log ctclient talks to here is the candidate's own ct_server on a
// loopback port, backed by this group's stub, so every answer is a pinned
// constant.
package main

import (
	"bytes"
	"encoding/hex"
	"os/exec"
	"strings"

	"ctverify/vhelp"
)

type result struct {
	rc     int
	stdout string
	stderr string
}

func run(t *vhelp.T, args ...string) result {
	cmd := vhelp.Command(vhelp.CandidateBin("ctclient"), args...)
	var out, errb bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errb
	rc := 0
	if err := cmd.Run(); err != nil {
		ee, ok := err.(*exec.ExitError)
		if !ok {
			t.Fatalf("ctclient %v: %v", args, err)
		}
		rc = ee.ExitCode()
	}
	return result{rc: rc, stdout: out.String(), stderr: errb.String()}
}

// after returns the part of the first line of s that begins at marker, so an
// assertion never has to name the log-message timestamp that precedes it.
func after(s, marker string) string {
	for _, line := range strings.Split(s, "\n") {
		if i := strings.Index(line, marker); i >= 0 {
			return line[i:]
		}
	}
	return "<no line containing " + marker + ">: " + s
}

func isHex(s string) bool {
	_, err := hex.DecodeString(s)
	return err == nil
}

func main() {
	t := vhelp.New()

	be := newBackend(backendA, rootNanosA, rootHashA[:], treeSizeA)
	addr := be.serve(t)
	cfg := "backends {\n" +
		"  backend { name: \"" + backendA + "\" backend_spec: \"" + addr + "\" }\n}\n" +
		"log_configs {\n" + logConfigBody(t, "  ", logIDA, prefixA, backendA, "") + "}\n"
	h := start(t, writeScratch(t, "one.textproto", cfg), []string{"--log_rpc_server="}, prefixA)
	defer h.stop()

	uri := h.baseURL + "/" + prefixA
	// A pinned leaf hash: 32 bytes, hex, nothing to do with the clock.
	leafHash := strings.Repeat("ab", 32)
	wantPath := hex.EncodeToString(rootHashA[:])

	// With -size the client does not need an STH: it fetches the proof and
	// prints the audit path. CLI.md section 3.1 does not fix the wording of
	// the header line above it, so only the path itself is graded.
	r := run(t, "-log_uri", uri, "-leaf_hash", leafHash, "-size", itoa(treeSizeA), "inclusion")
	t.Check("inclusion-prints-the-proof-for-a-given-tree-size",
		r.rc == 0 && strings.Contains(r.stdout, wantPath),
		"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)

	// The command table of CLI.md section 3.1 gives the command one alias,
	// `inclusion-proof`, from this same release. It is the same command word,
	// so the identical invocation must produce the identical audit path. This
	// run also takes -size, so like the one above it asks the log for no STH
	// and leaves the backend's request count alone for the case below.
	ra := run(t, "-log_uri", uri, "-leaf_hash", leafHash, "-size", itoa(treeSizeA), "inclusion-proof")
	t.Check("the-inclusion-proof-alias-is-the-same-command",
		ra.rc == r.rc && strings.Contains(ra.stdout, wantPath),
		"rc=%d stdout=%q stderr=%q", ra.rc, ra.stdout, ra.stderr)

	rootsBefore := len(be.rootLogIDs())

	// Without -size it takes the tree size from the log's current STH, so the
	// backend sees a signed-log-root request it did not see for the run above.
	// That RPC is the observable: nothing else on this path issues one, and the
	// periodic internal get-sth is disabled by --get_sth_interval=0.
	r = run(t, "-log_uri", uri, "-leaf_hash", leafHash, "inclusion")
	t.Check("inclusion-takes-the-tree-size-from-the-sth-when-none-is-given",
		len(be.rootLogIDs()) > rootsBefore,
		"signed-log-root requests %d -> %d; stdout=%q stderr=%q",
		rootsBefore, len(be.rootLogIDs()), r.stdout, r.stderr)
	// Having an STH it verifies the path against that STH's root -- which this
	// pinned proof does not satisfy. CLI.md section 3.1: any failure exits 1
	// without printing a verified-ok line, and reports on stderr. The text it
	// reports is merkletree's and is not graded.
	t.Check("inclusion-verifies-the-path-against-the-sth-root",
		r.rc == 1 && strings.TrimSpace(r.stderr) != "",
		"rc=%d stderr=%q", r.rc, r.stderr)

	// The leaf hash is required, and must be 32 bytes of hex. A refusal exits 1
	// and prints no proof; the wording of the refusal is not graded.
	for _, tc := range []struct{ id, hash string }{
		{"inclusion-refuses-a-missing-leaf-hash", ""},
		{"inclusion-refuses-a-leaf-hash-that-is-not-hex", strings.Repeat("zz", 32)},
		{"inclusion-refuses-a-leaf-hash-of-the-wrong-length", strings.Repeat("ab", 16)},
	} {
		r = run(t, "-log_uri", uri, "-leaf_hash", tc.hash, "-size", itoa(treeSizeA), "inclusion")
		t.Check(tc.id,
			r.rc == 1 && strings.TrimSpace(r.stderr) != "" && !strings.Contains(r.stdout, wantPath),
			"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)
	}

	// The command appears in the usage summary, at the column CLI.md section
	// 3.1 fixes: three spaces, then the name padded to twelve characters. The
	// description beside it is not fixed by any page, so it is not graded.
	// The row itself is the only process-boundary evidence that the command
	// arrived at this release at all, which is why the geometry is matched
	// (TEST_INTERFACE.md section 6.1, item 4).
	r = run(t, "-log_uri", uri, "no-such-command")
	t.Check("usage-lists-the-inclusion-command",
		r.rc == 1 && strings.Contains(r.stderr, "   inclusion   "),
		"rc=%d stderr=%q", r.rc, r.stderr)

	// upload now reports the leaf hash of what it submitted, alongside the
	// signature. The hash covers the SCT timestamp, which the personality
	// takes from its own clock, so only its shape is pinned here.
	r = run(t, "-log_uri", uri, "-cert_chain", vhelp.Fixture("minted", "chain-valid.pem"), "upload")
	lh := after(r.stdout, "LeafHash: ")
	t.Check("upload-reports-the-leaf-hash-of-the-submitted-chain",
		r.rc == 0 && strings.HasPrefix(lh, "LeafHash: ") && len(lh) == len("LeafHash: ")+64 &&
			isHex(strings.TrimPrefix(lh, "LeafHash: ")),
		"rc=%d stdout=%q stderr=%q", r.rc, r.stdout, r.stderr)
	// CLI.md section 3.1 fixes the signature rendering as
	// `Signature: Hash=%v Sign=%v Value=%x`, unchanged v1.0..v1.1.2. Whether an
	// implementation prints that line bare or under a label of its own is not
	// fixed, so only the rendering's own three fields are graded.
	sig := after(r.stdout, "Signature: ")
	t.Check("upload-labels-the-sct-signature",
		strings.HasPrefix(sig, "Signature: ") && strings.Contains(sig, "Hash=") &&
			strings.Contains(sig, "Sign=") && strings.Contains(sig, "Value="),
		"stdout=%q", r.stdout)

	// And the log is the one this group configured. CLI.md section 3.1 prints
	// the v1.0..v1.0.10 line complete, so it is compared whole from `Got STH`
	// onward; the `<when>:` prefix in front of it is Go's time.Time.String in
	// the local zone and is deliberately dropped (TEST_INTERFACE.md section
	// 6.1, item 4).
	r = run(t, "-log_uri", uri, "sth")
	t.Equal("the-same-client-reads-the-pinned-tree-head",
		itoa(int64(r.rc))+"|"+after(r.stdout, "Got STH"),
		"0|Got STH for V1 log (size="+itoa(treeSizeA)+") at "+uri+", hash "+wantPath)

	t.Done()
}

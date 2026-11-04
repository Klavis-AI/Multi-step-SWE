// Group ctclient-get-roots.
//
// `get-roots` is the one v1.0-era subcommand whose whole output is the
// certificate renderer: it fetches the accepted roots and prints each one,
// in the order the log returned them, through the renderer --text selects
// (CLI.md section 3.1's table and the renderer bullets under it). The rebuild
// keeps that behaviour and changes only how it is asked for -- the canonical
// name is now hyphenated, `getroots` and `roots` survive as declared aliases,
// and --text is the command's own flag written after the command word. This
// group serves a pinned two-root response over a loopback socket and compares
// the candidate's whole stdout, byte for byte, in both renderings.
//
// At v1.1.2 the subcommand is spelled `getroots`, it is a trailing positional
// and its flags must precede it, so `get-roots --text=false` is an unknown
// command and every case here fails.
package main

import (
	"encoding/base64"
	"encoding/pem"
	"fmt"
	"strings"

	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

func pemOf(der []byte) string {
	return string(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}))
}

func main() {
	t := vhelp.New()
	requireClient(t)

	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")

	// The pinned response. The order is the log's, and the tool must not
	// reorder it: the root is second here on purpose.
	body := fmt.Sprintf(`{"certificates":[%q,%q]}`,
		base64.StdEncoding.EncodeToString(intDER),
		base64.StdEncoding.EncodeToString(rootDER))

	parsedInt, err := x509.ParseCertificate(intDER)
	if err != nil {
		t.Fatalf("parse bench-intermediate: %v", err)
	}
	parsedRoot, err := x509.ParseCertificate(rootDER)
	if err != nil {
		t.Fatalf("parse bench-root: %v", err)
	}

	wantPEM := pemOf(intDER) + pemOf(rootDER)
	wantText := x509util.CertificateToString(parsedInt) + "\n" +
		x509util.CertificateToString(parsedRoot) + "\n"

	// 1. The command asks the one endpoint and exits clean.
	s := newStubLog()
	defer s.close()
	s.json("/ct/v1/get-roots", 200, body)
	r := runClient("get-roots", "--log_uri="+s.URL, "--text=false")
	t.Check("get-roots-exit-status", r.code == 0, "exit=%d stderr=%s", r.code, oneLine(r.stderr))
	t.Equal("get-roots-request", strings.Join(s.requests(), "|"), "GET /ct/v1/get-roots")

	// 2. --text=false renders every root as a PEM CERTIFICATE block, in the
	//    order the log returned them and with nothing else on stdout.
	t.Equal("pem-stdout", r.stdout, wantPEM)

	// 3. The default rendering is the text one.
	s2 := newStubLog()
	defer s2.close()
	s2.json("/ct/v1/get-roots", 200, body)
	r2 := runClient("get-roots", "--log_uri="+s2.URL)
	t.Equal("text-stdout", r2.stdout, wantText)

	// 4. Both declared aliases are the same command.
	s3 := newStubLog()
	defer s3.close()
	s3.json("/ct/v1/get-roots", 200, body)
	t.Equal("alias-getroots-stdout",
		runClient("getroots", "--log_uri="+s3.URL, "--text=false").stdout, wantPEM)

	s4 := newStubLog()
	defer s4.close()
	s4.json("/ct/v1/get-roots", 200, body)
	t.Equal("alias-roots-stdout",
		runClient("roots", "--log_uri="+s4.URL, "--text=false").stdout, wantPEM)

	// 5. A log that accepts nothing is not an error; it prints nothing.
	s5 := newStubLog()
	defer s5.close()
	s5.json("/ct/v1/get-roots", 200, `{"certificates":[]}`)
	r5 := runClient("get-roots", "--log_uri="+s5.URL, "--text=false")
	t.Check("empty-root-set-prints-nothing", r5.code == 0 && r5.stdout == "",
		"exit=%d stdout=%q stderr=%s", r5.code, r5.stdout, oneLine(r5.stderr))

	// 6. A refusal from the log is fatal and prints nothing.
	s6 := newStubLog()
	defer s6.close()
	s6.json("/ct/v1/get-roots", 500, `{"error":"no"}`)
	r6 := runClient("get-roots", "--log_uri="+s6.URL, "--text=false")
	t.Check("server-error-is-fatal", r6.code != 0 && r6.stdout == "",
		"exit=%d stdout=%q", r6.code, r6.stdout)

	// 7. get-roots takes no positional argument, and the persistent
	//    connection flags bind after the command word like everywhere else.
	r7 := runClient("get-roots", "surplus")
	t.Check("surplus-positional-rejected",
		r7.code == 255 && r7.stdout == "" && strings.TrimSpace(r7.stderr) != "",
		"exit=%d stdout=%q stderr=%s", r7.code, r7.stdout, oneLine(r7.stderr))

	t.Done()
}

func oneLine(s string) string {
	return strings.ReplaceAll(strings.TrimSpace(s), "\n", " / ")
}

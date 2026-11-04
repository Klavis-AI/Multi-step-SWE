// Group x509util-issuer-and-file-loading.
//
// v1.0.13 adds the two loaders the new command-line tools are built on:
//
//	x509util.ReadFileOrURL(target, client)  one call site for "a filename or
//	                                        an http(s) URL"
//	x509util.GetIssuer(cert, client)        fetch a certificate's issuer from
//	                                        the first Authority Information
//	                                        Access URL, or report that the
//	                                        certificate names none
//
// Boundary: the exported functions. Every HTTP fetch is served by this
// driver's own handler on a loopback listener bound to :0; nothing leaves the
// machine, and every served body is a pinned fixture.
package main

import (
	"bytes"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"

	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	caDER := pemCert(t, vhelp.ReadFixture("rfc6962", "ca-cert.pem")).Raw
	leaf := pemCert(t, vhelp.ReadFixture("rfc6962", "test-cert.pem"))
	body := []byte("pinned file body\n")

	// One loopback server, bound on a port the kernel chooses, serving only
	// pinned bytes.
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/body":
			w.Write(body)
		case "/ca.der":
			w.Write(caDER)
		case "/garbage":
			w.Write([]byte("this is not a certificate"))
		default:
			http.Error(w, "nope", http.StatusNotFound)
		}
	}))
	defer srv.Close()
	hc := srv.Client()

	// --- ReadFileOrURL -------------------------------------------------------
	path := filepath.Join(os.Getenv("CT_SCRATCH"), "body.txt")
	if err := os.WriteFile(path, body, 0o600); err != nil {
		t.Fatalf("writing the scratch file: %v", err)
	}

	got, err := x509util.ReadFileOrURL(path, hc)
	t.Check("read-file-or-url-reads-a-local-file",
		err == nil && bytes.Equal(got, body), "err=%v, got %q", err, got)

	_, err = x509util.ReadFileOrURL(filepath.Join(os.Getenv("CT_SCRATCH"), "absent.txt"), hc)
	t.Check("read-file-or-url-on-a-missing-file-is-an-error", err != nil,
		"a missing file was read without error")

	got, err = x509util.ReadFileOrURL(srv.URL+"/body", hc)
	t.Check("read-file-or-url-fetches-an-http-url",
		err == nil && bytes.Equal(got, body), "err=%v, got %q", err, got)

	got, err = x509util.ReadFileOrURL(srv.URL+"/ca.der", hc)
	t.Check("read-file-or-url-returns-the-body-verbatim",
		err == nil && bytes.Equal(got, caDER), "err=%v, got %d bytes want %d", err, len(got), len(caDER))

	// --- GetIssuer -----------------------------------------------------------
	noAIA := *leaf
	noAIA.IssuingCertificateURL = nil
	issuer, err := x509util.GetIssuer(&noAIA, hc)
	t.Check("certificate-with-no-aia-url-has-no-issuer-to-fetch",
		err == nil && issuer == nil, "err=%v, issuer=%v", err, issuer != nil)

	withAIA := *leaf
	withAIA.IssuingCertificateURL = []string{srv.URL + "/ca.der"}
	issuer, err = x509util.GetIssuer(&withAIA, hc)
	t.Check("get-issuer-fetches-and-parses-the-der-at-the-aia-url",
		err == nil && issuer != nil && bytes.Equal(issuer.Raw, caDER) &&
			issuer.Subject.Organization[0] == "Certificate Transparency CA",
		"err=%v, issuer=%v", err, describe(issuer))

	firstOfTwo := *leaf
	firstOfTwo.IssuingCertificateURL = []string{srv.URL + "/ca.der", srv.URL + "/garbage"}
	issuer, err = x509util.GetIssuer(&firstOfTwo, hc)
	t.Check("get-issuer-uses-the-first-aia-url",
		err == nil && issuer != nil && bytes.Equal(issuer.Raw, caDER),
		"err=%v, issuer=%v", err, describe(issuer))

	missing := *leaf
	missing.IssuingCertificateURL = []string{srv.URL + "/absent"}
	issuer, err = x509util.GetIssuer(&missing, hc)
	t.Check("get-issuer-reports-a-non-200-response",
		err != nil && issuer == nil, "err=%v, issuer=%v", err, describe(issuer))

	bad := *leaf
	bad.IssuingCertificateURL = []string{srv.URL + "/garbage"}
	issuer, err = x509util.GetIssuer(&bad, hc)
	t.Check("get-issuer-reports-a-body-that-is-not-a-certificate",
		err != nil && issuer == nil, "err=%v, issuer=%v", err, describe(issuer))

	dead := *leaf
	dead.IssuingCertificateURL = []string{"http://" + deadLoopbackAddr(t) + "/ca.der"}
	issuer, err = x509util.GetIssuer(&dead, hc)
	t.Check("get-issuer-reports-an-unreachable-aia-url",
		err != nil && issuer == nil, "err=%v, issuer=%v", err, describe(issuer))

	t.Done()
}

func describe(c *x509.Certificate) string {
	if c == nil {
		return "<nil>"
	}
	return c.Subject.String()
}

// deadLoopbackAddr returns a loopback host:port that nothing is listening on:
// the kernel picks the port, and the listener is closed before it is used.
func deadLoopbackAddr(t *vhelp.T) string {
	// Nothing must be listening here for the whole life of the case, so
	// the port is taken from below the ephemeral range: the kernel cannot
	// hand it to an unrelated process and quietly make this address live.
	return lowBandLoopbackAddr(t)
}

// lowBandLoopbackAddr returns a loopback host:port taken from BELOW the
// kernel's ephemeral range (32768+ on the harness host), probed free by an
// actual bind. A `:0` probe hands back a port the kernel may immediately
// reassign to any unrelated process on the machine in the window before it is
// used; a low-band port is never handed out by the ephemeral allocator, so
// only an explicit binder of that exact port can take it. The starting slot is
// dispersed by one throwaway `:0` bind so that sibling probes in the same run
// do not converge on the same slot.
func lowBandLoopbackAddr(t *vhelp.T) string {
	seed, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen 127.0.0.1:0 to disperse the probe: %v", err)
	}
	base := seed.Addr().(*net.TCPAddr).Port % 12000
	if err := seed.Close(); err != nil {
		t.Fatalf("close the dispersal listener: %v", err)
	}
	for i := 0; i < 512; i++ {
		l, err := net.ListenTCP("tcp", &net.TCPAddr{
			IP:   net.IPv4(127, 0, 0, 1),
			Port: 20000 + (base+i)%12000,
		})
		if err != nil {
			continue
		}
		a := l.Addr().String()
		if err := l.Close(); err != nil {
			t.Fatalf("close probe listener: %v", err)
		}
		return a
	}
	t.Fatalf("no free loopback port in 20000-31999 after 512 probes")
	return ""
}

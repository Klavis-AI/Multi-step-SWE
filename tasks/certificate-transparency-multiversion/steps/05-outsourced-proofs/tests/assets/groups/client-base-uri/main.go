// Group client-base-uri.
//
// v1.0.17 adds a BaseURI() accessor to every log-client transport (commit
// f8a1ba17, "*client: add BaseURI() method for convenience") and puts it in
// the client.CheckLogClient interface, so a checker can report which log an
// answer came from without being handed the URI separately. ctclient switches
// its get-sth and add-chain output over to it in the same release.
//
// Graded at the library boundary, per function, with exact strings. Nothing
// here opens a socket: neither constructor contacts the log.
//
// v1.0.13 has no such method on any of the three types and its
// CheckLogClient interface does not list it, so this driver does not compile
// there.
package main

import (
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/dnsclient"
	"github.com/google/certificate-transparency-go/jsonclient"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	// --- 1. the JSON transport reports the URI it normalised ---------------
	jc, err := jsonclient.New("http://ct.bench.invalid/log/", nil, jsonclient.Options{})
	if err != nil {
		t.Check("jsonclient-base-uri-trims-trailing-slash", false, "jsonclient.New: %v", err)
	} else {
		t.Equal("jsonclient-base-uri-trims-trailing-slash", jc.BaseURI(), "http://ct.bench.invalid/log")
	}

	// --- 2. ... including the degenerate multi-slash form -------------------
	jc2, err := jsonclient.New("http://ct.bench.invalid/log///", nil, jsonclient.Options{})
	if err != nil {
		t.Check("jsonclient-base-uri-trims-every-trailing-slash", false, "jsonclient.New: %v", err)
	} else {
		t.Equal("jsonclient-base-uri-trims-every-trailing-slash", jc2.BaseURI(), "http://ct.bench.invalid/log")
	}

	// --- 3. the log client inherits it -------------------------------------
	lc, err := client.New("http://ct.bench.invalid/pilot", nil, jsonclient.Options{})
	if err != nil {
		t.Check("logclient-base-uri", false, "client.New: %v", err)
		t.Check("checklogclient-interface-exposes-base-uri", false, "client.New: %v", err)
	} else {
		t.Equal("logclient-base-uri", lc.BaseURI(), "http://ct.bench.invalid/pilot")

		// --- 4. and the checking interface now demands it ------------------
		// The assignment is the point: at this milestone CheckLogClient lists
		// BaseURI, so a *client.LogClient satisfies it and the accessor is
		// reachable through the interface, which is how ctclient uses it.
		var clc client.CheckLogClient = lc
		t.Equal("checklogclient-interface-exposes-base-uri", clc.BaseURI(), "http://ct.bench.invalid/pilot")
	}

	// --- 5. the DNS transport reports an RFC 4501 dns: URI ------------------
	// dnsclient.New absolutises the base name with a trailing dot, and
	// BaseURI prefixes the scheme -- so BaseURI reports the stored form, dot
	// included, not the argument as passed. CLIENT-API.md section 6.1 states
	// both halves (the signature "returns dns:<base>", and the paragraph
	// under it that has New append the dot); TEST_INTERFACE.md section 5, the
	// dnsclient row, spells out the string that follows from them.
	dc, err := dnsclient.New("ct.bench.invalid", jsonclient.Options{})
	if err != nil {
		t.Check("dnsclient-base-uri-uses-dns-scheme", false, "dnsclient.New: %v", err)
	} else {
		t.Equal("dnsclient-base-uri-uses-dns-scheme", dc.BaseURI(), "dns:ct.bench.invalid.")
	}

	t.Done()
}

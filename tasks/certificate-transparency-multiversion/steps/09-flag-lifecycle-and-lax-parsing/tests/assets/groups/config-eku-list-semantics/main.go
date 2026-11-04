// Group config-eku-list-semantics.
//
// v1.1.2 changes what a log's ext_key_usages list means after validation.
// Before, an empty list was rewritten to the single element ExtKeyUsageAny and
// an explicit "Any" was just one more entry in the list. Now "Any" -- alone or
// anywhere in the list -- means "do not filter", and validation reports that
// as an empty KeyUsages slice, which is also what an empty list produces.
//
// Observed per function through ctfe.ValidateLogConfig on the exported
// ValidatedLogConfig.KeyUsages field. Each config below is a mirror, so no
// private key is needed and nothing is read from disk.
package main

import (
	"fmt"
	"strings"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian/crypto/keyspb"

	"ctverify/vhelp"
)

// The pinned log SPKI, as DER. Only its parseability matters here.
const spkiHex = "3059301306072a8648ce3d020106082a8648ce3d03010703420004" +
	"57609453c6d4b60d3d0826690f2b80af73b8e2924f3a54df8c8b06511e5372c5" +
	"4e21bfcb2d77ff29b305a6fa4f9788138c146a969c0f3d74cb09659ad791fac9"

func cfg(prefix string, ekus []string) *configpb.LogConfig {
	return &configpb.LogConfig{
		LogId:        1001,
		Prefix:       prefix,
		IsMirror:     true,
		PublicKey:    &keyspb.PublicKey{Der: unhex(spkiHex)},
		ExtKeyUsages: ekus,
	}
}

func unhex(s string) []byte {
	out := make([]byte, 0, len(s)/2)
	for i := 0; i+1 < len(s); i += 2 {
		out = append(out, byte(hexVal(s[i])<<4|hexVal(s[i+1])))
	}
	return out
}

func hexVal(c byte) int {
	switch {
	case c >= '0' && c <= '9':
		return int(c - '0')
	case c >= 'a' && c <= 'f':
		return int(c-'a') + 10
	case c >= 'A' && c <= 'F':
		return int(c-'A') + 10
	}
	panic("bad hex")
}

func main() {
	t := vhelp.New()

	check := func(id string, ekus []string, want string) {
		v, err := ctfe.ValidateLogConfig(cfg("p-"+id, ekus))
		if err != nil {
			t.Check(id, false, "ValidateLogConfig: %v", err)
			return
		}
		parts := make([]string, 0, len(v.KeyUsages))
		for _, ku := range v.KeyUsages {
			parts = append(parts, fmt.Sprintf("%d", int(ku)))
		}
		t.Equal(id, fmt.Sprintf("[%s]", strings.Join(parts, " ")), want)
	}

	// x509.ExtKeyUsageAny is 0, ServerAuth 1, ClientAuth 2, OCSPSigning 9.

	// 1. No list at all: no filtering, and the marker value is gone.
	check("empty-list-yields-no-filter", nil, "[]")

	// 2. "Any" alone: no filtering.
	check("any-alone-yields-no-filter", []string{"Any"}, "[]")

	// 3. "Any" anywhere clears everything that came before it and stops.
	check("any-anywhere-clears-the-list", []string{"ServerAuth", "Any", "ClientAuth"}, "[]")

	// 4. An explicit list is preserved in order.
	check("explicit-list-preserved", []string{"ServerAuth", "ClientAuth"}, "[1 2]")

	// 5. A single non-Any usage is preserved.
	check("single-usage-preserved", []string{"OCSPSigning"}, "[9]")

	// 6. An unknown usage is still a validation error.
	_, err := ctfe.ValidateLogConfig(cfg("p-bogus", []string{"NotAKeyUsage"}))
	t.Check("unknown-usage-rejected", err != nil, "ValidateLogConfig accepted an unknown extended key usage")

	t.Done()
}

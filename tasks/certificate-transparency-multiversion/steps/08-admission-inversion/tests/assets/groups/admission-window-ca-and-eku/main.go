// Group admission-window-ca-and-eku. KIND=scored.
//
// The three admission dimensions HTTP-API.md §5.1 lists either side of the one
// this milestone inverts. None of the three *decisions* changes at v1.1.0,
// which is exactly why they are graded: the leaf-only expiry test is being
// inserted in front of path building, and it is easy to move or break its
// neighbours while doing it.
//
// It is scored, not gate, and the distinction is the whole point of the tier.
// Every case below is expressed through ctfe.NewCertValidationOpts, which is
// this milestone's own way of building the options -- a constructor taking an
// explicit current time, where v1.0.21 had an unexported struct with no
// constructor at all. A driver naming it does not compile against v1.0.21, so
// as a gate group this was a migration sitting in the gate: it would have made
// the Oracle fail its own step (gate rule 3). As a scored group it is correct
// and required, and it cannot be satisfied by inherited work alone (rule 4),
// because the constructor is not there to call.
//
//	§5.1 item 4  the temporal shard window, half-open [start, limit)
//	§5.1 item 5  accept_only_ca
//	§5.1 item 6  ext_key_usages -- which at this release has *no effect on
//	             admission at all*, because path building sets
//	             DisableEKUChecks and the leaf-only EKU test does not arrive
//	             until v1.1.2 (HTTP-API.md §5.4). Three leaves that a
//	             codeSigning-only configuration would reject at v1.1.2 must all
//	             be admitted here.
//
// Everything runs through ctfe.NewCertValidationOpts with an injected clock,
// so no assertion touches the wall clock, and no error text is compared: each
// refusal is paired with an admission of the same chain through the same call
// with the one switch flipped.
package main

import (
	"encoding/pem"
	"time"

	"ctverify/vhelp"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509"
)

// Pinned instants, chosen around the minted corpus's notAfter values: every
// leaf below expires 2060-01-01Z and the intermediate 2100-01-01Z.
var (
	atNow      = time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)
	leafExpiry = time.Date(2060, 1, 1, 0, 0, 0, 0, time.UTC)
	before     = time.Date(2050, 1, 1, 0, 0, 0, 0, time.UTC)
	after      = time.Date(2070, 1, 1, 0, 0, 0, 0, time.UTC)
)

func der(t *vhelp.T, name string) []byte {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", name))
	if blk == nil || blk.Type != "CERTIFICATE" {
		t.Fatalf("fixture minted/%s is not a PEM certificate", name)
	}
	return blk.Bytes
}

func chainOf(t *vhelp.T, names ...string) [][]byte {
	out := make([][]byte, 0, len(names))
	for _, n := range names {
		out = append(out, der(t, n))
	}
	return out
}

func roots(t *vhelp.T) *ctfe.PEMCertPool {
	p := ctfe.NewPEMCertPool()
	if !p.AppendCertsFromPEM(vhelp.ReadFixture("minted", "bench-root.pem")) {
		t.Fatalf("could not load minted/bench-root.pem into a PEMCertPool")
	}
	return p
}

func main() {
	t := vhelp.New()
	pool := roots(t)

	leafChain := chainOf(t, "leaf-valid.pem", "bench-intermediate.pem")
	caChain := chainOf(t, "bench-intermediate.pem")
	unrooted := chainOf(t, "leaf-unrooted.pem")

	// window builds options that differ from the permissive default in the
	// shard window alone.
	window := func(start, limit *time.Time) ctfe.CertValidationOpts {
		return ctfe.NewCertValidationOpts(pool, atNow, false, false, start, limit, false, nil)
	}
	decide := func(id string, want bool, raw [][]byte, opts ctfe.CertValidationOpts) {
		got, err := ctfe.ValidateChain(raw, opts)
		if want {
			t.Check(id, err == nil && len(got) > 0,
				"ValidateChain returned %d certificates, err=%v; want an admission", len(got), err)
			return
		}
		t.Check(id, err != nil && len(got) == 0,
			"ValidateChain returned %d certificates, err=%v; want a refusal", len(got), err)
	}

	// --- §5.1 item 4: the shard window, on the leaf's notAfter --------------
	decide("shard-window-rejects-notafter-before-start", false, leafChain, window(&after, nil))
	decide("shard-window-admits-notafter-inside", true, leafChain, window(&before, &after))

	// Half-open [start, limit): notAfter exactly at start is inside, notAfter
	// exactly at limit is outside.
	decide("shard-window-start-is-inclusive", true, leafChain, window(&leafExpiry, nil))
	decide("shard-window-limit-is-exclusive", false, leafChain, window(nil, &leafExpiry))

	// --- §5.1 item 5: accept_only_ca ---------------------------------------
	onlyCA := ctfe.NewCertValidationOpts(pool, atNow, false, false, nil, nil, true, nil)
	decide("accept-only-ca-rejects-an-end-entity-leaf", false, leafChain, onlyCA)
	// The paired admission: the same switch, a chain whose leaf *is* a CA.
	decide("accept-only-ca-admits-a-ca-leaf", true, caChain, onlyCA)
	// And with the switch off the end-entity leaf is admitted again, so the
	// refusal above is attributable to the switch and to nothing else.
	decide("accept-only-ca-off-admits-the-same-leaf", true, leafChain, window(nil, nil))

	// --- §5.1 item 6 / §5.4: ext_key_usages does nothing at this release ----
	// A codeSigning-only list. From v1.1.2 each of the three leaves below is
	// refused with `rejecting certificate without EKU in %v`; here all three
	// are admitted, because DisableEKUChecks turns the test off inside path
	// building and the leaf-only test does not exist yet.
	codeSigningOnly := ctfe.NewCertValidationOpts(pool, atNow, false, false, nil, nil, false,
		[]x509.ExtKeyUsage{x509.ExtKeyUsageCodeSigning})
	decide("ext-key-usages-does-not-refuse-a-serverauth-leaf", true,
		chainOf(t, "leaf-eku-serverauth.pem", "bench-intermediate.pem"), codeSigningOnly)
	decide("ext-key-usages-does-not-refuse-a-clientauth-leaf", true,
		chainOf(t, "leaf-eku-clientauth.pem", "bench-intermediate.pem"), codeSigningOnly)
	decide("ext-key-usages-does-not-refuse-an-eku-less-leaf", true,
		chainOf(t, "leaf-eku-none.pem", "bench-intermediate.pem"), codeSigningOnly)

	// --- the guard against an implementation that admits everything --------
	decide("unrooted-chain-refused-with-every-switch-permissive", false, unrooted, window(nil, nil))

	t.Done()
}

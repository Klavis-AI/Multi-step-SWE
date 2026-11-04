// Group ctfe-reject-extensions.
//
// reject_extensions is a new LogConfig field at this milestone. It names OIDs
// that must not appear in the *submitted leaf*; the rule is applied before the
// chain is built, so a refusal never reaches the backend, and an OID that only
// the issuers carry is not a refusal. A syntactically invalid OID is refused
// when the instance is set up, not at submission time.
//
// SCOPE-BACKEND: no Trillian server runs. A refusal is decided before the
// backend is called, so it is an exact 400; a chain this configuration must
// admit is graded by the fact that it gets past admission and fails at the
// absent backend with a 503.
package main

import (
	"net/http"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	valid := chainJSON(t, "chain-valid.pem")
	precert := chainJSON(t, "chain-precert.pem")

	// Control: with no reject_extensions the pinned chain is admitted. Every
	// instance below is built against a backend that is not listening, so
	// "admitted" shows up as a 503 -- validation passed and the submission
	// died at the backend -- and "refused" shows up as a 400 decided before
	// any backend call was attempted. The two are never confusable.
	base, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{}), false)
	if err != nil {
		t.Fatalf("set up baseline instance: %v", err)
	}
	defer base.stop()
	r := base.post(t, ct.AddChainPath, valid)
	t.Check("no-reject-admits-chain", r.status == http.StatusServiceUnavailable,
		"status %d body %q", r.status, r.body)

	// The leaf's subjectAltName sits at index 4 of its extension list.
	san, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{RejectExtensions: []string{"2.5.29.17"}}), false)
	if err != nil {
		t.Fatalf("set up san-rejecting instance: %v", err)
	}
	defer san.stop()
	r = san.post(t, ct.AddChainPath, valid)
	t.Check("reject-san-refuses-leaf",
		r.status == http.StatusBadRequest &&
			r.body == "Bad Request\nfailed to verify add-chain contents: chain failed to verify: rejecting certificate containing extension 2.5.29.17 at index 4\n",
		"status %d body %q", r.status, r.body)

	// A refusal is decided before any backend call. This is read off the
	// counting client rather than off the status code: the admitted control
	// above reached the (dead) backend and so has a non-zero call count,
	// while the instance that refused the leaf made no call at all.
	t.Check("refusal-never-reaches-backend",
		san.backendCalls() == 0 && base.backendCalls() > 0,
		"refusing instance made %d backend calls, admitting control made %d",
		san.backendCalls(), base.backendCalls())

	// basicConstraints sits at index 2 of the same leaf.
	bc, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{RejectExtensions: []string{"2.5.29.19"}}), false)
	if err != nil {
		t.Fatalf("set up bc-rejecting instance: %v", err)
	}
	defer bc.stop()
	r = bc.post(t, ct.AddChainPath, valid)
	t.Check("reject-basicconstraints-refuses-leaf",
		r.status == http.StatusBadRequest &&
			r.body == "Bad Request\nfailed to verify add-chain contents: chain failed to verify: rejecting certificate containing extension 2.5.29.19 at index 2\n",
		"status %d body %q", r.status, r.body)

	// Two listed OIDs: the one that occurs earliest in the leaf is reported.
	two, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{RejectExtensions: []string{"2.5.29.17", "2.5.29.19"}}), false)
	if err != nil {
		t.Fatalf("set up two-oid instance: %v", err)
	}
	defer two.stop()
	r = two.post(t, ct.AddChainPath, valid)
	t.Check("two-oids-report-lowest-index",
		r.status == http.StatusBadRequest &&
			r.body == "Bad Request\nfailed to verify add-chain contents: chain failed to verify: rejecting certificate containing extension 2.5.29.19 at index 2\n",
		"status %d body %q", r.status, r.body)

	// An OID no certificate in the corpus carries changes nothing.
	absent, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{RejectExtensions: []string{"1.2.3.4.5"}}), false)
	if err != nil {
		t.Fatalf("set up absent-oid instance: %v", err)
	}
	defer absent.stop()
	r = absent.post(t, ct.AddChainPath, valid)
	t.Check("absent-oid-admits-chain", r.status == http.StatusServiceUnavailable,
		"status %d body %q", r.status, r.body)

	// subjectKeyIdentifier appears on the intermediate and the root but not on
	// the leaf, and the rule looks at the leaf only.
	ski, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{RejectExtensions: []string{"2.5.29.14"}}), false)
	if err != nil {
		t.Fatalf("set up ski-rejecting instance: %v", err)
	}
	defer ski.stop()
	r = ski.post(t, ct.AddChainPath, valid)
	t.Check("issuer-only-oid-admits-chain", r.status == http.StatusServiceUnavailable,
		"status %d body %q", r.status, r.body)

	// The rule applies to add-pre-chain too: the poison extension sits at
	// index 5 of the pinned precertificate.
	poison, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{RejectExtensions: []string{"1.3.6.1.4.1.11129.2.4.3"}}), false)
	if err != nil {
		t.Fatalf("set up poison-rejecting instance: %v", err)
	}
	defer poison.stop()
	r = poison.post(t, ct.AddPreChainPath, precert)
	t.Check("reject-poison-refuses-precert",
		r.status == http.StatusBadRequest &&
			r.body == "Bad Request\nfailed to verify add-chain contents: chain failed to verify: rejecting certificate containing extension 1.3.6.1.4.1.11129.2.4.3 at index 5\n",
		"status %d body %q", r.status, r.body)

	// A value that is not an OID is refused when the instance is built.
	bad, err := setUpDead(t, pinnedConfig(t, &configpb.LogConfig{RejectExtensions: []string{"one.banana"}}), false)
	t.Check("malformed-oid-refused-at-setup", bad == nil && err != nil, "instance %v err %v", bad != nil, err)

	t.Done()
}

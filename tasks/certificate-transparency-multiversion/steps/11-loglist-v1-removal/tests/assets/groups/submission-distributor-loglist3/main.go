// Group submission-distributor-loglist3.
//
// The submission proxy's core. At v1.1.4 every entry point it exposes is typed
// on the v3 log list:
//
//	type LogClientBuilder func(*loglist3.Log) (client.AddLogClient, error)
//	func NewDistributor(ll *loglist3.LogList, plc ctpolicy.CTPolicy, lcBuilder LogClientBuilder, mf monitoring.MetricFactory) (*Distributor, error)
//
// and the policy it is handed must itself be a v3-typed ctpolicy.CTPolicy.
// The previous release declares all three over the v2 schema, so neither the
// log-client builder below nor the policy below can be passed to it.
//
// No log is contacted: the builder hands the distributor a benchmark-owned
// client that answers get-roots from pinned fixtures and add-chain with a
// pinned SCT (SCOPE-NET, SCOPE-TIME).
package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"sort"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/ctpolicy"
	"github.com/google/certificate-transparency-go/loglist3"
	"github.com/google/certificate-transparency-go/submission"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// benchPolicy is a CTPolicy in the shape v1.1.4 declares: it is handed the v3
// log list. Declaring it at all is impossible against the previous release.
type benchPolicy struct{}

func (benchPolicy) LogsByGroup(cert *x509.Certificate, approved *loglist3.LogList) (ctpolicy.LogPolicyData, error) {
	base, err := ctpolicy.BaseGroupFor(approved, 2)
	return ctpolicy.LogPolicyData{base.Name: base}, err
}

func (benchPolicy) Name() string { return "Harbor Bench Policy" }

var _ ctpolicy.CTPolicy = benchPolicy{}

// pinnedSCT is the same for every log except for its LogID, which is the
// SHA-256 of that log's own pinned key. Timestamp and signature are constants.
func pinnedSCT(keyHash [32]byte) *ct.SignedCertificateTimestamp {
	sct := &ct.SignedCertificateTimestamp{
		SCTVersion: ct.V1,
		Timestamp:  1500000000000,
		Signature: ct.DigitallySigned{
			Algorithm: tls.SignatureAndHashAlgorithm{
				Hash:      tls.SHA256,
				Signature: tls.ECDSA,
			},
			Signature: []byte{0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x02},
		},
	}
	copy(sct.LogID.KeyID[:], keyHash[:])
	return sct
}

// benchLogClient answers the three calls client.AddLogClient declares without
// a socket.
type benchLogClient struct {
	url     string
	keyHash [32]byte
	roots   []ct.ASN1Cert
	rootErr error
}

func (c *benchLogClient) AddChain(_ context.Context, _ []ct.ASN1Cert) (*ct.SignedCertificateTimestamp, error) {
	return pinnedSCT(c.keyHash), nil
}

func (c *benchLogClient) AddPreChain(_ context.Context, _ []ct.ASN1Cert) (*ct.SignedCertificateTimestamp, error) {
	return nil, fmt.Errorf("bench log %s does not accept pre-chains", c.url)
}

func (c *benchLogClient) GetAcceptedRoots(_ context.Context) ([]ct.ASN1Cert, error) {
	if c.rootErr != nil {
		return nil, c.rootErr
	}
	return c.roots, nil
}

func main() {
	t := vhelp.New()

	ll, err := loglist3.NewFromJSON([]byte(benchLogList3JSON))
	if err != nil {
		t.Fatalf("parse pinned v3 log list: %v", err)
	}
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")

	built := map[string]bool{}
	// The builder has to *be* a submission.LogClientBuilder, which at this
	// milestone means it takes a *loglist3.Log. The pending log is the one
	// told to fail get-roots.
	var lcb submission.LogClientBuilder = func(lg *loglist3.Log) (client.AddLogClient, error) {
		built[lg.URL] = true
		c := &benchLogClient{url: lg.URL, roots: []ct.ASN1Cert{{Data: rootDER}, {Data: intDER}}}
		copy(c.keyHash[:], sha256Of(lg.Key))
		if strings.HasSuffix(lg.URL, "/pending/") {
			c.rootErr = fmt.Errorf("pinned get-roots failure")
		}
		return c, nil
	}

	d, err := submission.NewDistributor(ll, benchPolicy{}, lcb, nil)
	if err != nil {
		t.Fatalf("NewDistributor over the pinned v3 list: %v", err)
	}

	// A distributor is built only over the usable, pending and qualified logs
	// the v3 statuses select; the readonly and retired ones get no client.
	keys := make([]string, 0, len(built))
	for u := range built {
		keys = append(keys, u)
	}
	sort.Strings(keys)
	t.Equal("distributor-builds-clients-for-selected-statuses", strings.Join(keys, ","),
		"https://google.bench.invalid/qualified/,"+
			"https://google.bench.invalid/usable/,"+
			"https://shop.bench.invalid/pending/,"+
			"https://shop.bench.invalid/usable/")

	// RefreshRoots reports per-log failures by URL and succeeds elsewhere.
	ctx := context.Background()
	errs := d.RefreshRoots(ctx)
	failed := make([]string, 0, len(errs))
	for u := range errs {
		failed = append(failed, u)
	}
	sort.Strings(failed)
	t.Equal("refresh-roots-reports-failures-by-log-url", strings.Join(failed, ","),
		"https://shop.bench.invalid/pending/")

	// SubmitToLog reaches exactly the named log and returns its pinned SCT.
	sct, err := d.SubmitToLog(ctx, "https://google.bench.invalid/usable/",
		[]ct.ASN1Cert{{Data: leafDER}}, false)
	if err != nil {
		t.Check("submit-to-log-returns-that-logs-sct", false, "SubmitToLog: %v", err)
	} else {
		googleKey := ll.FindLogByURL("https://google.bench.invalid/usable/").Key
		t.Equal("submit-to-log-returns-that-logs-sct",
			hex.EncodeToString(sct.LogID.KeyID[:]), hex.EncodeToString(sha256Of(googleKey)))
	}

	// An unknown URL is refused rather than silently dropped.
	_, err = d.SubmitToLog(ctx, "https://not.in.the.list.invalid/", []ct.ASN1Cert{{Data: leafDER}}, false)
	t.Check("submit-to-unknown-log-is-refused", err != nil, "err=%v", err)

	// The whole add-chain path: policy over the v3 list, one SCT per usable log.
	chain := [][]byte{leafDER, intDER}
	scts, err := d.AddChain(ctx, chain, false)
	if err != nil {
		t.Check("add-chain-collects-one-sct-per-usable-log", false, "AddChain: %v", err)
		t.Check("add-chain-sct-list-is-exact-der", false, "AddChain: %v", err)
	} else {
		got := make([]string, 0, len(scts))
		for _, a := range scts {
			got = append(got, a.LogURL)
		}
		sort.Strings(got)
		t.Equal("add-chain-collects-one-sct-per-usable-log", strings.Join(got, ","),
			"https://google.bench.invalid/usable/,https://shop.bench.invalid/usable/")

		// The SCT list the proxy hands back is an exact DER encoding of the
		// pinned SCTs, in whatever order the race returned them.
		sorted := make([]*submission.AssignedSCT, len(scts))
		copy(sorted, scts)
		sort.Slice(sorted, func(i, j int) bool { return sorted[i].LogURL < sorted[j].LogURL })
		der, err := submission.ASN1MarshalSCTs(sorted)
		if err != nil {
			t.Check("add-chain-sct-list-is-exact-der", false, "ASN1MarshalSCTs: %v", err)
		} else {
			t.Equal("add-chain-sct-list-is-exact-der", hex.EncodeToString(der), wantSCTListDER)
		}
	}

	// add-pre-chain over a plain certificate is refused by type, before any
	// log is contacted: an error and no SCTs. The error's wording is
	// upstream's and is not graded.
	preSCTs, err := d.AddPreChain(ctx, chain, false)
	t.Check("add-pre-chain-rejects-a-plain-certificate",
		err != nil && len(preSCTs) == 0, "scts=%d err=%v", len(preSCTs), err)

	// An empty chain is refused the same way, and again before any log is
	// contacted -- so the refusal cannot be a transport failure in disguise.
	emptySCTs, err := d.AddChain(ctx, [][]byte{}, false)
	t.Check("add-chain-rejects-an-empty-chain",
		err != nil && len(emptySCTs) == 0, "scts=%d err=%v", len(emptySCTs), err)

	t.Done()
}

func sha256Of(b []byte) []byte {
	h := sha256.Sum256(b)
	return h[:]
}

// wantSCTListDER is the exact DER of the SCT list the two usable logs yield:
// an ASN.1 OCTET STRING wrapping a TLS-encoded SignedCertificateTimestampList
// of two 55-byte SCTs, ordered by log URL. Computed independently of the
// product from the pinned keys, the pinned timestamp and the pinned signature.
const wantSCTListDER = "0474007200370034ea2dc3d06d3f9948646be0db103c73323d8787c722ad103d73e0b09549" +
	"4aa50000015d3ef798000000040300083006020101020102003700df1c2ec11500945247a9" +
	"6168325ddc5c7959e8f7c6d388fc002e0bbd3f74d7640000015d3ef79800000004030008300" +
	"6020101020102"

// Group loglist3-filters.
//
// Alongside the v3 schema, v1.1.3 adds loglist3's selection API: SelectByStatus
// over the new six-state union, TemporallyCompatible against a certificate's
// NotAfter and a half-open per-log interval, RootCompatible against a
// per-log-URL map of x509util.PEMCertPool (the pool type moved in this same
// milestone), their Compatible composition, and the FindLogBy*/FuzzyFindLog
// lookups. This group drives all of them over a pinned v3 document and pinned
// minted certificates, and compares the surviving operator/log structure
// exactly.
//
// loglist3 does not exist at v1.1.2, and neither loglist nor loglist2 there
// has a LogRoots keyed on x509util.PEMCertPool or a rejected/undefined status,
// so no case here can be satisfied by the previous milestone.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"os"
	"strings"

	"github.com/google/certificate-transparency-go/loglist3"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

// render flattens a log list into "Operator:log,log|Operator:log" so a whole
// filter result is one exact comparison.
func render(ll loglist3.LogList) string {
	var parts []string
	for _, op := range ll.Operators {
		var names []string
		for _, l := range op.Logs {
			names = append(names, l.Description)
		}
		parts = append(parts, op.Name+":"+strings.Join(names, ","))
	}
	return strings.Join(parts, "|")
}

func names(logs []*loglist3.Log) string {
	var out []string
	for _, l := range logs {
		out = append(out, l.Description)
	}
	return strings.Join(out, ",")
}

func main() {
	t := vhelp.New()

	blob, err := os.ReadFile(vhelp.GroupFile("loglist-v3.json"))
	if err != nil {
		t.Fatalf("read pinned v3 log list: %v", err)
	}
	ll, err := loglist3.NewFromJSON(blob)
	if err != nil {
		t.Fatalf("NewFromJSON: %v", err)
	}
	if len(ll.Operators) != 3 {
		t.Fatalf("pinned list has %d operators, want 3", len(ll.Operators))
	}

	parse := func(rel string) *x509.Certificate {
		c, err := x509.ParseCertificate(vhelp.ReadFixture("minted", rel))
		if x509.IsFatal(err) {
			t.Fatalf("parse %s: %v", rel, err)
		}
		return c
	}
	leafValid := parse("leaf-valid.der")     // NotAfter 2060-01-01
	leafExpired := parse("leaf-expired.der") // NotAfter 2002-01-01
	benchRoot := parse("bench-root.der")
	rogueRoot := parse("rogue-root.der")

	// 1. Status selection over the six-way union.
	t.Equal("select-usable", render(ll.SelectByStatus([]loglist3.LogStatus{loglist3.UsableLogStatus})),
		"Bench Operator:bench-alpha")
	t.Equal("select-retired-and-pending",
		render(ll.SelectByStatus([]loglist3.LogStatus{
			loglist3.RetiredLogStatus, loglist3.PendingLogStatus})),
		"Google Bench:bench-gamma,bench-delta")
	t.Equal("select-undefined",
		render(ll.SelectByStatus([]loglist3.LogStatus{loglist3.UndefinedLogStatus})),
		"Third Operator:bench-eta")
	t.Equal("select-readonly-and-rejected",
		render(ll.SelectByStatus([]loglist3.LogStatus{
			loglist3.ReadOnlyLogStatus, loglist3.RejectedLogStatus})),
		"Bench Operator:bench-beta|Third Operator:bench-zeta")

	// 2. Temporal selection. A log with no interval is always compatible.
	t.Equal("temporal-2060-cert", render(ll.TemporallyCompatible(leafValid)),
		"Bench Operator:bench-alpha,bench-beta|Google Bench:bench-gamma,bench-delta|Third Operator:bench-eta")

	// 3. The interval is half-open: bench-epsilon starts exactly at this
	//    certificate's NotAfter and is kept; bench-zeta ends exactly there and
	//    is dropped.
	t.Equal("temporal-boundary-is-half-open", render(ll.TemporallyCompatible(leafExpired)),
		"Bench Operator:bench-beta|Google Bench:bench-gamma,bench-delta|Third Operator:bench-epsilon,bench-eta")

	// 4. A nil certificate selects nothing.
	t.Equal("temporal-nil-cert", render(ll.TemporallyCompatible(nil)), "")

	// 5. Root selection. Logs absent from the map are treated as unknown and
	//    kept; logs present are kept only if the pool accepts the root.
	alphaPool := x509util.NewPEMCertPool()
	if !alphaPool.AppendCertsFromPEM(vhelp.ReadFixture("minted", "bench-root.pem")) {
		t.Fatalf("failed to build alpha root pool")
	}
	betaPool := x509util.NewPEMCertPool()
	if !betaPool.AppendCertsFromPEM(vhelp.ReadFixture("minted", "rogue-root.pem")) {
		t.Fatalf("failed to build beta root pool")
	}
	roots := loglist3.LogRoots{
		"https://alpha.bench.example/": alphaPool,
		"https://beta.bench.example":   betaPool,
	}
	t.Equal("root-compatible-bench-root", render(ll.RootCompatible(benchRoot, roots)),
		"Bench Operator:bench-alpha|Google Bench:bench-gamma,bench-delta|Third Operator:bench-epsilon,bench-zeta,bench-eta")
	t.Equal("root-compatible-rogue-root", render(ll.RootCompatible(rogueRoot, roots)),
		"Bench Operator:bench-beta|Google Bench:bench-gamma,bench-delta|Third Operator:bench-epsilon,bench-zeta,bench-eta")

	// 6. A nil root drops every log whose roots are known.
	t.Equal("root-compatible-nil-root", render(ll.RootCompatible(nil, roots)),
		"Google Bench:bench-gamma,bench-delta|Third Operator:bench-epsilon,bench-zeta,bench-eta")

	// 7. A chain whose last certificate is not a CA selects nothing at all.
	t.Equal("root-compatible-non-ca", render(ll.RootCompatible(leafValid, roots)), "")

	// 8. Compatible is the temporal filter followed by the root filter.
	t.Equal("compatible-composition", render(ll.Compatible(leafValid, benchRoot, roots)),
		"Bench Operator:bench-alpha|Google Bench:bench-gamma,bench-delta|Third Operator:bench-eta")

	// 9. An empty root map leaves the temporal result untouched.
	t.Equal("compatible-empty-root-map",
		render(ll.Compatible(leafValid, benchRoot, loglist3.LogRoots{})),
		render(ll.TemporallyCompatible(leafValid)))

	// 10. Lookups.
	t.Equal("find-by-name-is-substring-and-case-insensitive",
		names(ll.FindLogByName("BENCH-GA")), "bench-gamma")
	t.Equal("find-by-name-matches-many", names(ll.FindLogByName("bench-")),
		"bench-alpha,bench-beta,bench-gamma,bench-delta,bench-epsilon,bench-zeta,bench-eta")

	alpha := ll.FindLogByURL("https://alpha.bench.example")
	if alpha == nil {
		t.Fatalf("bench-alpha not found by URL")
	}
	t.Check("find-by-url-ignores-trailing-slash",
		alpha.Description == "bench-alpha" &&
			ll.FindLogByURL("https://alpha.bench.example/") == alpha,
		"got %v", alpha.Description)
	t.Check("find-by-url-unknown-is-nil", ll.FindLogByURL("https://absent.bench.example") == nil,
		"got non-nil")

	var hash [sha256.Size]byte
	copy(hash[:], alpha.LogID)
	t.Check("find-by-key-hash", ll.FindLogByKeyHash(hash) == alpha, "got %v", ll.FindLogByKeyHash(hash))
	hexID := hex.EncodeToString(alpha.LogID)
	t.Equal("find-by-key-hash-prefix", names(ll.FindLogByKeyHashPrefix(hexID[:7])), "bench-alpha")
	t.Check("find-by-key", ll.FindLogByKey(alpha.Key) == alpha, "got %v", ll.FindLogByKey(alpha.Key))

	// 11. FuzzyFindLog accepts a URL, a hash with internal whitespace, and an
	//     odd number of hex digits as a key-hash prefix.
	t.Equal("fuzzy-by-url", names(ll.FuzzyFindLog("https://alpha.bench.example/")), "bench-alpha")
	t.Equal("fuzzy-by-spaced-hex-hash",
		names(ll.FuzzyFindLog(hexID[:16]+" "+hexID[16:32]+" "+hexID[32:48]+" "+hexID[48:])),
		"bench-alpha")
	t.Equal("fuzzy-by-odd-hex-prefix", names(ll.FuzzyFindLog(hexID[:7])), "bench-alpha")
	t.Equal("fuzzy-unknown-is-empty", names(ll.FuzzyFindLog("no-such-log")), "")

	t.Done()
}

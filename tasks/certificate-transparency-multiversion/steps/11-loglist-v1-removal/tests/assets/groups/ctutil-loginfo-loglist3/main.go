// Group ctutil-loginfo-loglist3.
//
// At v1.1.4 ctutil's log-list entry points stop speaking the v1 log-list
// schema and speak v3 instead:
//
//	func NewLogInfo(log *loglist3.Log, hc *http.Client) (*LogInfo, error)
//	func LogInfoByKeyHash(ll *loglist3.LogList, hc *http.Client) (LogInfoByHash, error)
//
// and the merge delay is read from the v3 field (`mmd`, seconds) rather than
// the v1 one. The previous release's ctutil takes *loglist.LogList, so this
// driver -- which passes a *loglist3.LogList -- cannot even be compiled
// against it.
//
// Nothing here reaches the network: every LogInfo is built from a pinned list
// and the http.Client is never used, because ctutil constructs its client
// lazily and no method that would dial is called.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"net/http"
	"sort"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/loglist3"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	ll, err := loglist3.NewFromJSON([]byte(benchLogList3JSON))
	if err != nil {
		t.Fatalf("parse pinned v3 log list: %v", err)
	}

	// The pinned list, restated here so the driver asserts against its own
	// expectation rather than against whatever it just parsed.
	type want struct {
		desc string
		mmd  time.Duration
	}
	wants := map[string]want{
		"https://google.bench.invalid/usable/":    {"Google Bench Usable Log", 86400 * time.Second},
		"https://google.bench.invalid/readonly/":  {"Google Bench Readonly Log", 3600 * time.Second},
		"https://google.bench.invalid/qualified/": {"Google Bench Qualified Log", 60 * time.Second},
		"https://shop.bench.invalid/usable/":      {"Shop Usable Log", 7200 * time.Second},
		"https://shop.bench.invalid/retired/":     {"Shop Retired Log", 1800 * time.Second},
		"https://shop.bench.invalid/pending/":     {"Shop Pending Log", 900 * time.Second},
	}

	// --- LogInfoByKeyHash over the whole v3 list --------------------------
	byHash, err := ctutil.LogInfoByKeyHash(ll, http.DefaultClient)
	if err != nil {
		t.Fatalf("LogInfoByKeyHash over the pinned v3 list: %v", err)
	}
	t.Equal("loginfo-by-keyhash-covers-every-operator", len(byHash), 6)

	// Every entry must be filed under SHA-256 of that log's own key, which is
	// the index RFC 6962 SCTs are looked up by.
	missing := []string{}
	mismatched := []string{}
	for _, op := range ll.Operators {
		for _, lg := range op.Logs {
			h := sha256.Sum256(lg.Key)
			li, ok := byHash[h]
			if !ok {
				missing = append(missing, hex.EncodeToString(h[:8]))
				continue
			}
			if li.Description != wants[lg.URL].desc {
				mismatched = append(mismatched, fmt.Sprintf("%s->%q", lg.URL, li.Description))
			}
		}
	}
	sort.Strings(missing)
	sort.Strings(mismatched)
	t.Check("loginfo-by-keyhash-indexed-by-sha256-of-key", len(missing) == 0 && len(mismatched) == 0,
		"missing=%v mismatched=%v", missing, mismatched)

	// --- one entry, in full ------------------------------------------------
	usable := ll.FindLogByURL("https://google.bench.invalid/usable/")
	if usable == nil {
		t.Fatalf("pinned list lost its usable log")
	}
	li, err := ctutil.NewLogInfo(usable, http.DefaultClient)
	if err != nil {
		t.Fatalf("NewLogInfo for the usable log: %v", err)
	}
	t.Equal("newloginfo-description", li.Description, "Google Bench Usable Log")

	// v1.1.4 reads loglist3.Log.MMD; the v1 schema called the same number
	// MaximumMergeDelay. Both are seconds, so the value is the assertion.
	t.Equal("newloginfo-mmd-from-v3-field", li.MMD, 86400*time.Second)
	t.EqualBytes("newloginfo-public-key-der", li.PublicKey, usable.Key)
	t.Check("newloginfo-builds-client-and-verifier", li.Client != nil && li.Verifier != nil,
		"client=%v verifier=%v", li.Client != nil, li.Verifier != nil)

	// --- MMD is carried through for every log, not just the first ----------
	bad := []string{}
	for url, w := range wants {
		lg := ll.FindLogByURL(url)
		if lg == nil {
			bad = append(bad, url+" absent")
			continue
		}
		info, err := ctutil.NewLogInfo(lg, http.DefaultClient)
		if err != nil {
			bad = append(bad, fmt.Sprintf("%s: %v", url, err))
			continue
		}
		if info.MMD != w.mmd {
			bad = append(bad, fmt.Sprintf("%s: mmd %v want %v", url, info.MMD, w.mmd))
		}
	}
	sort.Strings(bad)
	t.Check("newloginfo-mmd-for-every-log", len(bad) == 0, "%v", bad)

	// --- the STH cache the monitor tools rely on ---------------------------
	sth := &ct.SignedTreeHead{
		Version:   ct.V1,
		TreeSize:  pinnedGateTreeSize,
		Timestamp: pinnedGateTimestampMS,
	}
	before := li.LastSTH()
	li.SetSTH(sth)
	after := li.LastSTH()
	t.Check("loginfo-sth-cache-roundtrip",
		before == nil && after != nil && after.TreeSize == pinnedGateTreeSize && after.Timestamp == pinnedGateTimestampMS,
		"before=%v after=%v", before, after)

	t.Done()
}

const (
	pinnedGateTreeSize    = uint64(100000)
	pinnedGateTimestampMS = uint64(1500000000000)
)

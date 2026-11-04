// loglist3-tiled-log-finders
//
// v1.3.3 teaches loglist3 about a second kind of log. An Operator now carries
// a TiledLogs list beside its Logs list, a TiledLog is addressed by a
// SubmissionURL and a MonitoringURL instead of a single URL, and a full
// parallel family of finders searches only that half of the document:
// FindTiledLogByName, ...ByURL, ...ByKeyHash, ...ByKeyHashPrefix, ...ByKey and
// FuzzyFindTiledLog. The release also adds the AllLogListSignatureURL
// constant.
//
// The input is a log list this group builds itself, marshalled from a
// benchmark-owned struct with encoding/json so that the bytes handed to the
// product are not produced by the product. Every key, key hash, URL and name
// below is a compile-time constant; no clock, no socket, no network.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"time"

	"github.com/google/certificate-transparency-go/loglist3"

	"ctverify/vhelp"
)

// ---------------------------------------------------------------------------
// The pinned document. These types mirror the v3 log-list schema; they are
// this group's own, so the JSON is independent of the product's structs.
// ---------------------------------------------------------------------------

type jState struct {
	Usable *struct {
		Timestamp time.Time `json:"timestamp"`
	} `json:"usable,omitempty"`
}

type jLog struct {
	Description string  `json:"description"`
	LogID       []byte  `json:"log_id"`
	Key         []byte  `json:"key"`
	URL         string  `json:"url"`
	MMD         int32   `json:"mmd"`
	State       *jState `json:"state,omitempty"`
}

type jTiledLog struct {
	Description   string  `json:"description"`
	LogID         []byte  `json:"log_id"`
	Key           []byte  `json:"key"`
	SubmissionURL string  `json:"submission_url"`
	MonitoringURL string  `json:"monitoring_url"`
	MMD           int32   `json:"mmd"`
	State         *jState `json:"state,omitempty"`
}

type jOperator struct {
	Name      string       `json:"name"`
	Email     []string     `json:"email"`
	Logs      []*jLog      `json:"logs"`
	TiledLogs []*jTiledLog `json:"tiled_logs"`
}

type jLogList struct {
	Version   string       `json:"version"`
	Operators []*jOperator `json:"operators"`
}

var (
	rfcKey    = []byte("harbor-ct-bench-rfc6962-log-key")
	tiledKeyA = []byte("harbor-ct-bench-tiled-log-a-key")
	tiledKeyB = []byte("harbor-ct-bench-tiled-log-b-key")

	rfcHash    = sha256.Sum256(rfcKey)
	tiledHashA = sha256.Sum256(tiledKeyA)
	tiledHashB = sha256.Sum256(tiledKeyB)
)

const (
	rfcURL    = "https://rfc.bench.invalid/log/"
	subURLA   = "https://submit-a.bench.invalid/tile/"
	monURLA   = "https://monitor-a.bench.invalid/tile/"
	subURLB   = "https://submit-b.bench.invalid/tile/"
	monURLB   = "https://monitor-b.bench.invalid/tile/"
	descRFC   = "Harbor Bench RFC6962 Log"
	descTileA = "Harbor Bench Tiled Log Alpha"
	descTileB = "Harbor Bench Tiled Log Beta"
)

func usable() *jState {
	s := &jState{}
	s.Usable = &struct {
		Timestamp time.Time `json:"timestamp"`
	}{Timestamp: time.Unix(1500000000, 0).UTC()}
	return s
}

func document() []byte {
	doc := jLogList{
		Version: "harbor-13",
		Operators: []*jOperator{{
			Name:  "Harbor Bench Operator",
			Email: []string{"ops@bench.invalid"},
			Logs: []*jLog{{
				Description: descRFC, LogID: rfcHash[:], Key: rfcKey,
				URL: rfcURL, MMD: 86400, State: usable(),
			}},
			TiledLogs: []*jTiledLog{
				{
					Description: descTileA, LogID: tiledHashA[:], Key: tiledKeyA,
					SubmissionURL: subURLA, MonitoringURL: monURLA, MMD: 3600, State: usable(),
				},
				{
					Description: descTileB, LogID: tiledHashB[:], Key: tiledKeyB,
					SubmissionURL: subURLB, MonitoringURL: monURLB, MMD: 7200, State: usable(),
				},
			},
		}},
	}
	b, err := json.Marshal(doc)
	if err != nil {
		panic(err)
	}
	return b
}

func main() {
	t := vhelp.New()

	ll, err := loglist3.NewFromJSON(document())
	if err != nil || ll == nil {
		t.Fatalf("NewFromJSON over the pinned tiled document: %v", err)
	}
	if len(ll.Operators) != 1 {
		t.Fatalf("decoded %d operators, want 1", len(ll.Operators))
	}
	op := ll.Operators[0]

	// 1. The document decodes into both halves, with the tiled half intact.
	t.Check("operator-carries-both-kinds-of-log",
		len(op.Logs) == 1 && len(op.TiledLogs) == 2,
		"operator has %d Logs and %d TiledLogs, want 1 and 2", len(op.Logs), len(op.TiledLogs))

	// 2. Every TiledLog field round-trips exactly, including the two URLs.
	got := ""
	if len(op.TiledLogs) == 2 {
		a := op.TiledLogs[0]
		got = fmt.Sprintf("%s|%s|%s|%d|%x|%s", a.Description, a.SubmissionURL, a.MonitoringURL, a.MMD, a.LogID, string(a.Key))
	}
	t.Equal("tiled-log-fields-round-trip", got,
		fmt.Sprintf("%s|%s|%s|%d|%x|%s", descTileA, subURLA, monURLA, 3600, tiledHashA[:], string(tiledKeyA)))

	// 3. FindTiledLogByName searches only the tiled half.
	byName := ll.FindTiledLogByName("tiled log alpha")
	t.Check("find-tiled-log-by-name",
		len(byName) == 1 && byName[0].SubmissionURL == subURLA,
		"FindTiledLogByName(%q) returned %d logs", "tiled log alpha", len(byName))

	// 4. ... and never returns an RFC 6962 log, whatever its description.
	t.Check("find-tiled-log-by-name-ignores-rfc-logs",
		len(ll.FindTiledLogByName(descRFC)) == 0,
		"FindTiledLogByName(%q) returned %d logs, want 0", descRFC, len(ll.FindTiledLogByName(descRFC)))

	// 5. The URL finder matches the submission URL.
	bySub := ll.FindTiledLogByURL(subURLB)
	t.Check("find-tiled-log-by-submission-url",
		bySub != nil && bySub.Description == descTileB,
		"FindTiledLogByURL(%q) = %v", subURLB, bySub)

	// 6. ... and the monitoring URL of the very same log.
	byMon := ll.FindTiledLogByURL(monURLB)
	t.Check("find-tiled-log-by-monitoring-url",
		byMon != nil && byMon.Description == descTileB && byMon.SubmissionURL == subURLB,
		"FindTiledLogByURL(%q) = %v", monURLB, byMon)

	// 7. ... ignoring a trailing slash on either side of the comparison.
	noSlash := ll.FindTiledLogByURL("https://monitor-a.bench.invalid/tile")
	t.Check("find-tiled-log-by-url-ignores-trailing-slash",
		noSlash != nil && noSlash.Description == descTileA,
		"FindTiledLogByURL without the trailing slash = %v", noSlash)

	// 8. The key-hash finder, over the exact 32-byte hash.
	byHash := ll.FindTiledLogByKeyHash(tiledHashB)
	t.Check("find-tiled-log-by-key-hash",
		byHash != nil && byHash.Description == descTileB,
		"FindTiledLogByKeyHash = %v", byHash)

	// 9. The prefix form, over the first 8 hex digits of that hash.
	prefix := hex.EncodeToString(tiledHashA[:])[:8]
	byPrefix := ll.FindTiledLogByKeyHashPrefix(prefix)
	t.Check("find-tiled-log-by-key-hash-prefix",
		len(byPrefix) == 1 && byPrefix[0].Description == descTileA,
		"FindTiledLogByKeyHashPrefix(%q) returned %d logs", prefix, len(byPrefix))

	// 10. The DER-key finder.
	byKey := ll.FindTiledLogByKey(tiledKeyA)
	t.Check("find-tiled-log-by-key",
		byKey != nil && byKey.Description == descTileA,
		"FindTiledLogByKey = %v", byKey)

	// 11. FuzzyFindTiledLog resolves a base64 key hash to one tiled log.
	fuzzy := ll.FuzzyFindTiledLog(base64.StdEncoding.EncodeToString(tiledHashB[:]))
	t.Check("fuzzy-find-tiled-log-by-base64-key-hash",
		len(fuzzy) == 1 && fuzzy[0].Description == descTileB,
		"FuzzyFindTiledLog(base64 hash) returned %d logs", len(fuzzy))

	// 12. ... and a monitoring URL, which no other lookup would resolve.
	fuzzyURL := ll.FuzzyFindTiledLog(monURLA)
	t.Check("fuzzy-find-tiled-log-by-monitoring-url",
		len(fuzzyURL) == 1 && fuzzyURL[0].Description == descTileA,
		"FuzzyFindTiledLog(%q) returned %d logs", monURLA, len(fuzzyURL))

	// 13. The two halves are disjoint: the ordinary finders never see a tiled
	//     log, and the tiled finders never see an ordinary one.
	t.Check("the-two-finder-families-are-disjoint",
		ll.FindLogByURL(subURLA) == nil && ll.FindLogByURL(monURLA) == nil &&
			ll.FindTiledLogByURL(rfcURL) == nil &&
			ll.FindLogByKeyHash(tiledHashA) == nil && ll.FindTiledLogByKeyHash(rfcHash) == nil,
		"a finder crossed between the RFC 6962 half and the tiled half")

	// 14. ... while the ordinary finders still work on the ordinary half.
	rfc := ll.FindLogByURL(rfcURL)
	t.Check("ordinary-finders-still-find-ordinary-logs",
		rfc != nil && rfc.Description == descRFC && ll.FindLogByKeyHash(rfcHash) != nil,
		"FindLogByURL(%q) = %v", rfcURL, rfc)

	// 15. The signature URL for the all-logs list, new at this release.
	t.Equal("all-log-list-signature-url", loglist3.AllLogListSignatureURL,
		"https://www.gstatic.com/ct/log_list/v3/all_logs_list.sig")

	t.Done()
}

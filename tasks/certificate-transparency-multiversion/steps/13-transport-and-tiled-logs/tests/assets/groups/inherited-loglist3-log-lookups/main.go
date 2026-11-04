// INHERITED. The RFC 6962 half of loglist3 -- parsing a v3 log list, the six
// finders over Operator.Logs, the state machine behind LogStates.LogStatus
// and the SelectByStatus filter -- must survive this release adding a second,
// parallel population of tiled logs to the very same documents, structs and
// loops. Every case below is behaviour v1.2.0 already had, so this group is
// expected to pass against the previous tree as well.
//
// The input is a log list this group builds itself, marshalled with
// encoding/json from benchmark-owned structs, so the bytes handed to the
// product are never produced by the product. It contains no tiled logs at
// all: that is exactly the document whose handling must not change. Every
// key, hash, URL, name and timestamp below is a compile-time constant; no
// clock, no socket, no network.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"

	"github.com/google/certificate-transparency-go/loglist3"

	"ctverify/vhelp"
)

// ---------------------------------------------------------------------------
// The pinned document.
// ---------------------------------------------------------------------------

type jLogState struct {
	Timestamp string `json:"timestamp"`
}

type jTreeHead struct {
	SHA256RootHash []byte `json:"sha256_root_hash"`
	TreeSize       int64  `json:"tree_size"`
}

type jReadOnly struct {
	Timestamp     string    `json:"timestamp"`
	FinalTreeHead jTreeHead `json:"final_tree_head"`
}

type jStates struct {
	Usable   *jLogState `json:"usable,omitempty"`
	Retired  *jLogState `json:"retired,omitempty"`
	ReadOnly *jReadOnly `json:"readonly,omitempty"`
}

type jLog struct {
	Description string   `json:"description"`
	LogID       []byte   `json:"log_id"`
	Key         []byte   `json:"key"`
	URL         string   `json:"url"`
	MMD         int32    `json:"mmd"`
	State       *jStates `json:"state,omitempty"`
}

type jOperator struct {
	Name  string   `json:"name"`
	Email []string `json:"email"`
	Logs  []*jLog  `json:"logs"`
}

type jLogList struct {
	Operators []*jOperator `json:"operators"`
}

// Pinned "public keys". The finders never parse them, they compare bytes.
var (
	keyAlpha24 = bytes.Repeat([]byte{0x11}, 91)
	keyAlpha25 = bytes.Repeat([]byte{0x22}, 91)
	keyBeta    = bytes.Repeat([]byte{0x33}, 91)
)

const (
	tsUsable   = "2023-01-02T03:04:05Z"
	tsRetired  = "2022-06-07T08:09:10Z"
	tsReadOnly = "2021-11-12T13:14:15Z"

	urlAlpha24 = "https://alpha.example.com/logs/2024/"
	urlAlpha25 = "https://alpha.example.com/logs/2025/"
	urlBeta    = "https://beta.example.com/ct/"

	descAlpha24 = "Alpha Log 2024"
	descAlpha25 = "Alpha Log 2025"
	descBeta    = "Beta Archive Log"
)

var (
	idAlpha24 = sha256.Sum256(keyAlpha24)
	idAlpha25 = sha256.Sum256(keyAlpha25)
	idBeta    = sha256.Sum256(keyBeta)

	finalRootHash = bytes.Repeat([]byte{0x44}, 32)
)

func document() []byte {
	doc := &jLogList{Operators: []*jOperator{
		{
			Name:  "Alpha Operator",
			Email: []string{"ct@alpha.example.com"},
			Logs: []*jLog{
				{
					Description: descAlpha24, LogID: idAlpha24[:], Key: keyAlpha24,
					URL: urlAlpha24, MMD: 86400,
					State: &jStates{Usable: &jLogState{Timestamp: tsUsable}},
				},
				{
					Description: descAlpha25, LogID: idAlpha25[:], Key: keyAlpha25,
					URL: urlAlpha25, MMD: 86400,
					State: &jStates{Retired: &jLogState{Timestamp: tsRetired}},
				},
			},
		},
		{
			Name:  "Beta Operator",
			Email: []string{"google-ct-logs@googlegroups.com"},
			Logs: []*jLog{
				{
					Description: descBeta, LogID: idBeta[:], Key: keyBeta,
					URL: urlBeta, MMD: 604800,
					State: &jStates{ReadOnly: &jReadOnly{
						Timestamp:     tsReadOnly,
						FinalTreeHead: jTreeHead{SHA256RootHash: finalRootHash, TreeSize: 1234567},
					}},
				},
			},
		},
	}}
	b, err := json.Marshal(doc)
	if err != nil {
		panic(err)
	}
	return b
}

func names(logs []*loglist3.Log) string {
	out := make([]string, 0, len(logs))
	for _, l := range logs {
		out = append(out, l.Description)
	}
	return fmt.Sprint(out)
}

func main() {
	t := vhelp.New()
	raw := document()

	ll, err := loglist3.NewFromJSON(raw)
	if err != nil || ll == nil {
		t.Fatalf("NewFromJSON on the pinned document failed: %v", err)
	}

	// --- parsing ---------------------------------------------------------
	ok := len(ll.Operators) == 2 &&
		ll.Operators[0].Name == "Alpha Operator" && len(ll.Operators[0].Logs) == 2 &&
		ll.Operators[1].Name == "Beta Operator" && len(ll.Operators[1].Logs) == 1 &&
		ll.Operators[0].Logs[0].Description == descAlpha24 &&
		ll.Operators[0].Logs[0].URL == urlAlpha24 &&
		ll.Operators[0].Logs[0].MMD == 86400 &&
		bytes.Equal(ll.Operators[0].Logs[0].Key, keyAlpha24) &&
		bytes.Equal(ll.Operators[0].Logs[0].LogID, idAlpha24[:])
	t.Check("new-from-json-parses-operators-and-their-logs", ok,
		"parsed document does not match the pinned one: %d operators", len(ll.Operators))

	bad, err := loglist3.NewFromJSON([]byte(`{"operators": [ this is not json ]}`))
	t.Check("new-from-json-rejects-malformed-json", bad == nil && err != nil,
		"NewFromJSON on malformed input = (%v, %v), want (<nil>, <error>)", bad, err)

	// --- the six finders -------------------------------------------------
	got := ll.FindLogByName("alpha log 2024")
	t.Check("find-log-by-name-is-a-case-insensitive-match",
		len(got) == 1 && got[0].Description == descAlpha24,
		"FindLogByName(\"alpha log 2024\") = %s, want [%s]", names(got), descAlpha24)

	got = ll.FindLogByName("Alpha Log")
	t.Check("find-log-by-name-is-a-substring-match-over-every-operator",
		len(got) == 2 && got[0].Description == descAlpha24 && got[1].Description == descAlpha25,
		"FindLogByName(\"Alpha Log\") = %s, want [%s %s]", names(got), descAlpha24, descAlpha25)

	t.Check("find-log-by-name-misses-an-unknown-name", len(ll.FindLogByName("gamma")) == 0,
		"FindLogByName(\"gamma\") = %s, want no logs", names(ll.FindLogByName("gamma")))

	l := ll.FindLogByURL("https://beta.example.com/ct")
	t.Check("find-log-by-url-ignores-a-trailing-slash",
		l != nil && l.Description == descBeta,
		"FindLogByURL without the trailing slash = %v, want %s", l, descBeta)

	l = ll.FindLogByURL("https://delta.example.com/")
	t.Check("find-log-by-url-misses-an-unknown-url", l == nil, "FindLogByURL on an unknown URL = %v, want <nil>", l)

	l = ll.FindLogByKeyHash(idAlpha25)
	t.Check("find-log-by-key-hash-matches-the-log-id",
		l != nil && l.Description == descAlpha25,
		"FindLogByKeyHash = %v, want %s", l, descAlpha25)

	var absent [sha256.Size]byte
	l = ll.FindLogByKeyHash(absent)
	t.Check("find-log-by-key-hash-misses-an-unknown-hash", l == nil, "FindLogByKeyHash(zero) = %v, want <nil>", l)

	prefix := hex.EncodeToString(idBeta[:4])
	got = ll.FindLogByKeyHashPrefix(prefix)
	t.Check("find-log-by-key-hash-prefix-matches-a-hex-prefix",
		len(got) == 1 && got[0].Description == descBeta,
		"FindLogByKeyHashPrefix(%q) = %s, want [%s]", prefix, names(got), descBeta)

	l = ll.FindLogByKey(keyAlpha24)
	t.Check("find-log-by-key-matches-the-der-key",
		l != nil && l.Description == descAlpha24,
		"FindLogByKey = %v, want %s", l, descAlpha24)

	// --- fuzzy find ------------------------------------------------------
	got = ll.FuzzyFindLog(base64.StdEncoding.EncodeToString(idBeta[:]))
	t.Check("fuzzy-find-accepts-a-base64-key-hash",
		len(got) == 1 && got[0].Description == descBeta,
		"FuzzyFindLog(base64 log id) = %s, want [%s]", names(got), descBeta)

	spaced := hex.EncodeToString(idAlpha24[:16]) + " " + hex.EncodeToString(idAlpha24[16:])
	got = ll.FuzzyFindLog("  " + spaced + "\t")
	t.Check("fuzzy-find-strips-space-from-a-hex-key-hash",
		len(got) == 1 && got[0].Description == descAlpha24,
		"FuzzyFindLog(spaced hex log id) = %s, want [%s]", names(got), descAlpha24)

	got = ll.FuzzyFindLog("no such log anywhere")
	t.Check("fuzzy-find-returns-nothing-for-an-unrelated-string", len(got) == 0,
		"FuzzyFindLog(unrelated) = %s, want no logs", names(got))

	// --- states ----------------------------------------------------------
	statuses := fmt.Sprintf("%v/%v/%v",
		ll.Operators[0].Logs[0].State.LogStatus(),
		ll.Operators[0].Logs[1].State.LogStatus(),
		ll.Operators[1].Logs[0].State.LogStatus())
	t.Equal("log-status-is-read-from-the-state-object", statuses,
		"UsableLogStatus/RetiredLogStatus/ReadOnlyLogStatus")

	var unset *loglist3.LogStates
	t.Equal("an-absent-state-is-the-undefined-status", unset.LogStatus(), loglist3.UndefinedLogStatus)

	_, ro := ll.Operators[1].Logs[0].State.Active()
	t.Check("the-readonly-state-carries-the-final-tree-head",
		ro != nil && ro.FinalTreeHead.TreeSize == 1234567 && bytes.Equal(ro.FinalTreeHead.SHA256RootHash, finalRootHash),
		"Active() read-only state = %v, want tree size 1234567 and the pinned root hash", ro)

	t.Check("google-operated-is-decided-by-the-operator-email",
		!ll.Operators[0].GoogleOperated() && ll.Operators[1].GoogleOperated(),
		"GoogleOperated() = %v/%v, want false/true",
		ll.Operators[0].GoogleOperated(), ll.Operators[1].GoogleOperated())

	// --- the status filter ------------------------------------------------
	sel := ll.SelectByStatus([]loglist3.LogStatus{loglist3.UsableLogStatus})
	t.Check("select-by-status-keeps-only-the-named-status",
		len(sel.Operators) == 1 && sel.Operators[0].Name == "Alpha Operator" &&
			len(sel.Operators[0].Logs) == 1 && sel.Operators[0].Logs[0].Description == descAlpha24,
		"SelectByStatus(usable) kept %d operators", len(sel.Operators))

	sel = ll.SelectByStatus([]loglist3.LogStatus{loglist3.RetiredLogStatus, loglist3.ReadOnlyLogStatus})
	keptBoth := len(sel.Operators) == 2 &&
		len(sel.Operators[0].Logs) == 1 && sel.Operators[0].Logs[0].Description == descAlpha25 &&
		len(sel.Operators[1].Logs) == 1 && sel.Operators[1].Logs[0].Description == descBeta
	t.Check("select-by-status-spans-operators", keptBoth,
		"SelectByStatus(retired, readonly) kept %d operators", len(sel.Operators))

	sel = ll.SelectByStatus([]loglist3.LogStatus{loglist3.PendingLogStatus})
	t.Check("select-by-status-drops-operators-with-no-surviving-log", len(sel.Operators) == 0,
		"SelectByStatus(pending) kept %d operators, want 0", len(sel.Operators))

	// --- the published v3 endpoints ---------------------------------------
	t.Equal("the-v3-log-list-urls-are-unchanged",
		fmt.Sprintf("%s %s %s", loglist3.LogListURL, loglist3.LogListSignatureURL, loglist3.AllLogListURL),
		"https://www.gstatic.com/ct/log_list/v3/log_list.json "+
			"https://www.gstatic.com/ct/log_list/v3/log_list.sig "+
			"https://www.gstatic.com/ct/log_list/v3/all_logs_list.json")

	t.Done()
}

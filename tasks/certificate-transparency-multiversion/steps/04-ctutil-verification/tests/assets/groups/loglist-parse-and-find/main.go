// Group loglist-parse-and-find.
//
// v1.0.13 adds the loglist package: the v1 Chrome log-list schema
// (logs[] with description/key/maximum_merge_delay/operated_by/url plus the
// optional final_sth, disqualified_at and dns_api_endpoint, and an
// operators[] table) and the five lookups over it -- FindLogByName,
// FindLogByURL, FindLogByKeyHash, FindLogByKey and FuzzyFindLog.
//
// Boundary: loglist.NewFromJSON and the exported LogList methods, over a
// pinned log list shipped beside this group. One of its four logs is the
// RFC 6962 test log, so its key hash is exactly the LogID inside the pinned
// SCTs.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"os"

	"github.com/google/certificate-transparency-go/loglist"

	"ctverify/vhelp"
)

const (
	testLogKeyHashHex = "df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d764"
	rootHashHex       = "18041bd4665083001fba8c5411d2d748e8abbfdcdfd9218cb02b68a78e7d4c23"
	logKeyB64         = "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEmXg8sUUzwBYaWrRb+V0IopzQ6o3UyEJ04r5ZrRXGdpYM8K+hB0pXrGRLI0eeWz+3skXrS0IO83AhA3GpRL6s6w=="
	secondaryKeyB64   = "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAES0AfBkjr7b8b19p5Gk8plSAN16wWXZyhYsH6FMCEUK60t7pem/ckoPX8hupuaiJzJS0ZQ0SEoJGlFxkUFwft5g=="
)

func main() {
	t := vhelp.New()

	data, err := os.ReadFile(vhelp.GroupFile("loglist.json"))
	if err != nil {
		t.Fatalf("reading the pinned log list: %v", err)
	}
	ll, err := loglist.NewFromJSON(data)
	if err != nil {
		t.Fatalf("NewFromJSON on the pinned log list: %v", err)
	}

	wantKey, _ := base64.StdEncoding.DecodeString(logKeyB64)

	// --- schema --------------------------------------------------------------
	ok := len(ll.Logs) == 4
	if ok {
		l := ll.Logs[0]
		ok = l.Description == "Erw Wen RFC6962 Test Log" &&
			l.URL == "ct.example.com/testlog/" &&
			l.MaximumMergeDelay == 86400 &&
			len(l.OperatedBy) == 1 && l.OperatedBy[0] == 0 &&
			l.DNSAPIEndpoint == "testlog.ct.example.com" &&
			bytes.Equal(l.Key, wantKey)
	}
	t.Check("parses-every-field-of-a-v1-log-entry", ok,
		"got %d logs; first = %+v", len(ll.Logs), ll.Logs[0])

	t.Check("parses-the-operator-table",
		len(ll.Operators) == 2 &&
			ll.Operators[0].ID == 0 && ll.Operators[0].Name == "Erw Wen CT" &&
			ll.Operators[1].ID == 1 && ll.Operators[1].Name == "Wales Logs Ltd",
		"got operators %+v", ll.Operators)

	frozen := ll.Logs[2]
	wantRoot, _ := hex.DecodeString(rootHashHex)
	t.Check("parses-a-disqualified-log-with-its-final-sth",
		frozen.DisqualifiedAt == 1451606400 && frozen.FinalSTH != nil &&
			frozen.FinalSTH.TreeSize == 42 && frozen.FinalSTH.Timestamp == 1400000000000 &&
			bytes.Equal(frozen.FinalSTH.SHA256RootHash, wantRoot) &&
			len(frozen.FinalSTH.TreeHeadSignature) == 76,
		"disqualified_at=%d final_sth=%+v", frozen.DisqualifiedAt, frozen.FinalSTH)

	live := ll.Logs[1]
	t.Check("optional-fields-absent-from-the-json-stay-zero",
		live.FinalSTH == nil && live.DisqualifiedAt == 0 && live.DNSAPIEndpoint == "" &&
			live.MaximumMergeDelay == 172800,
		"log %q came back as %+v", live.Description, live)

	_, err = loglist.NewFromJSON([]byte("this is not a log list"))
	t.Check("input-that-is-not-a-log-list-is-an-error", err != nil,
		"accepted input that is not JSON")

	// --- FindLogByName -------------------------------------------------------
	byName := ll.FindLogByName("erw wen")
	all := ll.FindLogByName("Log")
	t.Check("find-log-by-name-is-a-case-insensitive-substring-match",
		len(byName) == 1 && byName[0].Description == "Erw Wen RFC6962 Test Log" && len(all) == 4,
		"'erw wen' matched %d, 'Log' matched %d", len(byName), len(all))

	t.Check("find-log-by-name-with-no-match-returns-nothing",
		len(ll.FindLogByName("no such log anywhere")) == 0,
		"an unknown name matched %d logs", len(ll.FindLogByName("no such log anywhere")))

	// --- FindLogByURL --------------------------------------------------------
	noSlash := ll.FindLogByURL("ct.example.com/testlog")
	withSlash := ll.FindLogByURL("ct.example.org/aber/")
	t.Check("find-log-by-url-ignores-trailing-slashes",
		noSlash != nil && noSlash.Description == "Erw Wen RFC6962 Test Log" &&
			withSlash != nil && withSlash.Description == "Aberystwyth Log" &&
			ll.FindLogByURL("ct.example.com/other") == nil,
		"without slash=%v with slash=%v", describe(noSlash), describe(withSlash))

	// --- FindLogByKeyHash ----------------------------------------------------
	hashBytes, _ := hex.DecodeString(testLogKeyHashHex)
	var keyHash [sha256.Size]byte
	copy(keyHash[:], hashBytes)
	found := ll.FindLogByKeyHash(keyHash)
	var absent [sha256.Size]byte
	t.Check("find-log-by-key-hash-finds-the-log-that-issued-the-pinned-sct",
		found != nil && found.Description == "Erw Wen RFC6962 Test Log" &&
			ll.FindLogByKeyHash(absent) == nil,
		"key hash %s matched %v", testLogKeyHashHex, describe(found))

	// --- FindLogByKey --------------------------------------------------------
	secKey, _ := base64.StdEncoding.DecodeString(secondaryKeyB64)
	byKey := ll.FindLogByKey(secKey)
	t.Check("find-log-by-key-matches-the-der-public-key",
		byKey != nil && byKey.Description == "Wales Secondary Log" &&
			ll.FindLogByKey([]byte("not a key")) == nil,
		"matched %v", describe(byKey))

	// --- FuzzyFindLog --------------------------------------------------------
	byDesc := ll.FuzzyFindLog(" Wales Secondary ")
	byURL := ll.FuzzyFindLog("ct.example.net/frozen")
	byB64 := ll.FuzzyFindLog(base64.StdEncoding.EncodeToString(hashBytes))
	spacedHex := testLogKeyHashHex[:32] + " " + testLogKeyHashHex[32:]
	byHex := ll.FuzzyFindLog(spacedHex)
	t.Check("fuzzy-find-accepts-a-name-a-url-a-base64-hash-and-a-spaced-hex-hash",
		len(byDesc) == 1 && byDesc[0].Description == "Wales Secondary Log" &&
			len(byURL) == 1 && byURL[0].Description == "Frozen Retired Log" &&
			len(byB64) == 1 && byB64[0].Description == "Erw Wen RFC6962 Test Log" &&
			len(byHex) == 1 && byHex[0].Description == "Erw Wen RFC6962 Test Log",
		"name=%d url=%d base64=%d hex=%d", len(byDesc), len(byURL), len(byB64), len(byHex))

	t.Check("fuzzy-find-returns-nothing-for-an-input-that-matches-no-log",
		len(ll.FuzzyFindLog("zzzz-not-a-log-zzzz")) == 0,
		"an unknown input matched %d logs", len(ll.FuzzyFindLog("zzzz-not-a-log-zzzz")))

	t.Done()
}

func describe(l *loglist.Log) string {
	if l == nil {
		return "<nil>"
	}
	return l.Description
}

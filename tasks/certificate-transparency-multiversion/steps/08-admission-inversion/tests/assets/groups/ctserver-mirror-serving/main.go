// Group ctserver-mirror-serving. KIND=scored.
//
// v1.1.0 is the first release whose `ct_server` will serve a mirror at all.
// The `is_mirror` config field and the library support behind it arrived at
// v1.0.21, but that release's `setupAndRegister` opens with
//
//	if cfg.IsMirror { return errors.New("mirrors are not supported") }
//
// so the process exits before it listens and there is no mirror to talk to.
// At v1.1.0 the personality registers the log, answers every read path, and
// makes both write paths 404 (HTTP-API.md §6.2).
//
// These cases were previously carried in the gate as though the whole of
// `is_mirror` were inherited. They are not, and the reason is the binary, not
// the library: the library support has been there since v1.0.21, but that
// release's `ct_server` refuses the config outright, so a v1.0.21 tree cannot
// reach even the first case below. Every case here needs a mirror that is
// actually listening, which is a v1.1.0-only object (CLIENT-API.md §8.1).
//
// SCOPE-BACKEND / SCOPE-NET / SCOPE-TIME: as documented in serverkit.go. Two
// personalities are started, one ordinary and one mirror, both against pinned
// in-process backends on kernel-chosen loopback ports.
package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"net/http"
	"strings"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

func nextPEM(b []byte) ([]byte, []byte) {
	const begin = "-----BEGIN CERTIFICATE-----"
	const end = "-----END CERTIFICATE-----"
	s := string(b)
	i := strings.Index(s, begin)
	if i < 0 {
		return nil, nil
	}
	j := strings.Index(s[i:], end)
	if j < 0 {
		return nil, nil
	}
	der, err := base64.StdEncoding.DecodeString(strings.Join(strings.Fields(s[i+len(begin):i+j]), ""))
	if err != nil {
		return nil, nil
	}
	return der, []byte(s[i+j+len(end):])
}

func chainJSON(t *vhelp.T, name string) string {
	raw := vhelp.ReadFixture("minted", name)
	var ders []string
	rest := raw
	for {
		der, r := nextPEM(rest)
		if der == nil {
			break
		}
		rest = r
		ders = append(ders, base64.StdEncoding.EncodeToString(der))
	}
	if len(ders) == 0 {
		t.Fatalf("fixture %s held no certificate", name)
	}
	b, err := json.Marshal(map[string][]string{"chain": ders})
	if err != nil {
		t.Fatalf("marshal chain: %v", err)
	}
	return string(b)
}

func main() {
	t := vhelp.New()

	// The ordinary log is the control that makes the mirror's two 404s mean
	// something: the same paths, the same build, the same fixtures, and one
	// switch flipped in the configuration.
	ord := startPersonality(t, "ord", nil, "")
	defer ord.stop()
	ac := ord.postJSON(t, ord.url(ct.AddChainPath), chainJSON(t, "chain-valid.pem"))

	m := startPersonalityAs(t, "mirror", nil, "", true)
	defer m.stop()

	ma := m.postJSON(t, m.url(ct.AddChainPath), chainJSON(t, "chain-valid.pem"))
	t.Check("mirror-add-chain-is-404", ma.status == http.StatusNotFound,
		"status %d body %q", ma.status, string(ma.body))

	mp := m.postJSON(t, m.url(ct.AddPreChainPath), chainJSON(t, "chain-precert.pem"))
	t.Check("mirror-add-pre-chain-is-404", mp.status == http.StatusNotFound,
		"status %d body %q", mp.status, string(mp.body))

	mr := m.get(t, m.url(ct.GetRootsPath))
	t.Check("mirror-still-serves-reads",
		ac.status == http.StatusOK && mr.status == http.StatusOK,
		"non-mirror add-chain %d, mirror get-roots %d", ac.status, mr.status)

	// The write half of the handler set is missing; the read half is not merely
	// present but identical, because the only difference between the two
	// configurations is the one boolean. A mirror that answered reads with
	// something of its own -- an empty root set, a stub proof -- would be a
	// different object from the one HTTP-API.md §6.2 describes, which withdraws
	// handlers and changes nothing else.
	or := ord.get(t, ord.url(ct.GetRootsPath))
	t.Check("mirror-serves-the-same-roots-as-the-log",
		mr.status == http.StatusOK && or.status == http.StatusOK &&
			len(mr.body) > 0 && bytes.Equal(mr.body, or.body),
		"mirror get-roots %d %q; log get-roots %d %q",
		mr.status, string(mr.body), or.status, string(or.body))

	// A second read path, so that "serves reads" is not one endpoint: the
	// consistency proof the mirror returns is the backend's, byte for byte the
	// one the ordinary log returns from the same backend shape.
	mc := m.get(t, m.url(ct.GetSTHConsistencyPath)+"?first=1&second=2")
	oc := ord.get(t, ord.url(ct.GetSTHConsistencyPath)+"?first=1&second=2")
	t.Check("mirror-serves-the-same-consistency-proof-as-the-log",
		mc.status == http.StatusOK && oc.status == http.StatusOK &&
			len(mc.body) > 0 && bytes.Equal(mc.body, oc.body),
		"mirror get-sth-consistency %d %q; log get-sth-consistency %d %q",
		mc.status, string(mc.body), oc.status, string(oc.body))

	t.Done()
}

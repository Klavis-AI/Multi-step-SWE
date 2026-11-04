// Group ctpolicy-apple-loglist3.
//
// The second half of the ctpolicy migration. Apple's rule keeps only the
// all-logs base group, and it is on that group that the exported weighting
// surface -- SetLogWeights, SetLogWeight, GetSubmissionSession -- is defined.
// All of it is reached here through
//
//	AppleCTPolicy.LogsByGroup(cert *x509.Certificate, approved *loglist3.LogList)
//
// which the previous release declares over *loglist2.LogList, so none of this
// file compiles against it.
//
// Every certificate is synthesised from pinned NotBefore/NotAfter values and
// nothing reaches the network (SCOPE-TIME, SCOPE-NET).
package main

import (
	"fmt"
	"sort"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/ctpolicy"
	"github.com/google/certificate-transparency-go/loglist3"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

var _ ctpolicy.CTPolicy = ctpolicy.AppleCTPolicy{}

const (
	usableLog   = "https://google.bench.invalid/usable/"
	readonlyLog = "https://google.bench.invalid/readonly/"
	shopLog     = "https://shop.bench.invalid/usable/"
	allLogs     = "https://google.bench.invalid/qualified/," +
		"https://google.bench.invalid/readonly/," +
		"https://google.bench.invalid/usable/," +
		"https://shop.bench.invalid/pending/," +
		"https://shop.bench.invalid/retired/," +
		"https://shop.bench.invalid/usable/"
)

func certOfLifetime(notBefore, notAfter string) *x509.Certificate {
	nb, err := time.Parse(time.RFC3339, notBefore)
	if err != nil {
		panic(err)
	}
	na, err := time.Parse(time.RFC3339, notAfter)
	if err != nil {
		panic(err)
	}
	return &x509.Certificate{NotBefore: nb, NotAfter: na}
}

func sortedJoin(m map[string]bool) string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	sort.Strings(out)
	return strings.Join(out, ",")
}

func main() {
	t := vhelp.New()

	ll, err := loglist3.NewFromJSON([]byte(benchLogList3JSON))
	if err != nil {
		t.Fatalf("parse pinned v3 log list: %v", err)
	}
	var p ctpolicy.AppleCTPolicy

	t.Equal("apple-policy-name", p.Name(), "Apple")

	data, err := p.LogsByGroup(certOfLifetime("2020-01-01T00:00:00Z", "2021-01-01T00:00:00Z"), ll)
	if err != nil {
		t.Fatalf("LogsByGroup over the pinned v3 list: %v", err)
	}
	names := make([]string, 0, len(data))
	for n := range data {
		names = append(names, n)
	}
	sort.Strings(names)
	t.Equal("apple-returns-only-the-base-group", strings.Join(names, ","), ctpolicy.BaseName)

	base := data[ctpolicy.BaseName]
	if base == nil {
		t.Fatalf("apple policy returned no base group")
	}
	t.Check("apple-base-group-holds-every-log",
		sortedJoin(base.LogURLs) == allLogs && base.IsBase && base.MinInclusions == 2 && data.TotalLogs() == 6,
		"urls=%q base=%v min=%d total=%d", sortedJoin(base.LogURLs), base.IsBase, base.MinInclusions, data.TotalLogs())

	steps := []struct {
		nb, na string
		want   int
	}{
		{"2020-01-01T00:00:00Z", "2021-01-01T00:00:00Z", 2}, // 12 months
		{"2020-01-01T00:00:00Z", "2022-04-01T00:00:00Z", 3}, // 27 months
		{"2020-01-01T00:00:00Z", "2022-05-01T00:00:00Z", 4}, // 28 months
		{"2020-01-01T00:00:00Z", "2023-05-01T00:00:00Z", 5}, // 40 months
	}
	bad := []string{}
	for _, s := range steps {
		d, err := p.LogsByGroup(certOfLifetime(s.nb, s.na), ll)
		if err != nil {
			bad = append(bad, fmt.Sprintf("%s..%s: %v", s.nb, s.na, err))
			continue
		}
		if got := d[ctpolicy.BaseName].MinInclusions; got != s.want {
			bad = append(bad, fmt.Sprintf("%s..%s: min=%d want %d", s.nb, s.na, got, s.want))
		}
	}
	t.Check("apple-inclusion-count-by-lifetime", len(bad) == 0, "%v", bad)

	// SetLogWeights resets every member to zero and then applies exactly the
	// weights it was given, provided enough of them stay positive to reach the
	// minimum inclusion count.
	if err := base.SetLogWeights(map[string]float32{usableLog: 2.0, shopLog: 3.0}); err != nil {
		t.Check("apple-set-log-weights-resets-the-rest", false, "SetLogWeights: %v", err)
	} else {
		want := map[string]float32{usableLog: 2.0, shopLog: 3.0}
		diff := []string{}
		for u, w := range base.LogWeights {
			if w != want[u] {
				diff = append(diff, fmt.Sprintf("%s=%v want %v", u, w, want[u]))
			}
		}
		sort.Strings(diff)
		t.Check("apple-set-log-weights-resets-the-rest", len(diff) == 0 && len(base.LogWeights) == 6, "%v (n=%d)", diff, len(base.LogWeights))
	}

	// Zeroing one of the only two positive weights would make the minimum of
	// two inclusions unreachable, so it is refused and nothing changes.
	err = base.SetLogWeight(usableLog, 0.0)
	t.Check("apple-set-log-weight-refuses-unreachable-minimum",
		err != nil && base.LogWeights[usableLog] == 2.0, "err=%v weight=%v", err, base.LogWeights[usableLog])

	// A log outside the group cannot be weighted at all.
	err = base.SetLogWeight("https://not.in.the.list.invalid/", 1.0)
	t.Check("apple-set-log-weight-refuses-non-member", err != nil, "err=%v", err)

	// A submission session offers exactly the logs with a positive weight.
	session := base.GetSubmissionSession()
	sort.Strings(session)
	t.Equal("apple-submission-session-is-the-positive-weight-logs",
		strings.Join(session, ","), usableLog+","+shopLog)

	// Raising a third log back above zero widens the session to three.
	if err := base.SetLogWeight(readonlyLog, 5.0); err != nil {
		t.Check("apple-raising-a-weight-widens-the-session", false, "SetLogWeight: %v", err)
	} else {
		s2 := base.GetSubmissionSession()
		sort.Strings(s2)
		t.Equal("apple-raising-a-weight-widens-the-session",
			strings.Join(s2, ","), readonlyLog+","+usableLog+","+shopLog)
	}

	// A list too small to satisfy the base group is an error.
	small := &loglist3.LogList{Operators: []*loglist3.Operator{{
		Name:  ll.Operators[0].Name,
		Email: ll.Operators[0].Email,
		Logs:  ll.Operators[0].Logs[:1],
	}}}
	d, err := p.LogsByGroup(certOfLifetime("2020-01-01T00:00:00Z", "2023-05-01T00:00:00Z"), small)
	t.Check("apple-unsatisfiable-policy-errors", err != nil && d == nil, "err=%v data=%v", err, d)

	t.Done()
}

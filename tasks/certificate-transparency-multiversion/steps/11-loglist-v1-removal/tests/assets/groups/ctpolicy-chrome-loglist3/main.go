// Group ctpolicy-chrome-loglist3.
//
// v1.1.4 re-types the whole ctpolicy surface onto the v3 log-list schema:
//
//	type CTPolicy interface {
//	        LogsByGroup(cert *x509.Certificate, approved *loglist3.LogList) (LogPolicyData, error)
//	        Name() string
//	}
//
// v1.1.4 is the release in which every log-list consumer in the product
// converges on the v3 schema: ctutil and the command-line tools jump straight
// from v1, ctpolicy and submission from v2. The previous release declares this
// same interface over *loglist2.LogList, so the compile-time assertion below
// -- ChromeCTPolicy must satisfy CTPolicy as this driver understands it --
// and every call in this file are impossible against it.
//
// Chrome's rule is partitioned into three groups: one per operator class plus
// the all-logs base group, whose minimum inclusion count is a step function of
// the certificate's lifetime. Every certificate here is synthesised with
// pinned NotBefore/NotAfter values, so no clock is consulted (SCOPE-TIME).
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

// The milestone's interface, asserted at compile time over the v3 schema.
var _ ctpolicy.CTPolicy = ctpolicy.ChromeCTPolicy{}

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

func urlSet(g *ctpolicy.LogGroupInfo) string {
	out := make([]string, 0, len(g.LogURLs))
	for u := range g.LogURLs {
		out = append(out, u)
	}
	sort.Strings(out)
	return strings.Join(out, ",")
}

const (
	googleURLs = "https://google.bench.invalid/qualified/," +
		"https://google.bench.invalid/readonly/," +
		"https://google.bench.invalid/usable/"
	shopURLs = "https://shop.bench.invalid/pending/," +
		"https://shop.bench.invalid/retired/," +
		"https://shop.bench.invalid/usable/"
)

func main() {
	t := vhelp.New()

	ll, err := loglist3.NewFromJSON([]byte(benchLogList3JSON))
	if err != nil {
		t.Fatalf("parse pinned v3 log list: %v", err)
	}
	var p ctpolicy.ChromeCTPolicy

	t.Equal("chrome-policy-name", p.Name(), "Chrome")

	// A one-year certificate: the base group needs two inclusions.
	data, err := p.LogsByGroup(certOfLifetime("2020-01-01T00:00:00Z", "2021-01-01T00:00:00Z"), ll)
	if err != nil {
		t.Fatalf("LogsByGroup over the pinned v3 list: %v", err)
	}

	names := make([]string, 0, len(data))
	for n, g := range data {
		names = append(names, n)
		if g.Name != n {
			t.Fatalf("group filed under %q calls itself %q", n, g.Name)
		}
	}
	sort.Strings(names)
	t.Equal("chrome-group-names", strings.Join(names, ","),
		"All-logs,Google-operated,Non-Google-operated")

	// Operator classification comes from the v3 operator's e-mail list.
	goog := data["Google-operated"]
	nong := data["Non-Google-operated"]
	base := data[ctpolicy.BaseName]
	if goog == nil || nong == nil || base == nil {
		t.Fatalf("chrome policy did not return all three groups")
	}

	t.Check("chrome-google-operated-group", urlSet(goog) == googleURLs && goog.MinInclusions == 1 && !goog.IsBase,
		"urls=%q min=%d base=%v", urlSet(goog), goog.MinInclusions, goog.IsBase)
	t.Check("chrome-non-google-operated-group", urlSet(nong) == shopURLs && nong.MinInclusions == 1 && !nong.IsBase,
		"urls=%q min=%d base=%v", urlSet(nong), nong.MinInclusions, nong.IsBase)
	t.Check("chrome-base-group-holds-every-log",
		urlSet(base) == googleURLs+","+shopURLs && base.IsBase && base.MinInclusions == 2,
		"urls=%q base=%v min=%d", urlSet(base), base.IsBase, base.MinInclusions)
	t.Equal("chrome-total-logs", data.TotalLogs(), 6)

	// Default weights are 1.0 for every member of every group.
	badW := []string{}
	for _, g := range []*ctpolicy.LogGroupInfo{goog, nong, base} {
		if len(g.LogWeights) != len(g.LogURLs) {
			badW = append(badW, fmt.Sprintf("%s: %d weights for %d logs", g.Name, len(g.LogWeights), len(g.LogURLs)))
		}
		for u, w := range g.LogWeights {
			if w != 1.0 {
				badW = append(badW, fmt.Sprintf("%s/%s=%v", g.Name, u, w))
			}
		}
	}
	sort.Strings(badW)
	t.Check("chrome-default-log-weights", len(badW) == 0, "%v", badW)

	// The inclusion-count step function, over pinned lifetimes.
	steps := []struct {
		nb, na string
		want   int
	}{
		{"2020-01-01T00:00:00Z", "2021-01-01T00:00:00Z", 2}, // 12 months
		{"2020-01-01T00:00:00Z", "2021-04-01T00:00:00Z", 3}, // 15 months
		{"2020-01-01T00:00:00Z", "2022-04-01T00:00:00Z", 3}, // 27 months
		{"2020-01-01T00:00:00Z", "2022-05-01T00:00:00Z", 4}, // 28 months
		{"2020-01-01T00:00:00Z", "2023-04-01T00:00:00Z", 4}, // 39 months
		{"2020-01-01T00:00:00Z", "2023-05-01T00:00:00Z", 5}, // 40 months
		{"2020-01-15T00:00:00Z", "2021-04-14T00:00:00Z", 2}, // 15 months less a day
	}
	badS := []string{}
	for _, s := range steps {
		d, err := p.LogsByGroup(certOfLifetime(s.nb, s.na), ll)
		if err != nil {
			badS = append(badS, fmt.Sprintf("%s..%s: %v", s.nb, s.na, err))
			continue
		}
		got := d[ctpolicy.BaseName].MinInclusions
		if got != s.want {
			badS = append(badS, fmt.Sprintf("%s..%s: min=%d want %d", s.nb, s.na, got, s.want))
		}
	}
	t.Check("chrome-inclusion-count-by-lifetime", len(badS) == 0, "%v", badS)

	// A list too small to satisfy the base group is an error, not a silent
	// under-count.
	small := &loglist3.LogList{Operators: []*loglist3.Operator{ll.Operators[0]}}
	small.Operators[0] = &loglist3.Operator{
		Name:  ll.Operators[0].Name,
		Email: ll.Operators[0].Email,
		Logs:  ll.Operators[0].Logs[:1],
	}
	d, err := p.LogsByGroup(certOfLifetime("2020-01-01T00:00:00Z", "2023-05-01T00:00:00Z"), small)
	t.Check("chrome-unsatisfiable-policy-errors", err != nil && d == nil,
		"err=%v data=%v", err, d)

	t.Done()
}

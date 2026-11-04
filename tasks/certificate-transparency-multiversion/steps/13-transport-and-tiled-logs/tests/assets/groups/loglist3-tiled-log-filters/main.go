// loglist3-tiled-log-filters
//
// The three log-list filters gained a tiled half at v1.3.3. SelectByStatus,
// TemporallyCompatible and RootCompatible now each build an activeOp.TiledLogs
// list beside activeOp.Logs, keep an operator when EITHER half is non-empty,
// and — this is the part that is easy to get wrong — RootCompatible keys the
// per-log root pool on a tiled log's SubmissionURL, not its MonitoringURL.
//
// Inputs: the pinned corpus certificates minted/bench-root.pem (a CA, valid
// 2000-01-01..2100-01-01), minted/rogue-root.pem (a different CA) and
// minted/leaf-valid.pem (NotAfter 2060-01-01). Every temporal interval below
// is a constant chosen against that pinned NotAfter, so no assertion reads a
// clock.
package main

import (
	"encoding/json"
	"time"

	"github.com/google/certificate-transparency-go/loglist3"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

// The group's own mirror of the v3 schema, so the JSON fed to the product is
// not produced by the product.

type jTemporal struct {
	StartInclusive time.Time `json:"start_inclusive"`
	EndExclusive   time.Time `json:"end_exclusive"`
}

type jStamp struct {
	Timestamp time.Time `json:"timestamp"`
}

type jStates struct {
	Usable  *jStamp `json:"usable,omitempty"`
	Retired *jStamp `json:"retired,omitempty"`
}

type jLog struct {
	Description string     `json:"description"`
	LogID       []byte     `json:"log_id"`
	Key         []byte     `json:"key"`
	URL         string     `json:"url"`
	MMD         int32      `json:"mmd"`
	State       *jStates   `json:"state,omitempty"`
	Temporal    *jTemporal `json:"temporal_interval,omitempty"`
}

type jTiledLog struct {
	Description   string     `json:"description"`
	LogID         []byte     `json:"log_id"`
	Key           []byte     `json:"key"`
	SubmissionURL string     `json:"submission_url"`
	MonitoringURL string     `json:"monitoring_url"`
	MMD           int32      `json:"mmd"`
	State         *jStates   `json:"state,omitempty"`
	Temporal      *jTemporal `json:"temporal_interval,omitempty"`
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
	stamp   = time.Date(2019, 1, 1, 0, 0, 0, 0, time.UTC)
	usable  = &jStates{Usable: &jStamp{Timestamp: stamp}}
	retired = &jStates{Retired: &jStamp{Timestamp: stamp}}

	// leaf-valid has NotAfter 2060-01-01, so this interval contains it and
	// the next one does not. Both endpoints are constants.
	covering = &jTemporal{
		StartInclusive: time.Date(2050, 1, 1, 0, 0, 0, 0, time.UTC),
		EndExclusive:   time.Date(2070, 1, 1, 0, 0, 0, 0, time.UTC),
	}
	missing = &jTemporal{
		StartInclusive: time.Date(2001, 1, 1, 0, 0, 0, 0, time.UTC),
		EndExclusive:   time.Date(2002, 1, 1, 0, 0, 0, 0, time.UTC),
	}
)

func id(b byte) []byte {
	out := make([]byte, 32)
	for i := range out {
		out[i] = b
	}
	return out
}

func parse(t *vhelp.T, ll *jLogList) *loglist3.LogList {
	raw, err := json.Marshal(ll)
	if err != nil {
		t.Fatalf("marshalling the pinned log list: %v", err)
	}
	got, err := loglist3.NewFromJSON(raw)
	if err != nil {
		t.Fatalf("loglist3.NewFromJSON: %v", err)
	}
	return got
}

// shape renders a filtered list as "operator{rfc-urls|tiled-submission-urls}",
// which is the whole observable result of a filter.
func shape(ll loglist3.LogList) string {
	out := ""
	for _, op := range ll.Operators {
		out += op.Name + "{"
		for i, l := range op.Logs {
			if i > 0 {
				out += ","
			}
			out += l.URL
		}
		out += "|"
		for i, l := range op.TiledLogs {
			if i > 0 {
				out += ","
			}
			out += l.SubmissionURL
		}
		out += "} "
	}
	return out
}

func readCert(t *vhelp.T, rel ...string) *x509.Certificate {
	pem := vhelp.ReadFixture(rel...)
	pool := x509util.NewPEMCertPool()
	if !pool.AppendCertsFromPEM(pem) {
		t.Fatalf("no certificate in fixture %v", rel)
	}
	certs := pool.RawCertificates()
	if len(certs) != 1 {
		t.Fatalf("fixture %v holds %d certificates, want 1", rel, len(certs))
	}
	return certs[0]
}

func main() {
	t := vhelp.New()

	benchRoot := readCert(t, "minted", "bench-root.pem")
	rogueRoot := readCert(t, "minted", "rogue-root.pem")
	leaf := readCert(t, "minted", "leaf-valid.pem")

	// The whole group is vacuous if the pinned certificates are not what the
	// intervals and pools below assume.
	t.Check("pinned-certificates-are-as-expected",
		benchRoot.IsCA && rogueRoot.IsCA && !leaf.IsCA &&
			leaf.NotAfter.Equal(time.Date(2060, 1, 1, 0, 0, 0, 0, time.UTC)) &&
			!benchRoot.Equal(rogueRoot),
		"bench-root.IsCA=%v rogue-root.IsCA=%v leaf.IsCA=%v leaf.NotAfter=%s",
		benchRoot.IsCA, rogueRoot.IsCA, leaf.IsCA, leaf.NotAfter.UTC().Format(time.RFC3339))

	// ---------------------------------------------------------------- status
	//
	// Alpha  : one usable RFC log, one usable and one retired tiled log.
	// Beta   : a retired RFC log and a usable tiled log — survives on the
	//          tiled half alone.
	// Gamma  : nothing usable in either half — must disappear.
	statusDoc := &jLogList{Version: "status", Operators: []*jOperator{
		{Name: "Alpha", Email: []string{"a@bench.invalid"},
			Logs: []*jLog{{Description: "alpha rfc", LogID: id(0x11), Key: []byte("k-a-rfc"), URL: "https://a-rfc.invalid/", MMD: 86400, State: usable}},
			TiledLogs: []*jTiledLog{
				{Description: "alpha tiled live", LogID: id(0x12), Key: []byte("k-a-t1"), SubmissionURL: "https://a-sub1.invalid/", MonitoringURL: "https://a-mon1.invalid/", MMD: 86400, State: usable},
				{Description: "alpha tiled gone", LogID: id(0x13), Key: []byte("k-a-t2"), SubmissionURL: "https://a-sub2.invalid/", MonitoringURL: "https://a-mon2.invalid/", MMD: 86400, State: retired},
			}},
		{Name: "Beta", Email: []string{"b@bench.invalid"},
			Logs:      []*jLog{{Description: "beta rfc", LogID: id(0x21), Key: []byte("k-b-rfc"), URL: "https://b-rfc.invalid/", MMD: 86400, State: retired}},
			TiledLogs: []*jTiledLog{{Description: "beta tiled", LogID: id(0x22), Key: []byte("k-b-t1"), SubmissionURL: "https://b-sub1.invalid/", MonitoringURL: "https://b-mon1.invalid/", MMD: 86400, State: usable}}},
		{Name: "Gamma", Email: []string{"g@bench.invalid"},
			Logs:      []*jLog{{Description: "gamma rfc", LogID: id(0x31), Key: []byte("k-g-rfc"), URL: "https://g-rfc.invalid/", MMD: 86400, State: retired}},
			TiledLogs: []*jTiledLog{{Description: "gamma tiled", LogID: id(0x32), Key: []byte("k-g-t1"), SubmissionURL: "https://g-sub1.invalid/", MonitoringURL: "https://g-mon1.invalid/", MMD: 86400, State: retired}}},
	}}

	byStatus := parse(t, statusDoc).SelectByStatus([]loglist3.LogStatus{loglist3.UsableLogStatus})
	t.Equal("select-by-status-filters-both-halves", shape(byStatus),
		"Alpha{https://a-rfc.invalid/|https://a-sub1.invalid/} Beta{|https://b-sub1.invalid/} ")

	retiredOnly := parse(t, statusDoc).SelectByStatus([]loglist3.LogStatus{loglist3.RetiredLogStatus})
	t.Equal("select-by-status-retired-selection", shape(retiredOnly),
		"Alpha{|https://a-sub2.invalid/} Beta{https://b-rfc.invalid/|} Gamma{https://g-rfc.invalid/|https://g-sub1.invalid/} ")

	none := parse(t, statusDoc).SelectByStatus([]loglist3.LogStatus{loglist3.PendingLogStatus})
	t.Check("select-by-status-drops-operators-with-neither-half",
		len(none.Operators) == 0, "selecting a status nothing has left %d operators", len(none.Operators))

	// -------------------------------------------------------------- temporal
	//
	// Delta  : an RFC log outside the leaf's window, a tiled log inside it,
	//          a tiled log outside it.
	// Epsilon: every entry outside the window — must disappear.
	// Zeta   : a tiled log with no interval at all — always compatible.
	tempDoc := &jLogList{Version: "temporal", Operators: []*jOperator{
		{Name: "Delta", Email: []string{"d@bench.invalid"},
			Logs: []*jLog{{Description: "delta rfc", LogID: id(0x41), Key: []byte("k-d-rfc"), URL: "https://d-rfc.invalid/", MMD: 86400, State: usable, Temporal: missing}},
			TiledLogs: []*jTiledLog{
				{Description: "delta tiled in", LogID: id(0x42), Key: []byte("k-d-t1"), SubmissionURL: "https://d-sub1.invalid/", MonitoringURL: "https://d-mon1.invalid/", MMD: 86400, State: usable, Temporal: covering},
				{Description: "delta tiled out", LogID: id(0x43), Key: []byte("k-d-t2"), SubmissionURL: "https://d-sub2.invalid/", MonitoringURL: "https://d-mon2.invalid/", MMD: 86400, State: usable, Temporal: missing},
			}},
		{Name: "Epsilon", Email: []string{"e@bench.invalid"},
			Logs:      []*jLog{{Description: "epsilon rfc", LogID: id(0x51), Key: []byte("k-e-rfc"), URL: "https://e-rfc.invalid/", MMD: 86400, State: usable, Temporal: missing}},
			TiledLogs: []*jTiledLog{{Description: "epsilon tiled", LogID: id(0x52), Key: []byte("k-e-t1"), SubmissionURL: "https://e-sub1.invalid/", MonitoringURL: "https://e-mon1.invalid/", MMD: 86400, State: usable, Temporal: missing}}},
		{Name: "Zeta", Email: []string{"z@bench.invalid"},
			Logs:      []*jLog{},
			TiledLogs: []*jTiledLog{{Description: "zeta tiled", LogID: id(0x62), Key: []byte("k-z-t1"), SubmissionURL: "https://z-sub1.invalid/", MonitoringURL: "https://z-mon1.invalid/", MMD: 86400, State: usable}}},
	}}

	temporal := parse(t, tempDoc).TemporallyCompatible(leaf)
	t.Equal("temporally-compatible-filters-tiled-logs", shape(temporal),
		"Delta{|https://d-sub1.invalid/} Zeta{|https://z-sub1.invalid/} ")

	t.Check("temporally-compatible-on-nil-cert-is-empty",
		len(parse(t, tempDoc).TemporallyCompatible(nil).Operators) == 0,
		"TemporallyCompatible(nil) kept operators")

	// ------------------------------------------------------------------ root
	//
	// Eta: a tiled log whose SubmissionURL pool holds bench-root (kept), a
	//      tiled log whose SubmissionURL pool holds only rogue-root (dropped),
	//      and a tiled log whose pool is registered under its MONITORING URL,
	//      which RootCompatible does not consult, so it counts as unknown and
	//      is kept. The RFC log's pool holds only rogue-root, so the operator
	//      survives on its tiled half alone.
	rootDoc := &jLogList{Version: "roots", Operators: []*jOperator{
		{Name: "Eta", Email: []string{"h@bench.invalid"},
			Logs: []*jLog{{Description: "eta rfc", LogID: id(0x71), Key: []byte("k-h-rfc"), URL: "https://h-rfc.invalid/", MMD: 86400, State: usable}},
			TiledLogs: []*jTiledLog{
				{Description: "eta tiled good", LogID: id(0x72), Key: []byte("k-h-t1"), SubmissionURL: "https://h-sub1.invalid/", MonitoringURL: "https://h-mon1.invalid/", MMD: 86400, State: usable},
				{Description: "eta tiled bad", LogID: id(0x73), Key: []byte("k-h-t2"), SubmissionURL: "https://h-sub2.invalid/", MonitoringURL: "https://h-mon2.invalid/", MMD: 86400, State: usable},
				{Description: "eta tiled monitoring-keyed", LogID: id(0x74), Key: []byte("k-h-t3"), SubmissionURL: "https://h-sub3.invalid/", MonitoringURL: "https://h-mon3.invalid/", MMD: 86400, State: usable},
			}},
		{Name: "Theta", Email: []string{"t@bench.invalid"},
			Logs:      []*jLog{{Description: "theta rfc", LogID: id(0x81), Key: []byte("k-t-rfc"), URL: "https://t-rfc.invalid/", MMD: 86400, State: usable}},
			TiledLogs: []*jTiledLog{{Description: "theta tiled", LogID: id(0x82), Key: []byte("k-t-t1"), SubmissionURL: "https://t-sub1.invalid/", MonitoringURL: "https://t-mon1.invalid/", MMD: 86400, State: usable}}},
	}}

	poolOf := func(certs ...*x509.Certificate) *x509util.PEMCertPool {
		p := x509util.NewPEMCertPool()
		for _, c := range certs {
			p.AddCert(c)
		}
		return p
	}
	roots := loglist3.LogRoots{
		"https://h-rfc.invalid/":  poolOf(rogueRoot),
		"https://h-sub1.invalid/": poolOf(benchRoot),
		"https://h-sub2.invalid/": poolOf(rogueRoot),
		"https://h-mon3.invalid/": poolOf(rogueRoot),
		"https://t-rfc.invalid/":  poolOf(rogueRoot),
		"https://t-sub1.invalid/": poolOf(rogueRoot),
	}

	rooted := parse(t, rootDoc).RootCompatible(benchRoot, roots)
	t.Equal("root-compatible-filters-tiled-logs-by-submission-url", shape(rooted),
		"Eta{|https://h-sub1.invalid/,https://h-sub3.invalid/} ")

	t.Check("root-compatible-rejects-a-non-ca-root",
		len(parse(t, rootDoc).RootCompatible(leaf, roots).Operators) == 0,
		"RootCompatible with a non-CA root kept operators")

	// ----------------------------------------------------------- end to end
	//
	// Compatible() = TemporallyCompatible then RootCompatible. Iota's tiled
	// log is inside the leaf's window and accepts bench-root; Kappa's is
	// inside the window but accepts only rogue-root, and its RFC log is
	// outside the window, so Kappa disappears entirely.
	bothDoc := &jLogList{Version: "both", Operators: []*jOperator{
		{Name: "Iota", Email: []string{"i@bench.invalid"},
			Logs:      []*jLog{{Description: "iota rfc", LogID: id(0x91), Key: []byte("k-i-rfc"), URL: "https://i-rfc.invalid/", MMD: 86400, State: usable, Temporal: missing}},
			TiledLogs: []*jTiledLog{{Description: "iota tiled", LogID: id(0x92), Key: []byte("k-i-t1"), SubmissionURL: "https://i-sub1.invalid/", MonitoringURL: "https://i-mon1.invalid/", MMD: 86400, State: usable, Temporal: covering}}},
		{Name: "Kappa", Email: []string{"k@bench.invalid"},
			Logs:      []*jLog{{Description: "kappa rfc", LogID: id(0xa1), Key: []byte("k-k-rfc"), URL: "https://k-rfc.invalid/", MMD: 86400, State: usable, Temporal: missing}},
			TiledLogs: []*jTiledLog{{Description: "kappa tiled", LogID: id(0xa2), Key: []byte("k-k-t1"), SubmissionURL: "https://k-sub1.invalid/", MonitoringURL: "https://k-mon1.invalid/", MMD: 86400, State: usable, Temporal: covering}}},
	}}
	bothRoots := loglist3.LogRoots{
		"https://i-sub1.invalid/": poolOf(benchRoot),
		"https://k-sub1.invalid/": poolOf(rogueRoot),
	}
	t.Equal("compatible-composes-both-filters-over-the-tiled-half",
		shape(parse(t, bothDoc).Compatible(leaf, benchRoot, bothRoots)),
		"Iota{|https://i-sub1.invalid/} ")

	t.Done()
}

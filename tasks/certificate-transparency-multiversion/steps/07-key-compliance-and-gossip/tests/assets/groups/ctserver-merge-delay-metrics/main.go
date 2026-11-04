// Group ctserver-merge-delay-metrics.
//
// v1.0.21 adds the two advertised merge delays to the configuration as
// promises -- validated, but not enforced (CONFIG.md section 2.2) -- and the
// personality publishes each of them as a gauge on its /metrics endpoint,
// labelled by log id. This is the only place the new config fields are
// observable from outside the process, so it is where "the configuration
// reached the running server" is graded; config-new-proto-fields grades the
// same two fields at the proto and text-proto level and never starts a
// server.
//
// This case moved out of gate-endtoend. It is this release's capability --
// v1.0.19's configpb.LogConfig has no field 14 or 15, so a config carrying
// them is refused before ct_server can serve anything at all -- and as a gate
// case it vetoed every candidate against the step-06 tree.
//
// Observed over a real socket against the candidate's own ct_server, started
// once with two logs whose four delay values are all different, so no gauge
// can be right by accident and neither log's value can stand in for the
// other's.
//
// SCOPE-NET: loopback only, every port obtained by binding :0.
// SCOPE-BACKEND: the log behind the personality is this group's own pinned
// stub. SCOPE-TIME: every value asserted is a compile-time constant; nothing
// here reads a clock.
package main

import (
	"fmt"
	"strconv"
	"strings"

	"ctverify/vhelp"
)

const (
	firstLogID   = int64(1701)
	firstPrefix  = "bench"
	firstMaxMD   = 86400
	firstExpMD   = 3600
	secondLogID  = int64(1702)
	secondPrefix = "oldlog"
	secondMaxMD  = 172800
	secondExpMD  = 7200

	// A log id no config names, used as the negative control.
	absentLogID = int64(1799)
)

// metricValue finds `<name>{logid="<id>"} <value>` in a Prometheus exposition
// dump and returns the value, or "ABSENT".
func metricValue(dump, name string, logID int64) string {
	want := name + `{logid="` + strconv.FormatInt(logID, 10) + `"}`
	for _, line := range strings.Split(dump, "\n") {
		if !strings.HasPrefix(line, want) {
			continue
		}
		rest := strings.TrimSpace(strings.TrimPrefix(line, want))
		if rest == "" {
			return "EMPTY"
		}
		return rest
	}
	return "ABSENT"
}

func main() {
	t := vhelp.New()

	cfg := logConfigText(t, firstLogID, firstPrefix, false,
		fmt.Sprintf("  max_merge_delay_sec: %d\n  expected_merge_delay_sec: %d\n", firstMaxMD, firstExpMD))
	cfg += logConfigText(t, secondLogID, secondPrefix, false,
		fmt.Sprintf("  max_merge_delay_sec: %d\n  expected_merge_delay_sec: %d\n", secondMaxMD, secondExpMD))

	h := startPersonalityNamed(t, "delays", nil, cfg)
	defer h.stop()

	res := h.get(t, h.metricsURL+"/metrics")
	dump := string(res.body)

	// --- 1. the dump is real and names both configured logs ---------------
	// known_logs is a gauge the personality has had since well before this
	// release. Asserting it first means an empty or unserved dump cannot pass
	// the three cases below by making every lookup "ABSENT" in the same way.
	t.Equal("metrics-dump-lists-both-configured-logs",
		fmt.Sprintf("status=%d first=%s second=%s", res.status,
			metricValue(dump, "known_logs", firstLogID),
			metricValue(dump, "known_logs", secondLogID)),
		"status=200 first=1 second=1")

	// --- 2. the maximum merge delay is exported per log -------------------
	t.Equal("max-merge-delay-exported-per-log",
		fmt.Sprintf("first=%s second=%s",
			metricValue(dump, "max_merge_delay", firstLogID),
			metricValue(dump, "max_merge_delay", secondLogID)),
		fmt.Sprintf("first=%d second=%d", firstMaxMD, secondMaxMD))

	// --- 3. and so is the expected merge delay ----------------------------
	t.Equal("expected-merge-delay-exported-per-log",
		fmt.Sprintf("first=%s second=%s",
			metricValue(dump, "expected_merge_delay", firstLogID),
			metricValue(dump, "expected_merge_delay", secondLogID)),
		fmt.Sprintf("first=%d second=%d", firstExpMD, secondExpMD))

	// --- 4. control: a log that was never configured has no gauges --------
	// Keeps the three cases above from being satisfied by a dump that answers
	// the same value to every label.
	t.Equal("merge-delay-gauges-absent-for-an-unconfigured-log",
		fmt.Sprintf("max=%s expected=%s known=%s",
			metricValue(dump, "max_merge_delay", absentLogID),
			metricValue(dump, "expected_merge_delay", absentLogID),
			metricValue(dump, "known_logs", absentLogID)),
		"max=ABSENT expected=ABSENT known=ABSENT")

	t.Done()
}

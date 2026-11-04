// submission-loglist-refresher-v3 observes submission.LogListRefresher and
// submission.LogListManager, the two things the submission proxy uses to keep
// a CT log list current. At v1.1.4 both hand back a *loglist3.LogList; at
// v1.1.3 they handed back a *loglist2.LogList, so this driver cannot even be
// compiled against the previous release.
//
// Everything is local: the "source" is a file in $CT_SCRATCH, so no refresh
// touches the network, and no assertion reads DownloadTime (SCOPE-TIME).
package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"os"
	"path/filepath"
	"time"

	"github.com/google/certificate-transparency-go/loglist3"
	"github.com/google/certificate-transparency-go/submission"

	"ctverify/vhelp"
)

// benchLogList3Trimmed is the same schema with a single operator running a
// single usable log. It is used to prove that a second Refresh of a changed
// source produces the changed list.
const benchLogList3Trimmed = `{
  "version": "11.1",
  "log_list_timestamp": "2022-02-02T00:00:00Z",
  "operators": [
    {
      "name": "Harbor Bench Shop",
      "email": [
        "ops@shop.bench.invalid"
      ],
      "logs": [
        {
          "description": "Shop Usable Log",
          "log_id": "3xwuwRUAlFJHqWFoMl3cXHlZ6PfG04j8AC4LvT9012Q=",
          "key": "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEmXg8sUUzwBYaWrRb+V0IopzQ6o3UyEJ04r5ZrRXGdpYM8K+hB0pXrGRLI0eeWz+3skXrS0IO83AhA3GpRL6s6w==",
          "url": "https://shop.bench.invalid/usable/",
          "mmd": 7200,
          "state": {
            "usable": {
              "timestamp": "2021-06-01T00:00:00Z"
            }
          }
        }
      ]
    }
  ]
}
`

func countLogs(ll *loglist3.LogList) int {
	n := 0
	for _, op := range ll.Operators {
		n += len(op.Logs)
	}
	return n
}

func main() {
	t := vhelp.New()

	dir := filepath.Join(os.Getenv("CT_SCRATCH"), "llr")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	path := filepath.Join(dir, "log_list.json")
	if err := os.WriteFile(path, []byte(benchLogList3JSON), 0o644); err != nil {
		t.Fatalf("write list: %v", err)
	}

	llr := submission.NewLogListRefresher(path)

	// 1. The refresher reports the source it was built from.
	t.Equal("refresher-source-is-the-path", llr.Source(), path)

	// 2. Before any refresh there is no last JSON, and it is an empty slice
	//    rather than a nil one.
	first := llr.LastJSON()
	t.Check("refresher-last-json-is-empty-before-refresh", first != nil && len(first) == 0,
		"LastJSON()=%q (nil=%v)", string(first), first == nil)

	// 3. The first refresh parses the file with the v3 schema.
	lld, err := llr.Refresh()
	if err != nil || lld == nil {
		t.Fatalf("first Refresh: data=%v err=%v", lld, err)
	}
	want := time.Date(2022, 1, 1, 0, 0, 0, 0, time.UTC)
	got := lld.List
	t.Check("refresh-returns-the-pinned-v3-list",
		got.Version == "11.0" && got.LogListTimestamp.Equal(want) &&
			len(got.Operators) == 2 && countLogs(got) == 6,
		"version=%q timestamp=%v operators=%d logs=%d",
		got.Version, got.LogListTimestamp.UTC(), len(got.Operators), countLogs(got))

	// 4. The bytes it kept are the bytes of the file, unaltered.
	t.EqualBytes("refresh-json-is-the-source-bytes", lld.JSON, []byte(benchLogList3JSON))

	// 5. LastJSON now reports the same bytes.
	sum := sha256.Sum256([]byte(benchLogList3JSON))
	gotSum := sha256.Sum256(llr.LastJSON())
	t.Equal("last-json-is-the-refreshed-bytes", hex.EncodeToString(gotSum[:]), hex.EncodeToString(sum[:]))

	// 6. Refreshing an unchanged source reports "no update" -- nil data and
	//    nil error, which is how the manager tells the two apart.
	again, err := llr.Refresh()
	t.Check("unchanged-source-refreshes-to-no-update", again == nil && err == nil,
		"data=%v err=%v", again, err)

	// 7. A changed source produces the changed list.
	if err := os.WriteFile(path, []byte(benchLogList3Trimmed), 0o644); err != nil {
		t.Fatalf("rewrite list: %v", err)
	}
	third, err := llr.Refresh()
	if err != nil || third == nil {
		t.Fatalf("third Refresh: data=%v err=%v", third, err)
	}
	t.Check("changed-source-refreshes-to-the-new-list",
		third.List.Version == "11.1" && len(third.List.Operators) == 1 && countLogs(third.List) == 1 &&
			third.List.Operators[0].Logs[0].Description == "Shop Usable Log",
		"version=%q operators=%d logs=%d",
		third.List.Version, len(third.List.Operators), countLogs(third.List))

	// 8. A source that cannot be read is an error, and no data comes back.
	missing := submission.NewLogListRefresher(filepath.Join(dir, "absent.json"))
	bad, err := missing.Refresh()
	t.Check("unreadable-source-is-an-error", bad == nil && err != nil,
		"data=%v err=%v", bad, err)

	// 9. The manager keeps the latest list and the one before it.
	if err := os.WriteFile(path, []byte(benchLogList3JSON), 0o644); err != nil {
		t.Fatalf("restore list: %v", err)
	}
	mllr := submission.NewLogListRefresher(path)
	llm := submission.NewLogListManager(mllr, nil)
	// Run installs the package's metrics. It is handed an already-cancelled
	// context so that schedule.Every returns without scheduling anything:
	// every refresh below is driven explicitly, so the group is deterministic.
	dead, stop := context.WithCancel(context.Background())
	stop()
	llm.Run(dead, time.Hour)
	ctx := context.Background()
	if _, err := llm.RefreshLogList(ctx); err != nil {
		t.Fatalf("manager refresh 1: %v", err)
	}
	if err := os.WriteFile(path, []byte(benchLogList3Trimmed), 0o644); err != nil {
		t.Fatalf("rewrite list for manager: %v", err)
	}
	if _, err := llm.RefreshLogList(ctx); err != nil {
		t.Fatalf("manager refresh 2: %v", err)
	}
	latest, previous := llm.GetTwoLatestLogLists()
	t.Check("manager-keeps-the-two-latest-lists",
		latest != nil && previous != nil &&
			latest.List.Version == "11.1" && countLogs(latest.List) == 1 &&
			previous.List.Version == "11.0" && countLogs(previous.List) == 6,
		"latest=%v previous=%v", latest, previous)

	// 10. The client-facing list is the latest one.
	client := llm.ProduceClientLogList()
	t.Check("client-log-list-is-the-latest-one",
		client.List != nil && client.List.Version == "11.1" && countLogs(client.List) == 1,
		"client=%v", client.List)

	// 11. The manager passes the source through, and reports the bytes of the
	//     list it last read.
	trimmedSum := sha256.Sum256([]byte(benchLogList3Trimmed))
	mgrSum := sha256.Sum256(llm.LastJSON())
	t.Check("manager-delegates-source-and-last-json",
		llm.Source() == path && hex.EncodeToString(mgrSum[:]) == hex.EncodeToString(trimmedSum[:]),
		"source=%q sha256=%s", llm.Source(), hex.EncodeToString(mgrSum[:]))

	// 12. A manager with no refresher refuses to refresh.
	empty := submission.NewLogListManager(nil, nil)
	nilData, err := empty.RefreshLogList(ctx)
	t.Check("manager-without-a-refresher-refuses", nilData == nil && err != nil,
		"data=%v err=%v", nilData, err)

	t.Done()
}

// Group config-notafter-range.
//
// v1.0.10's ValidateLogMultiConfig starts checking the temporal-shard window:
// not_after_start and not_after_limit must each be a valid protobuf timestamp,
// and the limit must not fall before the start. Either bound may be left unset.
//
// v1.0.5's validator never read those fields, so it accepts an inverted window
// and an out-of-range timestamp alike.
package main

import "ctverify/vhelp"

// Pinned instants. 2020-01-01T00:00:00Z and 2021-01-01T00:00:00Z.
const (
	start2020 = int64(1577836800)
	start2021 = int64(1609459200)
)

func window(start, limit *tsProto) *cfgLog {
	c := logCfg(1001, "bench", "a")
	c.NotAfterStart = start
	c.NotAfterLimit = limit
	return c
}

func one(l *cfgLog) []*cfgLog { return []*cfgLog{l} }

func beSet() []*cfgBackend { return []*cfgBackend{backend("a", "127.0.0.1:9001")} }

func main() {
	t := vhelp.New()

	runVerdicts(t, []verdict{
		{
			id:     "no-bounds-accepted",
			cfg:    multi(beSet(), one(window(nil, nil))),
			accept: true,
		},
		{
			id:     "start-before-limit-accepted",
			cfg:    multi(beSet(), one(window(ts(start2020, 0), ts(start2021, 0)))),
			accept: true,
		},
		{
			id:     "start-equal-limit-accepted",
			cfg:    multi(beSet(), one(window(ts(start2020, 0), ts(start2020, 0)))),
			accept: true,
		},
		{
			id:     "limit-before-start-rejected",
			cfg:    multi(beSet(), one(window(ts(start2021, 0), ts(start2020, 0)))),
			accept: false,
		},
		{
			id:     "limit-one-nanosecond-before-start-rejected",
			cfg:    multi(beSet(), one(window(ts(start2020, 1), ts(start2020, 0)))),
			accept: false,
		},
		{
			id:     "only-limit-set-accepted",
			cfg:    multi(beSet(), one(window(nil, ts(start2020, 0)))),
			accept: true,
		},
		{
			id:     "out-of-range-nanos-in-start-rejected",
			cfg:    multi(beSet(), one(window(ts(start2020, 1000000000), nil))),
			accept: false,
		},
		{
			id:     "out-of-range-nanos-in-limit-rejected",
			cfg:    multi(beSet(), one(window(nil, ts(start2020, -1)))),
			accept: false,
		},
	})

	t.Done()
}

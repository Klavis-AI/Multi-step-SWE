// Group config-empty-log-prefix.
//
// v1.0.10's ValidateLogMultiConfig requires every log's prefix to be
// non-empty. An empty prefix would mount a log at the personality's root and
// shadow every other log's paths, so the release refuses the configuration up
// front instead of discovering it at mux registration time.
//
// v1.0.5's validator never looked at Prefix at all, so it accepts all of the
// empty-prefix configurations below.
package main

import "ctverify/vhelp"

func main() {
	t := vhelp.New()

	runVerdicts(t, []verdict{
		{
			id: "non-empty-prefix-accepted",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1, "bench", "a")}),
			accept: true,
		},
		{
			id: "single-character-prefix-accepted",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1, "b", "a")}),
			accept: true,
		},
		{
			id: "only-log-has-empty-prefix-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1, "", "a")}),
			accept: false,
		},
		{
			id: "second-log-has-empty-prefix-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1, "bench", "a"), logCfg(2, "", "a")}),
			accept: false,
		},
		{
			id: "empty-prefix-on-second-backend-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001"), backend("b", "127.0.0.1:9002")},
				[]*cfgLog{logCfg(1, "bench", "a"), logCfg(2, "", "b")}),
			accept: false,
		},
	})

	t.Done()
}

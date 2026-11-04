// Group config-duplicate-tree-id.
//
// v1.0.10's ValidateLogMultiConfig rejects two log configurations that name
// the same backend *and* the same Trillian tree id: they would be two CT logs
// writing into one tree. The key is the pair, so the same tree id on two
// different backends is legitimate and must still be accepted.
//
// v1.0.5's validator had no notion of tree ids, so it accepts every
// configuration below.
package main

import "ctverify/vhelp"

func main() {
	t := vhelp.New()

	runVerdicts(t, []verdict{
		{
			id: "distinct-tree-ids-one-backend-accepted",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1001, "bench-a", "a"), logCfg(1002, "bench-b", "a")}),
			accept: true,
		},
		{
			id: "same-tree-id-one-backend-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1001, "bench-a", "a"), logCfg(1001, "bench-b", "a")}),
			accept: false,
		},
		{
			id: "same-tree-id-different-backends-accepted",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001"), backend("b", "127.0.0.1:9002")},
				[]*cfgLog{logCfg(1001, "bench-a", "a"), logCfg(1001, "bench-b", "b")}),
			accept: true,
		},
		{
			id: "first-and-third-share-backend-and-tree-id-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001"), backend("b", "127.0.0.1:9002")},
				[]*cfgLog{logCfg(1001, "bench-a", "a"), logCfg(1002, "bench-b", "b"), logCfg(1001, "bench-c", "a")}),
			accept: false,
		},
		{
			id: "unset-tree-id-twice-one-backend-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(0, "bench-a", "a"), logCfg(0, "bench-b", "a")}),
			accept: false,
		},
	})

	t.Done()
}

// Group config-duplicate-backend-spec.
//
// v1.0.10's ValidateLogMultiConfig adds a second uniqueness rule to the
// backend set: the backend *specs* must be distinct as well as the backend
// names. Two names pointing at one endpoint is a misconfiguration that
// silently doubles a log's traffic, and the release refuses it.
//
// v1.0.5 checked only that backend names were unique and non-empty, that
// specs were non-empty, and that every log names a defined backend, so it
// accepts every duplicate-spec config below.
//
// Three cases here are deliberately v1.0.5 rules and pass at both releases:
// empty-backend-spec-rejected, duplicate-backend-name-rejected and
// log-naming-undefined-backend-rejected. They are the surrounding rules the
// new one must not have displaced, and they carry the CONFIG.md 4.4 rows
// nothing else in the step reaches. The group as a whole still fails at
// v1.0.5, on its duplicate-spec cases, so the inherited- naming rule does not
// apply to it.
//
// The empty-name case is not one of those three. The rule is v1.0.5's, but
// v1.0.10 rewords its diagnostic -- "empty name for backend" becomes "empty
// backend name" (CONFIG.md 4.4) -- and that rewording is the only place in
// this step where a documented message change is observable, so the case
// grades the documented prefix as well as the verdict and fails at v1.0.5
// like the rest.
package main

import "ctverify/vhelp"

func main() {
	t := vhelp.New()

	runVerdicts(t, []verdict{
		{
			id: "single-backend-accepted",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1, "bench-a", "a")}),
			accept: true,
		},
		{
			id: "two-backends-distinct-specs-accepted",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001"), backend("b", "127.0.0.1:9002")},
				[]*cfgLog{logCfg(1, "bench-a", "a"), logCfg(2, "bench-b", "b")}),
			accept: true,
		},
		{
			id: "two-backends-same-spec-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001"), backend("b", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1, "bench-a", "a"), logCfg(2, "bench-b", "b")}),
			accept: false,
		},
		{
			id: "first-and-third-backend-same-spec-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001"), backend("b", "127.0.0.1:9002"), backend("c", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1, "bench-a", "a"), logCfg(2, "bench-b", "b"), logCfg(3, "bench-c", "c")}),
			accept: false,
		},
		{
			id: "empty-backend-spec-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "")},
				[]*cfgLog{logCfg(1, "bench-a", "a")}),
			accept: false,
		},
		{
			id: "empty-backend-name-rejected-and-reworded",
			cfg: multi(
				[]*cfgBackend{backend("", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1, "bench-a", "")}),
			accept: false,
			msg:    "empty backend name",
		},
		{
			// CONFIG.md 4.4: two backends with the same name. A v1.0.5 rule
			// with a wording that does not move, kept reachable here because
			// no other group in the step exercises it. The two specs differ,
			// so the duplicate-name arm is the one that fires and not the
			// duplicate-spec arm this group is named for.
			id: "duplicate-backend-name-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001"), backend("a", "127.0.0.1:9002")},
				[]*cfgLog{logCfg(1, "bench-a", "a")}),
			accept: false,
			msg:    "duplicate backend name",
		},
		{
			// CONFIG.md 4.4: a log naming a backend that is not defined. A
			// v1.0.5 rule, kept reachable here because no other group in the
			// step exercises it.
			id: "log-naming-undefined-backend-rejected",
			cfg: multi(
				[]*cfgBackend{backend("a", "127.0.0.1:9001")},
				[]*cfgLog{logCfg(1, "bench-a", "a"), logCfg(2, "bench-b", "nosuch")}),
			accept: false,
		},
	})

	t.Done()
}

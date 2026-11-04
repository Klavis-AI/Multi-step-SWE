# Milestone drives

This milestone uses exactly these `go` subcommands, with the module directory
as the working directory:

    $GOROOT/bin/go run .
    $GOROOT/bin/go build -o a.out .
    $GOROOT/bin/go test [-v] [-run PATTERN] ./...

Nothing else is invoked.

`go test` compiles the package together with its `_test.go` files, runs every
`func TestXxx(*testing.T)` it finds, and writes the result in the `go test`
format.

Under `-v`: a `=== RUN   Name` line per test, then a
`--- PASS|FAIL|SKIP: Name (0.00s)` line per test with subtests indented four
spaces under their parent, then `PASS` on its own line, then
`ok  \t<import path>\t0.00s`. When `-run` matched nothing, the summary line
reads `ok  \t<import path>\t0.00s [no tests to run]` and is preceded by
`testing: warning: no tests to run`.

Without `-v`, a passing package prints **only** the `ok` summary line: no
`=== RUN`, no `--- PASS`, and no bare `PASS`.

A failing package prints the `--- FAIL` block for the tests that failed
(whether or not `-v` was given), then `FAIL` on its own line, then
`FAIL\t<import path>\t0.00s`, then a final `FAIL`. Its exit status is 0 when
every test passed and 1 otherwise. `-run` matches an unanchored
regular expression against the test name, and a `/`-separated pattern matches
successive levels of subtest names.

Under `-v`, a line a test logs is printed when it is logged, before the next
`=== RUN`, indented by four spaces whatever the subtest's depth, and carries
the `file.go:LINE: ` position of the call that produced it. The `--- PASS`
block that follows repeats a test's subtests but not its already-printed log
lines.

Where the tested package's *own* writes go matters, and it is not the `go
run` rule. A `fmt.Print`, a built-in `println`, anything an `init` emits --
`go test` passes none of it through to the `go` command's standard error. It
captures both of the test binary's streams, merges them in the order the
program wrote them, and emits that text on the `go` command's **standard
output, ahead of the test report** -- so an `init`'s `println` lands before the
first `=== RUN` line, in its true position relative to that package's `fmt`
output. Without `-v` the same text is buffered and then **discarded if the
package passes**, or printed ahead of the `--- FAIL` block if it fails. Every
case writes this output before its first test starts. Section 4 of
`toolchain.md` states the rule.

Durations are not compared: in `go test` output every `<digits>.<digits>s` is
rewritten to `0.00s` on both sides before comparing, so the timings your
toolchain prints are irrelevant.

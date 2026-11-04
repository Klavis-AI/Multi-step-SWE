// Package logging bounds what the project's logger does, as opposed to which
// library writes it.
//
// Two things are at stake and they pull in opposite directions. The recorded
// suite reaches the rendered form -- `cli-errors.json` and
// `cli-client-tls.json` hold six lines of
// `time="..." level=... msg="..." key=value` captured from a process's
// standard error -- but it barely prices it: the default setting is `warn`, so
// of the suite's 1 118 inputs only three carry a log line at all, and a tree
// measured with every line rendered in a different dialect of logfmt still
// scored 1 115 of them. The rendering is priced here instead.
//
// What the recorded suite cannot reach at all is the *ordering* of the levels,
// because the harness runs almost every scenario at one level and never asks
// the same build to be quiet and loud in turn. That ordering is the sharpest
// single risk: in the library this tree starts with, a numerically larger
// level means a more verbose logger, and the eighteen hand-written guards in
// `api/context/context_logger.go` all read `>=`. Anything ported onto a
// library whose levels ascend with severity, with those guards carried across
// unchanged, inverts every one of them -- a build that prints debug lines when
// asked for errors only, and prints nothing when asked for everything. Both
// halves of that stay green against a suite recorded at a single level.
//
// So the tests below drive one build at four settings and check both
// directions at each: what has to appear, and what must not. They go through
// the plain context methods and through the field-logger wrapper separately,
// because the guards live on the wrapper and the plain methods delegate to the
// library.
//
// Output is collected at the file-descriptor level rather than by swapping a
// writer, so the checks hold for a tree that writes to `types.Stderr`, to
// `os.Stderr`, or to standard output, and do not depend on any package
// variable surviving the port.
package logging

import (
	"io"
	"log/slog"
	"os"
	"reflect"
	"strings"
	"syscall"
	"testing"

	"github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/types"
)

// levelEnv is the environment variable a fresh context reads its level from
// when nothing else has set one.
const levelEnv = "LIBSTORAGE_LOGGING_LEVEL"

// capture runs fn with both standard streams redirected into a pipe and
// returns everything written to either one.
//
// Redirecting the descriptors rather than a writer means the logger can hold
// whatever handle it captured at init and still be seen. Both streams are
// taken because a port is free to decide the logger belongs on stdout; the
// question these tests ask is whether a line was written at all, not which
// stream carried it.
func capture(t *testing.T, fn func()) string {
	t.Helper()

	r, w, err := os.Pipe()
	if err != nil {
		t.Fatalf("pipe: %v", err)
	}
	old1, err := syscall.Dup(1)
	if err != nil {
		t.Fatalf("dup stdout: %v", err)
	}
	old2, err := syscall.Dup(2)
	if err != nil {
		t.Fatalf("dup stderr: %v", err)
	}
	if err := syscall.Dup2(int(w.Fd()), 1); err != nil {
		t.Fatalf("redirect stdout: %v", err)
	}
	if err := syscall.Dup2(int(w.Fd()), 2); err != nil {
		t.Fatalf("redirect stderr: %v", err)
	}

	read := make(chan string, 1)
	go func() {
		b, _ := io.ReadAll(r)
		read <- string(b)
	}()

	func() {
		defer func() {
			// A logger that panics on its way out must not leave the
			// descriptors pointing at a pipe nobody is draining.
			syscall.Dup2(old1, 1)
			syscall.Dup2(old2, 2)
			syscall.Close(old1)
			syscall.Close(old2)
			w.Close()
		}()
		fn()
	}()

	out := <-read
	r.Close()
	return out
}

// emitted is the text one build writes at one level, for one set of calls.
func emitted(t *testing.T, level string, fn func(types.Context)) string {
	t.Helper()
	t.Setenv(levelEnv, level)
	return capture(t, func() { fn(context.Background()) })
}

// the messages the level tests look for. Each is distinct so a line that
// escapes its guard is named in the failure rather than merely counted.
const (
	msgDebug = "probe-debug-line"
	msgInfo  = "probe-info-line"
	msgWarn  = "probe-warn-line"
	msgError = "probe-error-line"
)

// plainAll logs one message at each of the four levels through the context
// itself.
func plainAll(ctx types.Context) {
	ctx.Debug(msgDebug)
	ctx.Info(msgInfo)
	ctx.Warn(msgWarn)
	ctx.Error(msgError)
}

// fieldAll logs the same four through the field-logger wrapper, which is where
// the hand-written level guards are.
func fieldAll(ctx types.Context) {
	ctx.WithField("probe", "field").Debug(msgDebug)
	ctx.WithFields(map[string]interface{}{"probe": "fields"}).Info(msgInfo)
	ctx.WithField("probe", "field").Warn(msgWarn)
	ctx.WithError(errProbe).Error(msgError)
}

type probeError struct{}

func (probeError) Error() string { return "probe error value" }

var errProbe = probeError{}

// expectation is which of the four messages a given setting may print.
type expectation struct {
	level string
	want  []string
	deny  []string
}

// The order the project's levels have run in since it was written: a setting
// prints its own messages and everything more severe, and nothing less severe.
var levelTable = []expectation{
	{"debug", []string{msgDebug, msgInfo, msgWarn, msgError}, nil},
	{"info", []string{msgInfo, msgWarn, msgError}, []string{msgDebug}},
	{"warn", []string{msgWarn, msgError}, []string{msgDebug, msgInfo}},
	{"error", []string{msgError}, []string{msgDebug, msgInfo, msgWarn}},
}

func checkLevels(t *testing.T, through func(types.Context)) {
	t.Helper()
	for _, e := range levelTable {
		got := emitted(t, e.level, through)
		for _, w := range e.want {
			if !strings.Contains(got, w) {
				t.Errorf("at level %q the message %q was not written; "+
					"output was:\n%s", e.level, w, got)
			}
		}
		for _, d := range e.deny {
			if strings.Contains(got, d) {
				t.Errorf("at level %q the message %q was written and "+
					"should not have been; output was:\n%s",
					e.level, d, got)
			}
		}
	}
}

// TestALevelPrintsItsOwnMessagesAndTheMoreSevereOnes drives the plain context
// methods at all four settings.
func TestALevelPrintsItsOwnMessagesAndTheMoreSevereOnes(t *testing.T) {
	checkLevels(t, plainAll)
}

// TestTheFieldLoggerObeysTheSameOrder drives the same four settings through
// WithField, WithFields and WithError. These are the calls the eighteen
// hand-written guards sit in front of, and they can be inverted independently
// of the plain ones.
func TestTheFieldLoggerObeysTheSameOrder(t *testing.T) {
	checkLevels(t, fieldAll)
}

// TestTheQuietestSettingIsStillNotSilent separates two failures that look the
// same from the outside: a logger whose comparison is inverted, and one that
// prints nothing at all.
func TestTheQuietestSettingIsStillNotSilent(t *testing.T) {
	got := emitted(t, "error", plainAll)
	if strings.TrimSpace(got) == "" {
		t.Fatalf("at level \"error\" nothing at all was written; an error " +
			"logged at the error setting has to come out")
	}
	if !strings.Contains(got, msgError) {
		t.Errorf("at level \"error\" the error message was missing from "+
			"output:\n%s", got)
	}
}

// TestEachLevelPrintsItsOwnName pins the level tokens the recorded expect
// files hold. `cli-errors.json` carries `level=info` and
// `cli-client-tls.json` carries `level=error`; the other two names are in the
// same lower-case, spelled-out form the library has always used.
func TestEachLevelPrintsItsOwnName(t *testing.T) {
	for _, c := range []struct{ level, token, msg string }{
		{"debug", "level=debug", msgDebug},
		{"debug", "level=info", msgInfo},
		{"debug", "level=warning", msgWarn},
		{"debug", "level=error", msgError},
	} {
		got := emitted(t, c.level, plainAll)
		line := lineWith(got, c.msg)
		if line == "" {
			t.Errorf("%s: no line carried %q; output was:\n%s",
				c.token, c.msg, got)
			continue
		}
		if !strings.Contains(line, c.token) {
			t.Errorf("the line carrying %q reads %q, which does not contain "+
				"%q", c.msg, line, c.token)
		}
	}
}

// TestEveryLineIsLogfmtWithATimestampAndAMessage pins the shape the recorded
// expect files hold: a timestamp first, then the level, then the message.
func TestEveryLineIsLogfmtWithATimestampAndAMessage(t *testing.T) {
	got := emitted(t, "debug", plainAll)
	lines := nonEmptyLines(got)
	if len(lines) == 0 {
		t.Fatalf("nothing was written at level \"debug\"")
	}
	for _, ln := range lines {
		if !strings.HasPrefix(ln, `time="`) {
			t.Errorf("line %q does not start with a quoted `time=` field", ln)
		}
		for _, want := range []string{" level=", " msg="} {
			if !strings.Contains(ln, want) {
				t.Errorf("line %q has no %q field", ln, strings.TrimSpace(want))
			}
		}
		if i, j := strings.Index(ln, " level="), strings.Index(ln, " msg="); i > j {
			t.Errorf("line %q puts the message before the level", ln)
		}
	}
}

// TestFieldsFollowTheMessageInAlphabeticalOrder pins the ordering the recorded
// lines were captured with: `msg=...` and then the attached fields by key.
// A rendering that emits them in insertion or map order produces a different
// line on every run, which is what the expect files would catch a step later.
//
// Six keys rather than three, and handed over in an order that is neither
// alphabetical nor the reverse: a rendering that walks the map instead of
// sorting it lands on the right order once in seven hundred runs rather than
// once in six, so a failure here is a finding and a pass is not luck.
func TestFieldsFollowTheMessageInAlphabeticalOrder(t *testing.T) {
	keys := []string{"alpha", "delta", "kilo", "mike", "tango", "zulu"}
	got := emitted(t, "info", func(ctx types.Context) {
		ctx.WithFields(map[string]interface{}{
			"zulu":  26,
			"alpha": 1,
			"mike":  13,
			"delta": 4,
			"tango": 20,
			"kilo":  11,
		}).Info(msgInfo)
	})
	line := lineWith(got, msgInfo)
	if line == "" {
		t.Fatalf("no line carried %q; output was:\n%s", msgInfo, got)
	}
	msgAt := strings.Index(line, " msg=")
	idx := make([]int, len(keys))
	for i, k := range keys {
		at := strings.Index(line, k+"=")
		if at < 0 {
			t.Fatalf("field %q is missing from %q", k, line)
		}
		if at < msgAt {
			t.Errorf("field %q comes before the message in %q", k, line)
		}
		idx[i] = at
	}
	for i := 1; i < len(idx); i++ {
		if idx[i-1] > idx[i] {
			t.Errorf("field %q comes after %q, so the fields are not in "+
				"alphabetical order in %q", keys[i-1], keys[i], line)
			break
		}
	}
}

// TestQuotingFollowsTheRecordedForm pins the two quoting decisions the expect
// files hold: a value with a space in it is quoted, one without is not.
func TestQuotingFollowsTheRecordedForm(t *testing.T) {
	const spaced = "a probe message with spaces"
	const bare = "probe-bare-message"

	got := emitted(t, "info", func(ctx types.Context) {
		ctx.Info(spaced)
		ctx.Info(bare)
	})
	if line := lineWith(got, spaced); line == "" {
		t.Errorf("no line carried %q; output was:\n%s", spaced, got)
	} else if !strings.Contains(line, `msg="`+spaced+`"`) {
		t.Errorf("a message containing spaces was not quoted: %q", line)
	}
	if line := lineWith(got, bare); line == "" {
		t.Errorf("no line carried %q; output was:\n%s", bare, got)
	} else if !strings.Contains(line, "msg="+bare+" ") &&
		!strings.HasSuffix(line, "msg="+bare) {
		t.Errorf("a message with no spaces was not written bare: %q", line)
	}

	got = emitted(t, "error", func(ctx types.Context) {
		ctx.WithError(errProbe).Error(msgError)
	})
	line := lineWith(got, msgError)
	if line == "" {
		t.Fatalf("no line carried %q; output was:\n%s", msgError, got)
	}
	if !strings.Contains(line, `error="`+errProbe.Error()+`"`) {
		t.Errorf("WithError did not put the error under an `error` key in %q",
			line)
	}
}

// TestAnEmptyMessageWritesNoMessageFieldAtAll pins what the rendering does
// with a call that carries fields and no message: the message field is left
// out rather than written empty. Nothing in the recorded corpus logs an empty
// message, and a rendering that always writes `msg=` writes a line this
// project never has the first time one is logged.
func TestAnEmptyMessageWritesNoMessageFieldAtAll(t *testing.T) {
	const marker = "empty-message-probe"

	got := emitted(t, "info", func(ctx types.Context) {
		ctx.WithField("probe", marker).Info("")
	})
	line := lineWith(got, marker)
	if line == "" {
		t.Fatalf("a line with fields and no message was not written at all; "+
			"output was:\n%s", got)
	}
	if strings.Contains(line, " msg=") {
		t.Errorf("line %q carries a msg field for a message that was empty",
			line)
	}
	if !strings.HasPrefix(line, `time="`) ||
		!strings.Contains(line, " level=info ") {
		t.Errorf("line %q is not the ordinary form with the message left "+
			"out", line)
	}
}

// TestAnEmptyValueIsWrittenBare pins the other end of the same judgement: a
// field whose value is the empty string is written as the key, the separator
// and nothing -- not as a pair of quotes. It is the one quoting decision the
// recorded lines do not reach, and the one a rendering built on a different
// library is most likely to make differently.
func TestAnEmptyValueIsWrittenBare(t *testing.T) {
	got := emitted(t, "info", func(ctx types.Context) {
		ctx.WithFields(map[string]interface{}{
			"empty": "",
			"zulu":  "last",
		}).Info(msgInfo)
	})
	line := lineWith(got, msgInfo)
	if line == "" {
		t.Fatalf("no line carried %q; output was:\n%s", msgInfo, got)
	}
	// Space-or-end on the right, the same way the quoting test reads its
	// last field: a rendering that walks the fields in map order can put
	// `empty=` at the end of the line, and the question here is the pair's
	// shape rather than where in the line it landed -- the ordering test
	// above is the one that judges that.
	if !strings.Contains(line, " empty= ") &&
		!strings.HasSuffix(line, " empty=") {
		t.Errorf("line %q does not write an empty value as a bare pair", line)
	}
}

// TestTheLevelSettingTakesTheLongSpellingAndFallsBackToWarning reads the
// level parser through the setting an operator writes, at both ends of it.
// The project's level names are the ones the library it was written against
// accepted, and `warning` is one of them: a parser that takes only the four
// short spellings turns a working configuration into a silent fall back to
// whatever the default is. That fall back is the second setting here, and it
// is the project's own rather than the standard library's: a name the parser
// does not know leaves the logger at warn, quietly, where `log/slog` left to
// itself would be at info and would print the informational line.
//
// The two settings are read the same way -- the warning line written, the
// informational one not -- because on this project they land on the same
// level. A port that dropped the long spelling and kept the fall back would
// pass the first half for the wrong reason, which is why the ticket states
// both behaviours rather than only the alias: neither half is a trap for a
// port that has read it.
func TestTheLevelSettingTakesTheLongSpellingAndFallsBackToWarning(
	t *testing.T) {

	for _, level := range []string{"warning", "not-a-level"} {
		got := emitted(t, level, plainAll)
		if !strings.Contains(got, msgWarn) {
			t.Errorf("at level %q the warning message was not written; "+
				"output was:\n%s", level, got)
		}
		if strings.Contains(got, msgInfo) {
			t.Errorf("at level %q the informational message was written, so "+
				"the logger is not at the warning level; output was:\n%s",
				level, got)
		}
	}
}

// TestTheFormattedAndSpacedCallsRenderTheirArguments drives three of the
// surface's less-used spellings for what they do rather than for their
// signature. The formatting methods take a format string and its arguments.
// The `ln` methods are the ones with a history: on the context itself they
// render like `fmt.Sprint`, which runs adjacent strings together, and on the
// field logger they render like `fmt.Sprintln` without the newline, which
// puts a space between every pair. Both are what this project writes today
// and the difference between them is invisible to the compiler, so a port
// that routes one spelling through the other changes a message that has
// always read one way.
func TestTheFormattedAndSpacedCallsRenderTheirArguments(t *testing.T) {
	const formatted = "probe-formatted-7-of-9"
	const runTogether = "probe-joinedleftright"
	const spaced = "probe-entry left right"

	got := emitted(t, "debug", func(ctx types.Context) {
		ctx.Debugf("probe-formatted-%d-of-%d", 7, 9)
		ctx.Infoln("probe-joined", "left", "right")
		ctx.WithField("probe", "spaced").Infoln("probe-entry", "left", "right")
	})

	if line := lineWith(got, "probe-formatted"); line == "" {
		t.Errorf("a formatted debug call wrote nothing; output was:\n%s", got)
	} else if !strings.Contains(line, " msg="+formatted) {
		t.Errorf("line %q does not carry ` msg=%s`", line, formatted)
	}

	if line := lineWith(got, "probe-joined"); line == "" {
		t.Errorf("a spaced info call on the context wrote nothing; output "+
			"was:\n%s", got)
	} else if !strings.Contains(line, " msg="+runTogether) {
		t.Errorf("line %q does not carry ` msg=%s`; the context's `ln` "+
			"methods run their arguments together", line, runTogether)
	}

	if line := lineWith(got, "probe-entry"); line == "" {
		t.Errorf("a spaced info call on the field logger wrote nothing; "+
			"output was:\n%s", got)
	} else if !strings.Contains(line, ` msg="`+spaced+`"`) {
		t.Errorf("line %q does not carry the field logger's message with a "+
			"space between each pair of arguments", line)
	}
}

// TestTheFieldLoggerSurfaceIsIntact reads the interface rather than the
// implementation. Every caller in the tree and every caller outside it goes
// through `types.FieldLogger`; twenty-six of its twenty-seven methods say
// nothing about which library writes the line and have no reason to change.
// The twenty-seventh, WithFields, names its argument type, so this only asks
// that it still takes one string-keyed map and returns a LogEntry.
func TestTheFieldLoggerSurfaceIsIntact(t *testing.T) {
	iface := reflect.TypeOf((*types.FieldLogger)(nil)).Elem()
	entry := reflect.TypeOf((*types.LogEntry)(nil)).Elem()

	formatted := "func(string, ...interface {})"
	variadic := "func(...interface {})"

	want := map[string]string{
		"WithField": "func(string, interface {}) types.LogEntry",
		"WithError": "func(error) types.LogEntry",
	}
	for _, base := range []string{
		"Debug", "Info", "Print", "Warn", "Warning",
		"Error", "Fatal", "Panic",
	} {
		want[base] = variadic
		want[base+"f"] = formatted
		want[base+"ln"] = variadic
	}

	for name, sig := range want {
		m, ok := iface.MethodByName(name)
		if !ok {
			t.Errorf("types.FieldLogger no longer has a %s method", name)
			continue
		}
		if got := m.Type.String(); got != sig {
			t.Errorf("types.FieldLogger.%s is %s, want %s", name, got, sig)
		}
	}

	if m, ok := iface.MethodByName("WithFields"); !ok {
		t.Errorf("types.FieldLogger no longer has a WithFields method")
	} else {
		ft := m.Type
		switch {
		case ft.NumIn() != 1 || ft.IsVariadic():
			t.Errorf("types.FieldLogger.WithFields is %s, want one "+
				"string-keyed map argument", ft)
		case ft.In(0).Kind() != reflect.Map ||
			ft.In(0).Key().Kind() != reflect.String:
			t.Errorf("types.FieldLogger.WithFields takes %s, want a "+
				"string-keyed map", ft.In(0))
		case ft.NumOut() != 1 || ft.Out(0) != entry:
			t.Errorf("types.FieldLogger.WithFields returns %s, want "+
				"types.LogEntry", ft)
		}
	}

	if n := iface.NumMethod(); n != len(want)+1 {
		t.Errorf("types.FieldLogger has %d methods, want %d", n, len(want)+1)
	}

	// The context is the implementation every caller actually holds.
	if !reflect.TypeOf((*types.Context)(nil)).Elem().Implements(iface) {
		t.Errorf("types.Context no longer satisfies types.FieldLogger")
	}
}

// TestTheLevelConstantsKeepTheirOrder reads the other half of the public
// surface. `types.Level` is declared with seven constants and their order is
// the ordering the whole tree is written against: Panic is the lowest value
// and the highest severity, Trace is the highest value and the most verbose.
// A library whose levels run the other way -- larger value, more severe -- is
// the obvious thing to declare these in terms of, and doing so silently
// reverses every comparison written against them anywhere in the repository
// or outside it. Nothing in this tree reads the constants today, which is
// exactly why no other check here would notice.
func TestTheLevelConstantsKeepTheirOrder(t *testing.T) {
	order := []struct {
		name string
		lvl  types.Level
	}{
		{"PanicLevel", types.PanicLevel},
		{"FatalLevel", types.FatalLevel},
		{"ErrorLevel", types.ErrorLevel},
		{"WarnLevel", types.WarnLevel},
		{"InfoLevel", types.InfoLevel},
		{"DebugLevel", types.DebugLevel},
		{"TraceLevel", types.TraceLevel},
	}
	for i := 1; i < len(order); i++ {
		lo, hi := order[i-1], order[i]
		if !(lo.lvl < hi.lvl) {
			t.Errorf("types.%s is %v and types.%s is %v; the more verbose "+
				"level has to be the larger value, the way the declaration "+
				"has always read", lo.name, lo.lvl, hi.name, hi.lvl)
		}
	}
}

// --- the logger that no context configured --------------------------------
//
// Not every line this repository writes comes from a context. Six call sites
// log through the library's package-level logger instead -- the server's two
// shutdown errors and its "all servers closed", the configuration
// compatibility shim's `Debug`, the store's expiry sweep, and the schema
// validator -- two more take that logger's io.Writer to dump an HTTP
// request or response into, two reach into it for the formatter and hooks a
// fresh context inherits, and two set its level from configuration and from
// the command line. Those lines are on the same standard error as every
// other one and an operator greps them the same way, so the form they are
// written in is the form pinned above.
//
// The two tests below are the only ones in this package that fail on the tree
// this step starts from, and they fail there for a reason that has nothing to
// do with the form being wrong: that tree has no `log/slog` in it at all, so
// the standard library's own logger is still the standard library's own
// default and renders `2009/11/10 23:00:00 INFO msg key=value`. A port that
// leaves this half of the repository's logging on whatever the standard
// library does out of the box, or hands it a handler that renders a different
// dialect of logfmt, changes every one of those twelve sites.

// TestTheLoggerOutsideAnyContextRendersTheSameLine asks the logger that no
// context configured for one line and reads its shape: the quoted timestamp,
// the level, the message, and the level before the message.
func TestTheLoggerOutsideAnyContextRendersTheSameLine(t *testing.T) {
	got := capture(t, func() { slog.Default().Info(msgInfo) })
	line := lineWith(got, msgInfo)
	if line == "" {
		t.Fatalf("a line logged outside any context did not appear at all; "+
			"output was:\n%s", got)
	}
	if !strings.HasPrefix(line, `time="`) {
		t.Errorf("line %q does not start with a quoted `time=` field", line)
	}
	if !strings.Contains(line, " level=info ") {
		t.Errorf("line %q does not carry ` level=info `", line)
	}
	if !strings.HasSuffix(line, " msg="+msgInfo) {
		t.Errorf("line %q does not end with ` msg=%s`", line, msgInfo)
	}
	if i, j := strings.Index(line, " level="), strings.Index(line, " msg="); i > j {
		t.Errorf("line %q puts the message before the level", line)
	}
}

// TestTheFieldsOutsideAnyContextAreSortedAndQuotedTheSameWay asks the same
// logger for a line with three fields handed to it out of order, one of which
// needs quoting and one of which is not a string.
func TestTheFieldsOutsideAnyContextAreSortedAndQuotedTheSameWay(t *testing.T) {
	const spaced = "a value with spaces"
	const bare = "bare-value"

	got := capture(t, func() {
		slog.Default().Info(msgInfo, "zulu", 26, "alpha", spaced, "mike", bare)
	})
	line := lineWith(got, msgInfo)
	if line == "" {
		t.Fatalf("a line logged outside any context did not appear at all; "+
			"output was:\n%s", got)
	}

	msgAt := strings.Index(line, " msg=")
	idx := map[string]int{}
	for _, k := range []string{"alpha", "mike", "zulu"} {
		at := strings.Index(line, " "+k+"=")
		if at < 0 {
			t.Fatalf("field %q is missing from %q", k, line)
		}
		if at < msgAt {
			t.Errorf("field %q comes before the message in %q", k, line)
		}
		idx[k] = at
	}
	if !(idx["alpha"] < idx["mike"] && idx["mike"] < idx["zulu"]) {
		t.Errorf("fields are not in alphabetical order in %q", line)
	}

	if !strings.Contains(line, ` alpha="`+spaced+`"`) {
		t.Errorf("a value containing spaces was not quoted in %q", line)
	}
	if !strings.Contains(line, " mike="+bare+" ") &&
		!strings.HasSuffix(line, " mike="+bare) {
		t.Errorf("a value needing no quotes was not written bare in %q", line)
	}
	if !strings.Contains(line, " zulu=26") {
		t.Errorf("a value that is not a string was not rendered the way fmt "+
			"renders it in %q", line)
	}
}

// --- controls -------------------------------------------------------------

// TestTheStandardStreamsCanBeCaptured is the control for the collection
// method. If this fails, every result above is meaningless rather than wrong.
func TestTheStandardStreamsCanBeCaptured(t *testing.T) {
	got := capture(t, func() {
		os.Stdout.WriteString("probe-stdout-sentinel\n")
		os.Stderr.WriteString("probe-stderr-sentinel\n")
	})
	for _, want := range []string{
		"probe-stdout-sentinel", "probe-stderr-sentinel",
	} {
		if !strings.Contains(got, want) {
			t.Errorf("%q was not captured; got %q", want, got)
		}
	}
}

// TestAContextLogsWithoutAnySettingAtAll is the control for the logger: with
// nothing configured a context still exists and an error still comes out.
func TestAContextLogsWithoutAnySettingAtAll(t *testing.T) {
	t.Setenv(levelEnv, "")
	got := capture(t, func() { context.Background().Error(msgError) })
	if !strings.Contains(got, msgError) {
		t.Errorf("an error logged with no level configured did not appear; "+
			"output was:\n%s", got)
	}
}

// TestTheLoggerOutsideAnyContextWritesAtAll is the control for the two form
// checks above: a logger that writes nothing at all, or nothing at the level
// those two read, fails them for a reason that has nothing to do with the form
// it would have used. Both levels are asked, and the informational one is the
// level the form checks use -- a logger left at a threshold that drops it
// would otherwise look like a rendering fault.
func TestTheLoggerOutsideAnyContextWritesAtAll(t *testing.T) {
	got := capture(t, func() { slog.Default().Error(msgError) })
	if !strings.Contains(got, msgError) {
		t.Errorf("an error logged outside any context did not appear; "+
			"output was:\n%s", got)
	}
	got = capture(t, func() { slog.Default().Info(msgInfo) })
	if !strings.Contains(got, msgInfo) {
		t.Errorf("an informational line logged outside any context did not "+
			"appear at all; output was:\n%s", got)
	}
}

// --- helpers --------------------------------------------------------------

func nonEmptyLines(s string) []string {
	var out []string
	for _, ln := range strings.Split(s, "\n") {
		if strings.TrimSpace(ln) != "" {
			out = append(out, strings.TrimRight(ln, "\r"))
		}
	}
	return out
}

func lineWith(s, substr string) string {
	for _, ln := range nonEmptyLines(s) {
		if strings.Contains(ln, substr) {
			return ln
		}
	}
	return ""
}

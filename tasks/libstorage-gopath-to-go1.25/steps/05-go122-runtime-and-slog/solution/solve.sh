#!/usr/bin/env bash
# Reference solution, step 5: the module reaches the rung where a loop variable
# is per-iteration, the logging library the repository has used since 2015 is
# replaced by the standard library's own, the router is replaced by the
# standard library's own, and the two standard-library packages that have been
# superseded since are retired.
#
# The shape of the change
# -----------------------
# Five clauses, and four of them are the same kind of change: something this
# repository reached outside the standard library for in 2015 is now in it.
#
#   * `go 1.22` is the rung, and it is the one edit here that changes the
#     meaning of code already in the tree. A variable declared by a `range` or
#     a three-clause `for` is per-iteration from this directive on, which
#     makes two idioms in this repository say something they did not say
#     before: `go func(srv *HTTPServer) { ... }(srv)` is now ceremony around a
#     variable that is already the goroutine's own, and the two references to
#     `service` inside `volumeDetachAll`'s task closure are now reading the
#     value the closure was handed rather than whichever one the loop had
#     reached. Both are edited here, in opposite directions: the first loses
#     its parameter, the second gains the one it already had.
#
#   * logrus goes. Thirty-one files import it; nineteen packages do. What it
#     is used for is a text formatter, seven level constants, a `Fields` map
#     and an `Entry` that carries fields from call to call -- and `log/slog`
#     has all of it except the formatter, which is a hundred and thirty lines
#     and is written out here in `api/types/types_log.go` because the bytes it
#     produces are what the recorded corpus holds. `types.Level` keeps its
#     name, its seven constants and their order; what changes is that it is
#     now an `int` this repository defines rather than an alias for a type in
#     a library.
#
#   * gournal goes with it. It is a logging facade with a logrus appender,
#     wired into the server context by `api/server/server.go` and read by
#     nothing: the six-case level switch and the appender both write context
#     values that no first-party code ever looks up. The library that made
#     them meaningful is the one being retired, so they go rather than being
#     ported.
#
#   * gorilla/mux goes. `net/http`'s own `ServeMux` has had method and
#     wildcard patterns since Go 1.22, which is the rung clause 1 raises, and
#     it can express every route this server declares. What it does not
#     express is the four behaviours the corpus records and the routing probe
#     checks -- query-string discrimination, a bare 404 for a method that does
#     not match, a root route that is not a catch-all, and a percent-encoded
#     separator that is a separator -- and each of those is written out below.
#     gorilla/context goes at the same time: `api/context/context.go` asks it
#     for per-request values that only gorilla/mux ever stored.
#
#   * `math/rand` goes. One file uses it, to pick three words out of three
#     word lists for a server name. `math/rand/v2` seeds its global source
#     randomly, so the explicit seeding goes with the package. `crypto/rand`
#     in `api/types/types_uuid.go` is untouched: it is the other `rand`, and
#     it is the one that has to stay.
#
# Every edit below is anchored on text that exists in the tree step 4 leaves
# behind. `edit` requires its anchor to occur exactly once and `sub` requires
# exactly the stated number of occurrences, so a file that has moved underneath
# this script fails loudly instead of being written back unchanged.
set -euo pipefail

cd /app
export GO111MODULE=on GOFLAGS=-mod=mod

# ---------------------------------------------------------------------------
# 1. The renderer. `log/slog` has the levels, the fields and the entry;
#    what it does not have is logrus's text format, and that format is what
#    every recorded exchange in this repository's corpus was captured
#    through, down to the placement of the separator. So it is written out
#    here: a handler that renders `key=value` pairs in alphabetical order,
#    quoting a value only when its text holds something that would make the
#    line ambiguous, and preceding each pair with a single space so that a
#    rendered line ends in its last value rather than in a separator.
#
#    The rest of the file is surface the tree already calls: the seven
#    levels' names and the strings `ParseLevel` accepts for them, the
#    mapping to and from slog's scale -- which runs in the opposite
#    direction, since slog counts up as severity rises -- the standard
#    logger's level and output, and the `io.Writer` that logs a line at a
#    time, which is what the two HTTP dumps and the server startup header
#    write into.
# ---------------------------------------------------------------------------

cat > api/types/types_log.go <<'GO'
package types

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"log/slog"
	"maps"
	"os"
	"slices"
	"strings"
	"sync"
	"time"
)

// String returns the name of the level, which is the name ParseLevel accepts
// for it.
func (lvl Level) String() string {
	switch lvl {
	case PanicLevel:
		return "panic"
	case FatalLevel:
		return "fatal"
	case ErrorLevel:
		return "error"
	case WarnLevel:
		return "warning"
	case InfoLevel:
		return "info"
	case DebugLevel:
		return "debug"
	case TraceLevel:
		return "trace"
	}
	return "unknown"
}

// ParseLevel returns the Level named by the provided string.
func ParseLevel(lvl string) (Level, error) {
	switch strings.ToLower(lvl) {
	case "panic":
		return PanicLevel, nil
	case "fatal":
		return FatalLevel, nil
	case "error":
		return ErrorLevel, nil
	case "warn", "warning":
		return WarnLevel, nil
	case "info":
		return InfoLevel, nil
	case "debug":
		return DebugLevel, nil
	}
	var l Level
	return l, fmt.Errorf("not a valid Level: %q", lvl)
}

// The slog levels the libStorage levels map onto. Only Error, Warn, Info and
// Debug are named by log/slog; the three levels this package adds sit at
// either end of that scale.
const (
	slogPanicLevel = slog.Level(12)
	slogFatalLevel = slog.Level(10)
	slogTraceLevel = slog.Level(-8)
)

// SlogLevel returns the log/slog level that corresponds to this one. The
// libStorage scale counts up as severity falls and the slog scale counts up
// as severity rises, so the two run in opposite directions.
func (lvl Level) SlogLevel() slog.Level {
	switch lvl {
	case PanicLevel:
		return slogPanicLevel
	case FatalLevel:
		return slogFatalLevel
	case ErrorLevel:
		return slog.LevelError
	case WarnLevel:
		return slog.LevelWarn
	case InfoLevel:
		return slog.LevelInfo
	case DebugLevel:
		return slog.LevelDebug
	case TraceLevel:
		return slogTraceLevel
	}
	return slog.LevelInfo
}

// LevelFromSlog returns the level that corresponds to a log/slog level.
func LevelFromSlog(lvl slog.Level) Level {
	switch {
	case lvl >= slogPanicLevel:
		return PanicLevel
	case lvl >= slogFatalLevel:
		return FatalLevel
	case lvl >= slog.LevelError:
		return ErrorLevel
	case lvl >= slog.LevelWarn:
		return WarnLevel
	case lvl >= slog.LevelInfo:
		return InfoLevel
	case lvl >= slog.LevelDebug:
		return DebugLevel
	}
	return TraceLevel
}

// logDest is the writer and the level that a handler and every handler
// derived from it share. Keeping them behind one pointer is what lets a
// level set after a handler has been given fields still take effect.
type logDest struct {
	mu    sync.Mutex
	out   io.Writer
	level Level
}

// logHandler renders records in the form libStorage has always emitted: the
// timestamp, the level and the message, followed by every field in
// alphabetical order, each an unquoted key and a value quoted only when it
// holds a character that would make the line ambiguous.
type logHandler struct {
	dst    *logDest
	fields Fields
	group  string
}

// NewLogHandler returns a log/slog handler that writes to out every record
// logged at lvl or above.
func NewLogHandler(out io.Writer, lvl Level) slog.Handler {
	return &logHandler{
		dst:    &logDest{out: out, level: lvl},
		fields: Fields{},
	}
}

func (h *logHandler) Enabled(_ context.Context, lvl slog.Level) bool {
	h.dst.mu.Lock()
	defer h.dst.mu.Unlock()
	return lvl >= h.dst.level.SlogLevel()
}

func (h *logHandler) WithAttrs(attrs []slog.Attr) slog.Handler {
	if len(attrs) == 0 {
		return h
	}
	c := &logHandler{dst: h.dst, group: h.group, fields: Fields{}}
	maps.Copy(c.fields, h.fields)
	for _, a := range attrs {
		c.fields[h.group+a.Key] = a.Value.Any()
	}
	return c
}

func (h *logHandler) WithGroup(name string) slog.Handler {
	if name == "" {
		return h
	}
	c := &logHandler{dst: h.dst, group: h.group + name + ".", fields: Fields{}}
	maps.Copy(c.fields, h.fields)
	return c
}

func (h *logHandler) Handle(_ context.Context, rec slog.Record) error {

	fields := Fields{}
	maps.Copy(fields, h.fields)
	rec.Attrs(func(a slog.Attr) bool {
		fields[h.group+a.Key] = a.Value.Any()
		return true
	})

	keys := make([]string, 0, len(fields))
	for key := range fields {
		keys = append(keys, key)
	}
	slices.Sort(keys)

	t := rec.Time
	if t.IsZero() {
		t = time.Now()
	}

	buf := &strings.Builder{}
	appendLogField(buf, "time", t.Format(time.RFC3339))
	appendLogField(buf, "level", LevelFromSlog(rec.Level).String())
	if rec.Message != "" {
		appendLogField(buf, "msg", rec.Message)
	}
	for _, key := range keys {
		appendLogField(buf, key, fields[key])
	}
	buf.WriteByte('\n')

	h.dst.mu.Lock()
	defer h.dst.mu.Unlock()
	n, err := io.WriteString(h.dst.out, buf.String())
	if err == nil && n < buf.Len() {
		err = io.ErrShortWrite
	}
	return err
}

// appendLogField writes one key and value, separated from whatever is
// already in the buffer by a single space. The separator goes before the pair
// rather than after it, so a rendered line ends in its last value.
func appendLogField(buf *strings.Builder, key string, val interface{}) {
	if buf.Len() > 0 {
		buf.WriteByte(' ')
	}
	buf.WriteString(key)
	buf.WriteByte('=')
	appendLogValue(buf, val)
}

// appendLogValue writes a field's value, in quotes if its text holds anything
// other than the characters that read unambiguously without them. A value
// that is not already a string is rendered with fmt and then judged on the
// same terms, so a slice or a struct is quoted just as its text would be.
func appendLogValue(buf *strings.Builder, val interface{}) {
	text, ok := val.(string)
	if !ok {
		text = fmt.Sprint(val)
	}
	for _, ch := range text {
		switch {
		case ch >= 'a' && ch <= 'z':
		case ch >= 'A' && ch <= 'Z':
		case ch >= '0' && ch <= '9':
		case ch == '-', ch == '.', ch == '_',
			ch == '/', ch == '@', ch == '^', ch == '+':
		default:
			fmt.Fprintf(buf, "%q", text)
			return
		}
	}
	buf.WriteString(text)
}

// stdLogHandler is the handler behind the standard logger, the destination
// of every entry not logged through a Context.
var stdLogHandler = &logHandler{
	dst:    &logDest{out: os.Stderr, level: InfoLevel},
	fields: Fields{},
}

func init() {
	slog.SetDefault(slog.New(stdLogHandler))
}

// StandardLogger returns the logger used by the code that does not log
// through a Context.
func StandardLogger() *slog.Logger {
	return slog.Default()
}

// SetLevel sets the level of the standard logger.
func SetLevel(lvl Level) {
	stdLogHandler.dst.mu.Lock()
	defer stdLogHandler.dst.mu.Unlock()
	stdLogHandler.dst.level = lvl
}

// GetLevel returns the level of the standard logger.
func GetLevel() Level {
	stdLogHandler.dst.mu.Lock()
	defer stdLogHandler.dst.mu.Unlock()
	return stdLogHandler.dst.level
}

// SetOutput sets the writer the standard logger writes to.
func SetOutput(out io.Writer) {
	stdLogHandler.dst.mu.Lock()
	defer stdLogHandler.dst.mu.Unlock()
	stdLogHandler.dst.out = out
}

// LogWriter is an io.WriteCloser that logs each line written to it as an
// entry of its own. A write that does not end in a newline is held until the
// rest of the line arrives, or until the writer is closed.
type LogWriter struct {
	logger *slog.Logger
	level  slog.Level
	mu     sync.Mutex
	buf    []byte
}

// NewLogWriter returns a LogWriter that logs to logger at lvl.
func NewLogWriter(logger *slog.Logger, lvl slog.Level) *LogWriter {
	return &LogWriter{logger: logger, level: lvl}
}

// Write logs every complete line in p.
func (w *LogWriter) Write(p []byte) (int, error) {
	w.mu.Lock()
	defer w.mu.Unlock()
	w.buf = append(w.buf, p...)
	for {
		i := bytes.IndexByte(w.buf, '\n')
		if i < 0 {
			break
		}
		w.log(string(w.buf[:i]))
		w.buf = w.buf[i+1:]
	}
	return len(p), nil
}

// Close logs whatever is left of a line that never ended.
func (w *LogWriter) Close() error {
	w.mu.Lock()
	defer w.mu.Unlock()
	if len(w.buf) > 0 {
		w.log(string(w.buf))
		w.buf = nil
	}
	return nil
}

func (w *LogWriter) log(line string) {
	w.logger.Log(context.Background(), w.level, line)
}
GO

# ---------------------------------------------------------------------------
# 2. The entry. A `types.Context` is a `types.FieldLogger`, so it answers
#    all twenty-four level methods and the three that accumulate fields,
#    and this file is the one place in the repository that touches a
#    logging library directly. logrus's `Entry` did the accumulating; here
#    an entry is the fields plus the context that will emit them, and each
#    method turns its arguments into a message the way logrus did -- `Info`
#    through `fmt.Sprint`, `Infoln` through a `Sprintln` whose trailing
#    newline is dropped -- and then hands the message and the fields to
#    `slog.Logger.LogAttrs`.
#
#    The level guard is written out rather than left to the handler because
#    the formatting happens before the call, and at the level this server
#    runs at most of those calls are building entries nobody will read.
# ---------------------------------------------------------------------------

cat > api/context/context_logger.go <<'GO'
package context

import (
	"context"
	"fmt"
	"log/slog"
	"maps"
	"os"
	"time"

	"github.com/codedellemc/libstorage/api/types"
)

const (
	keyFieldOffset = 1000
)

func (ctx *lsc) ctxFields() map[string]interface{} {

	fields := map[string]interface{}{
		"time": time.Now().UTC().UnixNano() / int64(time.Millisecond),
	}

	for key := Key(keyLoggable - 1); key > keyEOF; key-- {
		ctxFieldsProcessValue(key.String(), ctx.Value(key), fields)
	}

	for key := range CustomLoggerKeys() {
		ctxFieldsProcessValue(key, ctx.Value(key), fields)
	}

	return fields
}

func ctxFieldsProcessValue(
	key interface{}, val interface{}, fields map[string]interface{}) {

	var keyName string
	switch tk := key.(type) {
	case string:
		keyName = tk
	case fmt.Stringer:
		keyName = tk.String()
	default:
		keyName = fmt.Sprintf("%v", key)
	}

	switch tv := val.(type) {
	case types.Client:
		if tv.OS() != nil {
			fields["osDriver"] = tv.OS().Name()
		}
		if tv.Storage() != nil {
			fields["storageDriver"] = tv.Storage().Name()
		}
		if tv.Integration() != nil {
			fields["integrationDriver"] = tv.Integration().Name()
		}
	case types.ContextLoggerFieldAware:
		k, v := tv.ContextLoggerField()
		fields[k] = v
	case types.ContextLoggerFieldsAware:
		maps.Copy(fields, tv.ContextLoggerFields())
	case string:
		fields[keyName] = tv
	case hasName:
		fields[keyName] = tv.Name()
	case hasID:
		fields[keyName] = tv.ID()
	case fmt.Stringer:
		fields[keyName] = tv.String()
	case bool, uint, uint32, uint64, int, int32, int64, float32, float64:
		fields[keyName] = fmt.Sprintf("%v", tv)
	}
}

// log emits a single entry through this context's logger. An entry logged at
// a level less severe than the context's is dropped.
func (ctx *lsc) log(lvl types.Level, msg string, fields types.Fields) {
	if ctx.logLevel < lvl {
		return
	}
	attrs := make([]slog.Attr, 0, len(fields))
	for key, val := range fields {
		attrs = append(attrs, slog.Any(key, val))
	}
	ctx.logger.LogAttrs(context.Background(), lvl.SlogLevel(), msg, attrs...)
}

// sprintlnn formats its arguments the way Sprintln does, with a space
// between every pair of them, but without the trailing newline.
func sprintlnn(args ...interface{}) string {
	msg := fmt.Sprintln(args...)
	return msg[:len(msg)-1]
}

func (ctx *lsc) addkeyFieldOffsetFieldsToEntry(e *entry) {
	maps.Copy(e.fields, ctx.ctxFields())
}

func (ctx *lsc) WithField(key string, value interface{}) types.LogEntry {
	return &entry{fields: types.Fields{key: value}, ctx: ctx}
}
func (ctx *lsc) WithFields(fields types.Fields) types.LogEntry {
	e := &entry{fields: types.Fields{}, ctx: ctx}
	maps.Copy(e.fields, fields)
	return e
}
func (ctx *lsc) WithError(err error) types.LogEntry {
	return &entry{fields: types.Fields{"error": err}, ctx: ctx}
}

func (ctx *lsc) Debugf(format string, args ...interface{}) {
	ctx.Debug(fmt.Sprintf(format, args...))
}

func (ctx *lsc) Infof(format string, args ...interface{}) {
	ctx.Info(fmt.Sprintf(format, args...))
}

func (ctx *lsc) Printf(format string, args ...interface{}) {
	ctx.Print(fmt.Sprintf(format, args...))
}

func (ctx *lsc) Warnf(format string, args ...interface{}) {
	ctx.Warn(fmt.Sprintf(format, args...))
}

func (ctx *lsc) Warningf(format string, args ...interface{}) {
	ctx.Warning(fmt.Sprintf(format, args...))
}

func (ctx *lsc) Errorf(format string, args ...interface{}) {
	ctx.Error(fmt.Sprintf(format, args...))
}

func (ctx *lsc) Fatalf(format string, args ...interface{}) {
	ctx.Fatal(fmt.Sprintf(format, args...))
}

func (ctx *lsc) Panicf(format string, args ...interface{}) {
	ctx.Panic(fmt.Sprintf(format, args...))
}

func (ctx *lsc) Debug(args ...interface{}) {
	ctx.log(types.DebugLevel, fmt.Sprint(args...), ctx.ctxFields())
}

func (ctx *lsc) Info(args ...interface{}) {
	ctx.log(types.InfoLevel, fmt.Sprint(args...), ctx.ctxFields())
}

func (ctx *lsc) Print(args ...interface{}) {
	ctx.Info(args...)
}

func (ctx *lsc) Warn(args ...interface{}) {
	ctx.log(types.WarnLevel, fmt.Sprint(args...), ctx.ctxFields())
}

func (ctx *lsc) Warning(args ...interface{}) {
	ctx.Warn(args...)
}

func (ctx *lsc) Error(args ...interface{}) {
	ctx.log(types.ErrorLevel, fmt.Sprint(args...), ctx.ctxFields())
}

func (ctx *lsc) Fatal(args ...interface{}) {
	ctx.log(types.FatalLevel, fmt.Sprint(args...), ctx.ctxFields())
	os.Exit(1)
}

func (ctx *lsc) Panic(args ...interface{}) {
	msg := fmt.Sprint(args...)
	ctx.log(types.PanicLevel, msg, ctx.ctxFields())
	panic(msg)
}

func (ctx *lsc) Debugln(args ...interface{}) {
	ctx.log(types.DebugLevel, fmt.Sprint(args...), nil)
}

func (ctx *lsc) Infoln(args ...interface{}) {
	ctx.log(types.InfoLevel, fmt.Sprint(args...), nil)
}

func (ctx *lsc) Println(args ...interface{}) {
	ctx.Infoln(args...)
}

func (ctx *lsc) Warnln(args ...interface{}) {
	ctx.log(types.WarnLevel, fmt.Sprint(args...), nil)
}

func (ctx *lsc) Warningln(args ...interface{}) {
	ctx.Warnln(args...)
}

func (ctx *lsc) Errorln(args ...interface{}) {
	ctx.log(types.ErrorLevel, fmt.Sprint(args...), nil)
}

func (ctx *lsc) Fatalln(args ...interface{}) {
	ctx.log(types.FatalLevel, fmt.Sprint(args...), nil)
	os.Exit(1)
}

func (ctx *lsc) Panicln(args ...interface{}) {
	msg := fmt.Sprint(args...)
	ctx.log(types.PanicLevel, msg, nil)
	panic(msg)
}

// entry is a set of fields waiting to be logged, along with the context
// whose fields are added to them when they are.
type entry struct {
	fields types.Fields
	ctx    *lsc
}

func (e *entry) emit(lvl types.Level, msg string) {
	e.ctx.addkeyFieldOffsetFieldsToEntry(e)
	e.ctx.log(lvl, msg, e.fields)
}

func (e *entry) Debug(args ...interface{}) {
	if e.ctx.logLevel >= types.DebugLevel {
		e.emit(types.DebugLevel, fmt.Sprint(args...))
	}
}

func (e *entry) Print(args ...interface{}) {
	e.Info(args...)
}

func (e *entry) Info(args ...interface{}) {
	if e.ctx.logLevel >= types.InfoLevel {
		e.emit(types.InfoLevel, fmt.Sprint(args...))
	}
}

func (e *entry) Warn(args ...interface{}) {
	if e.ctx.logLevel >= types.WarnLevel {
		e.emit(types.WarnLevel, fmt.Sprint(args...))
	}
}

func (e *entry) Warning(args ...interface{}) {
	e.Warn(args...)
}

func (e *entry) Error(args ...interface{}) {
	if e.ctx.logLevel >= types.ErrorLevel {
		e.emit(types.ErrorLevel, fmt.Sprint(args...))
	}
}

func (e *entry) Fatal(args ...interface{}) {
	if e.ctx.logLevel >= types.FatalLevel {
		e.emit(types.FatalLevel, fmt.Sprint(args...))
	}
	os.Exit(1)
}

func (e *entry) Panic(args ...interface{}) {
	if e.ctx.logLevel >= types.PanicLevel {
		e.emit(types.PanicLevel, fmt.Sprint(args...))
	}
	panic(fmt.Sprint(args...))
}

// Entry Printf family functions

func (e *entry) Debugf(format string, args ...interface{}) {
	if e.ctx.logLevel >= types.DebugLevel {
		e.emit(types.DebugLevel, fmt.Sprintf(format, args...))
	}
}

func (e *entry) Infof(format string, args ...interface{}) {
	if e.ctx.logLevel >= types.InfoLevel {
		e.emit(types.InfoLevel, fmt.Sprintf(format, args...))
	}
}

func (e *entry) Printf(format string, args ...interface{}) {
	e.Infof(format, args...)
}

func (e *entry) Warnf(format string, args ...interface{}) {
	if e.ctx.logLevel >= types.WarnLevel {
		e.emit(types.WarnLevel, fmt.Sprintf(format, args...))
	}
}

func (e *entry) Warningf(format string, args ...interface{}) {
	e.Warnf(format, args...)
}

func (e *entry) Errorf(format string, args ...interface{}) {
	if e.ctx.logLevel >= types.ErrorLevel {
		e.emit(types.ErrorLevel, fmt.Sprintf(format, args...))
	}
}

func (e *entry) Fatalf(format string, args ...interface{}) {
	if e.ctx.logLevel >= types.FatalLevel {
		e.emit(types.FatalLevel, fmt.Sprintf(format, args...))
	}
	os.Exit(1)
}

func (e *entry) Panicf(format string, args ...interface{}) {
	if e.ctx.logLevel >= types.PanicLevel {
		e.emit(types.PanicLevel, fmt.Sprintf(format, args...))
	}
}

// Entry Println family functions

func (e *entry) Debugln(args ...interface{}) {
	if e.ctx.logLevel >= types.DebugLevel {
		e.emit(types.DebugLevel, sprintlnn(args...))
	}
}

func (e *entry) Infoln(args ...interface{}) {
	if e.ctx.logLevel >= types.InfoLevel {
		e.emit(types.InfoLevel, sprintlnn(args...))
	}
}

func (e *entry) Println(args ...interface{}) {
	e.Infoln(args...)
}

func (e *entry) Warnln(args ...interface{}) {
	if e.ctx.logLevel >= types.WarnLevel {
		e.emit(types.WarnLevel, sprintlnn(args...))
	}
}

func (e *entry) Warningln(args ...interface{}) {
	e.Warnln(args...)
}

func (e *entry) Errorln(args ...interface{}) {
	if e.ctx.logLevel >= types.ErrorLevel {
		e.emit(types.ErrorLevel, sprintlnn(args...))
	}
}

func (e *entry) Fatalln(args ...interface{}) {
	if e.ctx.logLevel >= types.FatalLevel {
		e.emit(types.FatalLevel, sprintlnn(args...))
	}
	os.Exit(1)
}

func (e *entry) Panicln(args ...interface{}) {
	if e.ctx.logLevel >= types.PanicLevel {
		e.emit(types.PanicLevel, sprintlnn(args...))
	}
}

func (e *entry) WithField(key string, value interface{}) types.LogEntry {
	return e.WithFields(types.Fields{key: value})
}

func (e *entry) WithFields(fields types.Fields) types.LogEntry {
	c := &entry{fields: types.Fields{}, ctx: e.ctx}
	maps.Copy(c.fields, e.fields)
	maps.Copy(c.fields, fields)
	return c
}

func (e *entry) WithError(err error) types.LogEntry {
	return e.WithFields(types.Fields{"error": err})
}
GO

# ---------------------------------------------------------------------------
# 3. Everything else, as anchored edits.
# ---------------------------------------------------------------------------

python3 - <<'PY'
import io
import sys


def read(path):
    return io.open(path).read()


def write(path, text):
    io.open(path, "w").write(text)


def edit(path, *pairs):
    """Apply (old, new) replacements to a file; every `old` must occur once."""
    text = read(path)
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            sys.exit("%s: anchor occurs %d times, expected 1:\n%s" %
                     (path, n, old))
        text = text.replace(old, new)
    write(path, text)


def sub(path, old, new, count):
    """Replace every `old` in a file; there must be exactly `count` of them."""
    text = read(path)
    n = text.count(old)
    if n != count:
        sys.exit("%s: %r occurs %d times, expected %d" % (path, old, n, count))
    write(path, text.replace(old, new))


def sweep(path, imports, *counts):
    """Fix a file's imports, then replace each (old, new, n) call site."""
    edit(path, *imports)
    for old, new, n in counts:
        sub(path, old, new, n)

# -------------------------------------------------------------------------
# 4. The rung. `go 1.22` is what makes a loop variable per-iteration, and
#    it is what lets the two goroutines below be read the way they now
#    read. It is also the floor for `net/http`'s method and wildcard
#    patterns and for `math/rand/v2`, both of which this step reaches
#    for, so it is a statement about the code below rather than a note
#    about the toolchain.
# -------------------------------------------------------------------------

edit("go.mod", ("\ngo 1.21\n", "\ngo 1.22\n"))

# -------------------------------------------------------------------------
# 5. The logging interface the rest of the repository is written against.
#    The twenty-seven methods and their names are unchanged; what changes
#    is that `WithFields` takes a map this package declares rather than
#    logrus's. The name has to come from somewhere now that the library
#    is gone, and this is the file that already declares the surface it
#    belongs to.
# -------------------------------------------------------------------------

edit("api/types/types_logger.go",
     ('import (\n'
      '\t"github.com/Sirupsen/logrus"\n'
      ')\n',
      '// Fields is the structured data attached to a log entry.\n'
      'type Fields map[string]interface{}\n'),

     ('\tWithFields(fields logrus.Fields) LogEntry\n',
      '\tWithFields(fields Fields) LogEntry\n'))

# -------------------------------------------------------------------------
# 6. The level type. It was an alias for logrus's, declared with an
#    `iota` offset from `log.PanicLevel` so that the two were the same
#    numbers. With the library gone the numbers are this package's own,
#    and they are the ones the constants already imply: panic is 0, and
#    each level below it is one more verbose. Every comparison in the
#    tree -- `lvl >= types.DebugLevel`, `ctx.logLevel < lvl` -- reads the
#    same way it did, which is why the order is worth stating in the doc
#    comment rather than leaving to the reader of an `iota`.
# -------------------------------------------------------------------------

edit("api/types/types_context.go",
     ('\n'
      '\tlog "github.com/Sirupsen/logrus"\n'
      ')\n'
      '\n'
      '// Level is a log level.\n'
      'type Level log.Level\n',
      ')\n'
      '\n'
      '// Level is a log level. The order is the one this repository has always used\n'
      '// and the one `Level.String` and `ParseLevel` agree with: the lower the\n'
      '// value, the more severe the level, so a logger set to a level emits that\n'
      '// level and every level below it.\n'
      'type Level int\n'),

     ('\tPanicLevel Level = Level(log.PanicLevel) + iota\n',
      '\tPanicLevel Level = iota\n'))

# -------------------------------------------------------------------------
# 7. The last two uses of a logging library inside api/types.
#    `slog.Debug` takes a message and alternating keys and values rather
#    than a list of things to print, so the call that printed a config
#    value renders it first; and the test that turned the level up turns
#    it up through this package.
# -------------------------------------------------------------------------

edit("api/types/types_config_compat.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      '\t"fmt"\n'
      '\t"log/slog"\n'
      '\n'),

     ('\t\t\tlog.Debug(config.Get(check[1]))\n',
      '\t\t\tslog.Debug(fmt.Sprint(config.Get(check[1])))\n'))

edit("api/types/types_test.go",
     ('\n'
      '\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tlog.SetLevel(log.DebugLevel)\n',
      '\tSetLevel(DebugLevel)\n'))

# -------------------------------------------------------------------------
# 8. The context's logger. It holds a `*slog.Logger` now, which means the
#    level and the output are no longer fields it can read back off the
#    logger -- so it keeps them, which is what `GetLogLevel` returns and
#    what `SetLogLevel` needs in order to build the replacement.
#    `LIBSTORAGE_LOGGING_LEVEL` is read exactly as before and still falls
#    back to warning.
#
#    gorilla/context goes here too. `Value` asked it for a value stored
#    against the request, which was gorilla/mux's way of carrying route
#    variables; nothing in this repository ever stored anything there,
#    and the router in section 11 hands route variables to the request
#    itself.
# -------------------------------------------------------------------------

edit("api/context/context.go",
     ('\t"fmt"\n'
      '\t"net/http"\n',
      '\t"fmt"\n'
      '\t"io"\n'
      '\t"log/slog"\n'
      '\t"net/http"\n'),

     ('\n'
      '\tlog "github.com/Sirupsen/logrus"\n'
      '\tgcontext "github.com/gorilla/context"\n',
      ''),

     ('\tlogger          *log.Logger\n',
      '\tlogger          *slog.Logger\n'
      '\tlogOut          io.Writer\n'
      '\tlogLevel        types.Level\n'),

     ('\t// figure out who the parent logger instance is. if there is none,\n'
      '\t// reference the log.StandardLogger as the parent.\n'
      '\tvar logger *log.Logger\n'
      '\tif ctx, ok := parent.(*lsc); ok {\n'
      '\t\tlogger = ctx.logger\n'
      '\t}\n'
      '\tif logger == nil {\n'
      '\t\tvar lvl log.Level\n'
      '\t\tif types.Debug {\n'
      '\t\t\tlvl = log.DebugLevel\n'
      '\t\t} else {\n'
      '\t\t\tll, err := log.ParseLevel(os.Getenv("LIBSTORAGE_LOGGING_LEVEL"))\n'
      '\t\t\tif err != nil {\n'
      '\t\t\t\tll = log.WarnLevel\n',
      '\t// figure out who the parent logger instance is. if there is none, build\n'
      '\t// one of this context\'s own.\n'
      '\tvar (\n'
      '\t\tlogger *slog.Logger\n'
      '\t\tlogOut io.Writer\n'
      '\t\tlvl    types.Level\n'
      '\t)\n'
      '\tif ctx, ok := parent.(*lsc); ok {\n'
      '\t\tlogger = ctx.logger\n'
      '\t\tlogOut = ctx.logOut\n'
      '\t\tlvl = ctx.logLevel\n'
      '\t}\n'
      '\tif logger == nil {\n'
      '\t\tif types.Debug {\n'
      '\t\t\tlvl = types.DebugLevel\n'
      '\t\t} else {\n'
      '\t\t\tll, err := types.ParseLevel(os.Getenv("LIBSTORAGE_LOGGING_LEVEL"))\n'
      '\t\t\tif err != nil {\n'
      '\t\t\t\tll = types.WarnLevel\n'),

     ('\t\tlogger = &log.Logger{\n'
      '\t\t\tFormatter: log.StandardLogger().Formatter,\n'
      '\t\t\tHooks:     log.StandardLogger().Hooks,\n'
      '\t\t\tLevel:     lvl,\n'
      '\t\t\tOut:       types.Stderr,\n'
      '\t\t}\n',
      '\t\tlogOut = types.Stderr\n'
      '\t\tlogger = slog.New(types.NewLogHandler(logOut, lvl))\n'),

     ('\t\tlogger:          logger,\n'
      '\t\tpathConfig:      pathConfig,\n',
      '\t\tlogger:          logger,\n'
      '\t\tlogOut:          logOut,\n'
      '\t\tlogLevel:        lvl,\n'
      '\t\tpathConfig:      pathConfig,\n'),

     ('\t}\n'
      '\n'
      '\tif ctx.req != nil {\n'
      '\t\tif req, ok := ctx.Context.Value(HTTPRequestKey).(*http.Request); ok {\n'
      '\t\t\tif val, ok := gcontext.GetOk(req, key); ok {\n'
      '\t\t\t\treturn val\n'
      '\t\t\t}\n'
      '\t\t}\n',
      ''),

     ('func SetLogLevel(ctx context.Context, lvl log.Level) {\n'
      '\tif logCtx, ok := ctx.(*lsc); ok {\n'
      '\t\tif lvl == logCtx.logger.Level {\n',
      'func SetLogLevel(ctx context.Context, lvl types.Level) {\n'
      '\tif logCtx, ok := ctx.(*lsc); ok {\n'
      '\t\tif lvl == logCtx.logLevel {\n'),

     ('\t\t\tparentLogger := logCtx.logger\n'
      '\t\t\tlogCtx.logger = &log.Logger{\n'
      '\t\t\t\tFormatter: parentLogger.Formatter,\n'
      '\t\t\t\tOut:       parentLogger.Out,\n'
      '\t\t\t\tHooks:     parentLogger.Hooks,\n'
      '\t\t\t\tLevel:     lvl,\n'
      '\t\t\t}\n',
      '\t\t\tlogCtx.logger = slog.New(\n'
      '\t\t\t\ttypes.NewLogHandler(logCtx.logOut, lvl))\n'
      '\t\t\tlogCtx.logLevel = lvl\n'),

     ('// GetLogLevel gets the context\'s log level.\n'
      'func GetLogLevel(ctx context.Context) (log.Level, bool) {\n'
      '\tif logCtx, ok := ctx.(*lsc); ok {\n'
      '\t\treturn logCtx.logger.Level, true\n',
      '// GetLogOutput gets the writer to which the context\'s logger writes.\n'
      'func GetLogOutput(ctx context.Context) (io.Writer, bool) {\n'
      '\tif logCtx, ok := ctx.(*lsc); ok {\n'
      '\t\treturn logCtx.logOut, true\n'
      '\t}\n'
      '\treturn nil, false\n'
      '}\n'
      '\n'
      '// GetLogLevel gets the context\'s log level.\n'
      'func GetLogLevel(ctx context.Context) (types.Level, bool) {\n'
      '\tif logCtx, ok := ctx.(*lsc); ok {\n'
      '\t\treturn logCtx.logLevel, true\n'))

edit("api/context/context_keys.go",
     ('\t\tctx.logger.WithFields(map[string]interface{}{\n'
      '\t\t\t"internalID": newCustomKey.internalID,\n'
      '\t\t\t"externalID": newCustomKey.externalID,\n'
      '\t\t\t"keyBitmask": newCustomKey.keyBitmask,\n'
      '\t\t}).Info("registered custom context key")\n',
      '\t\tctx.log(types.InfoLevel, "registered custom context key",\n'
      '\t\t\ttypes.Fields{\n'
      '\t\t\t\t"internalID": newCustomKey.internalID,\n'
      '\t\t\t\t"externalID": newCustomKey.externalID,\n'
      '\t\t\t\t"keyBitmask": newCustomKey.keyBitmask,\n'
      '\t\t\t})\n'))

# -------------------------------------------------------------------------
# 9. The context's tests. The two that set a level set it through
#    api/types, and the benchmarks that measured logrus's entry measure
#    the standard logger instead: same message, same fields, and a
#    discard writer, so that what is timed is the rendering rather than
#    the terminal.
# -------------------------------------------------------------------------

edit("api/context/context_test.go",
     ('\tlog "github.com/Sirupsen/logrus"\n'
      '\t"github.com/stretchr/testify/assert"\n',
      '\t"github.com/stretchr/testify/assert"\n'
      '\n'
      '\t"github.com/codedellemc/libstorage/api/types"\n'),

     ('\tlog.SetLevel(log.ErrorLevel)\n',
      '\ttypes.SetLevel(types.ErrorLevel)\n'))

edit("api/context/context_logger_test.go",
     ('\t"fmt"\n'
      '\t"os"\n',
      '\t"fmt"\n'
      '\t"log/slog"\n'
      '\t"os"\n'),

     ('\tlog "github.com/Sirupsen/logrus"\n',
      '\t"github.com/codedellemc/libstorage/api/types"\n'),

     ('\tlog.SetLevel(log.DebugLevel)\n'
      '\tlog.SetOutput(os.Stderr)\n'
      '\tfields := log.Fields{\n'
      '\t\t"time": time.Now().UTC().UnixNano() / int64(time.Millisecond),\n'
      '\t}\n'
      '\tfor i := 0; i < f; i++ {\n'
      '\t\tfields[fmt.Sprintf("key-%d", i)] = fmt.Sprintf("val-%d", i)\n',
      '\ttypes.SetLevel(types.DebugLevel)\n'
      '\ttypes.SetOutput(os.Stderr)\n'
      '\tattrs := []any{\n'
      '\t\t"time", time.Now().UTC().UnixNano() / int64(time.Millisecond),\n'
      '\t}\n'
      '\tfor i := 0; i < f; i++ {\n'
      '\t\tattrs = append(\n'
      '\t\t\tattrs, fmt.Sprintf("key-%d", i), fmt.Sprintf("val-%d", i))\n'),

     ('\t\tlog.WithFields(fields).Debug("hi")\n',
      '\t\tslog.Debug("hi", attrs...)\n'),

     ('\tbenchmarkContextLog(b, 1, log.DebugLevel)\n',
      '\tbenchmarkContextLog(b, 1, types.DebugLevel)\n'),

     ('\tbenchmarkContextLog(b, 10, log.DebugLevel)\n',
      '\tbenchmarkContextLog(b, 10, types.DebugLevel)\n'),

     ('\tbenchmarkContextLog(b, 100, log.DebugLevel)\n',
      '\tbenchmarkContextLog(b, 100, types.DebugLevel)\n'),

     ('\tbenchmarkContextLog(b, 1000, log.DebugLevel)\n',
      '\tbenchmarkContextLog(b, 1000, types.DebugLevel)\n'),

     ('\tbenchmarkContextLog(b, 1, log.ErrorLevel)\n',
      '\tbenchmarkContextLog(b, 1, types.ErrorLevel)\n'),

     ('\tbenchmarkContextLog(b, 10, log.ErrorLevel)\n',
      '\tbenchmarkContextLog(b, 10, types.ErrorLevel)\n'),

     ('\tbenchmarkContextLog(b, 100, log.ErrorLevel)\n',
      '\tbenchmarkContextLog(b, 100, types.ErrorLevel)\n'),

     ('\tbenchmarkContextLog(b, 1000, log.ErrorLevel)\n'
      '}\n'
      '\n'
      'func benchmarkContextLog(b *testing.B, f int, lvl log.Level) {\n'
      '\tlog.SetLevel(lvl)\n'
      '\tlog.SetOutput(os.Stderr)\n',
      '\tbenchmarkContextLog(b, 1000, types.ErrorLevel)\n'
      '}\n'
      '\n'
      'func benchmarkContextLog(b *testing.B, f int, lvl types.Level) {\n'
      '\ttypes.SetLevel(lvl)\n'
      '\ttypes.SetOutput(os.Stderr)\n'))

# -------------------------------------------------------------------------
# 10. The server. gournal goes: it is a logging facade whose only
#     appender is a logrus one, and the six-case level switch and the
#     appender write context values that no first-party code ever reads.
#     The library that gave them meaning is the one being retired, so
#     they go rather than being ported to a facade over a facade.
#
#     The loop variable clause is here too. `go func(srv *HTTPServer) {
#     ... }(srv)` handed the loop variable to the goroutine because at
#     `go 1.21` there was one variable for the whole loop, and a
#     goroutine that read it directly would have got whichever server the
#     loop had reached. At `go 1.22` there is one per iteration, so the
#     parameter is ceremony around a variable that is already the
#     goroutine's own.
# -------------------------------------------------------------------------

edit("api/server/server.go",
     ('\t"io"\n'
      '\t"net"\n',
      '\t"io"\n'
      '\t"log/slog"\n'
      '\t"net"\n'),

     ('\tlog "github.com/Sirupsen/logrus"\n'
      '\n'
      '\tgofig "github.com/akutz/gofig/types"\n'
      '\t"github.com/codedellemc/gournal"\n'
      '\tglogrus "github.com/codedellemc/gournal/logrus"\n',
      '\tgofig "github.com/akutz/gofig/types"\n'),

     ('\tif lvl, ok := context.GetLogLevel(ctx); ok {\n'
      '\t\tswitch lvl {\n'
      '\t\tcase log.DebugLevel:\n'
      '\t\t\tctx = context.WithValue(\n'
      '\t\t\t\tctx, gournal.LevelKey(),\n'
      '\t\t\t\tgournal.DebugLevel)\n'
      '\t\tcase log.InfoLevel:\n'
      '\t\t\tctx = context.WithValue(\n'
      '\t\t\t\tctx, gournal.LevelKey(),\n'
      '\t\t\t\tgournal.InfoLevel)\n'
      '\t\tcase log.WarnLevel:\n'
      '\t\t\tctx = context.WithValue(\n'
      '\t\t\t\tctx, gournal.LevelKey(),\n'
      '\t\t\t\tgournal.WarnLevel)\n'
      '\t\tcase log.ErrorLevel:\n'
      '\t\t\tctx = context.WithValue(\n'
      '\t\t\t\tctx, gournal.LevelKey(),\n'
      '\t\t\t\tgournal.ErrorLevel)\n'
      '\t\tcase log.FatalLevel:\n'
      '\t\t\tctx = context.WithValue(\n'
      '\t\t\t\tctx, gournal.LevelKey(),\n'
      '\t\t\t\tgournal.FatalLevel)\n'
      '\t\tcase log.PanicLevel:\n'
      '\t\t\tctx = context.WithValue(\n'
      '\t\t\t\tctx, gournal.LevelKey(),\n'
      '\t\t\t\tgournal.PanicLevel)\n'
      '\t\t}\n'
      '\t}\n'
      '\n'
      '\tif logger, ok := ctx.Value(context.LoggerKey).(*log.Logger); ok {\n'
      '\t\tctx = context.WithValue(\n'
      '\t\t\tctx, gournal.AppenderKey(),\n'
      '\t\t\tglogrus.NewWithOptions(\n'
      '\t\t\t\tlogger.Out, logger.Level, logger.Formatter))\n'
      '\t}\n'
      '\n',
      ''),

     ('\tif logger, ok := s.ctx.Value(context.LoggerKey).(*log.Logger); ok {\n'
      '\t\ts.PrintServerStartupHeader(logger.Out)\n',
      '\tif out, ok := context.GetLogOutput(s.ctx); ok {\n'
      '\t\ts.PrintServerStartupHeader(out)\n'),

     ('\tif lvl, err := log.ParseLevel(\n',
      '\tif lvl, err := types.ParseLevel(\n'),

     ('\tlogFields := log.Fields{}\n',
      '\tlogFields := types.Fields{}\n'),

     ('\tauthFields := log.Fields{}\n',
      '\tauthFields := types.Fields{}\n'),

     ('\t\tgo func(srv *HTTPServer) {\n',
      '\t\tgo func() {\n'),

     ('\t\t}(srv)\n',
      '\t\t}()\n'),

     ('\tif logger, ok := s.ctx.Value(context.LoggerKey).(*log.Logger); ok {\n'
      '\t\ts.PrintServerStartupFooter(logger.Out)\n',
      '\tif out, ok := context.GetLogOutput(s.ctx); ok {\n'
      '\t\ts.PrintServerStartupFooter(out)\n'),

     ('\t\tif err := s.stdOut.Close(); err != nil {\n'
      '\t\t\tlog.Error(err)\n'
      '\t\t}\n',
      '\t\tif err := s.stdOut.Close(); err != nil {\n'
      '\t\t\tslog.Error(err.Error())\n'
      '\t\t}\n'),

     ('\t\tif err := s.stdErr.Close(); err != nil {\n'
      '\t\t\tlog.Error(err)\n'
      '\t\t}\n',
      '\t\tif err := s.stdErr.Close(); err != nil {\n'
      '\t\t\tslog.Error(err.Error())\n'
      '\t\t}\n'),

     ('\t\tlog.Info("all servers closed")\n',
      '\t\tslog.Info("all servers closed")\n'))

# -------------------------------------------------------------------------
# 11. The router. Every route this server declares is a method, a path
#     with `{name}` wildcards, and sometimes a set of query strings;
#     `net/http`'s own patterns express the first two exactly. The four
#     behaviours they do not express are behaviours the corpus records,
#     so each is written out.
#
#     Query strings, which `ServeMux` does not look at: the routes that
#     share a method and a path are collected, and the request goes to
#     the first of them whose queries it satisfies, which makes
#     registration order the tie-break it already was.
#
#     A method the path does not serve, which `ServeMux` answers 405 with
#     an `Allow` header and this server has always answered 404: every
#     path that is registered at all is also registered bare, and that
#     registration is a `NotFoundHandler`.
#
#     `/`, which as a `ServeMux` pattern matches everything beneath it
#     and as a route matches only itself: a pattern ending in a slash is
#     anchored with `{$}`.
#
#     And a percent-encoded slash, which `ServeMux` reads as a character
#     inside one segment and every recorded exchange reads as a
#     separator: the wrapper clears `RawPath`, which is what makes
#     `URL.Path` the string the mux matches on. The same wrapper answers
#     a path that is not already clean with a 301 to the clean one, which
#     is what gorilla/mux's `CleanPath` did.
#
#     Route variables arrive through `Request.PathValue`, which needs
#     their names, which come from the route's own path -- read once when
#     the handler is built rather than once per request.
# -------------------------------------------------------------------------

edit("api/server/server_http.go",
     ('\tgolog "log"\n'
      '\t"net"\n',
      '\tgolog "log"\n'
      '\t"log/slog"\n'
      '\t"net"\n'),

     ('\t"os"\n'
      '\t"sync"\n',
      '\t"os"\n'
      '\t"path"\n'
      '\t"slices"\n'
      '\t"strings"\n'
      '\t"sync"\n'),

     ('\tlog "github.com/Sirupsen/logrus"\n'
      '\t"github.com/akutz/gotil"\n'
      '\t"github.com/gorilla/mux"\n',
      '\t"github.com/akutz/gotil"\n'),

     ('\n'
      '\t\ts.ctx.WithFields(log.Fields{\n'
      '\t\t\t"endpoint": endpoint, "address": laddr}).Debug("endpoint info")\n',
      '\n'
      '\t\ts.ctx.WithFields(types.Fields{\n'
      '\t\t\t"endpoint": endpoint, "address": laddr}).Debug("endpoint info")\n'),

     ('\t\ts.addRouter(r)\n'
      '\t\ts.ctx.WithFields(log.Fields{\n'
      '\t\t\t"router":      r.Name(),\n',
      '\t\ts.addRouter(r)\n'
      '\t\ts.ctx.WithFields(types.Fields{\n'
      '\t\t\t"router":      r.Name(),\n'),

     ('\troute types.Route) http.HandlerFunc {\n'
      '\n',
      '\troute types.Route) http.HandlerFunc {\n'
      '\n'
      '\tvars := pathVarNames(route.GetPath())\n'
      '\n'),

     ('\t\tvars := mux.Vars(req)\n'
      '\t\tif vars == nil {\n'
      '\t\t\tvars = map[string]string{}\n'
      '\t\t}\n'
      '\t\tstore := utils.NewStoreWithVars(vars)\n',
      '\t\trouteVars := map[string]string{}\n'
      '\t\tfor _, name := range vars {\n'
      '\t\t\trouteVars[name] = req.PathValue(name)\n'
      '\t\t}\n'
      '\t\tstore := utils.NewStoreWithVars(routeVars)\n'),

     ('func (s *server) createMux(ctx types.Context) *mux.Router {\n'
      '\tm := mux.NewRouter()\n',
      '// muxPattern returns the net/http pattern that matches the requests the\n'
      '// route\'s path describes. The two syntaxes name wildcards the same way; they\n'
      '// part company at a trailing slash, which net/http reads as "everything\n'
      '// below here" unless the pattern is anchored.\n'
      'func muxPattern(routePath string) string {\n'
      '\tif strings.HasSuffix(routePath, "/") {\n'
      '\t\treturn routePath + "{$}"\n'
      '\t}\n'
      '\treturn routePath\n'
      '}\n'
      '\n'
      '// pathVarNames returns the names of the wildcards in a route\'s path, in the\n'
      '// order they appear in it.\n'
      'func pathVarNames(routePath string) []string {\n'
      '\tvar names []string\n'
      '\tfor _, seg := range strings.Split(routePath, "/") {\n'
      '\t\tif len(seg) > 2 && seg[0] == \'{\' && seg[len(seg)-1] == \'}\' {\n'
      '\t\t\tnames = append(names, seg[1:len(seg)-1])\n'
      '\t\t}\n'
      '\t}\n'
      '\treturn names\n'
      '}\n'
      '\n'
      '// matchQueries reports whether the request carries every query string the\n'
      '// route asks for. A route asks for a key and the value its first instance\n'
      '// must have, which for most of them is the empty value that a bare flag has.\n'
      'func matchQueries(req *http.Request, route types.Route) bool {\n'
      '\tqueries := route.GetQueries()\n'
      '\tif len(queries) == 0 {\n'
      '\t\treturn true\n'
      '\t}\n'
      '\tvalues := req.URL.Query()\n'
      '\tfor i := 0; i+1 < len(queries); i += 2 {\n'
      '\t\tvals, ok := values[queries[i]]\n'
      '\t\tif !ok || len(vals) == 0 {\n'
      '\t\t\treturn false\n'
      '\t\t}\n'
      '\t\tif queries[i+1] != "" && vals[0] != queries[i+1] {\n'
      '\t\t\treturn false\n'
      '\t\t}\n'
      '\t}\n'
      '\treturn true\n'
      '}\n'
      '\n'
      '// queryRoute is one of the routes that answer on a single method and path.\n'
      'type queryRoute struct {\n'
      '\troute   types.Route\n'
      '\thandler http.HandlerFunc\n'
      '}\n'
      '\n'
      '// dispatchQueries returns the handler for a method and path, which hands the\n'
      '// request to the first of that pattern\'s routes whose query strings the\n'
      '// request satisfies. A request that satisfies none of them is not a request\n'
      '// this server answers.\n'
      'func dispatchQueries(routes []queryRoute) http.HandlerFunc {\n'
      '\treturn func(w http.ResponseWriter, req *http.Request) {\n'
      '\t\tfor _, qr := range routes {\n'
      '\t\t\tif matchQueries(req, qr.route) {\n'
      '\t\t\t\tqr.handler(w, req)\n'
      '\t\t\t\treturn\n'
      '\t\t\t}\n'
      '\t\t}\n'
      '\t\thttp.NotFound(w, req)\n'
      '\t}\n'
      '}\n'
      '\n'
      'func (s *server) createMux(ctx types.Context) http.Handler {\n'
      '\n'
      '\tm := http.NewServeMux()\n'
      '\n'
      '\ttype pattern struct {\n'
      '\t\tmethod string\n'
      '\t\tpath   string\n'
      '\t}\n'
      '\n'
      '\tvar order []pattern\n'
      '\tgroups := map[pattern][]queryRoute{}\n'
      '\tpaths := map[string]bool{}\n'
      '\n'),

     ('\t\t\tf := s.makeHTTPHandler(ctx, r)\n'
      '\t\t\tmr := m.Path(r.GetPath())\n'
      '\t\t\tmr = mr.Name(r.GetName())\n'
      '\t\t\tmr = mr.Methods(r.GetMethod())\n'
      '\t\t\tmr = mr.Queries(r.GetQueries()...)\n'
      '\t\t\tmr.Handler(f)\n'
      '\n'
      '\t\t\tif l, ok := context.GetLogLevel(ctx); ok && l >= log.DebugLevel {\n'
      '\t\t\t\tctx.WithFields(log.Fields{\n',
      '\t\t\tp := pattern{r.GetMethod(), muxPattern(r.GetPath())}\n'
      '\t\t\tif _, ok := groups[p]; !ok {\n'
      '\t\t\t\torder = append(order, p)\n'
      '\t\t\t}\n'
      '\t\t\tgroups[p] = append(\n'
      '\t\t\t\tgroups[p], queryRoute{r, s.makeHTTPHandler(ctx, r)})\n'
      '\t\t\tpaths[p.path] = true\n'
      '\n'
      '\t\t\tif l, ok := context.GetLogLevel(ctx); ok && l >= types.DebugLevel {\n'
      '\t\t\t\tctx.WithFields(types.Fields{\n'),

     ('\treturn m\n',
      '\n'
      '\tfor _, p := range order {\n'
      '\t\tm.Handle(p.method+" "+p.path, dispatchQueries(groups[p]))\n'
      '\t}\n'
      '\n'
      '\t// A path this server answers for one method is not a path it answers for\n'
      '\t// another: the reply is the same not-found an unknown path gets, with no\n'
      '\t// Allow header offering the methods that would have worked. The HEAD\n'
      '\t// patterns are here because net/http reads a GET pattern as matching a\n'
      '\t// HEAD request too, and this server has never served HEAD.\n'
      '\tunmethoded := make([]string, 0, len(paths))\n'
      '\tfor p := range paths {\n'
      '\t\tunmethoded = append(unmethoded, p)\n'
      '\t}\n'
      '\tslices.Sort(unmethoded)\n'
      '\tfor _, p := range unmethoded {\n'
      '\t\tm.Handle(p, http.NotFoundHandler())\n'
      '\t\tm.Handle("HEAD "+p, http.NotFoundHandler())\n'
      '\t}\n'
      '\n'
      '\treturn &cleanPathHandler{handler: m}\n'
      '}\n'
      '\n'
      '// cleanPathHandler answers a request whose path is not already in its\n'
      '// shortest form with a redirect to that form, and matches every other\n'
      '// request against the decoded path rather than the escaped one. Both are\n'
      '// what this server has always done: a percent-encoded separator names a path\n'
      '// separator, and a doubled or backtracked path is a redirect rather than a\n'
      '// route of its own.\n'
      'type cleanPathHandler struct {\n'
      '\thandler http.Handler\n'
      '}\n'
      '\n'
      'func (h *cleanPathHandler) ServeHTTP(\n'
      '\tw http.ResponseWriter, req *http.Request) {\n'
      '\n'
      '\tif p := cleanPath(req.URL.Path); p != req.URL.Path {\n'
      '\t\tu := *req.URL\n'
      '\t\tu.Path = p\n'
      '\t\tu.RawPath = ""\n'
      '\t\tw.Header().Set("Location", u.String())\n'
      '\t\tw.WriteHeader(http.StatusMovedPermanently)\n'
      '\t\treturn\n'
      '\t}\n'
      '\n'
      '\tif req.URL.RawPath != "" {\n'
      '\t\treq = req.Clone(req.Context())\n'
      '\t\treq.URL.RawPath = ""\n'
      '\t}\n'
      '\n'
      '\th.handler.ServeHTTP(w, req)\n'
      '}\n'
      '\n'
      '// cleanPath returns the shortest path equivalent to p, keeping the trailing\n'
      '// slash that tells a collection apart from the resource of the same name.\n'
      'func cleanPath(p string) string {\n'
      '\tif p == "" {\n'
      '\t\treturn "/"\n'
      '\t}\n'
      '\tif p[0] != \'/\' {\n'
      '\t\tp = "/" + p\n'
      '\t}\n'
      '\tnp := path.Clean(p)\n'
      '\tif p[len(p)-1] == \'/\' && np != "/" {\n'
      '\t\tnp += "/"\n'
      '\t}\n'
      '\treturn np\n'),

     ('\tlogger := ctx.Value(context.LoggerKey).(*log.Logger)\n',
      '\tlogger := ctx.Value(context.LoggerKey).(*slog.Logger)\n'),

     ('\t\t}\n'
      '\t\tctx.WithFields(log.Fields{\n'
      '\t\t\t"logType": propName,\n',
      '\t\t}\n'
      '\t\tctx.WithFields(types.Fields{\n'
      '\t\t\t"logType": propName,\n'),

     ('\tif logger, ok := ctx.Value(context.LoggerKey).(*log.Logger); ok {\n'
      '\t\treturn logger.Writer()\n',
      '\tif logger, ok := ctx.Value(context.LoggerKey).(*slog.Logger); ok {\n'
      '\t\treturn types.NewLogWriter(logger, slog.LevelInfo)\n'),

     ('\tlogger *log.Logger\n',
      '\tlogger *slog.Logger\n'))

# -------------------------------------------------------------------------
# 12. The server names. One file picks three words out of three word
#     lists. `math/rand/v2` seeds its global source randomly, so the
#     package-level generator and the timestamp it was seeded from both
#     go, and `Intn` is `IntN`. `crypto/rand` in api/types/types_uuid.go
#     is untouched: it is the other `rand`, and it is the one that has to
#     stay.
# -------------------------------------------------------------------------

edit("api/server/server_names.go",
     ('\t"math/rand"\n'
      '\t"time"\n',
      '\t"math/rand/v2"\n'),

     ('\tx := serverNameRandomizer.Intn(maxAdjectives)\n'
      '\ty := serverNameRandomizer.Intn(maxNouns)\n'
      '\tz := serverNameRandomizer.Intn(maxCountries)\n',
      '\tx := rand.IntN(maxAdjectives)\n'
      '\ty := rand.IntN(maxNouns)\n'
      '\tz := rand.IntN(maxCountries)\n'),

     ('\tserverNameRandomizer = rand.New(rand.NewSource(time.Now().UnixNano()))\n'
      '\tpossibleServerNames  = map[string][]string{\n',
      '\tpossibleServerNames = map[string][]string{\n'))

# -------------------------------------------------------------------------
# 13. The second goroutine that was handed its loop variable, for the
#     same reason and with the same outcome as the one in section 10.
# -------------------------------------------------------------------------

edit("api/server/services/services_task.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\t\tt.ctx.WithFields(log.Fields{\n',
      '\t\tt.ctx.WithFields(types.Fields{\n'),

     ('\t\t\tgo func(tid int) {\n'
      '\t\t\t\ts.TaskWait(tid)\n'
      '\t\t\t\twg.Done()\n'
      '\t\t\t}(tid)\n',
      '\t\t\tgo func() {\n'
      '\t\t\t\ts.TaskWait(tid)\n'
      '\t\t\t\twg.Done()\n'
      '\t\t\t}()\n'))

# -------------------------------------------------------------------------
# 14. And the loop variable clause read the other way. `volumeDetachAll`
#     ranges over the storage services and starts a task per service; the
#     closure the task runs already takes the service as a parameter, and
#     then read the loop variable twice inside itself anyway. Those two
#     reads happen when the task runs, which is after the range has moved
#     on -- so for every service but whichever one the loop reached last,
#     the error names the wrong service and the reply is filed under the
#     wrong name. Reading the parameter fixes both, and is what the
#     parameter was for.
# -------------------------------------------------------------------------

edit("api/server/router/volume/volume_routes.go",
     ('\n'
      '\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tlf log.Fields,\n',
      '\tlf types.Fields,\n'),

     ('\t\tlf = log.Fields{}\n',
      '\t\tlf = types.Fields{}\n'),

     ('\t\tlf := log.Fields{\n',
      '\t\tlf := types.Fields{\n'),

     ('\t\t\t\treturn nil, utils.NewMissingInstanceIDError(service.Name())\n',
      '\t\t\t\treturn nil, utils.NewMissingInstanceIDError(svc.Name())\n'),

     ('\t\t\t\t\treply[service.Name()] = volumeMap\n',
      '\t\t\t\t\treply[svc.Name()] = volumeMap\n'))

# -------------------------------------------------------------------------
# 15. The handlers: two field maps, one level constant, and one
#     commented-out import that would otherwise be the last mention of
#     logrus in the tree.
# -------------------------------------------------------------------------

edit("api/server/handlers/handlers_errors.go",
     ('\n'
      '\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tif lvl, ok := context.GetLogLevel(ctx); ok && lvl == log.DebugLevel {\n',
      '\tif lvl, ok := context.GetLogLevel(ctx); ok && lvl == types.DebugLevel {\n'))

edit("api/server/handlers/handlers_query_params.go",
     ('\n'
      '\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\t\tctx.WithFields(log.Fields{\n',
      '\t\tctx.WithFields(types.Fields{\n'))

edit("api/server/handlers/handlers_schema_validator.go",
     ('\n'
      '\t//log "github.com/Sirupsen/logrus"\n',
      ''))

# -------------------------------------------------------------------------
# 16. api/utils. The two that parse and set a level do it through
#     api/types now; the three that log without a context log through the
#     standard logger, which section 1 points at the same renderer; and
#     the two that take a field map take this repository's.
# -------------------------------------------------------------------------

edit("api/utils/utils_logging.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tLevel log.Level\n',
      '\tLevel types.Level\n'),

     ('\tfields log.Fields,\n',
      '\tfields types.Fields,\n'),

     ('\t\tLevel: log.WarnLevel,\n'
      '\t}\n'
      '\n'
      '\tif lvl, err := log.ParseLevel(\n',
      '\t\tLevel: types.WarnLevel,\n'
      '\t}\n'
      '\n'
      '\tif lvl, err := types.ParseLevel(\n'))

edit("api/utils/config/config.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tll, err := log.ParseLevel(config.GetString(types.ConfigLogLevel))\n',
      '\tll, err := types.ParseLevel(config.GetString(types.ConfigLogLevel))\n'),

     ('\tlog.SetLevel(ll)\n',
      '\ttypes.SetLevel(ll)\n'))

edit("api/utils/schema/schema.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      '\t"log/slog"\n'
      '\n'),

     ('\t\tlog.StandardLogger().WithFields(log.Fields{\n',
      '\t\tslog.Default().Debug("validating schema",\n'
      '\t\t\t"schema", string(s),\n'
      '\t\t\t"body", string(d))\n'
      '\t} else {\n'
      '\t\tctx.WithFields(types.Fields{\n'),

     ('\t} else {\n'
      '\t\tctx.WithFields(log.Fields{\n'
      '\t\t\t"schema": string(s),\n'
      '\t\t\t"body":   string(d),\n'
      '\t\t}).Debug("validating schema")\n',
      ''))

edit("api/utils/utils_store.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      '\t"log/slog"\n'),

     ('\t\t\tlog.WithField("key", k).Debug("expiring cached key")\n',
      '\t\t\tslog.Debug("expiring cached key", "key", k)\n'))

edit("api/utils/utils_auth.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tfields log.Fields,\n',
      '\tfields types.Fields,\n'))

edit("api/utils/utils_tls.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tfields log.Fields,\n',
      '\tfields types.Fields,\n'))

# -------------------------------------------------------------------------
# 17. The client. Both HTTP dumps are written a line at a time into a
#     writer that logs each one, which is the `LogWriter` section 1
#     declares.
# -------------------------------------------------------------------------

edit("api/client/client_logging.go",
     ('\t"fmt"\n'
      '\t"net/http"\n',
      '\t"fmt"\n'
      '\t"log/slog"\n'
      '\t"net/http"\n'),

     ('\tlog "github.com/Sirupsen/logrus"\n'
      '\t"github.com/akutz/gotil"\n',
      '\t"github.com/akutz/gotil"\n'
      '\n'
      '\t"github.com/codedellemc/libstorage/api/types"\n'),

     ('\t}\n'
      '\n'
      '\tw := log.StandardLogger().Writer()\n'
      '\n'
      '\tfmt.Fprintln(w, "")\n',
      '\t}\n'
      '\n'
      '\tw := types.NewLogWriter(slog.Default(), slog.LevelInfo)\n'
      '\tdefer w.Close()\n'
      '\n'
      '\tfmt.Fprintln(w, "")\n'),

     ('\t}\n'
      '\n'
      '\tw := log.StandardLogger().Writer()\n'
      '\n'
      '\tfmt.Fprintln(w)\n',
      '\t}\n'
      '\n'
      '\tw := types.NewLogWriter(slog.Default(), slog.LevelInfo)\n'
      '\tdefer w.Close()\n'
      '\n'
      '\tfmt.Fprintln(w)\n'))

edit("client/client.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tlogFields := log.Fields{}\n',
      '\tlogFields := types.Fields{}\n'))

# -------------------------------------------------------------------------
# 18. The drivers and the registry, which between them hold most of the
#     repository's structured logging. Nothing about these calls changes
#     except the name of the map they build, so the three that build more
#     than a handful are swept: the import goes and every `log.Fields{`
#     becomes `types.Fields{`, with the count stated, so that a file that
#     has grown a call since fails here rather than quietly keeping one.
# -------------------------------------------------------------------------

sweep("api/registry/registry_integration.go",
      [('\tlog "github.com/Sirupsen/logrus"\n'
        '\tgofig "github.com/akutz/gofig/types"\n',
        '\tgofig "github.com/akutz/gofig/types"\n')],
      ("log.Fields{", "types.Fields{", 13))

sweep("drivers/integration/linux/linux.go",
      [('\tlog "github.com/Sirupsen/logrus"\n'
        '\n'
        '\tgofig "github.com/akutz/gofig/types"\n',
        '\tgofig "github.com/akutz/gofig/types"\n')],
      ("log.Fields{", "types.Fields{", 11))

sweep("drivers/storage/mock/mock_driver.go",
      [('\tlog "github.com/Sirupsen/logrus"\n'
        '\n'
        '\t"github.com/codedellemc/libstorage/api/context"\n',
        '\t"github.com/codedellemc/libstorage/api/context"\n')],
      ("log.Fields{", "types.Fields{", 5))

edit("drivers/os/linux/linux.go",
     ('\n'
      '\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tctx.WithFields(log.Fields{\n',
      '\tctx.WithFields(types.Fields{\n'))

edit("drivers/storage/vfs/executor/vfs_executor.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tctx.WithFields(log.Fields{\n',
      '\tctx.WithFields(types.Fields{\n'))

edit("drivers/storage/libstorage/libstorage_driver.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tlogFields := log.Fields{}\n',
      '\tlogFields := types.Fields{}\n'))

# -------------------------------------------------------------------------
# 19. The two commands, which read a level out of configuration and set
#     it.
# -------------------------------------------------------------------------

edit("cli/lss/lss.go",
     ('\n'
      '\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\tif lvl, err := log.ParseLevel(\n'
      '\t\tconfig.GetString(apitypes.ConfigLogLevel)); err == nil {\n'
      '\t\tlog.SetLevel(lvl)\n',
      '\tif lvl, err := apitypes.ParseLevel(\n'
      '\t\tconfig.GetString(apitypes.ConfigLogLevel)); err == nil {\n'
      '\t\tapitypes.SetLevel(lvl)\n'))

edit("imports/config/imports_config.go",
     ('\tlog "github.com/Sirupsen/logrus"\n',
      ''),

     ('\t\t\tvar lvl log.Level\n'
      '\t\t\tif types.Debug {\n'
      '\t\t\t\tlvl = log.DebugLevel\n'
      '\t\t\t} else {\n'
      '\t\t\t\tll, err := log.ParseLevel(os.Getenv("LIBSTORAGE_LOGGING_LEVEL"))\n'
      '\t\t\t\tif err != nil {\n'
      '\t\t\t\t\tll = log.WarnLevel\n',
      '\t\t\tvar lvl types.Level\n'
      '\t\t\tif types.Debug {\n'
      '\t\t\t\tlvl = types.DebugLevel\n'
      '\t\t\t} else {\n'
      '\t\t\t\tll, err := types.ParseLevel(\n'
      '\t\t\t\t\tos.Getenv("LIBSTORAGE_LOGGING_LEVEL"))\n'
      '\t\t\t\tif err != nil {\n'
      '\t\t\t\t\tll = types.WarnLevel\n'))

PY

# ---------------------------------------------------------------------------
# 20. logrus, gournal, gorilla/mux and gorilla/context all leave the module
#     graph here, and nothing joins it: every import added above is from
#     the standard library. Then the checks step 1 established, plus the
#     cross-compile step 3 added.
# ---------------------------------------------------------------------------

go mod tidy

unformatted=$(gofmt -l -s $(find . -name '*.go' -not -path './vendor/*'))
if [ -n "$unformatted" ]; then
    echo "not gofmt -s clean:" >&2
    echo "$unformatted" >&2
    exit 1
fi

go build ./...
go vet ./...
GOOS=darwin go build ./...

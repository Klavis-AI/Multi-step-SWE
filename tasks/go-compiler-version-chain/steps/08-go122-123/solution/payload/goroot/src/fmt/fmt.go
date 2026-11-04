// Package fmt implements formatted I/O with functions analogous to C's printf and scanf.
package fmt

import (
	"errors"
	"io"
	"os"
)

func sprintf(format string, a []any) string
func sprint(a []any) string
func sprintln(a []any) string

// Stringer is implemented by any value that has a String method.
type Stringer interface {
	String() string
}

// GoStringer is implemented by any value that has a GoString method.
type GoStringer interface {
	GoString() string
}

func Sprintf(format string, a ...any) string { return sprintf(format, a) }
func Sprint(a ...any) string                 { return sprint(a) }
func Sprintln(a ...any) string               { return sprintln(a) }

func Printf(format string, a ...any) (int, error) {
	s := sprintf(format, a)
	os.Stdout.WriteString(s)
	return len(s), nil
}

func Print(a ...any) (int, error) {
	s := sprint(a)
	os.Stdout.WriteString(s)
	return len(s), nil
}

func Println(a ...any) (int, error) {
	s := sprintln(a)
	os.Stdout.WriteString(s)
	return len(s), nil
}

func Fprintf(w io.Writer, format string, a ...any) (int, error) {
	s := sprintf(format, a)
	return io.WriteString(w, s)
}

func Fprint(w io.Writer, a ...any) (int, error) {
	s := sprint(a)
	return io.WriteString(w, s)
}

func Fprintln(w io.Writer, a ...any) (int, error) {
	s := sprintln(a)
	return io.WriteString(w, s)
}

type wrapError struct {
	msg string
	err error
}

func (e *wrapError) Error() string { return e.msg }
func (e *wrapError) Unwrap() error { return e.err }

// Errorf formats according to a format specifier and returns the string as a
// value that satisfies error. A %w verb wraps its operand.
func Errorf(format string, a ...any) error {
	s := sprintf(format, a)
	idx := -1
	n := 0
	for i := 0; i < len(format); i++ {
		if format[i] != '%' {
			continue
		}
		i++
		for i < len(format) && (format[i] == '-' || format[i] == '+' || format[i] == '#' || format[i] == ' ' || format[i] == '0') {
			i++
		}
		for i < len(format) && format[i] >= '0' && format[i] <= '9' {
			i++
		}
		if i < len(format) && format[i] == '.' {
			i++
			for i < len(format) && format[i] >= '0' && format[i] <= '9' {
				i++
			}
		}
		if i >= len(format) {
			break
		}
		if format[i] == '%' {
			continue
		}
		if format[i] == 'w' {
			idx = n
		}
		n++
	}
	if idx >= 0 && idx < len(a) {
		if e, ok := a[idx].(error); ok {
			return &wrapError{s, e}
		}
	}
	return errors.New(s)
}

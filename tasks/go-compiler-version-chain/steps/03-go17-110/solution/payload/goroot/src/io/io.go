// Package io provides basic interfaces to I/O primitives.
package io

import "errors"

type Writer interface {
	Write(p []byte) (n int, err error)
}

type Reader interface {
	Read(p []byte) (n int, err error)
}

type StringWriter interface {
	WriteString(s string) (n int, err error)
}

type Closer interface {
	Close() error
}

var EOF = errors.New("EOF")
var ErrUnexpectedEOF = errors.New("unexpected EOF")

func WriteString(w Writer, s string) (n int, err error) {
	if sw, ok := w.(StringWriter); ok {
		return sw.WriteString(s)
	}
	return w.Write([]byte(s))
}
